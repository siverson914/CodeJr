package cjp

import (
	"bytes"
	"fmt"
	"math/rand"
	"sync"
	"testing"
	"time"
)

func TestCRC16(t *testing.T) {
	if got := CRC16([]byte("123456789")); got != 0x29B1 {
		t.Fatalf("CRC16 = %#04x, want 0x29b1 (CRC-16/CCITT-FALSE)", got)
	}
}

func TestFrameRoundTrip(t *testing.T) {
	payload := make([]byte, MaxPayload)
	for i := range payload {
		payload[i] = byte(i * 7) // hits FEND, FESC, XON, XOFF, 0xFF
	}
	payload[0], payload[1], payload[2], payload[3], payload[4] = FEND, FESC, 0x11, 0x13, 0xFF
	f := Frame{Type: FileData, Seq: 0xC0, Ack: 0x13, Payload: payload}
	wire := Encode(f)
	for _, c := range wire[1 : len(wire)-1] {
		if needsEscape(c) && c != FESC {
			t.Fatalf("unescaped %#x on the wire", c)
		}
	}
	var d Decoder
	var got *Frame
	for _, c := range append([]byte("noise\r\nCONNECT"), wire...) {
		fr, err := d.Feed(c)
		if err != nil {
			t.Fatal(err)
		}
		if fr != nil {
			got = fr
		}
	}
	if got == nil || got.Type != f.Type || got.Seq != f.Seq || got.Ack != f.Ack || !bytes.Equal(got.Payload, payload) {
		t.Fatalf("round trip mismatch: %+v", got)
	}
	// One flipped bit must fail the CRC.
	bad := append([]byte(nil), wire...)
	bad[10] ^= 0x04
	var d2 Decoder
	sawErr := false
	for _, c := range bad {
		if fr, err := d2.Feed(c); err != nil {
			sawErr = true
		} else if fr != nil {
			t.Fatal("corrupted frame accepted")
		}
	}
	if !sawErr {
		t.Fatal("corruption not reported")
	}
}

// line is one direction of a simulated serial line.
type line struct {
	mu            sync.Mutex
	ch            chan byte
	rng           *rand.Rand
	drop, corrupt float64
}

func (l *line) Write(p []byte) (int, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	for _, b := range p {
		r := l.rng.Float64()
		if r < l.drop {
			continue
		}
		if r < l.drop+l.corrupt {
			b ^= byte(1 << l.rng.Intn(8))
		}
		select {
		case l.ch <- b:
		default:
		}
	}
	return len(p), nil
}

func pair(seed int64, drop, corrupt float64) (*Link, *Link) {
	ab := &line{ch: make(chan byte, 1<<16), rng: rand.New(rand.NewSource(seed)), drop: drop, corrupt: corrupt}
	ba := &line{ch: make(chan byte, 1<<16), rng: rand.New(rand.NewSource(seed + 1)), drop: drop, corrupt: corrupt}
	a := NewLink(ba.ch, ab, 60*time.Millisecond, false)
	b := NewLink(ab.ch, ba, 60*time.Millisecond, false)
	return a, b
}

func exchange(t *testing.T, a, b *Link, n int) {
	t.Helper()
	msg := func(i int) []byte {
		return []byte(fmt.Sprintf("message %d %s", i, bytes.Repeat([]byte{FEND, 0x11, 0x13, 0xFF, FESC}, i%20)))
	}
	errc := make(chan error, 2)
	go func() {
		for i := 0; i < n; i++ {
			if err := a.Send(Text, msg(i)); err != nil {
				errc <- err
				return
			}
		}
		errc <- nil
	}()
	go func() {
		for i := 0; i < n; i++ {
			if err := b.Send(Input, msg(i)); err != nil {
				errc <- err
				return
			}
		}
		errc <- nil
	}()
	check := func(l *Link, typ byte) {
		for i := 0; i < n; i++ {
			select {
			case m := <-l.Recv():
				if m.Type != typ || !bytes.Equal(m.Payload, msg(i)) {
					t.Errorf("message %d: got %q", i, m.Payload)
					return
				}
			case <-time.After(10 * time.Second):
				t.Errorf("timed out at message %d", i)
				return
			}
		}
	}
	check(b, Text)
	check(a, Input)
	for i := 0; i < 2; i++ {
		if err := <-errc; err != nil {
			t.Fatal(err)
		}
	}
}

func TestLinkClean(t *testing.T) {
	a, b := pair(1, 0, 0)
	defer a.Close()
	defer b.Close()
	exchange(t, a, b, 200)
}

func TestLinkNoisy(t *testing.T) {
	for seed := int64(0); seed < 3; seed++ {
		a, b := pair(seed, 0.003, 0.003)
		exchange(t, a, b, 80)
		t.Logf("seed %d: resent %d/%d, bad frames %d/%d", seed, a.Resent, b.Resent, a.BadFrames, b.BadFrames)
		a.Close()
		b.Close()
	}
}

