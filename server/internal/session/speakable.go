package session

import (
	"regexp"
	"strings"
)

// Speakable rewrites a reply for the voice: the screen keeps the text as
// written ("2 + 2398 = 2400", "PCjr", "GW-BASIC"), and the speech
// converter gets what should be said ("2 plus 2398 equals 2400", "PC
// Junior", "G W Basic"). A neural voice reads symbols and odd names
// badly or skips them, so they are spelled out here.
//
// The rules run in order: DOS paths (before anything touches their
// backslashes), names and units (so "PCjr" is gone before any rule looks
// at letters and dots), file names, symbols (arithmetic before the
// Markdown cleanup, which would eat the "*"), and last a tidy of the
// spacing.
func Speakable(s string) string {
	s = dosPath.ReplaceAllStringFunc(s, func(m string) string {
		parts := strings.FieldsFunc(m[2:], func(r rune) bool { return r == '\\' })
		out := m[:1] + " drive"
		for _, p := range parts {
			out += ", " + strings.ToLower(p)
		}
		return out
	})
	for _, r := range speakRules {
		s = r.re.ReplaceAllString(s, r.to)
	}
	s = fileName.ReplaceAllStringFunc(s, func(m string) string {
		i := strings.LastIndex(m, ".")
		ext := strings.ToUpper(m[i+1:])
		said, ok := extSaid[ext]
		if !ok {
			return m // not a file type we know: leave "3.14" and "e.g" alone
		}
		return strings.ToLower(m[:i]) + " dot " + said
	})
	for _, r := range symbolRules {
		s = r.re.ReplaceAllString(s, r.to)
	}
	return strings.Join(strings.Fields(s), " ")
}

type speakRule struct {
	re *regexp.Regexp
	to string
}

func rule(pattern, to string) speakRule { return speakRule{regexp.MustCompile(pattern), to} }

// Names and abbreviations. (?i) where people type them either way.
var speakRules = []speakRule{
	rule(`(?i)\bPC[ -]?jr\b\.?`, "PC Junior"),
	rule(`(?i)\bCodeJr\b`, "Code Junior"),
	rule(`(?i)\bjr-IDE\b`, "Junior I D E"),
	rule(`\bJRDOOM\b`, "Junior Doom"),
	rule(`\bJRFLIGHT\b`, "Junior Flight"),
	rule(`\bJRCUBE\b`, "Junior Cube"),
	rule(`\bJRAI\b`, "Junior A I"),
	rule(`\bJRGFX\b`, "Junior graphics"),
	rule(`(?i)\bGW-?BASIC\b`, "G W Basic"),
	rule(`\bBASICA\b`, "Basic A"),
	rule(`\bQBASIC\b`, "Q Basic"),
	rule(`(?i)\bMS-DOS\b`, "M S DOS"),
	rule(`(?i)\bPC[- ]DOS\b`, "P C DOS"),
	rule(`(?i)\bMS-Kermit\b`, "M S Kermit"),
	rule(`\bSN76496\b`, "S N 7 6 4 9 6"),
	rule(`\bSN76489\b`, "S N 7 6 4 8 9"),
	rule(`\b8088\b`, "eighty eighty-eight"),
	rule(`\b8086\b`, "eighty eighty-six"),
	rule(`\b8253\b`, "eighty-two fifty-three"),
	rule(`\b8259\b`, "eighty-two fifty-nine"),
	rule(`(?i)\bRS-232\b`, "R S two thirty-two"),
	rule(`(?i)\bADPCM\b`, "A D P C M"),
	rule(`(?i)\bWi-?Fi\b`, "wifi"),
	rule(`(?i)\be\.g\.,?`, "for example,"),
	rule(`(?i)\bi\.e\.,?`, "that is,"),
	rule(`(?i)\bvs\.?(\s)`, "versus$1"),
	rule(`(?i)\betc\.`, "and so on."),
	// units after a number
	rule(`(\d)\s?KB\b`, "$1 kilobytes"),
	rule(`(\d)\s?MB\b`, "$1 megabytes"),
	rule(`(\d)\s?K\b`, "$1 K"),
	rule(`(\d)\s?kHz\b`, "$1 kilohertz"),
	rule(`(\d)\s?MHz\b`, "$1 megahertz"),
	rule(`(\d)\s?Hz\b`, "$1 hertz"),
	rule(`(\d)\s?bps\b`, "$1 bits per second"),
	rule(`(\d)\s?fps\b`, "$1 frames per second"),
	rule(`(\d)\s?ms\b`, "$1 milliseconds"),
	rule(`(\d)x(\d)`, "$1 by $2"), // 80x24, 320x200
	// a slash command: /voice, /run
	rule(`(^|\s)/([a-z]+)\b`, "$1 slash $2"),
}

// NAME.EXT, where EXT is a file type people would say.
var fileName = regexp.MustCompile(`\b[A-Za-z0-9_]{1,8}\.[A-Za-z]{1,3}\b`)

var extSaid = map[string]string{
	"BAS": "bas", "EXE": "E X E", "COM": "com", "BAT": "bat", "TXT": "text",
	"C": "C", "H": "H", "ASM": "A S M", "SND": "S N D", "CFG": "config",
	"SYS": "sys", "DAT": "dat", "LOG": "log", "INI": "ini", "DOC": "doc",
}

// C:\CODEJR\PROGS
var dosPath = regexp.MustCompile(`\b[A-Za-z]:(?:\\[A-Za-z0-9_.]*)+`)

// Symbols, after the names: "=" and friends are never said right.
var symbolRules = []speakRule{
	rule(`<=`, " less than or equal to "),
	rule(`>=`, " greater than or equal to "),
	rule(`<>|!=`, " not equal to "),
	rule(`==?`, " equals "),
	rule(`(\d)\s*\+\s*(\d)`, "$1 plus $2"),
	rule(`(\d)\s+-\s+(\d)`, "$1 minus $2"),
	rule(`(\d)\s*[*×]\s*(\d)`, "$1 times $2"),
	rule(`(\d)\s+/\s+(\d)`, "$1 divided by $2"),
	rule(`(\d)\s*\^\s*(\d)`, "$1 to the power of $2"),
	rule("[`*_]{1,3}", ""),        // stray Markdown
	rule(`(?m)^\s*[-#>]+\s+`, ""), // list marks, headings, quotes
	rule(`\+`, " plus "),
	rule(`(\d)\s?%`, "$1 percent"),
	rule(`\s&\s`, " and "),
	rule(`#(\d)`, "number $1"),
	rule(`<`, " less than "),
	rule(`>`, " greater than "),
	rule(`~(\d)`, "about $1"),
	rule(`°`, " degrees"),
	rule(`\\`, " backslash "),
	rule(`[|{}\[\]]`, " "),
}
