package session

import (
	"errors"
	"fmt"
	"os"
	"strings"
	"sync"
	"time"

	"codejr/internal/cjp"
)

// Remote work on the PCjr: the server can ask a client that said
// remote=1 to run a DOS command and send back what it printed (Exec), or
// to send a file (Fetch). The answer comes as Upload messages over the
// acknowledged link, which is the PCjr's clean direction: 'X' (a
// command's output follows) or 'B' + name, then 'D' + bytes..., then 'E';
// or 'F' + a reason.
//
// Three callers: the user (/dos, /send), the PCjr's Claude (the dos and
// fetch tools, through the control socket) and whoever is at the laptop
// (`just pcjr`, the same socket).

const maxUpload = 512 << 10

// collect waits for one upload to finish.
func (n *native) collect(timeout time.Duration) ([]byte, error) {
	var data []byte
	idle := time.NewTimer(timeout)
	defer idle.Stop()
	for {
		select {
		case m := <-n.link.Recv():
			if m.Type != cjp.Upload {
				switch m.Type {
				case cjp.Input, cjp.InputMore, cjp.Bye, cjp.Result:
					n.backlog = append(n.backlog, m)
				}
				continue
			}
			if len(m.Payload) == 0 {
				continue
			}
			idle.Reset(timeout)
			switch m.Payload[0] {
			case 'D':
				if len(data) < maxUpload {
					data = append(data, m.Payload[1:]...)
				}
			case 'E':
				return data, nil
			case 'F':
				return nil, errors.New(string(m.Payload[1:]))
			}
		case <-n.link.Done():
			return nil, errors.New("the PCjr hung up")
		case <-idle.C:
			return nil, errors.New("no answer from the PCjr")
		}
	}
}

// safeVerbs are DOS commands that only look or create: they run without
// asking. Anything else (DEL, REN, COPY, FORMAT, a program, output sent
// to a file with >) is confirmed by the user at the PCjr first.
var safeVerbs = map[string]bool{
	"DIR": true, "TYPE": true, "TREE": true, "MEM": true, "VER": true, "VOL": true,
	"CHKDSK": true, "PATH": true, "SET": true, "ECHO": true, "FIND": true,
	"SORT": true, "MD": true, "MKDIR": true, "CD": true, "CHDIR": true,
}

// Prompting says why a command would sit waiting for typing on the PCjr,
// where nobody can answer and nothing can stop it from here ("" if it
// won't). Found the hard way: a bare DATE hung the real PCjr until Steve
// pressed Ctrl-Break. These are refused even with /trust on.
func Prompting(cmd string) string {
	for _, part := range strings.Split(cmd, "|") {
		f := strings.Fields(strings.ToUpper(part))
		if len(f) == 0 {
			continue
		}
		verb := f[0]
		if i := strings.IndexAny(verb, `/\.`); i > 0 {
			verb = verb[:i]
		}
		rest := strings.Join(f[1:], " ")
		switch verb {
		case "DATE", "TIME":
			if len(f) == 1 {
				return verb + " with nothing after it asks for a new value (the clock is set from the laptop at every connect)"
			}
		case "PAUSE", "MORE", "COMP", "EDLIN", "DEBUG", "COMMAND", "CTTY", "FORMAT", "LABEL", "DISKCOPY", "DISKCOMP", "GWBASIC", "BASIC", "BASICA":
			return verb + " waits for typing"
		case "DEL", "ERASE":
			if strings.Contains(rest, "*.*") || rest == "." {
				return verb + " *.* asks \"Are you sure\" (delete the files by name or pattern)"
			}
		case "DIR":
			if strings.Contains(strings.ToUpper(part), "/P") {
				return "DIR /P pauses at each page"
			}
		}
	}
	return ""
}

// SafeDOS reports whether a command line can run without asking.
func SafeDOS(cmd string) bool {
	if strings.ContainsAny(cmd, "><") {
		return false // redirection can overwrite a file
	}
	for _, part := range strings.Split(cmd, "|") {
		f := strings.Fields(part)
		if len(f) == 0 {
			return false
		}
		verb := strings.ToUpper(f[0])
		// DIR/W and CD\ are DIR and CD.
		if i := strings.IndexAny(verb, `/\.`); i > 0 {
			verb = verb[:i]
		}
		if !safeVerbs[verb] {
			return false
		}
		if verb == "CHKDSK" && strings.Contains(strings.ToUpper(part), "/F") {
			return false // /F repairs, which changes the disk
		}
	}
	return true
}

