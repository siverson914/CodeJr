package kermit

import (
	"bytes"
	"io"
	"math/rand"
	"sync"
	"testing"
	"time"
)

func TestCRC16(t *testing.T) {
	// CRC-16/KERMIT check value from the CRC catalogue.
	if got := crc16([]byte("123456789")); got != 0x2189 {
		t.Fatalf("crc16 = %#04x, want 0x2189", got)
	}
}

func TestEncodeDecodeRoundTrip(t *testing.T) {
	all := make([]byte, 256)
	for i := range all {
		all[i] = byte(i)
	}
	for _, qbin := range []byte{0, '&'} {
		for _, max := range []int{4, 10, 91} {
			var got []byte
			for off := 0; off < len(all); {
				enc, n := encodeData(all[off:], qbin, max)
				if n == 0 {
					t.Fatalf("qbin=%q max=%d: no progress at %d", qbin, max, off)
				}
				if len(enc) > max {
					t.Fatalf("encoded %d bytes > max %d", len(enc), max)
				}
				for _, c := range enc {
					if lo := c & 0x7F; lo < 32 || lo == 127 || (qbin != 0 && c > 126) {
						t.Fatalf("qbin=%q: unprintable byte %#x on the wire", qbin, c)
					}
				}
				got = append(got, decodeData(enc, qbin)...)
				off += n
			}
			if !bytes.Equal(got, all) {
				t.Fatalf("qbin=%q max=%d: round trip mismatch", qbin, max)
			}
		}
	}
}

func TestPacketChecks(t *testing.T) {
	for _, chk := range []byte{'1', '3'} {
		p := packet{seq: 5, typ: 'D', data: []byte("hello #M#J")}
		wire := encodePacket(p, chk)
		in := make(chan byte, len(wire)+10)
		for _, b := range append([]byte("noise"), wire...) {
			in <- b
		}
		got, err := readPacket(ChanPort{In: in}, chk, time.Second)
		if err != nil || got.seq != 5 || got.typ != 'D' || string(got.data) != "hello #M#J" {
			t.Fatalf("chk %c: got %v, %v", chk, got, err)
		}
		// Corrupt one data byte: must fail the check.
		bad := append([]byte(nil), wire...)
		bad[6] ^= 1
		in2 := make(chan byte, len(bad))
		for _, b := range bad {
			in2 <- b
		}
		if _, err := readPacket(ChanPort{In: in2}, chk, 100*time.Millisecond); err != errBadPacket {
			t.Fatalf("chk %c: corrupted packet: err = %v", chk, err)
		}
	}
}

// link is one direction of a simulated serial line that can drop and
// corrupt bytes.
type link struct {
	mu      sync.Mutex
	ch      chan byte
	rng     *rand.Rand
	drop    float64
	corrupt float64
}

func (l *link) Write(p []byte) (int, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	for _, b := range p {
		r := l.rng.Float64()
		switch {
		case r < l.drop:
			continue
		case r < l.drop+l.corrupt:
			b ^= byte(1 << l.rng.Intn(7))
		}
		select {
		case l.ch <- b:
		default: // overflow drops, like a PCjr without a FIFO
		}
	}
	return len(p), nil
}

func pair(seed int64, drop, corrupt float64) (Port, Port) {
	ab := &link{ch: make(chan byte, 1<<16), rng: rand.New(rand.NewSource(seed)), drop: drop, corrupt: corrupt}
	ba := &link{ch: make(chan byte, 1<<16), rng: rand.New(rand.NewSource(seed + 1)), drop: drop, corrupt: corrupt}
	return ChanPort{In: ba.ch, Out: ab}, ChanPort{In: ab.ch, Out: ba}
}

func testFiles() []File {
	rng := rand.New(rand.NewSource(1))
	bin := make([]byte, 5000)
	rng.Read(bin)
	return []File{
		{Name: "SNAKE.BAS", Data: []byte("10 CLS\r\n20 PRINT \"#&~\"\r\n30 END\r\n")},
		{Name: "EMPTY.TXT", Data: nil},
		{Name: "GAME.COM", Data: bin},
	}
}

func transfer(t *testing.T, a, b Port, opt Options) {
	t.Helper()
	files := testFiles()
	errc := make(chan error, 1)
	go func() { errc <- Send(a, files, opt, nil) }()
	got, err := Receive(b, opt, nil)
	if err != nil {
		t.Fatalf("receive: %v", err)
	}
	if err := <-errc; err != nil {
		t.Fatalf("send: %v", err)
	}
	if len(got) != len(files) {
		t.Fatalf("got %d files, want %d", len(got), len(files))
	}
	for i := range files {
		if got[i].Name != files[i].Name || !bytes.Equal(got[i].Data, files[i].Data) {
			t.Fatalf("file %d: got %q (%d bytes), want %q (%d bytes)",
				i, got[i].Name, len(got[i].Data), files[i].Name, len(files[i].Data))
		}
	}
}

func TestTransferClean(t *testing.T) {
	for _, chk := range []byte{'1', '3'} {
		a, b := pair(1, 0, 0)
		transfer(t, a, b, Options{Timeout: 200 * time.Millisecond, BlockCheck: chk})
	}
}

func TestTransferNoisy(t *testing.T) {
	for seed := int64(0); seed < 40; seed++ {
		a, b := pair(seed, 0.002, 0.002)
		transfer(t, a, b, Options{Timeout: 50 * time.Millisecond, Retries: 50, BlockCheck: '3'})
	}
}

var _ io.Writer = (*link)(nil)
