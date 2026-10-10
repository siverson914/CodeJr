package web

import "strings"

// Width is how many columns a page line may fill on the PCjr's 80-column
// screen (the last column stays free: writing there wraps the cursor).
const Width = 79

// Wrap breaks the page's lines at Width columns, at spaces where it can,
// keeping each piece's style.
func Wrap(lines [][]Seg, width int) [][]Seg {
	var out [][]Seg
	for _, l := range lines {
		if len(l) == 0 {
			out = append(out, nil)
			continue
		}
		// One style per character, then cut.
		var text []byte
		var style []byte
		for _, s := range l {
			t := toASCII(s.Text)
			text = append(text, t...)
			for range t {
				style = append(style, s.Style)
			}
		}
		for len(text) > 0 {
			n := len(text)
			if n > width {
				n = width
				if i := strings.LastIndexByte(string(text[:width+1]), ' '); i > width/3 {
					n = i
				}
			}
			out = append(out, segsOf(text[:n], style[:n]))
			text, style = text[n:], style[n:]
			for len(text) > 0 && text[0] == ' ' {
				text, style = text[1:], style[1:]
			}
		}
	}
	return out
}

func segsOf(text, style []byte) []Seg {
	var segs []Seg
	start := 0
	for i := 1; i <= len(text); i++ {
		if i == len(text) || style[i] != style[start] {
			segs = append(segs, Seg{style[start], string(text[start:i])})
			start = i
		}
	}
	return segs
}

// toASCII makes text the PCjr can show: common typographic characters
// become their plain cousins, the rest of what isn't ASCII a question
// mark.
func toASCII(s string) string {
	var b strings.Builder
	for _, r := range s {
		switch {
		case r == '\t' || r == '\n' || r == '\r' || r == 0xA0:
			b.WriteByte(' ')
		case r >= 32 && r < 127:
			b.WriteRune(r)
		case r == 0x2018 || r == 0x2019 || r == 0x2032:
			b.WriteByte('\'')
		case r == 0x201C || r == 0x201D:
			b.WriteByte('"')
		case r == 0x2013 || r == 0x2014 || r == 0x2212:
			b.WriteByte('-')
		case r == 0x2026:
			b.WriteString("...")
		case r == 0x2022 || r == 0xB7:
			b.WriteByte('*')
		case r == 0xD7:
			b.WriteByte('x')
		case r == 0xA9:
			b.WriteString("(c)")
		case r == 0xAE:
			b.WriteString("(R)")
		case r == 0x2122:
			b.WriteString("(TM)")
		case r == 0x20AC:
			b.WriteString("EUR")
		case r == 0xA3:
			b.WriteString("GBP")
		case r >= 0x200B && r <= 0x200F, r == 0xFEFF, r == 0xAD:
			// zero-width: nothing
		case r >= 0xC0 && r <= 0x17F:
			b.WriteByte(unaccent(r))
		default:
			b.WriteByte('?')
		}
	}
	return b.String()
}

// unaccent is the base letter of an accented Latin letter (roughly).
func unaccent(r rune) byte {
	const from = "ÀÁÂÃÄÅàáâãäåÈÉÊËèéêëÌÍÎÏìíîïÒÓÔÕÖØòóôõöøÙÚÛÜùúûüÇçÑñÝýÿ"
	const to = "AAAAAAaaaaaaEEEEeeeeIIIIiiiiOOOOOOooooooUUUUuuuuCcNnYyy"
	i := 0
	for _, f := range from {
		if f == r {
			return to[i]
		}
		i++
	}
	return '?'
}

// Plain is a line's text without styles.
func Plain(l []Seg) string {
	var b strings.Builder
	for _, s := range l {
		b.WriteString(s.Text)
	}
	return b.String()
}