// exec runs a DOS command on the PCjr and returns what it printed. Unless
// the user typed it (trusted), a command that isn't in the safe list is
// shown on the PCjr and runs only if the user presses Y.
func (n *native) exec(cmd string, trusted bool) (string, error) {
	cmd = strings.TrimSpace(cmd)
	if !n.remote {
		return "", errors.New("this CODEJR.EXE can't run commands yet; it updates itself on the next call")
	}
	if cmd == "" || len(cmd) > 100 || strings.ContainsAny(cmd, "\r\n") {
		return "", errors.New("give one DOS command line, up to 100 characters")
	}
	if why := Prompting(cmd); why != "" {
		return "", errors.New("not run: " + why + ", which would hang the PCjr")
	}
	n.sendPending()
	n.s.logf("native: running on the PCjr: %s", cmd)
	payload := []byte(cmd)
	if !trusted && !n.s.ws.Flag("trust") && !SafeDOS(cmd) {
		payload = append([]byte{cjp.ExecAsk}, payload...)
	}
	if err := n.link.Send(cjp.Exec, payload); err != nil {
		return "", errors.New("the PCjr hung up")
	}
	b, err := n.collect(5 * time.Minute)
	if err != nil {
		return "", err
	}
	return string(fromDOS(b)), nil
}

// run starts a program in its project folder on the PCjr ("" = PROGS) and
// returns the whole story: what it printed, its exit code, the text
// screen it left and its LOG.TXT. Pending files go first, so the PCjr
// runs the newest build. Running a program can do anything, so it asks
// the user unless they typed /trust.
//
// keys is a key script to press while it runs (see parseKeys) and shot
// says when to take a picture of a graphics screen ("" none, seconds
// after the start, or "last"); the picture is saved as ShotPath.
func (n *native) run(folder, cmd, keys, shot string) (string, error) {
	folder = strings.ToUpper(strings.TrimSpace(folder))
	cmd = strings.TrimSpace(cmd)
	if folder == "." {
		folder = ""
	}
	if !n.runs {
		return "", errors.New("this CODEJR.EXE can't run programs for CodeJr yet; it updates itself on the next call")
	}
	if folder != "" && !validDir(folder) {
		return "", errors.New("the folder must be a project folder name (or . for the top)")
	}
	if cmd == "" || len(cmd) > 100 || strings.ContainsAny(cmd, "\r\n<>|") {
		return "", errors.New("give a program and its arguments, up to 100 characters, with no < > |")
	}
	n.sync()
	n.sendPending()
	n.link.Flush(10 * time.Second)
	n.s.logf("native: running on the PCjr in %q: %s", folder, cmd)
	payload := []byte(folder + "\n" + cmd)
	script, err := parseKeys(keys)
	if err != nil {
		return "", err
	}
	when, err := parseShot(shot)
	if err != nil {
		return "", err
	}
	if len(script) > 0 || when != 0 {
		if !n.keys {
			return "", errors.New("this CODEJR.EXE can't press keys or take screenshots yet; it updates itself on the next call")
		}
		payload = append(payload, 0, when, byte(len(script)))
		for _, k := range script {
			payload = append(payload, k.delay, k.scan, k.ascii)
		}
	}
	if !n.s.ws.Flag("trust") {
		payload = append([]byte{cjp.ExecAsk}, payload...)
	}
	if len(payload) > cjp.MaxPayload {
		return "", fmt.Errorf("the command and its keys come to %d bytes; %d is the most (use fewer keys or a shorter command)", len(payload), cjp.MaxPayload)
	}
	os.Remove(n.s.ws.ShotPath())
	if err := n.link.Send(cjp.Run, payload); err != nil {
		return "", errors.New("the PCjr hung up")
	}
	b, err := n.collect(10 * time.Minute)
	if err != nil {
		return "", err
	}
	out := string(fromDOS(b))
	if when == 0 {
		return out, nil
	}
	// The picture follows as a second upload (or the reason there is none).
	raw, err := n.collect(3 * time.Minute)
	if err != nil {
		return out + "\n--- no screenshot: " + err.Error() + "\n", nil
	}
	img, what, err := DecodeShot(raw)
	if err != nil {
		return out + "\n--- no screenshot: " + err.Error() + "\n", nil
	}
	if err := os.WriteFile(n.s.ws.ShotPath(), img, 0o644); err != nil {
		return out + "\n--- no screenshot: " + err.Error() + "\n", nil
	}
	n.s.logf("native: screenshot from the PCjr, %s, %d bytes of video memory", what, len(raw)-6)
	return out + fmt.Sprintf("\n--- screenshot: %s, saved as %s (standard colors: the PCjr's palette can't be read back)\n", what, n.s.ws.ShotPath()), nil
}

