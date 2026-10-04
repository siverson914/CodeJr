package session

import (
	"context"
	"encoding/binary"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"codejr/internal/cjp"
	"codejr/internal/claude"
	"codejr/internal/term"
)

// The native front end: the PCjr runs CODEJR.EXE, which draws its own
// screen, and everything goes over the CodeJr protocol (package cjp)
// instead of escape codes and Kermit.

const nativeNote = `

Client: the user is running CodeJr's own PCjr program (CODEJR.EXE), not
MS-Kermit. It draws its own screen, so ignore the MS-Kermit details above:
files arrive with an on-screen progress bar, and to run a program the user
types /run (a numbered list of programs; a number runs one, and CodeJr
calls back when it ends). End BASIC programs with SYSTEM rather than END,
so they return to DOS and CodeJr comes back by itself. You cannot see the PCjr's screen; if the user reports a
display problem, say so and suggest /help, rather than guessing settings.`

type native struct {
	s    *session
	link *cjp.Link
	wrap wrapper
	pull bool // the client fetches files with the pull protocol (CODEJR/2)
	// Messages that arrived while a file was going over (a line typed
	// early, say); handled once the transfer is done.
	backlog []cjp.Msg
	baud    int // the serial speed the client reported
	// The client echoes typed lines itself and takes Ready and Temp
	// messages (it said ui=1 in its HELLO).
	ui   bool
	temp bool // a Temp line is showing

	// Text waiting to go out. Claude streams a few characters at a time;
	// sending each piece as its own frame wastes the window on tiny frames,
	// so text is collected into full frames (flushed every 100 ms, when a
	// frame fills, or before anything else is sent).
	pend      []byte
	pendStyle byte
}

func (s *session) native() {
	link := cjp.NewLink(s.in, s.out, 2*time.Second, true)
	link.SetLineSpeed(s.cfg.Baud / 10)
	// One frame at a time: on this PCjr, whatever arrives while it is
	// transmitting (an ack, say) is corrupted, so it must only answer
	// into a quiet line. See docs/JOURNAL.md.
	link.SetWindow(1)
	if s.cfg.LinkLog != "" {
		if f, err := os.OpenFile(s.cfg.LinkLog, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644); err == nil {
			defer f.Close()
			var mu sync.Mutex
			start := time.Now()
			fmt.Fprintf(f, "--- call at %s\n", start.Format(time.RFC3339))
			link.Trace = func(ev string) {
				mu.Lock()
				fmt.Fprintf(f, "%8.3f %s\n", time.Since(start).Seconds(), ev)
				mu.Unlock()
			}
		}
	}
	defer link.Close()
	defer func() { s.logf("native: call ended; %s", link.Stats()) }()
	n := &native{s: s, link: link}

	// The client speaks first.
	select {
	case m := <-link.Recv():
		if m.Type != cjp.Hello {
			s.logf("native: expected HELLO, got type %d", m.Type)
			return
		}
		s.logf("native: client says %q", m.Payload)
		n.hello(string(m.Payload))
	case <-link.Done():
		return
	case <-time.After(10 * time.Second):
		s.logf("native: no HELLO")
		return
	}
	if n.ui {
		link.Send(cjp.Hello, []byte("CODEJR/2 server ui"))
	} else {
		link.Send(cjp.Hello, []byte("CODEJR/2 server"))
	}

	n.print(cjp.StyleBanner, "Welcome to CodeJr. Ask for a program and CodeJr writes it straight onto\n")
	n.print(cjp.StyleBanner, "this PCjr, into "+ProgDir+".  Type /help for commands.\n")
	if s.ws.Session() != "" {
		n.print(cjp.StyleNote, "Continuing your last conversation. /new starts a fresh one.\n")
	}
	n.status("Ready.  Enter sends,  /help for commands,  Alt-X quits.")
	n.sync()
	n.sendPending()
	n.ready()

	for {
		var m cjp.Msg
		if len(n.backlog) > 0 {
			m, n.backlog = n.backlog[0], n.backlog[1:]
		} else {
			select {
			case m = <-link.Recv():
			case <-link.Done():
				return
			}
		}
		switch m.Type {
		case cjp.Input:
			ok := n.submit(strings.TrimSpace(string(m.Payload)))
			n.sendPending()
			if ok {
				n.ready()
			}
			if !ok {
				link.Send(cjp.Bye, nil)
				link.Flush(5 * time.Second)
				return
			}
		case cjp.Bye:
			s.logf("native: client hung up")
			return
		}
	}
}

