package session

import (
	"bytes"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"codejr/internal/claude"
	"codejr/internal/kermit"
)

// fakePCjr plays MS-Kermit's part: it reads the terminal stream and runs
// RECEIVE when the server sends an APC.
type fakePCjr struct {
	t    *testing.T
	conn net.Conn
	in   <-chan byte
	seen bytes.Buffer
}

func (p *fakePCjr) send(s string) { p.conn.Write([]byte(s)) }

// waitFor reads until the terminal stream contains s.
func (p *fakePCjr) waitFor(s string) {
	p.t.Helper()
	deadline := time.After(10 * time.Second)
	for !strings.Contains(p.seen.String(), s) {
		select {
		case b, ok := <-p.in:
			if !ok {
				p.t.Fatalf("hung up while waiting for %q; got %q", s, p.seen.String())
			}
			p.seen.WriteByte(b)
		case <-deadline:
			p.t.Fatalf("timed out waiting for %q; got %q", s, p.seen.String())
		}
	}
}

func TestCallEndToEnd(t *testing.T) {
	dir := t.TempDir()
	ws := &Workspace{Dir: dir}
	ws.Init()
	fake, _ := filepath.Abs("testdata/fakeclaude.sh")
	cfg := Config{Workspace: ws, Baud: 0, Claude: claude.Config{Bin: fake, Dir: dir}}

	server, client := net.Pipe()
	done := make(chan struct{})
	go func() { Serve(cfg, server, "test"); close(done) }()
	pc := &fakePCjr{t: t, conn: client, in: kermit.Pump(client)}

	// The Kermit script dials; the server plays modem.
	go pc.send("ATDT192.168.0.178:4860\r")
	pc.waitFor("CONNECT")
	pc.waitFor("READY")
	go pc.send("\r")
	pc.waitFor("Welcome to CodeJr")

	go pc.send("hello there\r")
	pc.waitFor("[writes HELLO.BAS]")
	pc.waitFor("you asked: hello there") // em dash became ASCII
	if strings.Contains(pc.seen.String(), "—") {
		t.Fatal("Unicode reached the PCjr")
	}
	pc.waitFor("set file type binary, set file collision overwrite, receive\x1b\\")
	files, err := kermit.Receive(kermit.ChanPort{In: pc.in, Out: client}, kermit.Options{Timeout: time.Second}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(files) != 1 || files[0].Name != "HELLO.BAS" || string(files[0].Data) != "10 PRINT \"HI\"\r\n20 END\r\n" {
		t.Fatalf("files = %+v", files)
	}
	pc.seen.Reset()
	pc.waitFor("Saved on the PCjr")
	if ws.Session() != "fake-session" {
		t.Fatalf("session = %q", ws.Session())
	}

	// Already synced: /files shows no star, /sync sends nothing.
	go pc.send("/files\r")
	pc.waitFor("HELLO.BAS")
	if strings.Contains(pc.seen.String(), "* HELLO.BAS") {
		t.Fatal("HELLO.BAS still marked unsynced")
	}

	go pc.send("/quit\r")
	pc.waitFor("Bye")
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("server didn't hang up")
	}
}

func TestWorkspaceNames(t *testing.T) {
	dir := t.TempDir()
	ws := &Workspace{Dir: dir}
	ws.Init()
	os.WriteFile(filepath.Join(dir, "snake.bas"), []byte("10 END\n"), 0o644)
	os.WriteFile(filepath.Join(dir, "toolongname.bas"), []byte("x"), 0o644)
	os.WriteFile(filepath.Join(dir, ".hidden"), []byte("x"), 0o644)
	files, hashes, problems, err := ws.Pending()
	if err != nil {
		t.Fatal(err)
	}
	if len(files) != 1 || files[0].Name != "SNAKE.BAS" || string(files[0].Data) != "10 END\r\n" {
		t.Fatalf("files = %+v", files)
	}
	if len(problems) != 1 || problems[0].Name != "toolongname.bas" {
		t.Fatalf("problems = %+v", problems)
	}
	ws.MarkSynced(hashes)
	if files, _, _, _ := ws.Pending(); len(files) != 0 {
		t.Fatalf("still pending after sync: %+v", files)
	}
	// A file fetched from the PCjr is stored with Unix endings and counts as synced.
	ws.SaveFromPCjr("LOG.TXT", []byte("a\r\nb\r\n\x1a"))
	if b, _ := os.ReadFile(filepath.Join(dir, "LOG.TXT")); string(b) != "a\nb\n" {
		t.Fatalf("LOG.TXT = %q", b)
	}
	if files, _, _, _ := ws.Pending(); len(files) != 0 {
		t.Fatalf("fetched file would be sent back: %+v", files)
	}
}

func TestKermitLeavesBigFilesForNative(t *testing.T) {
	dir := t.TempDir()
	ws := &Workspace{Dir: dir}
	ws.Init()
	os.WriteFile(filepath.Join(dir, "GAME.EXE"), make([]byte, kermitMax+1), 0o644)
	fake, _ := filepath.Abs("testdata/fakeclaude.sh")
	cfg := Config{Workspace: ws, Baud: 0, Claude: claude.Config{Bin: fake, Dir: dir}}
	server, client := net.Pipe()
	go Serve(cfg, server, "test")
	pc := &fakePCjr{t: t, conn: client, in: kermit.Pump(client)}
	pc.waitFor("READY")
	go pc.send("\r")
	pc.waitFor("Welcome to CodeJr")
	go pc.send("/sync\r")
	pc.waitFor("GAME.EXE (40.0 KB) is too big for Kermit")
	if strings.Contains(pc.seen.String(), "receive\x1b\\") {
		t.Fatal("a Kermit transfer was started for the big file")
	}
	client.Close()
}