// fetch copies a file from the PCjr into the workspace.
func (n *native) fetch(name string) (int, error) {
	name = strings.ToUpper(strings.ReplaceAll(strings.TrimSpace(name), "/", `\`))
	if !n.remote {
		return 0, errors.New("this CODEJR.EXE can't send files yet; it updates itself on the next call")
	}
	if !ValidPath(name) {
		return 0, errors.New("give a file name: NAME.EXT or FOLDER\\NAME.EXT")
	}
	n.sendPending()
	if err := n.link.Send(cjp.Fetch, []byte(name)); err != nil {
		return 0, errors.New("the PCjr hung up")
	}
	b, err := n.collect(2 * time.Minute)
	if err != nil {
		return 0, err
	}
	if err := n.s.ws.SaveFromPCjr(name, b); err != nil {
		return 0, err
	}
	n.s.logf("native: got %s from the PCjr (%d bytes)", name, len(b))
	return len(b), nil
}

// ---- the control socket's side ----

type ctlReq struct {
	op, arg string
	reply   chan ctlResp
}

type ctlResp struct {
	out string
	err error
}

var (
	activeMu sync.Mutex
	active   *native // the native call in progress, if any
)

func setActive(n *native) {
	activeMu.Lock()
	active = n
	activeMu.Unlock()
}

// Control runs op ("dos" or "fetch") with arg on the PCjr that is connected
// now. It waits its turn: the call's own loop does the work between other
// messages.
func Control(op, arg string) (string, error) {
	activeMu.Lock()
	n := active
	activeMu.Unlock()
	if n == nil {
		return "", errors.New("the PCjr isn't connected (run CODEJR on it)")
	}
	r := ctlReq{op: op, arg: arg, reply: make(chan ctlResp, 1)}
	select {
	case n.ctl <- r:
	case <-n.link.Done():
		return "", errors.New("the PCjr hung up")
	case <-time.After(ctlWait(op)):
		return "", errors.New("the PCjr is busy (a file transfer?); try again")
	}
	select {
	case resp := <-r.reply:
		return resp.out, resp.err
	case <-n.link.Done():
		return "", errors.New("the PCjr hung up")
	}
}

// ctlWait is how long a request waits for the call's loop to take it.
func ctlWait(op string) time.Duration {
	if op == "restart" {
		return 4 * time.Second // the server is exiting; don't hold it up
	}
	return 30 * time.Second
}

func (n *native) handleCtl(r ctlReq) {
	var resp ctlResp
	switch r.op {
	case "dos":
		resp.out, resp.err = n.exec(r.arg, false)
	case "run":
		// "FOLDER\nCOMMAND", then optionally "\nKEYS\nSHOT"
		f := strings.SplitN(r.arg, "\n", 4)
		for len(f) < 4 {
			f = append(f, "")
		}
		resp.out, resp.err = n.run(f[0], f[1], f[2], f[3])
	case "restart":
		// The server is about to exit: the client hangs up and calls back.
		n.sendPending()
		n.link.Send(cjp.Redial, []byte("CodeJr's server is restarting. Calling back..."))
		n.link.Flush(3 * time.Second)
		resp.out = "told the PCjr to call back"
	case "sync":
		before, _, _, _ := n.s.ws.Pending()
		n.sync()
		n.sendPending()
		after, _, _, _ := n.s.ws.Pending()
		resp.out = fmt.Sprintf("%d file(s) sent to the PCjr, %d still waiting.", len(before)-len(after), len(after))
	case "fetch":
		var size int
		size, resp.err = n.fetch(r.arg)
		if resp.err == nil {
			resp.out = fmt.Sprintf("Saved %s in the workspace (%d bytes).", strings.ToUpper(r.arg), size)
		}
	default:
		resp.err = fmt.Errorf("no operation %q", r.op)
	}
	r.reply <- resp
}
