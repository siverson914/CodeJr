package web

import (
	"fmt"
	"strconv"
	"strings"
	"time"
)

// Screen is where a session shows things: the PCjr over its link, or a
// terminal on the laptop for trying pages out.
type Screen interface {
	Text(style byte, s string) // style as in Seg
	Status(s string)
}

// Part is how many lines of a page are shown at a time. The PCjr keeps
// 200 lines of scrollback; a part must fit with room to spare.
const Part = 150

// Session is one person browsing.
type Session struct {
	B    *Browser
	Out  Screen
	Logf func(format string, a ...any)

	page  *Page
	lines [][]Seg // the page, wrapped
	shown int     // lines of it shown so far
	whole bool    // the next read takes the whole page
}

const help = `ChromeJr: a real browser on the laptop, shown as text.
  an address        amazon.com   en.wikipedia.org/wiki/IBM_PCjr
  anything else     searches the web for it
  12                follow link 12, or press button <12>
  12 some text      type into field {12} and press Enter
  12= some text     type into field {12} without pressing Enter
  .add to cart      press the first button or link that says so
  m                 more of this page        t   this page from the top
  b                 back                     r   load the page again
  l                 list the page's fields and buttons
  w                 the whole page: menus, side bars, the search box
  ?                 this help                /exit   leave
`

// Command does one typed line.
func (s *Session) Command(line string) {
	line = strings.TrimSpace(line)
	if line == "" {
		return
	}
	start := time.Now()
	lower := strings.ToLower(line)
	first, rest, _ := strings.Cut(line, " ")
	rest = strings.TrimSpace(rest)
	switch {
	case lower == "?" || lower == "help" || lower == "/help":
		s.Out.Text('c', help)
		return
	case lower == "m":
		s.more()
		return
	case lower == "t":
		s.shown = 0
		s.more()
		return
	case lower == "l":
		s.list()
		return
	case lower == "w":
		// The same page with its menus and side bars.
		s.whole = true
		s.Out.Status("Reading the whole page...")
		s.did(nil, start, "whole page")
		return
	case lower == "b":
		s.Out.Status("Going back...")
		s.did(s.B.Back(), start, "back")
		return
	case lower == "r":
		s.Out.Status("Loading the page again...")
		s.did(s.B.Reload(), start, "reload")
		return
	}
	// .words: the first button or link that says so (".add to cart").
	if strings.HasPrefix(line, ".") && len(line) > 1 && s.page != nil {
		want := strings.ToLower(strings.TrimSpace(line[1:]))
		for pass := 0; pass < 2; pass++ { // buttons before links
			for _, t := range s.page.Targets {
				if (pass == 0) == (t.Kind == "button") && (t.Kind == "button" || t.Kind == "link") && strings.Contains(strings.ToLower(t.Label), want) {
					s.Out.Status("Opening " + label(t) + "...")
					s.did(s.B.Press(t.N), start, fmt.Sprintf("press %d by name (%s %q)", t.N, t.Kind, t.Label))
					return
				}
			}
		}
		s.Out.Text('c', "Nothing on this page says \""+want+"\".\n")
		return
	}
	// A number, with or without text: something on the page.
	setOnly := strings.HasSuffix(first, "=")
	if n, err := strconv.Atoi(strings.TrimSuffix(first, "=")); err == nil && s.page != nil {
		t, ok := s.page.Find(n)
		if !ok {
			s.Out.Text('c', fmt.Sprintf("There is no %d on this page.\n", n))
			return
		}
		switch t.Kind {
		case "field", "password":
			if rest == "" && !setOnly {
				s.Out.Text('c', fmt.Sprintf("%d is a field (%s). Type: %d your text\n", n, t.Label, n))
				return
			}
			s.Out.Status("Typing into " + label(t) + "...")
			s.did(s.B.Fill(n, rest, !setOnly), start, fmt.Sprintf("fill %d (%s)", n, t.Kind))
		case "select":
			if rest == "" {
				s.Out.Text('c', fmt.Sprintf("%d is a list of choices (%s). Type: %d part of the choice\n", n, t.Label, n))
				return
			}
			s.Out.Status("Choosing...")
			s.did(s.B.Choose(n, rest), start, fmt.Sprintf("choose %d", n))
		case "image":
			s.Out.Text('c', "Pictures aren't in this prototype yet.\n")
		default:
			s.Out.Status("Opening " + label(t) + "...")
			s.did(s.B.Press(n), start, fmt.Sprintf("press %d (%s %q)", n, t.Kind, t.Label))
		}
		return
	}
	u := Address(line)
	s.Out.Status("Loading " + short(u, 60) + "...")
	s.did(s.B.Go(u), start, "go "+u)
}