// hello reads the client's greeting: "CODEJR/<protocol> client <version>
// [baud=<speed>]".
func (n *native) hello(h string) {
	n.baud = n.s.cfg.Baud
	for _, f := range strings.Fields(h) {
		if v, ok := strings.CutPrefix(f, "CODEJR/"); ok {
			if p, err := strconv.Atoi(v); err == nil && p >= 2 {
				n.pull = true
			}
		}
		if f == "ui=1" {
			n.ui = true
		}
		if v, ok := strings.CutPrefix(f, "baud="); ok {
			if b, err := strconv.Atoi(v); err == nil && b >= 300 && b <= 115200 {
				n.baud = b
			}
		}
	}
	// A little under the line speed, so nothing queues up in the modem.
	n.s.pacer.Set(n.baud*97/100, true)
	n.link.SetLineSpeed(n.baud / 10)
}

// sendPull offers a file, then answers the client's requests for its
// chunks until the client reports the result (see cjp/pull.go).
func (n *native) sendPull(name string, data []byte) (bool, string) {
	chunks := cjp.MakeChunks(data)
	if err := n.link.Send(cjp.FileOffer, cjp.OfferPayload(name, len(data), len(chunks))); err != nil {
		return false, "connection lost"
	}
	const quiet = 60 * time.Second
	idle := time.NewTimer(quiet)
	defer idle.Stop()
	pulls, sent := 0, 0
	burster := cjp.NewBurster(n.s.out)
	defer burster.Stop()
	for {
		select {
		case m := <-n.link.Recv():
			switch m.Type {
			case cjp.Pull:
				k, bitmap, ok := cjp.ParsePull(m.Payload)
				if !ok || k >= len(chunks) {
					continue
				}
				b := cjp.Burst(k, chunks[k], bitmap)
				pulls++
				sent += len(b)
				burster.Send(b)
				idle.Reset(quiet)
			case cjp.FileResult:
				if len(m.Payload) == 0 {
					continue
				}
				n.s.logf("native: %s: %d requests for %d chunks, %d bytes on the line", name, pulls, len(chunks), sent)
				if m.Payload[0] == 0 {
					return true, ""
				}
				return false, string(m.Payload[1:])
			case cjp.Input, cjp.Bye:
				n.backlog = append(n.backlog, m)
			}
		case <-n.link.Done():
			return false, "connection lost"
		case <-idle.C:
			return false, "no answer from the PCjr"
		}
	}
}

func (n *native) print(style byte, text string) {
	n.temp = false
	n.wrap.style = style
	n.send(n.wrap.feed(term.ToASCII(text)), style)
}

func (n *native) flush() {
	n.send(n.wrap.flush(), n.wrap.style)
}

// endLine finishes the current transcript line, if it has text, so what
// comes next starts on a line of its own.
func (n *native) endLine() {
	n.flush()
	if n.wrap.col > 0 {
		n.print(n.wrap.style, "\n")
	}
}

func (n *native) send(text string, style byte) {
	if text == "" {
		return
	}
	if len(n.pend) > 0 && style != n.pendStyle {
		n.sendPending()
	}
	n.pendStyle = style
	n.pend = append(n.pend, text...)
	for len(n.pend) >= cjp.ChunkSize-1 {
		n.link.Send(cjp.Text, append([]byte{style}, n.pend[:cjp.ChunkSize-1]...))
		n.pend = n.pend[cjp.ChunkSize-1:]
	}
}

// ready tells a ui client that the user can type again.
func (n *native) ready() {
	if n.ui {
		n.sendPending()
		n.link.Send(cjp.Ready, nil)
	}
}

// showTemp puts a transient line in a ui client's transcript ("CodeJr is
// thinking"); the next text erases it.
func (n *native) showTemp(msg string) {
	if !n.ui {
		return
	}
	n.endLine()
	n.sendPending()
	n.link.Send(cjp.Temp, []byte(msg))
	n.temp = true
}

// clearTemp erases a Temp line nothing has replaced.
func (n *native) clearTemp() {
	if n.temp {
		n.print(cjp.StylePlain, "\n") // the client drops the line instead
	}
}

// sendPending sends collected text now.
func (n *native) sendPending() {
	if len(n.pend) > 0 {
		n.link.Send(cjp.Text, append([]byte{n.pendStyle}, n.pend...))
		n.pend = n.pend[:0]
	}
}

func (n *native) status(msg string) {
	n.sendPending()
	if len(msg) > cjp.MaxPayload {
		msg = msg[:cjp.MaxPayload]
	}
	n.link.Send(cjp.Status, []byte(term.ToASCII(msg)))
}

// submit handles one typed line. It returns false to hang up.
func (n *native) submit(line string) bool {
	s := n.s
	if line == "" {
		return true
	}
	n.endLine()
	if strings.HasPrefix(line, "/") {
		return n.command(line)
	}
	if !n.ui { // a ui client showed the line the moment it was typed
		n.print(cjp.StyleUser, "> "+line+"\n")
	}
	n.showTemp("CodeJr is thinking...")
	n.turn(line)
	n.clearTemp()
	n.endLine()
	n.sync()
	n.status("Ready.  Enter sends,  /help for commands,  Alt-X quits.")
	s.logf("native turn done; %s", n.link.Stats())
	return true
}

