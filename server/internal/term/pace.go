package term

import (
	"bytes"
	"io"
	"sync"
	"time"
)

// Pacer limits writes to the speed of the serial line behind the modem, so
// the modem's buffer never overflows and the PCjr (no UART FIFO) keeps up.
type Pacer struct {
	mu   sync.Mutex
	w    io.Writer
	cps  int       // characters per second; 0 means unpaced
	raw  bool      // binary data: line feeds cost nothing extra
	next time.Time // when the line will be idle
}

// Set changes the line speed. raw is for binary protocols, where a line
// feed is just a byte (the extra time is for MS-Kermit scrolling its screen).
func (p *Pacer) Set(baud int, raw bool) {
	p.mu.Lock()
	p.cps, p.raw = baud/10, raw
	p.mu.Unlock()
}

// NewPacer paces w for a serial line at baud (8N1: 10 bits per character).
func NewPacer(w io.Writer, baud int) *Pacer {
	return &Pacer{w: w, cps: baud / 10}
}

const (
	chunk  = 16
	lfCost = 24
)

func (p *Pacer) Write(b []byte) (int, error) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.cps <= 0 {
		return p.w.Write(b)
	}
	written := 0
	// About 60 writes a second at any speed.
	chunk := max(chunk, p.cps/60)
	for len(b) > 0 {
		n := min(chunk, len(b))
		now := time.Now()
		if p.next.Before(now) {
			p.next = now
		}
		// Allow one chunk of slack so the line never sits idle.
		if wait := p.next.Sub(now) - time.Duration(chunk)*time.Second/time.Duration(p.cps); wait > 0 {
			time.Sleep(wait)
		}
		m, err := p.w.Write(b[:n])
		written += m
		if err != nil {
			return written, err
		}
		// Scrolling a region takes MS-Kermit on a 4.77 MHz PCjr far longer
		// than one character time, and the PCjr's UART has no FIFO, so give
		// each line feed the time of lfCost characters.
		cost := n
		if !p.raw {
			cost += lfCost * bytes.Count(b[:n], []byte{'\n'})
		}
		p.next = p.next.Add(time.Duration(cost) * time.Second / time.Duration(p.cps))
		b = b[n:]
	}
	return written, nil
}
