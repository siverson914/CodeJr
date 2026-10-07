package pcjr

import (
	"bufio"
	"encoding/json"
	"errors"
	"net"
	"os"
	"path/filepath"
	"time"
)

// removeStale deletes a socket file left by a server that is gone.
func removeStale(p string) {
	if c, err := net.DialTimeout("unix", p, time.Second); err == nil {
		c.Close() // a live server owns it; Listen will fail and say so
		return
	}
	os.Remove(p)
}

// The control socket: a Unix socket in the workspace's .codejr folder
// where the running server takes requests for the PCjr that is connected
// right now. One JSON line each way. The tools here use it, and so does
// `codejr ctl` (the `just pcjr` tasks).

// CtlRequest asks the connected PCjr for something: Op "dos" runs the
// command in Arg and returns what it printed; "fetch" copies the file
// named in Arg into the workspace.
type CtlRequest struct {
	Op  string `json:"op"`
	Arg string `json:"arg"`
}

// CtlReply is the answer.
type CtlReply struct {
	Out string `json:"out"`
	Err string `json:"err,omitempty"`
}

// CtlPath is where the socket lives for a workspace.
func CtlPath(workspace string) string {
	return filepath.Join(workspace, ".codejr", "ctl.sock")
}

// CtlCall sends one request to the server that owns workspace.
func CtlCall(workspace, op, arg string) (string, error) {
	c, err := net.DialTimeout("unix", CtlPath(workspace), 3*time.Second)
	if err != nil {
		return "", errors.New("the CodeJr server isn't running (or is an older one)")
	}
	defer c.Close()
	if err := json.NewEncoder(c).Encode(CtlRequest{Op: op, Arg: arg}); err != nil {
		return "", err
	}
	c.SetReadDeadline(time.Now().Add(6 * time.Minute))
	var r CtlReply
	rd := bufio.NewReaderSize(c, 1<<20)
	if err := json.NewDecoder(rd).Decode(&r); err != nil {
		return "", errors.New("the server gave no answer")
	}
	if r.Err != "" {
		return r.Out, errors.New(r.Err)
	}
	return r.Out, nil
}

// CtlServe answers requests on the workspace's socket with handle until
// the listener fails. The server runs it in the background.
func CtlServe(workspace string, handle func(op, arg string) (string, error)) error {
	p := CtlPath(workspace)
	removeStale(p)
	ln, err := net.Listen("unix", p)
	if err != nil {
		return err
	}
	for {
		c, err := ln.Accept()
		if err != nil {
			return err
		}
		go func() {
			defer c.Close()
			var q CtlRequest
			if json.NewDecoder(bufio.NewReader(c)).Decode(&q) != nil {
				return
			}
			out, err := handle(q.Op, q.Arg)
			r := CtlReply{Out: out}
			if err != nil {
				r.Err = err.Error()
			}
			json.NewEncoder(c).Encode(r)
		}()
	}
}
