package web

import (
	"io"
	"log"
	"strconv"
	"strings"
	"time"

	"codejr/internal/cjp"
	"codejr/internal/kermit"
	"codejr/internal/session"
	"codejr/internal/term"
)

// Serve answers one call from the PCjr. The prototype's PCjr half is the
// CodeJr client program (CODEJR.EXE dialing this server's port): it gives
// a screen with scrollback and paging, a status bar and an input line,
// which is all a text browser needs to be tried out.
func Serve(b *Browser, conn io.ReadWriteCloser, baud int, name string) {
	defer conn.Close()
	logf := func(f string, a ...any) { log.Printf(name+": "+f, a...) }
	out := term.NewPacer(conn, baud)
	in := kermit.Pump(session.TelnetFilter(conn))
	logf("connected")
	// Until the client's first frame: the same banner CodeJr sends, which
	// the client waits for after dialing.
	banner := func() { io.WriteString(out, "\r\nCodeJr READY - press Enter\r\n") }
	banner()
	tick := time.NewTicker(2 * time.Second)
	started := false
	for !started {
		select {
		case <-tick.C:
			banner()
		case c, ok := <-in:
			if !ok {
				tick.Stop()
				logf("hung up before starting")
				return
			}
			started = c == cjp.FEND
		}
	}
	tick.Stop()
	link := cjp.NewLink(in, out, 2*time.Second, true)
	link.SetLineSpeed(baud / 10)
	link.SetWindow(1) // as CodeJr: this PCjr can't hear while it sends
	defer link.Close()

	select {
	case m := <-link.Recv():
		if m.Type != cjp.Hello {
			logf("expected HELLO, got type %d", m.Type)
			return
		}
		logf("client says %q", m.Payload)
		for _, f := range strings.Fields(string(m.Payload)) {
			if v, ok := strings.CutPrefix(f, "baud="); ok {
				if n, err := strconv.Atoi(v); err == nil && n >= 300 && n <= 115200 {
					out.Set(n*97/100, true)
					link.SetLineSpeed(n / 10)
				}
			}
		}
	case <-link.Done():
		return
	case <-time.After(10 * time.Second):
		logf("no HELLO")
		return
	}
	link.Send(cjp.Hello, []byte("CODEJR/2 server ui t="+time.Now().Format("20060102150405")))

	scr := &linkScreen{link: link}
	s := &Session{B: b, Out: scr, Logf: logf}
	scr.Text('h', "ChromeJr prototype: the web on a PCjr.\n")
	scr.Text('c', "Type an address (wikipedia.org), or words to search for. ? for help.\n")
	scr.Status("ChromeJr ready.   ? = help")
	scr.flush()
	link.Send(cjp.Ready, nil)
	var long []byte
	for {
		select {
		case m := <-link.Recv():
			switch m.Type {
			case cjp.InputMore:
				if len(long) < 4000 {
					long = append(long, m.Payload...)
				}
			case cjp.Input:
				line := string(append(long, m.Payload...))
				long = nil
				if l := strings.ToLower(strings.TrimSpace(line)); l == "/exit" || l == "/quit" || l == "q" {
					scr.Text('c', "Goodbye.\n")
					scr.flush()
					link.Send(cjp.Bye, nil)
					link.Flush(3 * time.Second)
					return
				}
				logf("typed: %q", line)
				s.Command(line)
				scr.flush()
				link.Send(cjp.Ready, nil)
			case cjp.Bye:
				logf("client hung up")
				return
			}
		case <-link.Done():
			logf("disconnected")
			return
		}
	}
}

// linkScreen shows a session on the CodeJr client: text in frames of one
// style each, and the status bar.
type linkScreen struct {
	link  *cjp.Link
	pend  []byte
	style byte
}

// The client's five styles, for a page's kinds of text.
func cjpStyle(s byte) byte {
	switch s {
	case 'h':
		return cjp.StyleBanner
	case 'l':
		return cjp.StyleUser // yellow: what a link says
	case 'n':
		return cjp.StyleNote // cyan: its number
	case 'c':
		return cjp.StyleNote
	}
	return cjp.StylePlain
}

func (l *linkScreen) Text(style byte, s string) {
	st := cjpStyle(style)
	if st != l.style {
		l.flush()
		l.style = st
	}
	l.pend = append(l.pend, toASCII2(s)...)
	for len(l.pend) >= cjp.MaxPayload-1 {
		l.link.Send(cjp.Text, append([]byte{l.style}, l.pend[:cjp.MaxPayload-1]...))
		l.pend = l.pend[cjp.MaxPayload-1:]
	}
}

func (l *linkScreen) flush() {
	if len(l.pend) > 0 {
		l.link.Send(cjp.Text, append([]byte{l.style}, l.pend...))
		l.pend = l.pend[:0]
	}
}

func (l *linkScreen) Status(s string) {
	l.flush()
	if len(s) > 78 {
		s = s[:78]
	}
	l.link.Send(cjp.Status, []byte(toASCII(s)))
}

// toASCII2 is toASCII that keeps line ends.
func toASCII2(s string) string {
	parts := strings.Split(s, "\n")
	for i, p := range parts {
		parts[i] = toASCII(p)
	}
	return strings.Join(parts, "\n")
}