func label(t Target) string {
	if t.Label == "" {
		return "number " + strconv.Itoa(t.N)
	}
	return short(t.Label, 40)
}

func short(s string, n int) string {
	if len(s) > n {
		return s[:n-3] + "..."
	}
	return s
}

// did reads the page after an action and shows it from the top.
func (s *Session) did(err error, start time.Time, what string) {
	if err != nil {
		s.Out.Text('c', "That didn't work: "+short(err.Error(), 120)+"\n")
	}
	p, rerr := s.B.Read(s.whole)
	s.whole = false
	if rerr != nil {
		s.Out.Text('c', "Couldn't read the page: "+short(rerr.Error(), 120)+"\n")
		s.Logf("%s: %v / %v", what, err, rerr)
		return
	}
	s.page = p
	s.lines = Wrap(p.Lines, Width)
	s.shown = 0
	chars := 0
	for _, l := range s.lines {
		chars += len(Plain(l)) + 1
	}
	s.Logf("%s: %q %s, %d lines, %d chars, %d numbered, %.1fs", what, p.Title, p.URL, len(s.lines), chars, len(p.Targets), time.Since(start).Seconds())
	s.Out.Text('h', "\n== "+short(toASCII(p.Title), 70)+"\n")
	s.Out.Text('n', short(toASCII(p.URL), 78)+"\n")
	s.more()
}

// more shows the next part of the page.
func (s *Session) more() {
	if s.page == nil {
		s.Out.Text('c', "No page yet. Type an address, or words to search for. ? for help.\n")
		return
	}
	end := s.shown + Part
	if end > len(s.lines) {
		end = len(s.lines)
	}
	for _, l := range s.lines[s.shown:end] {
		for _, seg := range l {
			s.Out.Text(seg.Style, seg.Text)
		}
		s.Out.Text('p', "\n")
	}
	s.shown = end
	if end < len(s.lines) {
		s.Out.Text('c', fmt.Sprintf("-- %d of %d lines. m for more --\n", end, len(s.lines)))
		s.Out.Status(fmt.Sprintf("%s   line %d of %d   m = more, ? = help", short(toASCII(s.page.Title), 34), end, len(s.lines)))
	} else {
		if s.page.Whole {
			s.Out.Text('c', "-- end of page --\n")
		} else {
			s.Out.Text('c', "-- end of the page's main part. w shows the whole page (menus, search box) --\n")
		}
		s.Out.Status(short(toASCII(s.page.Title), 50) + "   ? = help")
	}
}

// list shows the page's fields and buttons, which are easy to lose in a
// long page.
func (s *Session) list() {
	if s.page == nil {
		s.more()
		return
	}
	n := 0
	for _, t := range s.page.Targets {
		if t.Kind == "link" || t.Kind == "image" {
			continue
		}
		s.Out.Text('c', fmt.Sprintf("%4d  %-8s %s\n", t.N, t.Kind, toASCII(t.Label)))
		n++
	}
	if n == 0 {
		s.Out.Text('c', "This page has no fields or buttons.\n")
	}
}