const nativeHelp = `Commands:
  /files       list your programs (* = not on the PCjr yet)
  /sync        send files the PCjr doesn't have yet
  /send NAME   send a file again
  /new         start a fresh conversation
  /run         number your programs; type a number to run one
  /quit        hang up (or press Alt-X)
Alt-S shows the line's error counters.
`

func (n *native) command(line string) bool {
	s := n.s
	f := strings.Fields(line)
	cmd, arg := strings.ToLower(f[0]), ""
	if len(f) > 1 {
		arg = strings.ToUpper(f[1])
	}
	if !n.ui {
		n.print(cjp.StyleUser, line+"\n")
	}
	switch cmd {
	case "/help", "/?":
		n.print(cjp.StyleNote, nativeHelp)
	case "/new":
		s.ws.SetSession("")
		n.print(cjp.StyleNote, "Fresh conversation. CodeJr has forgotten the earlier chat; files stay.\n")
	case "/files", "/dir", "/ls":
		entries, _, err := s.ws.Scan()
		if err != nil {
			n.print(cjp.StyleWarn, err.Error()+"\n")
			break
		}
		if len(entries) == 0 {
			n.print(cjp.StyleNote, "No files yet in "+ProgDir+".\n")
		}
		for _, e := range entries {
			if e.Problem != "" {
				n.print(cjp.StyleWarn, fmt.Sprintf("  %-12s  %s\n", e.Name, e.Problem))
				continue
			}
			mark := " "
			if !e.OnPCjr {
				mark = "*"
			}
			n.print(cjp.StyleNote, fmt.Sprintf("%s %-12s %7d bytes\n", mark, e.Name, e.Size))
		}
	case "/sync":
		n.sync()
	case "/send":
		if !ValidName(arg) {
			n.print(cjp.StyleWarn, "Usage: /send NAME.EXT\n")
			break
		}
		s.ws.Forget(arg)
		n.sync()
	case "/quit", "/bye", "/exit":
		n.print(cjp.StyleNote, "Bye! Hanging up.\n")
		return false
	default:
		n.print(cjp.StyleWarn, "Unknown command. Type /help.\n")
	}
	return true
}

// turn runs one Claude reply.
func (n *native) turn(prompt string) {
	s := n.s
	session := s.ws.Session()
	err := n.runClaude(session, prompt)
	if err != nil && session != "" && err != context.Canceled {
		s.logf("resume failed (%v), retrying fresh", err)
		s.ws.SetSession("")
		n.flush()
		n.print(cjp.StyleNote, "\n(Couldn't continue the old conversation; starting a fresh one.)\n")
		err = n.runClaude("", prompt)
	}
	n.flush()
	switch {
	case err == context.Canceled:
		n.print(cjp.StyleWarn, "\nStopped.\n")
	case err != nil:
		n.print(cjp.StyleWarn, "\nCodeJr error: "+err.Error()+"\n")
	}
}

func (n *native) runClaude(session, prompt string) error {
	s := n.s
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	cfg := s.cfg.Claude
	cfg.SystemPrompt += nativeNote + contextNote(time.Now(), s.cfg.Location)
	events := make(chan claude.Event, 256)
	errc := make(chan error, 1)
	go func() {
		errc <- claude.Run(ctx, cfg, session, prompt, func(e claude.Event) { events <- e })
		close(events)
	}()

	start := time.Now()
	activity := "CodeJr is thinking"
	tick := time.NewTicker(time.Second)
	defer tick.Stop()
	textTick := time.NewTicker(100 * time.Millisecond)
	defer textTick.Stop()
	status := func() {
		n.status(fmt.Sprintf("%s  %ds   (Ctrl-C stops)", activity, int(time.Since(start).Seconds())))
	}
	status()
	s.logf("turn: %q", prompt)
	for {
		select {
		case e, ok := <-events:
			if !ok {
				err := <-errc
				if err == nil && ctx.Err() != nil {
					err = context.Canceled
				}
				return err
			}
			switch e.Kind {
			case claude.Text:
				n.print(cjp.StylePlain, e.Text)
			case claude.Thinking:
				activity = "CodeJr is thinking"
			case claude.ToolUse:
				note, act := describeTool(e)
				activity = act
				if note != "" {
					n.flush()
					if n.wrap.col > 0 {
						n.print(cjp.StylePlain, "\n")
					}
					n.print(cjp.StyleNote, "  "+note+"\n")
					n.showTemp("CodeJr is working...")
				}
			case claude.Done:
				if e.SessionID != "" {
					s.ws.SetSession(e.SessionID)
				}
				if e.IsError {
					n.flush()
					n.print(cjp.StyleWarn, "\nCodeJr: "+firstLine(e.Result)+"\n")
				}
			}
		case <-tick.C:
			status()
		case <-textTick.C:
			n.sendPending()
		case m := <-n.link.Recv():
			if m.Type == cjp.Key && len(m.Payload) > 0 && m.Payload[0] == cjp.KeyCtrlC {
				cancel()
			}
			if m.Type == cjp.Bye {
				cancel()
			}
		case <-n.link.Done():
			cancel()
			return context.Canceled
		}
	}
}

