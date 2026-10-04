package term

import "strings"

// ToASCII turns Claude's Unicode into plain 7-bit text the PCjr can show.
// Tabs become two spaces; other control characters except newline are
// dropped; anything without a sensible ASCII stand-in becomes '?'.
func ToASCII(s string) string {
	var b strings.Builder
	for _, r := range s {
		switch {
		case r == '\n':
			b.WriteRune(r)
		case r == '\t':
			b.WriteString("  ")
		case r < 32 || r == 127:
			// drop
		case r < 127:
			b.WriteRune(r)
		default:
			if t, ok := translit[r]; ok {
				b.WriteString(t)
			} else {
				b.WriteByte('?')
			}
		}
	}
	return b.String()
}

var translit = map[rune]string{
	'‘': "'", '’': "'", '‚': "'", '′': "'",
	'“': `"`, '”': `"`, '„': `"`, '″': `"`,
	'–': "-", '—': "-", '―': "-", '−': "-", '‐': "-", '‑': "-",
	'…': "...",
	'•': "*", '·': "*", '●': "*", '◦': "o", '‣': ">",
	'→': "->", '←': "<-", '↑': "^", '↓': "v", '⇒': "=>",
	'≤': "<=", '≥': ">=", '≠': "!=", '×': "x", '÷': "/",
	' ': " ", ' ': " ", ' ': " ", '​': "",
	'✓': "ok", '✔': "ok", '✗': "X", '✘': "X",
	'©': "(c)", '®': "(R)", '™': "(TM)",
	'°': " deg", '½': "1/2", '¼': "1/4", '¾': "3/4",
	'─': "-", '│': "|", '┌': "+", '┐': "+", '└': "+", '┘': "+",
	'├': "+", '┤': "+", '┬': "+", '┴': "+", '┼': "+",
	'═': "=", '║': "|", '█': "#", '░': ".", '▒': ":", '▓': "#",
	'é': "e", 'è': "e", 'ê': "e", 'á': "a", 'à': "a", 'ä': "a",
	'ö': "o", 'ü': "u", 'ñ': "n", 'ç': "c",
}
