package session

import (
	"context"
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
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
files arrive with an on-screen progress bar. You have two tools for the
real machine: dos runs a DOS command there and gives you what it printed
(DIR, TREE, TYPE, MEM and other commands that only look or create run at
once; for anything that could delete or change something, the PCjr asks
the user to press Y first, so say why you need it), and fetch copies a
file from it into your folder. run_on_pcjr runs a program from a project
folder on the real machine and returns what it printed, its exit code,
the text screen it left and its LOG.TXT: after the emulator passes, use
it to check real speed and behavior (the user sees it run). It can press
keys for the program on a schedule (keys: "{2}{SPACE}{3}q" waits 2
seconds, presses Space, waits 3, presses q) and take a picture of a
graphics screen (screenshot: seconds after the start, or "last"), which
you get as an image in the standard 16 colors. Make sure the program
ends by itself or by a scripted key: one that hangs there can only be
stopped by the user. The user
has the same as /dos COMMAND and /send NAME. To run a program the user
types /run (a numbered list of programs, including those in project
folders; a number runs one, and CodeJr calls back when it ends). End BASIC programs with SYSTEM rather than END,
so they return to DOS and CodeJr comes back by itself. You cannot see the PCjr's screen; if the user reports a
display problem, say so and suggest /help, rather than guessing settings.`

// maxLine is the longest typed line the server puts together from pieces.
const maxLine = 4000

type native struct {
	s    *session
	link *cjp.Link
	// For /usage: this call's counts, the projects the turn in progress
	// has touched, what the turn cost, and the last transfer's bytes on
	// the line.
	call     Stats
	touched  map[string]bool
	turnOut  int
	turnCost float64
	lineSent int
	wrap     wrapper
	pull     bool // the client fetches files with the pull protocol (CODEJR/2)
	// Messages that arrived while a file was going over (a line typed
	// early, say); handled once the transfer is done.
	backlog []cjp.Msg
	long    []byte // the pieces so far of a line too long for one frame (InputMore)
	baud    int    // the serial speed the client reported
	// The client echoes typed lines itself and takes Ready and Temp
	// messages (it said ui=1 in its HELLO).
	ui       bool
	temp     bool            // a Temp line is showing
	dirs     bool            // the client makes project folders (dirs=1)
	delta    bool            // the client applies edit scripts (delta=1)
	voice    bool            // the client plays clips (voice=1)
	remote   bool            // the client runs commands and sends files (remote=1)
	runs     bool            // the client runs programs in their folders (run=1)
	sys      bool            // the client keeps CodeJr's own files in C:\CODEJR (sys=1)
	keys     bool            // the client presses scripted keys and takes screenshots (keys=1)
	ctl      chan ctlReq     // requests from the control socket (see remote.go)
	reply    strings.Builder // this turn's reply text, to speak
	playNext string          // a clip to play once this turn's files are over
	// A run report arriving from the client (Result messages).
	result     strings.Builder
	resultName string

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
	n := &native{s: s, link: link, ctl: make(chan ctlReq)}
	setActive(n)
	defer setActive(nil)
	began := time.Now()
	n.count("", func(st *Stats) { st.Calls++ })
	defer func() {
		secs := int(time.Since(began).Seconds())
		resent := link.ResentCount()
		n.count("", func(st *Stats) { st.ConnectedSec += secs; st.Resent += resent })
	}()

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
		// t= is this laptop's local time: the client sets the PCjr's
		// clock from it, so files written there get a real date.
		link.Send(cjp.Hello, []byte("CODEJR/2 server ui results t="+time.Now().Format("20060102150405")))
	} else {
		link.Send(cjp.Hello, []byte("CODEJR/2 server"))
	}

	n.print(cjp.StyleBanner, "Welcome to CodeJr. Ask me anything or let's make a program! /help for commands.\n")
	if s.ws.Session() != "" {
		n.print(cjp.StyleNote, "Continuing your last conversation.\n")
		// What CodeJr said last, so the screen picks up where it left off.
		if chars, last := s.ws.Convo(); last != "" {
			n.print(cjp.StylePlain, strings.TrimSpace(last)+"\n")
			n.info(chars)
		}
	}
	n.status("Ready.")
	n.sync()
	n.sendPending()
	n.sendVoiceState()
	n.ready()

	for {
		var m cjp.Msg
		if len(n.backlog) > 0 {
			m, n.backlog = n.backlog[0], n.backlog[1:]
		} else {
			select {
			case m = <-link.Recv():
			case r := <-n.ctl:
				n.handleCtl(r)
				continue
			case <-link.Done():
				return
			}
		}
		switch m.Type {
		case cjp.InputMore:
			if len(n.long) < maxLine { // the client stops at 1,000
				n.long = append(n.long, m.Payload...)
			}
		case cjp.Input:
			line := string(append(n.long, m.Payload...))
			n.long = nil
			ok := n.submit(strings.TrimSpace(line))
			n.sendPending()
			if ok {
				n.ready()
			}
			if !ok {
				link.Send(cjp.Bye, nil)
				link.Flush(5 * time.Second)
				return
			}
		case cjp.Result:
			n.takeResult(m.Payload)
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
		if f == "dirs=1" {
			n.dirs = true
		}
		if f == "delta=1" {
			n.delta = true
		}
		if f == "keys=1" {
			n.keys = true
		}
		if f == "sys=1" {
			n.sys = true
		}
		if f == "run=1" {
			n.runs = true
		}
		if f == "remote=1" {
			n.remote = true
		}
		if f == "voice=1" {
			n.voice = true
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
	return n.serve(name, chunks, cjp.FileOffer, cjp.OfferPayload(name, len(data), len(chunks)))
}

// sendDelta sends only what changed since the copy the PCjr has: an edit
// script, pulled like a file. It returns ok=false with used=false when
// the script wouldn't save much, so the caller sends the whole file.
func (n *native) sendDelta(name string, base, data []byte) (ok, used bool, msg string) {
	script := cjp.Delta(base, data)
	chunks := cjp.MakeChunks(script)
	if chunksLen(chunks)*10 > chunksLen(cjp.MakeChunks(data))*7 {
		return false, false, ""
	}
	n.s.logf("native: %s: sending changes, %d bytes of script for %d bytes", name, len(script), len(data))
	ok, msg = n.serve(name, chunks, cjp.DeltaOffer,
		cjp.DeltaOfferPayload(name, len(data), len(chunks), len(script), base, cjp.CRC16(data)))
	return ok, true, msg
}

func chunksLen(cs [][]byte) (n int) {
	for _, c := range cs {
		n += len(c)
	}
	return n
}

// serve offers chunks and answers the client's pulls until it reports.
func (n *native) serve(name string, chunks [][]byte, offerType byte, offer []byte) (bool, string) {
	if err := n.link.Send(offerType, offer); err != nil {
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
				n.lineSent = sent
				if m.Payload[0] == 0 {
					return true, ""
				}
				return false, string(m.Payload[1:])
			case cjp.Input, cjp.InputMore, cjp.Bye, cjp.Result:
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

// info puts the conversation's size at the right end of the client's
// header (clients from 1.5; older ones ignore the frame).
func (n *native) info(chars int) {
	if n.ui && chars > 0 {
		n.sendPending()
		n.link.Send(cjp.Info, []byte(fmt.Sprintf("%d chars", chars)))
	}
}

// ready tells a ui client that the user can type again. A blank line
// first, so the user's next line stands apart from what came before.
func (n *native) ready() {
	if n.ui {
		n.endLine()
		n.print(n.wrap.style, "\n")
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
	n.reply.Reset()
	n.turn(line)
	n.clearTemp()
	n.endLine()
	n.speak()
	n.sync()
	n.playQueued()
	n.tidy()
	n.status("Ready.  Enter sends,  /help for commands,  /exit quits.")
	s.logf("native turn done; %s", n.link.Stats())
	return true
}

const nativeHelp = `Commands:
  /files       list your programs (* = not on the PCjr yet)
  /sync        send files the PCjr doesn't have yet
  /send NAME   send a file from this PCjr to CodeJr
  /get NAME    get a file from CodeJr again
  /dos CMD     run a DOS command; CodeJr sees what it printed
  /trust       let CodeJr run programs and change files without asking (on/off)
  /new         start a fresh conversation
  /run         number your programs; type a number to run one
  /voice       speak replies aloud (on/off); replies get short
  /usage       what CodeJr has done: messages, files, time (/usage NAME: one project)
  /exit        hang up (or press Alt-X)
Alt-S shows the line's error counters. PgUp, PgDn and the arrows scroll
back through the chat; a long reply stops at -- More -- (Space goes on).
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
	case "/get":
		// Names read from the PCjr's seat: /get brings CodeJr's copy
		// here again, /send sends this PCjr's file to CodeJr.
		if !ValidPath(arg) {
			n.print(cjp.StyleWarn, "Usage: /get NAME.EXT  (or FOLDER\\NAME.EXT)\n")
			break
		}
		s.ws.Forget(arg)
		n.sync()
	case "/send":
		raw := strings.TrimSpace(line[len(f[0]):])
		n.status("Sending " + strings.ToUpper(raw) + " to CodeJr...")
		size, err := n.fetch(raw)
		if err != nil {
			n.print(cjp.StyleWarn, "Couldn't send it: "+err.Error()+"\n")
			break
		}
		n.print(cjp.StyleNote, fmt.Sprintf("CodeJr has %s now (%d bytes).\n", strings.ToUpper(raw), size))
	case "/dos":
		raw := strings.TrimSpace(line[len(f[0]):])
		out, err := n.exec(raw, true) // the user typed it: no question
		if err != nil {
			n.print(cjp.StyleWarn, "Couldn't run it: "+err.Error()+"\n")
			break
		}
		if strings.TrimSpace(out) == "" {
			out = "(it printed nothing)\n"
		}
		n.print(cjp.StylePlain, strings.TrimRight(out, "\n")+"\n")
		// CodeJr sees it with the next message.
		if len(out) > 6000 {
			out = out[:6000] + "\n... (cut)"
		}
		s.ws.SetLastRun("DOS command the user ran with /dos: " + raw + "\nWhat it printed:\n" + out)
	case "/trust":
		// Only the PCjr's own keyboard can flip this.
		on := !s.ws.Flag("trust")
		s.ws.SetFlag("trust", on)
		if on {
			n.print(cjp.StyleNote, "Trust on: CodeJr may run programs and change files on this PCjr without\nasking first. /trust turns it off.\n")
		} else {
			n.print(cjp.StyleNote, "Trust off: CodeJr asks (Y or N) before anything that runs a program or\ncould delete or change a file. Looking (DIR, TYPE) never asks.\n")
		}
	case "/usage", "/stats":
		n.print(cjp.StylePlain, s.ws.UsageText(arg, &n.call))
	case "/voice":
		n.toggleVoice()
	case "/quit", "/bye", "/exit":
		n.print(cjp.StyleNote, "Bye! Hanging up.\n")
		return false
	default:
		n.print(cjp.StyleWarn, "Unknown command. Type /help.\n")
	}
	return true
}

