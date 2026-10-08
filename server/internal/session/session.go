// Package session runs one CodeJr call: the screen, the line editor,
// Claude turns and file transfers to and from the PCjr.
package session

import (
	"context"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"strings"
	"time"

	"codejr/internal/cjp"
	"codejr/internal/claude"
	"codejr/internal/kermit"
	"codejr/internal/term"
)

// Config is shared by all calls.
type Config struct {
	Workspace *Workspace
	Claude    claude.Config
	Baud      int
	Port      int    // TCP port we listen on, for /setup
	Location  string // where the PCjr is, for Claude (optional)
	LinkLog   string // native protocol event timeline, for diagnosis (optional)
	COM       string // Kermit port name written by /setup (default COM2)
	Speech    string // apps/say/mksnd.py, for /voice ("" turns voice off)
}

type session struct {
	cfg       Config
	ws        *Workspace
	conn      io.ReadWriteCloser
	out       io.Writer // paced
	pacer     *term.Pacer
	forceSend string // a file /send named: goes even if it is big
	in        <-chan byte
	port      kermit.ChanPort
	scr       *term.Screen
	logf      func(string, ...any)
	host      string // address the caller dialed, for /setup
	hist      []string
	histI     int
	lastCR    bool
}

// Serve runs a call until the caller hangs up or types /quit.
func Serve(cfg Config, conn io.ReadWriteCloser, name string) {
	defer conn.Close()
	out := term.NewPacer(conn, cfg.Baud)
	in := kermit.Pump(&telnetFilter{r: conn})
	s := &session{
		cfg:   cfg,
		ws:    cfg.Workspace,
		conn:  conn,
		out:   out,
		pacer: out,
		in:    in,
		port:  kermit.ChanPort{In: in, Out: out},
		scr:   term.New(out, "CodeJr  -  Claude Code for the IBM PCjr"),
		logf:  func(f string, a ...any) { log.Printf(name+": "+f, a...) },
	}
	if nc, ok := conn.(interface{ LocalAddr() net.Addr }); ok {
		if ta, ok := nc.LocalAddr().(*net.TCPAddr); ok {
			s.host = ta.IP.String()
		}
	}
	s.logf("connected")
	started, native := s.waitForStart()
	switch {
	case !started:
		s.logf("hung up before starting")
		return
	case native:
		s.native()
	default:
		s.welcome()
		s.loop()
	}
	s.logf("disconnected")
}

// waitForStart waits for the caller to press Enter. Until then it repeats
// a "ready" banner every two seconds: CODEJR.INI waits for that word, so
// it doesn't depend on how the modem words its own CONNECT message, and it
// tells someone dialing by hand what to do. It also answers modem AT
// commands: through a real WiFi modem the server never sees them, but when
// the emulator's serial port is wired straight to the server this makes
// the same Kermit script work unchanged.
func (s *session) waitForStart() (started, native bool) {
	banner := func() { fmt.Fprint(s.out, "\r\nCodeJr READY - press Enter\r\n") }
	banner()
	tick := time.NewTicker(2 * time.Second)
	defer tick.Stop()
	var line []byte
	for {
		select {
		case <-tick.C:
			banner()
		case b, ok := <-s.in:
			if !ok {
				return false, false
			}
			if b == cjp.FEND {
				// The native client's HELLO frame.
				s.logf("started (native client)")
				return true, true
			}
			switch b {
			case '\r', '\n':
				l := strings.ToUpper(strings.TrimSpace(string(line)))
				line = line[:0]
				if l != "" {
					s.logf("before start: %q", l)
				}
				switch {
				case strings.HasPrefix(l, "ATD"):
					fmt.Fprintf(s.out, "\r\nCONNECT %d\r\n", s.cfg.Baud)
					banner()
				case strings.HasPrefix(l, "AT"):
					fmt.Fprint(s.out, "\r\nOK\r\n")
				case b == '\r':
					s.logf("started")
					return true, false
				}
			case 0x0C: // Ctrl-L
				return true, false
			default:
				if len(line) < 80 {
					line = append(line, b)
				}
			}
		}
	}
}

