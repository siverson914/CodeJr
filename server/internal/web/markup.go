package web

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

// Junior markup: a view as the PCjr gets it. One block per line, its
// first character the block's type, its fields split by tabs, all ASCII:
//
//	T<title>
//	H<heading>
//	P<text>
//	I<n>\t<title>\t<detail>\t<price>   a numbered item (a result, a product)
//	A<n>\t<label>                      an action
//	F<n>\t<label>                      an input box
//	S<n>\t<heading>                    a section that opens
//	-                                  a divider
//
// n is the view's own number, which is what the PCjr sends back. Lines
// of any other type are passed over, so the format can grow.

var linkNum = regexp.MustCompile(`\[\d+\]|<\d+:|\{\d+:|\(\d+:`)

func field(s string) string {
	return strings.NewReplacer("\t", " ", "\n", " ", "\r", " ").Replace(toASCII(s))
}

// Markup is a view in Junior markup.
func Markup(v *View) string {
	var b strings.Builder
	b.WriteString("T" + field(v.Title) + "\n")
	for _, k := range v.Blocks {
		if m := blockMarkup(k); m != "" {
			b.WriteString(m + "\n")
		}
	}
	return b.String()
}

// blockMarkup is one block's line of Junior markup.
func blockMarkup(k Block) string {
	switch k.Type {
	case 'H', 'P':
		return string(k.Type) + field(k.Text)
	case 'I':
		return fmt.Sprintf("I%d\t%s\t%s\t%s", k.N, field(k.Text), field(k.Detail), field(k.Price))
	case 'A', 'F', 'S':
		return fmt.Sprintf("%c%d\t%s", k.Type, k.N, field(k.Text))
	case '-':
		return "-"
	}
	return ""
}

// RenderMarkup draws Junior markup as styled lines width columns wide.
func RenderMarkup(m string, width int) [][]Seg {
	d := Drawer{Width: width}
	var out [][]Seg
	for _, line := range strings.Split(m, "\n") {
		out = append(out, d.Line(line)...)
	}
	return append(out, d.End()...)
}

// Drawer draws Junior markup a line at a time, as the CodeJr client (and
// this laptop's terminal) can show it: 'h' headings, 'l' what can be
// picked, 'n' its number, 'c' notes. Actions sit side by side, so the
// last of them waits until something else (or End) comes.
type Drawer struct {
	Width  int
	title  string
	bar    []Seg
	barLen int
	last   []Seg // the last line drawn: no blank line at the top, none twice
	drawn  bool
}

// Line draws one line of markup and returns the screen lines ready.
func (d *Drawer) Line(line string) [][]Seg {
	if line == "" {
		return nil
	}
	width := d.Width
	var out [][]Seg
	add := func(l ...[]Seg) {
		for _, x := range l {
			out = append(out, x)
			d.last, d.drawn = x, true
		}
	}
	blank := func() {
		if d.drawn && len(d.last) > 0 {
			add(nil)
		}
	}
	endBar := func() {
		if len(d.bar) > 0 {
			add(d.bar)
			d.bar, d.barLen = nil, 0
		}
	}
	kind, rest := line[0], line[1:]
	f := strings.Split(rest, "\t")
	n := 0
	if kind == 'I' || kind == 'A' || kind == 'F' || kind == 'S' {
		n, _ = strconv.Atoi(f[0])
		f = f[1:]
		if len(f) == 0 {
			return nil
		}
	}
	get := func(i int) string {
		if i < len(f) {
			return strings.TrimSpace(f[i])
		}
		return ""
	}
	num := fmt.Sprintf("%2d ", n)
	if kind != 'A' {
		endBar()
	}
	switch kind {
	case 'T':
		d.title = rest
		add([]Seg{{'h', short(rest, width)}}, []Seg{{'p', strings.Repeat("=", min(len(rest), width))}})
	case 'H':
		if strings.EqualFold(strings.TrimSpace(rest), strings.TrimSpace(d.title)) {
			break // the title again
		}
		blank()
		add(Wrap([][]Seg{{{'h', rest}}}, width)...)
	case 'P':
		blank()
		add(Wrap([][]Seg{{{'p', rest}}}, width)...)
	case '-':
		blank()
		add([]Seg{{'p', strings.Repeat("-", 40)}})
	case 'I':
		name, detail, price := get(0), get(1), get(2)
		// the price at the right of the title's first line; a long
		// title goes on to a second line
		room := width - len(num) - 1
		if price != "" {
			room -= len(price) + 2
		}
		rows := Wrap([][]Seg{{{'l', name}}}, room)
		if len(rows) > 2 {
			rows = [][]Seg{rows[0], {{'l', short(Plain(rows[1])+" "+Plain(rows[2]), room)}}}
		}
		first := append([]Seg{{'n', num}}, rows[0]...)
		if price != "" {
			pad := width - len(num) - len(Plain(rows[0])) - len(price) - 1
			first = append(first, Seg{'p', strings.Repeat(" ", max(pad, 2))}, Seg{'c', price})
		}
		add(first)
		for _, r := range rows[1:] {
			add(append([]Seg{{'p', "   "}}, r...))
		}
		if detail != "" {
			add([]Seg{{'p', "   " + short(detail, width-4)}})
		}
	case 'A':
		label := short(get(0), 30)
		w := len(num) + len(label) + 2
		if d.barLen > 0 && d.barLen+w > width {
			endBar()
		}
		if d.barLen == 0 {
			blank()
		}
		d.bar = append(d.bar, Seg{'n', "[" + strconv.Itoa(n) + "]"}, Seg{'l', " " + label + "  "})
		d.barLen += w
	case 'F':
		blank()
		label := short(get(0), 30)
		add([]Seg{{'n', num}, {'c', label + ": [" + strings.Repeat("_", max(width-len(num)-len(label)-4, 10)) + "]"}})
	case 'S':
		add([]Seg{{'n', num}, {'l', "> " + short(get(0), width-6)}})
	}
	return out
}

// End returns what is still waiting (the last actions).
func (d *Drawer) End() [][]Seg {
	if len(d.bar) == 0 {
		return nil
	}
	b := d.bar
	d.bar, d.barLen = nil, 0
	d.last, d.drawn = b, true
	return [][]Seg{b}
}