// turn runs one Claude reply.
// takeResult collects the report of a program the user ran with /run:
// 'B' + its name, then 'D' + text pieces, then 'E'. The report is kept
// for Claude's next turn.
func (n *native) takeResult(p []byte) {
	if len(p) == 0 {
		return
	}
	switch p[0] {
	case 'B':
		n.result.Reset()
		n.resultName = string(p[1:])
	case 'D':
		if n.result.Len() < 16<<10 {
			n.result.Write(p[1:])
		}
	case 'E':
		report := fmt.Sprintf("Program: %s\n%s", n.resultName, n.result.String())
		if err := n.s.ws.SetLastRun(report); err != nil {
			n.s.logf("native: saving run report: %v", err)
			return
		}
		n.s.logf("native: run report for %s (%d bytes)", n.resultName, len(report))
		n.print(cjp.StyleNote, fmt.Sprintf("CodeJr has the results of %s: ask about it any time.\n", n.resultName))
		n.result.Reset()
	}
}

func (n *native) turn(prompt string) {
	s := n.s
	session := s.ws.Session()
	// What happened when the user last ran a program, once.
	if r := s.ws.LastRun(); r != "" {
		prompt = "[The user ran a program on the real PCjr since your last reply. What the PCjr reported:\n" +
			r + "\n]\n\n" + prompt
		s.ws.SetLastRun("")
	}
	typed := len(prompt)
	began := time.Now()
	n.touched = map[string]bool{}
	n.turnOut, n.turnCost = 0, 0
	defer func() {
		// The turn counts once overall and once for each project it
		// touched (a build, a run, a file written there).
		secs, reply, out, cost := int(time.Since(began).Seconds()), n.reply.Len(), n.turnOut, n.turnCost
		add := func(st *Stats) {
			st.Turns++
			st.TypedChars += typed
			st.ReplyChars += reply
			st.TurnSec += secs
			st.OutTokens += out
			st.CostUSD += cost
		}
		n.count("", add)
		for p := range n.touched {
			n.countProject(p, add)
		}
		n.touched = nil
	}()
	err := n.runClaude(session, prompt)
	if err != nil && session != "" && err != context.Canceled {
		s.logf("resume failed (%v), retrying fresh", err)
		s.ws.SetSession("")
		n.flush()
		n.print(cjp.StyleNote, "\n(Couldn't continue the old conversation; starting a fresh one.)\n")
		err = n.runClaude("", prompt)
	}
	n.flush()
	if err == nil {
		// For the next call's welcome: the last reply, and how long the
		// conversation has grown.
		chars, _ := s.ws.Convo()
		chars += len(prompt) + n.reply.Len()
		s.ws.SetConvo(chars, n.reply.String())
		n.info(chars)
	}
	switch {
	case err == context.Canceled:
		n.print(cjp.StyleWarn, "\nStopped.\n")
	case err != nil:
		n.print(cjp.StyleWarn, "\nCodeJr error: "+err.Error()+"\n")
	}
}

