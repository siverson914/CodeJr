package web

import (
	"strings"
	"testing"
)

func TestWrap(t *testing.T) {
	long := strings.Repeat("word ", 30)
	lines := Wrap([][]Seg{{{'p', long}, {'l', "a link"}, {'n', "[12]"}}, nil, {{'h', "Title"}}}, 40)
	for _, l := range lines {
		if n := len(Plain(l)); n > 40 {
			t.Fatalf("a line of %d columns: %q", n, Plain(l))
		}
	}
	all := ""
	for _, l := range lines {
		all += Plain(l) + " "
	}
	if strings.Join(strings.Fields(all), " ") != strings.Join(strings.Fields(long+"a link[12] Title"), " ") {
		t.Fatalf("words changed: %q", all)
	}
	// Styles survive the cut.
	last := lines[len(lines)-3]
	if last[len(last)-1].Style != 'n' || last[len(last)-1].Text != "[12]" {
		t.Fatalf("the link's number lost its style: %+v", last)
	}
	if len(lines[len(lines)-2]) != 0 || lines[len(lines)-1][0].Style != 'h' {
		t.Fatalf("blank line or heading lost: %+v", lines[len(lines)-2:])
	}
}

func TestASCIIAndAddress(t *testing.T) {
	if got := toASCII("“Café” – 5 × 3… €9"); got != `"Cafe" - 5 x 3... EUR9` {
		t.Fatalf("toASCII: %q", got)
	}
	for in, want := range map[string]string{
		"amazon.com":             "https://amazon.com",
		"http://example.com/a b": "http://example.com/a b",
		"pcjr sidecar":           "https://html.duckduckgo.com/html/?q=pcjr+sidecar",
		"tetris":                 "https://html.duckduckgo.com/html/?q=tetris",
	} {
		if got := Address(in); got != want {
			t.Errorf("Address(%q) = %q, want %q", in, got, want)
		}
	}
}
