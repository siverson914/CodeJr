package cjp

import (
	"errors"
	"fmt"
	"io"
	"sync"
	"time"
)

// Msg is one delivered message.
type Msg struct {
	Type     byte
	Payload  []byte
	Seq, Ack byte // set for datagrams only (types from DatagramBase up)
}

// Link is a reliable, ordered message channel over an unreliable byte
// stream: frames, sequence numbers, a window of Window unacknowledged
// frames, cumulative acks and go-back-N retransmission.
type Link struct {
	out     io.Writer
	timeout time.Duration

	mu       sync.Mutex
	cond     *sync.Cond
	base     byte      // oldest unacknowledged sequence number
	next     byte      // next sequence number to send
	pending  [][]byte  // encoded frames base..next-1
	sentAt   time.Time // when the oldest pending frame finished going out
	cps      int       // line speed in characters per second, 0 if unknown
	window   int       // frames that may be unacknowledged (default Window)
	expected byte      // next sequence number we accept
	closed   bool
	err      error

	inq  []Msg         // received, not yet taken from recv
	inc  *sync.Cond    // signals inq changes (uses mu)
	ackc chan struct{} // wakes the ack sender
	recv chan Msg
	done chan struct{}

	// Counters, for status lines and tests.
	Resent, BadFrames int

	// Trace, if set, gets a line for every frame event (for diagnosing
	// timing on real hardware). It is called from several goroutines.
	Trace func(string)
}

func (l *Link) trace(format string, a ...any) {
	if l.Trace != nil {
		l.Trace(fmt.Sprintf(format, a...))
	}
}

// ErrClosed is returned once the link has been closed or has failed.
var ErrClosed = errors.New("cjp: link closed")

// SetWindow limits how many frames may be unacknowledged at once. A
// window of 1 makes the link half duplex: the other side only ever answers
// into a quiet line (the PCjr corrupts what it receives while it sends).
func (l *Link) SetWindow(n int) {
	l.mu.Lock()
	l.window = n
	l.mu.Unlock()
}

// SetLineSpeed tells the link how fast the slowest hop is (characters per
// second), so resend timers allow for frames still queued ahead.
func (l *Link) SetLineSpeed(cps int) {
	l.mu.Lock()
	l.cps = cps
	l.mu.Unlock()
}

// NewLink starts a link reading bytes from in and writing to out. If
// inFrame is true, a FEND was already read from in.
func NewLink(in <-chan byte, out io.Writer, timeout time.Duration, inFrame bool) *Link {
	l := &Link{out: out, timeout: timeout, recv: make(chan Msg, 64), done: make(chan struct{}), ackc: make(chan struct{}, 1)}
	l.cond = sync.NewCond(&l.mu)
	l.inc = sync.NewCond(&l.mu)
	var d Decoder
	if inFrame {
		d.StartInFrame()
	}
	go l.reader(in, &d)
	go l.deliverer()
	go l.acker()
	go l.retransmitter()
	return l
}

// Recv delivers incoming messages in order. Select on Done as well: Recv
// is not closed when the link ends.
func (l *Link) Recv() <-chan Msg { return l.recv }

// Done is closed when the link ends.
func (l *Link) Done() <-chan struct{} { return l.done }

func inWindow(base, seq, next byte) bool {
	// Is seq in [base, next) modulo 256?
	return byte(seq-base) < byte(next-base)
}

func (l *Link) reader(in <-chan byte, d *Decoder) {
	defer l.Close()
	for c := range in {
		f, err := d.Feed(c)
		if err != nil {
			l.mu.Lock()
			l.BadFrames++
			l.mu.Unlock()
			l.trace("rx BAD frame")
			continue
		}
		if f == nil {
			continue
		}
		if f.Type >= DatagramBase {
			l.trace("rx datagram type %#x len %d", f.Type, len(f.Payload))
			l.mu.Lock()
			l.inq = append(l.inq, Msg{Type: f.Type, Payload: f.Payload, Seq: f.Seq, Ack: f.Ack})
			l.inc.Signal()
			l.mu.Unlock()
			continue
		}
		if f.Type == Ack {
			l.trace("rx ack %d", f.Ack)
		} else {
			l.trace("rx type %d seq %d ack %d len %d", f.Type, f.Seq, f.Ack, len(f.Payload))
		}
		l.handleAck(f.Ack)
		if f.Type == Ack || f.Type == Nak {
			continue
		}
		l.mu.Lock()
		accept := f.Seq == l.expected
		if accept {
			l.expected++
		}
		l.mu.Unlock()
		// Ack right away (the PCjr client acks after handling instead), but
		// never block on it: the writer may be stuck behind a slow line,
		// and the reader must keep processing the other side's acks.
		select {
		case l.ackc <- struct{}{}:
		default: // an ack is already due; it will carry the newest number
		}
		if accept {
			// Queue without blocking: if nobody is reading right now, the
			// reader must still keep processing acks for our own sends.
			l.mu.Lock()
			l.inq = append(l.inq, Msg{Type: f.Type, Payload: f.Payload})
			l.inc.Signal()
			l.mu.Unlock()
		}
	}
}

// acker sends acks. Acks are cumulative, so when several are due only
// the newest needs to go out.
func (l *Link) acker() {
	for {
		select {
		case <-l.ackc:
		case <-l.done:
			return
		}
		l.mu.Lock()
		ack := l.expected
		l.mu.Unlock()
		l.write(Encode(Frame{Type: Ack, Ack: ack}))
	}
}