// count adds to the usage record (all time, today, the project if there
// is one) and to this call's own counts.
func (n *native) count(project string, add func(*Stats)) {
	add(&n.call)
	if err := n.s.ws.AddUsage(project, add); err != nil {
		n.s.logf("usage: %v", err)
	}
}

// countProject adds to one project's row only (the rest was counted).
func (n *native) countProject(project string, add func(*Stats)) {
	if err := n.s.ws.AddProjectUsage(project, add); err != nil {
		n.s.logf("usage: %v", err)
	}
}

// countTool counts one tool call, for the project it names if it does.
func (n *native) countTool(e claude.Event) {
	project := ""
	if p, ok := e.Input["project"].(string); ok && validDir(strings.ToUpper(p)) {
		project = strings.ToUpper(p)
	} else if p, ok := e.Input["file_path"].(string); ok {
		if rel, err := filepath.Rel(n.s.ws.Dir, p); err == nil && !strings.HasPrefix(rel, "..") {
			project = projectOf(filepath.ToSlash(rel))
		}
	}
	var add func(*Stats)
	switch e.Name {
	case "Write", "Edit", "MultiEdit":
		add = func(st *Stats) { st.Edits++ }
	case "mcp__pcjr__build":
		add = func(st *Stats) { st.Builds++ }
	case "mcp__pcjr__run":
		add = func(st *Stats) { st.EmuRuns++ }
	case "mcp__pcjr__run_on_pcjr":
		add = func(st *Stats) { st.PCjrRuns++ }
	case "WebSearch", "WebFetch":
		add = func(st *Stats) { st.Web++ }
	default:
		return
	}
	if project == strings.ToUpper(LibDir) {
		project = "" // the library is everybody's
	}
	if project != "" && n.touched != nil {
		n.touched[project] = true
	}
	n.count(project, add)
}

