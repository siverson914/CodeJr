package cjp

import (
	"bytes"
	"io"
	"math/bits"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"testing"
	"time"

	"codejr/internal/lz"
)

func testFile(t *testing.T) []byte {
	if b, err := os.ReadFile("../../../apps/jrdoom/JRDOOM.EXE"); err == nil {
		return b
	}
	// No game built: a mix of text, runs and random bytes.
	rng := rand.New(rand.NewSource(3))
	b := bytes.Repeat([]byte("10 PRINT \"HELLO\"\r\n"), 400)
	r := make([]byte, 20000)
	rng.Read(r)
	return append(append(b, r...), make([]byte, 9000)...)
}

func TestMakeChunksAndBurst(t *testing.T) {
	data := testFile(t)
	chunks := MakeChunks(data)
	if want := (len(data) + ChunkRaw - 1) / ChunkRaw; len(chunks) != want {
		t.Fatalf("%d chunks, want %d", len(chunks), want)
	}
	var back []byte
	sent := 0
	for i, c := range chunks {
		if len(c) > BlocksPerChunk*BlockSize {
			t.Fatalf("chunk %d is %d bytes", i, len(c))
		}
		sent += len(c)
		rawLen := int(c[1]) | int(c[2])<<8
		body := c[7:]
		raw := body
		if c[0] == 1 {
			var err error
			if raw, err = lz.Decompress(body, rawLen); err != nil {
				t.Fatal(err)
			}
		}
		if len(raw) != rawLen || CRC16(raw) != uint16(c[5])|uint16(c[6])<<8 {
			t.Fatalf("chunk %d header doesn't match its data", i)
		}
		back = append(back, raw...)
	}
	if !bytes.Equal(back, data) {
		t.Fatal("chunks don't reassemble to the file")
	}
	t.Logf("%d bytes -> %d on the line (%d%%), %d chunks", len(data), sent, sent*100/len(data), len(chunks))

	// A burst always carries exactly 2 FENDs per requested block, even for
	// blocks the chunk doesn't have, and no FEND anywhere else.
	for _, bitmap := range []uint32{0xFFFFFFFF, 1, 0x80000001, 0x0000F0F0} {
		last := len(chunks) - 1
		b := Burst(last, chunks[last], bitmap)
		if got, want := bytes.Count(b, []byte{FEND}), 2*bits.OnesCount32(bitmap); got != want {
			t.Fatalf("bitmap %#x: %d FENDs, want %d", bitmap, got, want)
		}
	}
}

// TestPullInteropC runs the PCjr client's own pull code (client/pull.c,
// lz.c, cjp.c, built for the host) against this package over a line that
// drops and flips bytes in both directions.
func TestPullInteropC(t *testing.T) {
	cc, err := exec.LookPath("gcc")
	if err != nil {
		t.Skip("gcc not installed")
	}
	src, _ := filepath.Abs("../../../client")
	bin := filepath.Join(t.TempDir(), "pullcli")
	if out, err := exec.Command(cc, "-std=c99", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
		"-o", bin, filepath.Join(src, "test", "pullcli.c"), filepath.Join(src, "pull.c"),
		filepath.Join(src, "lz.c"), filepath.Join(src, "cjp.c")).CombinedOutput(); err != nil {
		t.Fatalf("building pullcli: %v\n%s", err, out)
	}
	data := testFile(t)
	chunks := MakeChunks(data)

	for _, c := range []struct {
		noise  float64
		stalls bool
	}{{0, false}, {0.004, false}, {0, true}, {0.002, true}} {
		noise := c.noise
		outFile := filepath.Join(t.TempDir(), "out.bin")
		cmd := exec.Command(bin, outFile, strconv.Itoa(len(data)), strconv.Itoa(len(chunks)))
		stdin, _ := cmd.StdinPipe()
		stdout, _ := cmd.StdoutPipe()
		cmd.Stderr = os.Stderr
		if err := cmd.Start(); err != nil {
			t.Fatal(err)
		}
		var line io.Writer = &noisy{w: stdin, rng: rand.New(rand.NewSource(11)), drop: noise / 2, corrupt: noise / 2}
		if c.stalls {
			// The real WiFi link pauses now and then for longer than the
			// client waits (this is what broke the first real transfers).
			line = &stalling{w: line, every: 2500, pause: 700 * time.Millisecond}
		}
		burster := NewBurster(line)
		rng := rand.New(rand.NewSource(12))
		pulls, sent := 0, 0
		go func() {
			var d Decoder
			buf := make([]byte, 256)
			for {
				n, err := stdout.Read(buf)
				for _, c := range buf[:n] {
					// The requests cross the same bad line.
					if r := rng.Float64(); r < noise/2 {
						continue
					} else if r < noise {
						c ^= byte(1 << rng.Intn(8))
					}
					f, _ := d.Feed(c)
					if f == nil || f.Type != Pull {
						continue
					}
					k, bitmap, ok := ParsePull(f.Payload)
					if !ok || k >= len(chunks) {
						continue
					}
					b := Burst(k, chunks[k], bitmap)
					pulls++
					sent += len(b)
					burster.Send(b)
				}
				if err != nil {
					return
				}
			}
		}()
		done := make(chan error, 1)
		go func() { done <- cmd.Wait() }()
		select {
		case err := <-done:
			if err != nil {
				t.Fatalf("noise %.3f stalls %v: pullcli: %v (%d pulls)", noise, c.stalls, err, pulls)
			}
		case <-time.After(120 * time.Second):
			cmd.Process.Kill()
			t.Fatalf("noise %.3f stalls %v: timed out after %d pulls", noise, c.stalls, pulls)
		}
		got, _ := os.ReadFile(outFile)
		if !bytes.Equal(got, data) {
			t.Fatalf("noise %.3f: file differs (%d bytes, want %d)", noise, len(got), len(data))
		}
		burster.Stop()
		t.Logf("noise %.3f stalls %-5v: %d bytes fetched with %d pulls for %d chunks, %d bytes queued", noise, c.stalls, len(data), pulls, len(chunks), sent)
	}
}

// stalling pauses the line for a while after every `every` bytes.
type stalling struct {
	w     io.Writer
	every int
	pause time.Duration
	n     int
}

func (s *stalling) Write(p []byte) (int, error) {
	s.n += len(p)
	if s.n >= s.every {
		s.n = 0
		time.Sleep(s.pause)
	}
	return s.w.Write(p)
}