// deliverer moves queued messages to the recv channel.
func (l *Link) deliverer() {
	for {
		l.mu.Lock()
		for len(l.inq) == 0 && !l.closed {
			l.inc.Wait()
		}
		if len(l.inq) == 0 {
			l.mu.Unlock()
			return
		}
		m := l.inq[0]
		l.inq = l.inq[1:]
		l.mu.Unlock()
		select {
		case l.recv <- m:
		case <-l.done:
			return
		}
	}
}

func (l *Link) handleAck(a byte) {
	l.mu.Lock()
	defer l.mu.Unlock()
	if a == l.base || !inWindow(l.base, a-1, l.next) {
		return
	}
	n := int(byte(a - l.base))
	l.pending = l.pending[n:]
	l.base = a
	l.sentAt = time.Now()
	l.cond.Broadcast()
}

func (l *Link) retransmitter() {
	t := time.NewTicker(l.timeout / 4)
	defer t.Stop()
	for {
		select {
		case <-l.done:
			return
		case <-t.C:
		}
		l.mu.Lock()
		var resend [][]byte
		if len(l.pending) > 0 && time.Since(l.sentAt) >= l.deadline() {
			resend = append(resend, l.pending...)
			l.Resent += len(resend)
		}
		l.mu.Unlock()
		// Resent frames keep their old ack field. Acks are cumulative,
		// so a stale one is simply ignored by the other side.
		if resend != nil {
			l.trace("RESEND %d frames (timer)", len(resend))
		}
		for _, fr := range resend {
			l.write(fr)
		}
		if resend != nil {
			// Time from when the resends have actually gone out: the
			// writer may block while a slow line drains.
			l.mu.Lock()
			l.sentAt = time.Now()
			l.mu.Unlock()
		}
	}
}

// deadline is how long to wait for an ack before resending: the base
// timeout plus the time the pending frames take on the line at its speed
// (the frame being acked may be queued behind the others). Call with mu held.
func (l *Link) deadline() time.Duration {
	d := l.timeout
	if l.cps > 0 {
		n := 0
		for _, fr := range l.pending {
			n += len(fr)
		}
		d += time.Duration(n) * time.Second / time.Duration(l.cps)
	}
	return d
}

func (l *Link) write(b []byte) {
	if _, err := l.out.Write(b); err != nil {
		l.fail(err)
	}
}

func (l *Link) fail(err error) {
	l.mu.Lock()
	if l.err == nil {
		l.err = err
	}
	l.mu.Unlock()
	l.Close()
}

// Send queues one message, waiting while the window is full. Payloads
// longer than MaxPayload are an error; use SendChunks to split.
func (l *Link) Send(typ byte, payload []byte) error {
	if len(payload) > MaxPayload {
		return errors.New("cjp: payload too long")
	}
	l.mu.Lock()
	w := l.window
	if w <= 0 {
		w = Window
	}
	for !l.closed && int(byte(l.next-l.base)) >= w {
		l.cond.Wait()
	}
	if l.closed {
		l.mu.Unlock()
		return ErrClosed
	}
	fr := Encode(Frame{Type: typ, Seq: l.next, Ack: l.expected, Payload: payload})
	first := len(l.pending) == 0
	l.pending = append(l.pending, fr)
	l.next++
	if first {
		l.sentAt = time.Now()
	}
	l.mu.Unlock()
	l.write(fr)
	if first {
		// The writer may have blocked while a slow line drained; time the
		// ack from when the frame actually went out.
		l.mu.Lock()
		if len(l.pending) > 0 {
			l.sentAt = time.Now()
		}
		l.mu.Unlock()
	}
	return nil
}

// SendDatagram sends one unacknowledged message (a type from DatagramBase up).
func (l *Link) SendDatagram(typ byte, payload []byte) {
	l.write(Encode(Frame{Type: typ, Payload: payload}))
}

// SendChunks sends data as several messages of the same type, each with
// prefix in front (for example a Text style byte).
func (l *Link) SendChunks(typ byte, prefix, data []byte) error {
	room := MaxPayload - len(prefix)
	for first := true; first || len(data) > 0; first = false {
		n := min(room, len(data))
		p := append(append([]byte(nil), prefix...), data[:n]...)
		if err := l.Send(typ, p); err != nil {
			return err
		}
		data = data[n:]
	}
	return nil
}

// Flush waits until everything sent has been acknowledged, or timeout.
func (l *Link) Flush(timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	l.mu.Lock()
	defer l.mu.Unlock()
	for !l.closed && len(l.pending) > 0 {
		if time.Now().After(deadline) {
			return errors.New("cjp: flush timed out")
		}
		l.mu.Unlock()
		time.Sleep(20 * time.Millisecond)
		l.mu.Lock()
	}
	if l.closed && len(l.pending) > 0 {
		return ErrClosed
	}
	return nil
}

// Stats summarizes the link's error counters for logs.
func (l *Link) Stats() string {
	l.mu.Lock()
	defer l.mu.Unlock()
	return fmt.Sprintf("link: %d frames resent, %d bad frames received", l.Resent, l.BadFrames)
}

// Close ends the link.
func (l *Link) Close() {
	l.mu.Lock()
	if l.closed {
		l.mu.Unlock()
		return
	}
	l.closed = true
	l.cond.Broadcast()
	l.inc.Broadcast()
	l.mu.Unlock()
	close(l.done)
}

// Err reports why the link ended, if it failed.
func (l *Link) Err() error {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.err
}