// compactPrompt is Claude Code's own command for replacing a conversation
// with a summary of it.
const compactPrompt = "/compact"

// tidyAbove is the conversation size, in tokens, past which it is
// shortened after a turn. A resumed conversation is read in full at the
// start of every turn (measured 2026-10-08: nothing of it comes from the
// cache, whatever the system prompt), so a long one makes every answer
// slower to start.
const tidyAbove = 80000

// tidy shortens a conversation that has grown long, after the reply and
// its files are on the PCjr, while the user reads. Lines typed meanwhile
// wait in the client. Ctrl-C skips it.
func (n *native) tidy() {
	s := n.s
	session, before := s.ws.Session(), s.ws.Context()
	if session == "" || before < tidyAbove {
		return
	}
	s.logf("conversation is %d tokens: tidying", before)
	err := n.runClaude(session, compactPrompt)
	n.flush()
	after := s.ws.Context()
	switch {
	case err != nil:
		s.logf("tidying failed: %v", err)
		s.ws.SetContext(0) // not again after every turn; the next turn measures it anew
	case after < before:
		n.print(cjp.StyleNote, fmt.Sprintf("(Long conversation tidied up: %s tokens down to %s. CodeJr keeps a summary.)\n", kilo(before), kilo(after)))
	}
}

// kilo writes a token count the short way: 850, 1.2K, 153K.
func kilo(n int) string {
	switch {
	case n < 1000:
		return strconv.Itoa(n)
	case n < 10000:
		return fmt.Sprintf("%.1fK", float64(n)/1000)
	}
	return fmt.Sprintf("%dK", (n+500)/1000)
}

