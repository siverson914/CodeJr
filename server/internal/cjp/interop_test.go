package cjp

import (
	"bytes"
	"fmt"
	"io"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

// These tests build client/cjp.c (the PCjr client's protocol code) with
// the host's C compiler and run it against this package, so the C and Go
// implementations are checked against each other byte for byte.

func buildEcho(t *testing.T) string {
	t.Helper()
	cc, err := exec.LookPath("gcc")
	if err != nil {
		t.Skip("gcc not installed")
	}
	src, _ := filepath.Abs("../../../client")
	bin := filepath.Join(t.TempDir(), "cjpecho")
	out, err := exec.Command(cc, "-std=c99", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
		"-o", bin, filepath.Join(src, "test", "cjpecho.c"), filepath.Join(src, "cjp.c")).CombinedOutput()
	if err != nil {
		t.Fatalf("building cjpecho: %v\n%s", err, out)
	}
	return bin
}

// noisy drops and flips bytes going through it.
type noisy struct {
	w             io.Writer
	rng           *rand.Rand
	drop, corrupt float64
}

func (n *noisy) Write(p []byte) (int, error) {
	q := make([]byte, 0, len(p))
	for _, b := range p {
		r := n.rng.Float64()
		if r < n.drop {
			continue
		}
		if r < n.drop+n.corrupt {
			b ^= byte(1 << n.rng.Intn(8))
		}
		q = append(q, b)
	}
	if _, err := n.w.Write(q); err != nil {
		return 0, err
	}
	return len(p), nil
}

func runEcho(t *testing.T, seed int64, drop, corrupt float64, count int) {
	bin := buildEcho(t)
	cmd := exec.Command(bin)
	stdin, _ := cmd.StdinPipe()
	stdout, _ := cmd.StdoutPipe()
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	defer cmd.Process.Kill()

	rng := rand.New(rand.NewSource(seed))
	in := make(chan byte, 1<<16)
	go func() {
		buf := make([]byte, 4096)
		for {
			n, err := stdout.Read(buf)
			for _, b := range buf[:n] {
				r := rng.Float64()
				if r < drop {
					continue
				}
				if r < drop+corrupt {
					b ^= 1 << (int(r*1e6) % 8)
				}
				in <- b
			}
			if err != nil {
				close(in)
				return
			}
		}
	}()
	link := NewLink(in, &noisy{w: stdin, rng: rand.New(rand.NewSource(seed + 99)), drop: drop, corrupt: corrupt}, 150*time.Millisecond, false)
	defer link.Close()

	msg := func(i int) []byte {
		b := []byte(fmt.Sprintf("msg %d ", i))
		for j := 0; j < i%MaxPayload && len(b) < MaxPayload; j++ {
			b = append(b, byte(j*13+i)) // every byte value turns up
		}
		return b
	}
	go func() {
		for i := 0; i < count; i++ {
			typ := Text
			if i%3 == 0 {
				typ = FileData
			}
			if err := link.Send(typ, msg(i)); err != nil {
				return
			}
		}
	}()
	for i := 0; i < count; i++ {
		select {
		case m := <-link.Recv():
			want := Text
			if i%3 == 0 {
				want = FileData
			}
			if m.Type != want || !bytes.Equal(m.Payload, msg(i)) {
				t.Fatalf("echo %d: type %d payload %q", i, m.Type, m.Payload)
			}
		case <-time.After(20 * time.Second):
			t.Fatalf("timed out at echo %d (resent %d, bad %d)", i, link.Resent, link.BadFrames)
		}
	}
	link.Send(Bye, nil)
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("cjpecho: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("cjpecho didn't exit after BYE")
	}
	t.Logf("seed %d: Go resent %d, bad frames %d", seed, link.Resent, link.BadFrames)
}

func TestInteropCClean(t *testing.T) { runEcho(t, 1, 0, 0, 300) }

func TestInteropCNoisy(t *testing.T) {
	for seed := int64(1); seed <= 3; seed++ {
		runEcho(t, seed, 0.002, 0.002, 150)
	}
}