func TestDroppedByteAlwaysCaught(t *testing.T) {
	payload := []byte("msg 84 Tan{\x88\x95\xa2\xaf\xbc\xc9")
	wire := Encode(Frame{Type: FileData, Seq: 7, Ack: 3, Payload: payload})
	// Drop each byte between the FENDs in turn: no frame may be accepted.
	for i := 1; i < len(wire)-1; i++ {
		cut := append(append([]byte(nil), wire[:i]...), wire[i+1:]...)
		var d Decoder
		for _, c := range cut {
			if f, _ := d.Feed(c); f != nil {
				t.Fatalf("frame with byte %d dropped was accepted: %q", i, f.Payload)
			}
		}
	}
}

// slowLine models the WiFi modem: it accepts writes at once into its
// buffer, then delivers them at a fixed rate, like a 9600 baud serial line.
type slowLine struct {
	ch   chan byte
	buf  chan byte
	once sync.Once
	cps  int
}

func (l *slowLine) Write(p []byte) (int, error) {
	l.once.Do(func() {
		go func() {
			for b := range l.buf {
				time.Sleep(time.Second / time.Duration(l.cps))
				l.ch <- b
			}
		}()
	})
	for _, b := range p {
		l.buf <- b
	}
	return len(p), nil
}

func TestNoResendsOnASlowCleanLine(t *testing.T) {
	ab := &slowLine{ch: make(chan byte, 1<<16), buf: make(chan byte, 1<<16), cps: 2000}
	ba := &slowLine{ch: make(chan byte, 1<<16), buf: make(chan byte, 1<<16), cps: 2000}
	a := NewLink(ba.ch, ab, 300*time.Millisecond, false)
	b := NewLink(ab.ch, ba, 300*time.Millisecond, false)
	a.SetLineSpeed(2000)
	b.SetLineSpeed(2000)
	defer a.Close()
	defer b.Close()
	payload := bytes.Repeat([]byte("x"), MaxPayload)
	go func() {
		for i := 0; i < 40; i++ { // about 5 KB: 2.7 s of line time
			a.Send(FileData, payload)
		}
	}()
	for i := 0; i < 40; i++ {
		select {
		case <-b.Recv():
		case <-time.After(20 * time.Second):
			t.Fatalf("stalled at %d", i)
		}
	}
	if a.Resent != 0 {
		t.Fatalf("%d frames resent on a clean but slow line", a.Resent)
	}
}

// blockingLine models the server's pacer: every write blocks until the
// bytes have "gone out" at the line speed.
type blockingLine struct {
	mu  sync.Mutex
	ch  chan byte
	cps int
}

func (l *blockingLine) Write(p []byte) (int, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	time.Sleep(time.Duration(len(p)) * time.Second / time.Duration(l.cps))
	for _, b := range p {
		l.ch <- b
	}
	return len(p), nil
}

// TestBusyWriterDoesNotStallAcks is the bug from the first real native
// session: the reader wrote its own acks through the paced writer, so
// while a window of data drained it stopped processing the PCjr's acks,
// the resend timer fired, and resends snowballed (334 for a 1.3 KB file).
func TestBusyWriterDoesNotStallAcks(t *testing.T) {
	ab := &blockingLine{ch: make(chan byte, 1<<16), cps: 1000}
	ba := &blockingLine{ch: make(chan byte, 1<<16), cps: 1000}
	a := NewLink(ba.ch, ab, 400*time.Millisecond, false)
	b := NewLink(ab.ch, ba, 400*time.Millisecond, false)
	a.SetLineSpeed(1000)
	b.SetLineSpeed(1000)
	defer a.Close()
	defer b.Close()
	payload := bytes.Repeat([]byte("y"), MaxPayload)
	// Both directions busy at once, like a reply streaming while the
	// client sends acks and input.
	go func() {
		for i := 0; i < 20; i++ {
			a.Send(FileData, payload)
		}
	}()
	go func() {
		for i := 0; i < 20; i++ {
			b.Send(Text, payload)
		}
	}()
	for i := 0; i < 20; i++ {
		select {
		case <-b.Recv():
		case <-time.After(30 * time.Second):
			t.Fatalf("a->b stalled at %d", i)
		}
		select {
		case <-a.Recv():
		case <-time.After(30 * time.Second):
			t.Fatalf("b->a stalled at %d", i)
		}
	}
	if a.Resent+b.Resent > 4 {
		t.Fatalf("too many resends on a clean line: %d and %d", a.Resent, b.Resent)
	}
	t.Logf("resent %d and %d", a.Resent, b.Resent)
}

// TestCRNeverSentRaw: Zimodem's "ATDT" turns on telnet translation, which
// drops a NUL after a CR. Frames must never contain a raw CR.
func TestCRNeverSentRaw(t *testing.T) {
	payload := []byte{0x0D, 0x00, 'a', 0x0D, 0x0A, 0x0D}
	wire := Encode(Frame{Type: Text, Seq: 0x0D, Ack: 0x0D, Payload: payload})
	if bytes.IndexByte(wire, 0x0D) >= 0 {
		t.Fatalf("raw CR on the wire: % x", wire)
	}
	var d Decoder
	for _, c := range wire {
		if f, err := d.Feed(c); err != nil {
			t.Fatal(err)
		} else if f != nil && !bytes.Equal(f.Payload, payload) {
			t.Fatalf("payload %q", f.Payload)
		}
	}
}