// sync sends every file the PCjr doesn't have yet.
func (n *native) sync() {
	s := n.s
	files, hashes, problems, err := s.ws.Pending()
	if err != nil {
		n.print(cjp.StyleWarn, err.Error()+"\n")
		return
	}
	for _, p := range problems {
		n.print(cjp.StyleWarn, fmt.Sprintf("Not sending %s: %s.\n", p.Name, p.Problem))
	}
	n.sendPending()
	for _, f := range files {
		if !n.pull && len(f.Data) > kermitMax {
			// The first client's transfers are slow; it only needs to
			// fetch its replacement.
			n.print(cjp.StyleNote, fmt.Sprintf("%s (%s) will come once you restart with the new CODEJR.EXE.\n", f.Name, size(len(f.Data))))
			continue
		}
		start := time.Now()
		var ok bool
		var msg string
		if n.pull {
			n.status(fmt.Sprintf("Sending %s (%s)...", f.Name, size(len(f.Data))))
			n.link.Flush(10 * time.Second) // the line must be quiet before bursts
			ok, msg = n.sendPull(f.Name, f.Data)
		} else {
			// The first client (0.1): every frame acknowledged in turn.
			var hdr [4]byte
			binary.LittleEndian.PutUint32(hdr[:], uint32(len(f.Data)))
			if err := n.link.Send(cjp.FileBegin, append(hdr[:], f.Name...)); err != nil {
				return
			}
			if len(f.Data) > 0 {
				n.link.SendChunks(cjp.FileData, nil, f.Data)
			}
			n.link.Send(cjp.FileEnd, nil)
			ok, msg = n.waitFileResult(f.Name, 30*time.Second)
		}
		if !ok {
			s.logf("native: %s failed: %s", f.Name, msg)
			n.print(cjp.StyleWarn, fmt.Sprintf("Couldn't save %s on the PCjr: %s\n", f.Name, msg))
			continue
		}
		s.ws.MarkSynced(map[string]string{f.Name: hashes[f.Name]})
		el := time.Since(start)
		s.logf("native: sent %s (%d bytes) in %s, %.0f bytes/s; %s", f.Name, len(f.Data), el.Round(100*time.Millisecond), float64(len(f.Data))/el.Seconds(), n.link.Stats())
		if !n.pull {
			// Pull clients print their own line, with the line's speed.
			n.print(cjp.StyleNote, fmt.Sprintf("Saved %s (%s) in %s, %s.\n", f.Name, size(len(f.Data)), ProgDir, bps(len(f.Data), el)))
		}
	}
}

func (n *native) waitFileResult(name string, timeout time.Duration) (bool, string) {
	deadline := time.After(timeout)
	for {
		select {
		case m := <-n.link.Recv():
			if m.Type != cjp.FileResult || len(m.Payload) == 0 {
				continue // typing during a transfer is dropped
			}
			if m.Payload[0] == 0 {
				return true, ""
			}
			return false, string(m.Payload[1:])
		case <-n.link.Done():
			return false, "connection lost"
		case <-deadline:
			return false, "no answer from the PCjr"
		}
	}
}

// wrapper word-wraps streamed text at 79 columns, so the client only has
// to handle newlines. Words are held until a space or newline shows where
// they end.
type wrapper struct {
	col   int
	word  []byte
	style byte
}

const wrapWidth = 79

func (w *wrapper) feed(text string) string {
	var out strings.Builder
	for i := 0; i < len(text); i++ {
		c := text[i]
		switch c {
		case '\n':
			out.WriteString(w.flushWord())
			out.WriteByte('\n')
			w.col = 0
		case ' ':
			out.WriteString(w.flushWord())
			if w.col < wrapWidth {
				out.WriteByte(' ')
				w.col++
			}
		default:
			w.word = append(w.word, c)
			if len(w.word) >= wrapWidth {
				out.WriteString(w.flushWord())
			}
		}
	}
	return out.String()
}

func (w *wrapper) flushWord() string {
	if len(w.word) == 0 {
		return ""
	}
	s := ""
	if w.col+len(w.word) > wrapWidth {
		s = "\n"
		w.col = 0
	}
	s += string(w.word)
	w.col += len(w.word)
	w.word = w.word[:0]
	return s
}

func (w *wrapper) flush() string { return w.flushWord() }