func (s *session) welcome() {
	// MS-Kermit erases with its own default colors, not ours (as real DEC
	// terminals did). Make its default white on black; 20 keeps it after
	// an SGR reset.
	s.drain(time.Second) // Kermit may still be finishing its script
	s.apc("set terminal color 0, 37, 40, 20")
	time.Sleep(300 * time.Millisecond)
	s.scr.SetEcho(!s.ws.Flag("noecho"))
	s.scr.Redraw()
	s.scr.Println(term.Banner, "Welcome to CodeJr. Ask for a program and Claude writes it straight onto")
	s.scr.Println(term.Banner, `this PCjr, into `+ProgDir+`.  Type /help for commands.`)
	if s.ws.Session() != "" {
		s.scr.Println(term.Note, "Continuing your last conversation. /new starts a fresh one.")
	}
	s.scr.Println(term.Plain, "")
	s.ready()
}

func (s *session) ready() {
	s.scr.Status("Ready.  Enter sends,  /help for commands,  Ctrl-L redraws.")
	s.scr.FocusInput()
}

func (s *session) loop() {
	for b := range s.in {
		cr := s.lastCR
		s.lastCR = b == '\r'
		switch {
		case b == '\r' || (b == '\n' && !cr):
			line := strings.TrimSpace(s.scr.Input())
			s.scr.SetInput("")
			if line == "" {
				continue
			}
			s.hist = append(s.hist, line)
			s.histI = len(s.hist)
			if !s.submit(line) {
				return
			}
		case b == '\n':
			// LF after CR
		case b == 0x08 || b == 0x7F:
			s.scr.Backspace()
		case b == 0x15: // Ctrl-U
			s.scr.SetInput("")
		case b == 0x0C: // Ctrl-L
			s.scr.Redraw()
		case b == 0x1B:
			s.escape()
		case b >= 32 && b < 127:
			if len(s.scr.Input()) < 1000 {
				s.scr.Type(rune(b))
			}
		}
	}
}

// escape handles arrow keys (ESC [ A / ESC O A) for input history.
func (s *session) escape() {
	b, err := s.port.ReadTimeout(150 * time.Millisecond)
	if err != nil || (b != '[' && b != 'O') {
		return
	}
	// Read the whole sequence: parameters, then a final byte @..~. This
	// also swallows terminal replies such as the Device Attributes answer.
	var params []byte
	for {
		c, err := s.port.ReadTimeout(150 * time.Millisecond)
		if err != nil {
			return
		}
		if c >= 0x40 && c <= 0x7E {
			if len(params) == 0 {
				s.arrow(c)
			}
			return
		}
		params = append(params, c)
	}
}

func (s *session) arrow(c byte) {
	switch c {
	case 'A':
		if s.histI > 0 {
			s.histI--
			s.scr.SetInput(s.hist[s.histI])
		}
	case 'B':
		if s.histI < len(s.hist)-1 {
			s.histI++
			s.scr.SetInput(s.hist[s.histI])
		} else {
			s.histI = len(s.hist)
			s.scr.SetInput("")
		}
	}
}

// settle gives MS-Kermit time to get back to its terminal emulator: after a
// file transfer a 4.77 MHz PCjr needs a moment to close the file and restore
// its screen, and anything sent before then is lost. (MS-Kermit doesn't
// reliably answer status queries, so we can't simply ask.)
func (s *session) settle() {
	s.drain(1500 * time.Millisecond)
}

// submit handles one line. It returns false to hang up.
func (s *session) submit(line string) bool {
	if up := strings.ToUpper(line); strings.HasPrefix(up, "ATD") {
		// CODEJR was started again while the modem was still connected
		// (Kermit was left without /quit): play modem and start over.
		s.logf("redial over an open call")
		fmt.Fprintf(s.out, "\r\nCONNECT %d\r\n", s.cfg.Baud)
		started, native := s.waitForStart()
		switch {
		case !started:
			return false
		case native:
			s.native()
			return false
		}
		s.welcome()
		return true
	} else if up == "AT" || strings.HasPrefix(up, "AT&") || strings.HasPrefix(up, "ATE") {
		fmt.Fprint(s.out, "\r\nOK\r\n")
		return true
	}
	// The PCjr's keyboard interrupt (Enter's key release) can make it miss
	// serial input; let it settle before sending anything.
	time.Sleep(250 * time.Millisecond)
	if strings.HasPrefix(line, "/") {
		return s.command(line)
	}
	s.scr.Println(term.User, "> "+line)
	s.turn(line)
	s.sync()
	s.ready()
	return true
}

