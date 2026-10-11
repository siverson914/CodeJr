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
		switch k.Type {
		case 'H', 'P':
			b.WriteString(string(k.Type) + field(k.Text) + "\n")
		case 'I':
			fmt.Fprintf(&b, "I%d\t%s\t%s\t%s\n", k.N, field(k.Text), field(k.Detail), field(k.Price))
		case 'A', 'F', 'S':
			fmt.Fprintf(&b, "%c%d\t%s\n", k.Type, k.N, field(k.Text))
		case '-':
			b.WriteString("-\n")
		}
	}
	return b.String()
}

// RenderMarkup draws Junior markup as styled lines width columns wide, as
// the CodeJr client (and this laptop's terminal) can show them: 'h'
// headings, 'l' what can be picked, 'n' its number, 'c' notes.
func RenderMarkup(m string, width int) [][]Seg {
	var out [][]Seg
	blank := func() {
		if len(out) > 0 && len(out[len(out)-1]) > 0 {
			out = append(out, nil)
		}
	}
	wrapped := func(lines ...[]Seg) { out = append(out, Wrap(lines, width)...) }
	// actions sit side by side on as few lines as they fit
	var bar []Seg
	barLen := 0
	endBar := func() {
		if len(bar) > 0 {
			out = append(out, bar)
			bar, barLen = nil, 0
		}
	}
	title := ""
	for _, line := range strings.Split(m, "\n") {
		if line == "" {
			continue
		}
		kind, rest := line[0], line[1:]
		f := strings.Split(rest, "\t")
		n := 0
		if kind == 'I' || kind == 'A' || kind == 'F' || kind == 'S' {
			n, _ = strconv.Atoi(f[0])
			f = f[1:]
			if len(f) == 0 {
				continue
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
			title = rest
			out = append(out, []Seg{{'h', short(rest, width)}})
			out = append(out, []Seg{{'p', strings.Repeat("=", min(len(rest), width))}})
		case 'H':
			if strings.EqualFold(strings.TrimSpace(rest), strings.TrimSpace(title)) {
				continue // the title again
			}
			blank()
			wrapped([]Seg{{'h', rest}})
		case 'P':
			blank()
			wrapped([]Seg{{'p', rest}})
		case '-':
			blank()
			out = append(out, []Seg{{'p', strings.Repeat("-", 40)}})
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
			out = append(out, first)
			for _, r := range rows[1:] {
				out = append(out, append([]Seg{{'p', "   "}}, r...))
			}
			if detail != "" {
				out = append(out, []Seg{{'p', "   " + short(detail, width-4)}})
			}
		case 'A':
			label := short(get(0), 30)
			w := len(num) + len(label) + 2
			if barLen > 0 && barLen+w > width {
				endBar()
			}
			if barLen == 0 {
				blank()
			}
			bar = append(bar, Seg{'n', "[" + strconv.Itoa(n) + "]"}, Seg{'l', " " + label + "  "})
			barLen += w
		case 'F':
			blank()
			label := short(get(0), 30)
			out = append(out, []Seg{{'n', num}, {'c', label + ": [" + strings.Repeat("_", max(width-len(num)-len(label)-4, 10)) + "]"}})
		case 'S':
			out = append(out, []Seg{{'n', num}, {'l', "> " + short(get(0), width-6)}})
		}
	}
	endBar()
	return out
}
