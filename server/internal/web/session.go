package web

import (
	"context"
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
	// SaveDir, when set, keeps every page read there as JSON (for trying
	// the page filter on saved pages: chromejr -filter).
	SaveDir string
	// Filter, when set, lays each page out simply (a View) before it is
	// shown; nil shows every page as numbered text.
	Filter *Filterer

	page  *Page
	view  *View   // the page laid out, or nil
	raw   bool    // showing the page itself (its own numbers), not the view
	off   bool    // the filter is turned off ("f")
	lines [][]Seg // what is being shown, wrapped
	shown int     // lines of it shown so far
	end   string  // said after the last line
	whole bool    // the next read takes the whole page
}

const help = `ChromeJr: a real browser on the laptop, shown as text.
  an address        amazon.com   en.wikipedia.org/wiki/IBM_PCjr
  anything else     searches the web for it
  3                 pick 3: follow it, press it, or open that section
  3 some text       type into box 3 and press Enter
  3= some text      type into box 3 without pressing Enter
  .add to cart      press the first button or link that says so
  m                 more of this page        t   this page from the top
  b                 back                     r   load the page again
  w                 the page itself, every link numbered (w again: menus too)
  v                 the simple view again    f   simple views off/on
  l                 list the page's fields and buttons
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
	case lower == "v":
		if s.view == nil {
			s.Out.Text('c', "This page has no simple view. w shows it all.\n")
			return
		}
		s.raw = false
		s.showView()
		return
	case lower == "f":
		s.off = !s.off
		if s.off {
			s.Out.Text('c', "Simple views off: pages come as numbered text. f turns them on.\n")
		} else {
			s.Out.Text('c', "Simple views on (from the next page; v shows this one's).\n")
		}
		return
	case lower == "w" && s.page != nil && !s.raw:
		// The page itself, as it was read: its own numbers.
		s.raw = true
		s.showRaw()
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
	if n, err := strconv.Atoi(strings.TrimSuffix(first, "=")); err == nil && s.page != nil && s.view != nil && !s.raw {
		s.pick(n, rest, setOnly, start)
		return
	}
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
	asked := s.whole // "w": the whole page, as it is
	p, rerr := s.B.Read(s.whole)
	s.whole = false
	if rerr != nil {
		s.Out.Text('c', "Couldn't read the page: "+short(rerr.Error(), 120)+"\n")
		s.Logf("%s: %v / %v", what, err, rerr)
		return
	}
	s.page = p
	s.view = nil
	s.raw = true
	if s.SaveDir != "" {
		if f, err := SavePage(s.SaveDir, p); err != nil {
			s.Logf("saving the page: %v", err)
		} else {
			s.Logf("saved %s", f)
		}
	}
	lines := Wrap(p.Lines, Width)
	chars := 0
	for _, l := range lines {
		chars += len(Plain(l)) + 1
	}
	s.Logf("%s: %q %s, %d lines, %d chars, %d numbered, %.1fs", what, p.Title, p.URL, len(lines), chars, len(p.Targets), time.Since(start).Seconds())
	if s.Filter != nil && !s.off && !asked {
		s.Out.Status("Simplifying the page...")
		v, st, err := s.Filter.Filter(context.Background(), p)
		if err != nil {
			s.Logf("filter: %v (%s)", err, st)
			s.Out.Text('c', "(No simple view: "+short(err.Error(), 100)+")\n")
		} else {
			s.Logf("filter: %s, %d choices, %s, %.1fs in all", v.Kind, choices(v), st, time.Since(start).Seconds())
			s.view, s.raw = v, false
			s.showView()
			return
		}
	}
	s.showRaw()
}

func choices(v *View) int {
	n := 0
	for _, b := range v.Blocks {
		if b.N > 0 {
			n++
		}
	}
	return n
}

// showRaw shows the page itself from the top: its text with its own
// numbers.
func (s *Session) showRaw() {
	s.lines = Wrap(s.page.Lines, Width)
	s.shown = 0
	s.end = "-- end of the page's main part. w shows the whole page (menus, search box) --"
	if s.page.Whole {
		s.end = "-- end of page --"
	}
	if s.view != nil {
		s.end += "\n-- v: the simple view --"
	}
	s.Out.Text('h', "\n== "+short(toASCII(s.page.Title), 70)+"\n")
	s.Out.Text('n', short(toASCII(s.page.URL), 78)+"\n")
	s.more()
}

// showView shows the page as the filter laid it out.
func (s *Session) showView() {
	s.lines = RenderMarkup(Markup(s.view), Width)
	s.shown = 0
	s.end = "-- type a number to pick it. w: the whole page, numbered --"
	s.Out.Text('p', "\n")
	s.more()
}

// pick does the view's choice n.
func (s *Session) pick(n int, rest string, setOnly bool, start time.Time) {
	b, ok := s.view.Choice(n)
	if !ok {
		s.Out.Text('c', fmt.Sprintf("There is no %d here. w shows the whole page, numbered.\n", n))
		return
	}
	switch b.Type {
	case 'S':
		s.lines = Wrap(Section(s.page, s.view, n), Width)
		s.shown = 0
		s.end = "-- end of the section. v: back to the page --"
		s.Out.Text('p', "\n")
		s.more()
	case 'F':
		if rest == "" && !setOnly {
			s.Out.Text('c', fmt.Sprintf("%d is a box to type in (%s). Type: %d your words\n", n, b.Text, n))
			return
		}
		t, _ := s.page.Find(b.Target)
		if t.Kind == "select" {
			s.Out.Status("Choosing...")
			s.did(s.B.Choose(b.Target, rest), start, fmt.Sprintf("choose %d (view %d)", b.Target, n))
			return
		}
		s.Out.Status("Typing into " + short(b.Text, 40) + "...")
		s.did(s.B.Fill(b.Target, rest, !setOnly), start, fmt.Sprintf("fill %d (view %d)", b.Target, n))
	default:
		s.Out.Status("Opening " + short(b.Text, 40) + "...")
		s.did(s.B.Press(b.Target), start, fmt.Sprintf("press %d (view %d, %q)", b.Target, n, b.Text))
	}
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
		s.Out.Text('c', s.end+"\n")
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