const help = `Commands:
  /files       list your programs (* = not on the PCjr yet)
  /sync        send files the PCjr doesn't have yet
  /send NAME   send a file from the PCjr to Claude (e.g. a LOG.TXT)
  /get NAME    get a file from Claude again
  /setup       install or update the CODEJR launcher on the PCjr
  /echo off    stop echoing typing (if letters appear twice)
  /new         start a fresh conversation
  /quit        hang up
Keys: Up/Down recall lines, Ctrl-U clears, Ctrl-L redraws, Ctrl-C stops Claude.
To run a program: Ctrl-] C, then RUN GWBASIC NAME, then CONNECT to return.`

func (s *session) command(line string) bool {
	f := strings.Fields(line)
	cmd, arg := strings.ToLower(f[0]), ""
	if len(f) > 1 {
		arg = strings.ToUpper(f[1])
	}
	s.scr.Println(term.User, line)
	switch cmd {
	case "/help", "/?":
		s.scr.Println(term.Note, help)
	case "/new":
		s.ws.SetSession("")
		s.scr.Println(term.Note, "Fresh conversation. Claude has forgotten the earlier chat; files stay.")
	case "/files", "/dir", "/ls":
		s.listFiles()
	case "/sync":
		s.sync()
	case "/get":
		// Names read from the PCjr's seat: /get brings Claude's copy here
		// again, /send sends the PCjr's file to Claude.
		if !ValidName(arg) {
			s.scr.Println(term.Warn, "Usage: /get NAME.EXT")
			break
		}
		s.ws.Forget(arg)
		s.forceSend = arg
		s.sync()
	case "/send":
		s.get(arg)
	case "/setup":
		s.setup(arg)
	case "/echo":
		switch arg {
		case "OFF":
			s.ws.SetFlag("noecho", true)
			s.scr.SetEcho(false)
			s.scr.Println(term.Note, "Echo off: the server no longer echoes your typing (use this if letters appear twice).")
		case "ON":
			s.ws.SetFlag("noecho", false)
			s.scr.SetEcho(true)
			s.scr.Println(term.Note, "Echo on: the server echoes your typing.")
		default:
			s.scr.Println(term.Warn, "Usage: /echo on  or  /echo off  (off if letters appear twice)")
		}
	case "/quit", "/bye", "/exit":
		s.scr.Println(term.Note, "Bye! Hanging up.")
		s.scr.Status("")
		// Reset the terminal, then ask Kermit to exit back to DOS. If its
		// APC safety setting refuses EXIT, hanging up still ends the call.
		fmt.Fprint(s.out, "\x1b[r\x1b[0m\x1b[2J\x1b[H")
		s.apc("exit")
		time.Sleep(time.Second)
		return false
	default:
		s.scr.Println(term.Warn, "Unknown command. Type /help.")
	}
	s.ready()
	return true
}

func (s *session) listFiles() {
	entries, _, err := s.ws.Scan()
	if err != nil {
		s.scr.Println(term.Warn, err.Error())
		return
	}
	if len(entries) == 0 {
		s.scr.Println(term.Note, `No files yet in `+ProgDir+`.`)
		return
	}
	for _, e := range entries {
		switch {
		case e.Problem != "":
			s.scr.Println(term.Warn, fmt.Sprintf("  %-12s  %s", e.Name, e.Problem))
		default:
			mark := " "
			if !e.OnPCjr {
				mark = "*"
			}
			s.scr.Println(term.Note, fmt.Sprintf("%s %-12s %7d bytes", mark, e.Name, e.Size))
		}
	}
}

// turn runs one Claude reply, streaming it to the screen.
func (s *session) turn(prompt string) {
	session := s.ws.Session()
	err := s.runClaude(session, prompt)
	if err != nil && session != "" && err != context.Canceled {
		// The saved conversation may be gone; start over once.
		s.logf("resume failed (%v), retrying fresh", err)
		s.ws.SetSession("")
		s.scr.EndBlock()
		s.scr.Println(term.Note, "(Couldn't continue the old conversation; starting a fresh one.)")
		err = s.runClaude("", prompt)
	}
	s.scr.EndBlock()
	switch {
	case err == context.Canceled:
		s.scr.Println(term.Warn, "Stopped.")
	case err != nil:
		s.scr.Println(term.Warn, "Claude error: "+err.Error())
	}
}