// describeWriting is the status line while Claude is still writing a tool
// call: a long file takes minutes, and nothing else shows meanwhile.
func describeWriting(e claude.Event) string {
	file := strings.ToUpper(baseName(e.Text))
	size := ""
	if e.Bytes >= 1024 {
		size = fmt.Sprintf(", %d KB so far", e.Bytes/1024)
	}
	switch e.Name {
	case "Write":
		if file == "" {
			return "CodeJr is writing a file"
		}
		return "CodeJr is writing " + file + size
	case "Edit", "MultiEdit":
		if file == "" {
			return "CodeJr is changing a file"
		}
		return "CodeJr is changing " + file + size
	}
	return "CodeJr is getting a tool ready"
}

func (n *native) runClaude(session, prompt string) error {
	s := n.s
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	cfg := s.cfg.Claude
	cfg.SystemPrompt += nativeNote + contextNote(time.Now(), s.cfg.Location)
	if n.voiceOn() {
		cfg.SystemPrompt += voiceNote
	} else {
		cfg.SystemPrompt += progressNote
	}
	events := make(chan claude.Event, 256)
	errc := make(chan error, 1)
	go func() {
		errc <- claude.Run(ctx, cfg, session, prompt, func(e claude.Event) { events <- e })
		close(events)
	}()

	start := time.Now()
	activity := "CodeJr is thinking"
	tidy := prompt == compactPrompt
	if tidy {
		activity = "Tidying up a long conversation"
	}
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
			if tidy && e.Kind != claude.Done && e.Kind != claude.Compacted {
				continue // nothing of a tidy-up is for the screen
			}
			switch e.Kind {
			case claude.Text:
				activity = "CodeJr is answering"
				n.print(cjp.StylePlain, e.Text)
				n.reply.WriteString(e.Text)
			case claude.Thinking:
				activity = "CodeJr is thinking"
			case claude.Thought:
				activity = "CodeJr is thinking, " + kilo(e.Tokens) + " tokens so far"
			case claude.ToolInput:
				activity = describeWriting(e)
			case claude.Compacting:
				activity = "Tidying up a long conversation"
			case claude.Compacted:
				s.ws.SetContext(e.Tokens)
				s.logf("conversation tidied to %d tokens", e.Tokens)
			case claude.ToolUse:
				if !tidy {
					n.countTool(e)
				}
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
				if e.Tokens > 0 && !tidy {
					s.ws.SetContext(e.Tokens)
				}
				n.turnOut += e.OutTokens
				n.turnCost += e.CostUSD
				if e.IsError {
					n.flush()
					n.print(cjp.StyleWarn, "\nCodeJr: "+firstLine(e.Result)+"\n")
				}
			}
		case <-tick.C:
			status()
		case <-textTick.C:
			n.sendPending()
		case r := <-n.ctl:
			// One of Claude's tools wants something from the PCjr.
			n.handleCtl(r)
		case m := <-n.link.Recv():
			if m.Type == cjp.Key && len(m.Payload) > 0 && m.Payload[0] == cjp.KeyCtrlC {
				cancel()
			}
			if m.Type == cjp.Bye {
				cancel()
			}
			if m.Type == cjp.Result {
				n.backlog = append(n.backlog, m) // after the reply
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
		if IsSystem(f.Name) && !n.sys {
			// A client from before CodeJr's files moved to C:\CODEJR: it
			// can't write there. Give it the new client at the old place
			// (the top of PROGS); the rest comes once it runs that.
			old := f.Name[len(SysPrefix):]
			if old != "CODEJR.EXE" || !n.pull || s.ws.SyncedHash(old) == hashes[f.Name] {
				continue
			}
			n.status("Sending the new CODEJR.EXE...")
			n.link.Flush(10 * time.Second)
			if ok, msg := n.sendPull(old, f.Data); ok {
				s.ws.MarkSynced(map[string]string{old: hashes[f.Name]})
				s.logf("native: sent %s to an older client (%d bytes)", old, len(f.Data))
				n.print(cjp.StyleNote, "This CodeJr is out of date. Type /exit, then CODEJR, to start the new one.\n")
			} else {
				s.logf("native: %s failed: %s", old, msg)
			}
			continue
		}
		if InFolder(f.Name) && !n.dirs {
			n.print(cjp.StyleNote, fmt.Sprintf("%s is in a project folder; it comes once CodeJr is updated.\n", f.Name))
			continue
		}
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
			used := false
			if base := s.ws.Have(f.Name); n.delta && base != nil {
				ok, used, msg = n.sendDelta(f.Name, base, f.Data)
				if used && !ok {
					// Most likely the PCjr's copy isn't the one we kept.
					s.logf("native: %s: changes refused (%s), sending it whole", f.Name, msg)
					n.link.Flush(10 * time.Second)
				}
			}
			if !used || !ok {
				ok, msg = n.sendPull(f.Name, f.Data)
			}
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
		if err := s.ws.SetHave(f.Name, f.Data); err != nil {
			s.logf("native: keeping a copy of %s: %v", f.Name, err)
		}
		el := time.Since(start)
		{
			size, line, secs := int64(len(f.Data)), int64(n.lineSent), int(el.Seconds()+0.5)
			if !n.pull {
				line = size
			}
			n.count(projectOf(f.Name), func(st *Stats) {
				st.FilesSent++
				st.FileBytes += size
				st.LineBytes += line
				st.SendSec += secs
			})
			n.lineSent = 0
		}
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

// ---- voice (see voice.go) ----

// voiceOn: this client plays clips, the server can make them, and the
// user turned voice on.
func (n *native) voiceOn() bool {
	return n.voice && n.s.cfg.Speech != "" && n.s.ws.Flag("voice")
}

func (n *native) sendVoiceState() {
	if !n.voice {
		return
	}
	b := byte(0)
	if n.voiceOn() {
		b = 1
	}
	n.link.Send(cjp.Voice, []byte{b})
}

func (n *native) toggleVoice() {
	s := n.s
	if !n.voice {
		n.print(cjp.StyleWarn, "This CODEJR.EXE can't play speech yet; it updates itself on the next call.\n")
		return
	}
	if s.cfg.Speech == "" {
		n.print(cjp.StyleWarn, "Voice isn't set up on the server (no speech converter).\n")
		return
	}
	on := !s.ws.Flag("voice")
	s.ws.SetFlag("voice", on)
	n.sendVoiceState()
	clip := "OFF"
	if on {
		clip = "ON"
		n.print(cjp.StyleNote, "Voice on: replies are spoken too, and kept to a sentence or two.\n")
	} else {
		n.print(cjp.StyleNote, "Voice off.\n")
	}
	n.sync() // the fixed clips, if the PCjr doesn't have them yet
	n.queuePlay(VoiceDir + `\` + clip + ".SND")
	n.playQueued()
}

// speak turns this turn's reply into VOICE\REPLY.SND, if voice is on.
func (n *native) speak() {
	// The screen keeps the reply as written; the voice gets it spelled out
	// ("PCjr" as "PC Junior", "=" as "equals").
	text := Speakable(spokenText(n.reply.String()))
	if !n.voiceOn() || text == "" {
		return
	}
	n.status("Making speech...")
	start := time.Now()
	if err := MakeClip(context.Background(), n.s.cfg.Speech, n.s.ws.Dir, "REPLY", text); err != nil {
		n.s.logf("voice: %v", err)
		n.print(cjp.StyleWarn, "(Couldn't make speech for that reply.)\n")
		return
	}
	n.s.logf("voice: made the reply clip in %s (%d characters)", time.Since(start).Round(100*time.Millisecond), len(text))
	n.queuePlay(VoiceDir + `\REPLY.SND`)
}

func (n *native) queuePlay(name string) { n.playNext = name }

// playQueued tells the client to play the waiting clip; it does so when
// the turn ends (READY), when the line is quiet.
func (n *native) playQueued() {
	if n.playNext != "" && n.voice {
		n.sendPending()
		n.link.Send(cjp.Play, []byte(n.playNext))
	}
	n.playNext = ""
}
