package kermit

import (
	"bytes"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

// These tests talk to the real C-Kermit (pacman: ckermit) over TCP, so a
// bug can't hide by being symmetric between our sender and receiver.

func ckermit(t *testing.T, dir, script string) (net.Conn, *exec.Cmd) {
	t.Helper()
	bin, err := exec.LookPath("ckermit")
	if err != nil {
		t.Skip("ckermit not installed")
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	port := ln.Addr().(*net.TCPAddr).Port
	cmds := fmt.Sprintf("set host 127.0.0.1 %d /raw-socket, set transfer mode manual, set file type binary, set file collision overwrite, cd %s, %s, exit", port, dir, script)
	cmd := exec.Command(bin, "-B", "-Y", "-C", cmds)
	cmd.Stdout, cmd.Stderr = os.Stderr, os.Stderr
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	ln.(*net.TCPListener).SetDeadline(time.Now().Add(10 * time.Second))
	conn, err := ln.Accept()
	if err != nil {
		cmd.Process.Kill()
		t.Fatal(err)
	}
	return conn, cmd
}

func TestInteropSendToCKermit(t *testing.T) {
	dir := t.TempDir()
	conn, cmd := ckermit(t, dir, "receive")
	defer conn.Close()
	files := testFiles()
	if err := Send(ChanPort{In: Pump(conn), Out: conn}, files, Options{Timeout: 3 * time.Second}, nil); err != nil {
		t.Fatalf("send: %v", err)
	}
	conn.Close()
	cmd.Wait()
	for _, f := range files {
		got, err := os.ReadFile(filepath.Join(dir, f.Name))
		if err != nil {
			// C-Kermit lowercases names by default when receiving.
			got, err = os.ReadFile(filepath.Join(dir, lower(f.Name)))
		}
		if err != nil || !bytes.Equal(got, f.Data) {
			t.Fatalf("%s: err=%v, %d bytes, want %d", f.Name, err, len(got), len(f.Data))
		}
	}
}

func TestInteropReceiveFromCKermit(t *testing.T) {
	dir := t.TempDir()
	files := testFiles()
	for _, f := range files {
		os.WriteFile(filepath.Join(dir, f.Name), f.Data, 0o644)
	}
	conn, cmd := ckermit(t, dir, "set delay 0, send SNAKE.BAS, send GAME.COM")
	defer conn.Close()
	port := ChanPort{In: Pump(conn), Out: conn}
	for _, want := range []File{files[0], files[2]} {
		got, err := Receive(port, Options{Timeout: 3 * time.Second}, nil)
		if err != nil {
			t.Fatalf("receive: %v", err)
		}
		if len(got) != 1 || !bytes.Equal(got[0].Data, want.Data) {
			t.Fatalf("got %d files; want %s: %q", len(got), want.Name, got[0].Data[:min(80, len(got[0].Data))])
		}
		t.Logf("received %s (%d bytes)", got[0].Name, len(got[0].Data))
	}
	conn.Close()
	cmd.Wait()
}

func lower(s string) string { return string(bytes.ToLower([]byte(s))) }