func (s *session) runClaude(session, prompt string) error {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	events := make(chan claude.Event, 256)
	errc := make(chan error, 1)
	go func() {
		cfg := s.cfg.Claude
		cfg.SystemPrompt += contextNote(time.Now(), s.cfg.Location)
		errc <- claude.Run(ctx, cfg, session, prompt, func(e claude.Event) { events <- e })
		close(events)
	}()

	start := time.Now()
	activity := "Claude is thinking"
	tick := time.NewTicker(time.Second)
	defer tick.Stop()
	spin := `|/-\`
	n := 0
	status := func() {
		secs := int(time.Since(start).Seconds())
		s.scr.Status(fmt.Sprintf("%c %s  %ds   (Ctrl-C stops)", spin[n%4], activity, secs))
	}
	status()
	s.logf("turn: %q", prompt)

	var runErr error
	in := s.in
	for {
		select {
		case e, ok := <-events:
			if !ok {
				runErr = <-errc
				if runErr == nil && ctx.Err() != nil {
					runErr = context.Canceled
				}
				return runErr
			}
			switch e.Kind {
			case claude.Text:
				s.scr.Print(term.Plain, e.Text)
			case claude.Thinking:
				activity = "Claude is thinking"
			case claude.ToolUse:
				note, act := describeTool(e)
				activity = act
				if note != "" {
					s.scr.EndBlock()
					s.scr.Println(term.Note, "  "+note)
				}
			case claude.Done:
				if e.SessionID != "" {
					s.ws.SetSession(e.SessionID)
				}
				if e.IsError {
					s.scr.EndBlock()
					s.scr.Println(term.Warn, "Claude: "+firstLine(e.Result))
				}
				s.logf("turn done in %s", time.Since(start).Round(time.Second))
			}
			status()
		case <-tick.C:
			n++
			status()
		case b, ok := <-in:
			if !ok { // caller hung up
				in = nil
				cancel()
				continue
			}
			if b == 0x03 { // Ctrl-C
				cancel()
			}
		}
	}
}

func firstLine(s string) string {
	s = strings.TrimSpace(s)
	if i := strings.IndexByte(s, '\n'); i >= 0 {
		return s[:i]
	}
	return s
}

// describeTool returns a transcript note and a status-line activity.
func describeTool(e claude.Event) (string, string) {
	file := ""
	if p, ok := e.Input["file_path"].(string); ok {
		file = strings.ToUpper(baseName(p))
	}
	switch e.Name {
	case "Write":
		return "[writes " + file + "]", "Writing " + file
	case "Edit", "MultiEdit":
		return "[edits " + file + "]", "Editing " + file
	case "Read":
		return "", "Reading " + file
	case "Glob", "Grep":
		return "", "Looking through files"
	case "mcp__pcjr__dos":
		c, _ := e.Input["command"].(string)
		return "[runs on this PCjr: " + c + "]", "Running " + c + " on the PCjr"
	case "mcp__pcjr__run_on_pcjr":
		c, _ := e.Input["command"].(string)
		return "[runs " + c + " on this PCjr]", "Running " + c + " on the PCjr"
	case "mcp__pcjr__fetch":
		p, _ := e.Input["path"].(string)
		p = strings.ToUpper(p)
		return "[takes " + p + " from this PCjr]", "Getting " + p + " from the PCjr"
	case "mcp__pcjr__build":
		p, _ := e.Input["project"].(string)
		p = strings.ToUpper(p)
		return "[builds " + p + "]", "Building " + p
	case "mcp__pcjr__run":
		p, _ := e.Input["program"].(string)
		p = strings.ToUpper(p)
		return "[tests " + p + " in the emulator]", "Testing " + p + " in the emulator"
	}
	return "[" + strings.ToLower(e.Name) + "]", "Working"
}

func baseName(p string) string {
	if i := strings.LastIndexAny(p, `/\`); i >= 0 {
		return p[i+1:]
	}
	return p
}

// recvSetup goes in front of every RECEIVE, so transfers work even when
// Kermit wasn't started by CODEJR.INI: binary, and replace existing files
// (MS-Kermit's default renames them).
const recvSetup = "set file type binary, set file collision overwrite, "

// apc sends a command for MS-Kermit to run (needs SET TERMINAL APC ON).
func (s *session) apc(cmd string) {
	fmt.Fprintf(s.out, "\x1b_%s\x1b\\", cmd)
}

// drain throws away stray bytes (late NAKs, line noise) after a transfer.
func (s *session) drain(quiet time.Duration) {
	for {
		if _, err := s.port.ReadTimeout(quiet); err != nil {
			return
		}
	}
}

// kermitMax is the biggest file sent over Kermit without being asked.
const kermitMax = 40 << 10

var xferOpt = kermit.Options{Timeout: 4 * time.Second, Retries: 8, InitialWait: 20 * time.Second}

// sync sends every file the PCjr doesn't have yet.
func (s *session) sync() {
	files, hashes, problems, err := s.ws.Pending()
	if err != nil {
		s.scr.Println(term.Warn, err.Error())
		return
	}
	for _, p := range problems {
		s.scr.Println(term.Warn, fmt.Sprintf("Not sending %s: %s.", p.Name, p.Problem))
	}
	// Kermit manages about 235 bytes a second here: leave big files for
	// the native client, which is many times faster.
	kept := files[:0]
	for _, f := range files {
		if IsSystem(f.Name) {
			// CodeJr's own files go to C:\CODEJR, which only the native
			// client (or /setup) can do.
			delete(hashes, f.Name)
			continue
		}
		if InFolder(f.Name) {
			// Kermit can't make folders; the native client can.
			s.scr.Println(term.Note, fmt.Sprintf("%s is in a project folder; type CODEJR at DOS to get it.", f.Name))
			delete(hashes, f.Name)
			continue
		}
		if len(f.Data) > kermitMax && f.Name != s.forceSend {
			s.scr.Println(term.Note, fmt.Sprintf("%s (%s) is too big for Kermit; type CODEJR at DOS to get it (or /get %s).", f.Name, size(len(f.Data)), f.Name))
			delete(hashes, f.Name) // still waiting for the native client
			continue
		}
		kept = append(kept, f)
	}
	files = kept
	s.forceSend = ""
	if len(files) == 0 {
		return
	}
	total := 0
	var names []string
	for _, f := range files {
		total += len(f.Data)
		names = append(names, f.Name)
	}
	// About 1.3 wire bytes per data byte with prefixes and packet overhead.
	est := int(float64(total)*1.3/float64(max(s.cfg.Baud/10, 1))) + 2*len(files) + 2
	s.scr.Println(term.Note, fmt.Sprintf("Sending %s (%s) to the PCjr...", strings.Join(names, ", "), size(total)))
	s.scr.Status(fmt.Sprintf("Sending %d file(s), about %ds. Kermit shows its own progress.", len(files), est))

	s.apc(recvSetup + "receive")
	time.Sleep(700 * time.Millisecond)
	start := time.Now()
	err = kermit.Send(s.port, files, xferOpt, nil)
	s.settle()
	s.scr.Redraw() // Kermit's transfer display drew over our screen
	if err != nil {
		s.logf("send failed: %v", err)
		s.abortTransfer()
		s.scr.Println(term.Warn, "Transfer failed: "+err.Error())
		s.scr.Println(term.Warn, "Nothing is lost. Type /sync to try again.")
		return
	}
	s.ws.MarkSynced(hashes)
	s.logf("sent %v (%d bytes) in %s", names, total, time.Since(start).Round(100*time.Millisecond))
	s.scr.Println(term.Note, fmt.Sprintf(`Saved on the PCjr in %s (%ds, %s).`, ProgDir, int(time.Since(start).Seconds()+0.5), bps(total, time.Since(start))))
}

// get copies a file from the PCjr into the workspace.
func (s *session) get(name string) {
	if !ValidName(name) {
		s.scr.Println(term.Warn, "Usage: /send NAME.EXT  (a file in "+ProgDir+" on the PCjr)")
		return
	}
	s.scr.Println(term.Note, "Fetching "+name+" from the PCjr...")
	s.scr.Status("Receiving " + name + "...")
	s.apc("send " + name)
	start := time.Now()
	files, err := kermit.Receive(s.port, xferOpt, nil)
	took := time.Since(start)
	s.settle()
	s.scr.Redraw()
	if err != nil {
		s.logf("receive failed: %v", err)
		s.abortTransfer()
		s.scr.Println(term.Warn, "Couldn't get "+name+": "+err.Error())
		return
	}
	for _, f := range files {
		if err := s.ws.SaveFromPCjr(f.Name, f.Data); err != nil {
			s.scr.Println(term.Warn, err.Error())
			continue
		}
		s.logf("got %s (%d bytes)", f.Name, len(f.Data))
		s.scr.Println(term.Note, fmt.Sprintf("Got %s (%s, %s). Claude can read it now.", strings.ToUpper(f.Name), size(len(f.Data)), bps(len(f.Data), took)))
	}
}

// setup installs the CodeJr launcher in C:\ on the PCjr, pointed at the
// address the caller dialed. com overrides the Kermit port (COM1, COM2).
func (s *session) setup(com string) {
	if com == "" {
		com = s.cfg.COM
	}
	if com != "COM1" && com != "COM2" {
		s.scr.Println(term.Warn, "Usage: /setup [COM1|COM2]")
		return
	}
	if s.host == "" {
		s.scr.Println(term.Warn, "Can't tell which address you dialed.")
		return
	}
	files := DOSFiles(DOSSetup{Host: s.host, Port: s.cfg.Port, COM: com, Baud: s.cfg.Baud})
	// The client and its screen grabber too: they land in C:\ and the
	// launcher moves them into C:\CODEJR.
	for _, n := range []string{"CODEJR.EXE", "SCRDUMP.EXE"} {
		if b, err := os.ReadFile(filepath.Join(s.ws.Dir, SysDir, n)); err == nil {
			files = append(files, kermit.File{Name: n, Data: b})
		}
	}
	s.scr.Println(term.Note, fmt.Sprintf(`Installing the CodeJr launcher and settings in C:\ (dials %s:%d on %s)...`, s.host, s.cfg.Port, com))
	s.scr.Status("Installing the CodeJr launcher...")
	var failed error
	for _, f := range files {
		// One transfer per file so each can be named with a path. MS-Kermit
		// 3.14 takes the backslash literally (a doubled one stays doubled).
		s.apc(recvSetup + `receive C:\` + f.Name)
		time.Sleep(700 * time.Millisecond)
		if err := kermit.Send(s.port, []kermit.File{f}, xferOpt, nil); err != nil {
			failed = fmt.Errorf("%s: %w", f.Name, err)
			s.logf("setup failed: %v", failed)
			s.abortTransfer()
			break
		}
		s.settle()
	}
	s.scr.Redraw()
	if failed != nil {
		s.scr.Println(term.Warn, "Setup failed: "+failed.Error())
		return
	}
	s.logf("setup done (%s)", com)
	s.scr.Println(term.Note, "Launcher installed. Now sending the native client and programs to "+ProgDir+"...")
	s.sync()
	s.scr.Println(term.Note, "Done. Type /quit, then CODEJR at the DOS prompt to call in (CJKERMIT")
	s.scr.Println(term.Note, "for this Kermit way). Modem tips: C:\\CODEJR\\CODEJR.TXT.")
}

// abortTransfer tells the PCjr's Kermit to give up, so it returns to the
// terminal instead of waiting out its own retries.
func (s *session) abortTransfer() {
	kermit.Abort(s.port, "CodeJr server gave up")
	s.drain(time.Second)
	s.settle()
}

// bps is a transfer's average speed in bits per second as the serial line
// counts them: 10 per byte (start and stop bits), so it compares directly
// with the baud rate. Compressed files can come out above it.
func bps(bytes int, took time.Duration) string {
	if took <= 0 {
		return "0 bps"
	}
	return fmt.Sprintf("%.0f bps", float64(bytes)*10/took.Seconds())
}

func size(n int) string {
	if n < 1024 {
		return fmt.Sprintf("%d bytes", n)
	}
	return fmt.Sprintf("%.1f KB", float64(n)/1024)
}
