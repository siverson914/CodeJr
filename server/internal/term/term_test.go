package term

import (
	"bytes"
	"strings"
	"testing"
	"time"
)

func TestToASCII(t *testing.T) {
	cases := map[string]string{
		"plain text":    "plain text",
		"it’s “quoted”": `it's "quoted"`,
		"a — b – c":     "a - b - c",
		"wait…":         "wait...",
		"• item":        "* item",
		"tab\there":     "tab  here",
		"bell\a gone":   "bell gone",
		"line\nbreak":   "line\nbreak",
		"日本":            "??",
	}
	for in, want := range cases {
		if got := ToASCII(in); got != want {
			t.Errorf("ToASCII(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestProgressBar(t *testing.T) {
	for _, c := range []struct {
		done, total int
		want        string
	}{
		{0, 100, "[..........]"},
		{50, 100, "[#####.....]"},
		{100, 100, "[##########]"},
		{150, 100, "[##########]"},
		{5, 0, "[##########]"},
	} {
		if got := ProgressBar(c.done, c.total, 10); got != c.want {
			t.Errorf("ProgressBar(%d, %d) = %q, want %q", c.done, c.total, got, c.want)
		}
	}
}

func TestScreenWrapsAndKeepsOutput7Bit(t *testing.T) {
	var b bytes.Buffer
	s := New(&b, "Test")
	s.Redraw()
	s.Print(Plain, strings.Repeat("abcdefghij ", 20)+"— end\n")
	for _, l := range s.lines {
		if len(l.text) > wrapAt {
			t.Fatalf("line of %d columns: %q", len(l.text), l.text)
		}
	}
	for _, c := range b.Bytes() {
		if c > 127 {
			t.Fatalf("8-bit byte %#x sent to the terminal", c)
		}
	}
	if !strings.Contains(b.String(), "- end") {
		t.Fatal("em dash not converted")
	}
}

func TestScreenInputEcho(t *testing.T) {
	var b bytes.Buffer
	s := New(&b, "Test")
	s.Redraw()
	s.FocusInput()
	b.Reset()
	s.Type('h')
	s.Type('i')
	s.Backspace()
	if s.Input() != "h" {
		t.Fatalf("input = %q", s.Input())
	}
	if got := b.String(); got != "hi\b \b" {
		t.Fatalf("echo = %q", got)
	}
	s.SetEcho(false)
	b.Reset()
	s.Type('x')
	s.Backspace()
	if got := b.String(); got != " \b" {
		t.Fatalf("no-echo output = %q, want just the erase", got)
	}
}

func TestPacer(t *testing.T) {
	var b bytes.Buffer
	p := NewPacer(&b, 2400) // 240 characters per second
	start := time.Now()
	p.Write(bytes.Repeat([]byte("x"), 120))
	if el := time.Since(start); el < 350*time.Millisecond {
		t.Fatalf("120 bytes at 240 cps took %v, want about 0.5 s", el)
	}
	if b.Len() != 120 {
		t.Fatalf("wrote %d bytes", b.Len())
	}
}
