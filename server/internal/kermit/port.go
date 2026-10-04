package kermit

import (
	"io"
	"time"
)

// ChanPort is a Port fed by a channel of incoming bytes, so the same
// connection can be shared between a terminal session and transfers.
type ChanPort struct {
	In  <-chan byte
	Out io.Writer
}

func (p ChanPort) ReadTimeout(timeout time.Duration) (byte, error) {
	t := time.NewTimer(timeout)
	defer t.Stop()
	select {
	case b, ok := <-p.In:
		if !ok {
			return 0, io.EOF
		}
		return b, nil
	case <-t.C:
		return 0, ErrTimeout
	}
}

func (p ChanPort) Write(b []byte) (int, error) { return p.Out.Write(b) }

// Pump copies r into a channel until r fails, then closes the channel.
func Pump(r io.Reader) <-chan byte {
	ch := make(chan byte, 4096)
	go func() {
		defer close(ch)
		buf := make([]byte, 512)
		for {
			n, err := r.Read(buf)
			for _, b := range buf[:n] {
				ch <- b
			}
			if err != nil {
				return
			}
		}
	}()
	return ch
}
