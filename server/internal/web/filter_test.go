package web

import (
	"strings"
	"testing"
)

func load(t *testing.T, name string) *Page {
	t.Helper()
	p, err := LoadPage("testdata/" + name)
	if err != nil {
		t.Fatal(err)
	}
	return p
}

// Google's page: 2 is the search box, 3 a button, 6 "Google Search".
func TestBuildChecksNumbers(t *testing.T) {
	p := load(t, "google.com.json")
	reply := "```\nKIND search\nTITLE Google\nH Google\nF 2 | Search\nA 6 | Search\nA 3 | Search by voice\n" +
		"A 999 | Made up\nF 3 | A button is not a box\nI 2 | A box is not a link\nA 3 | Twice\nnonsense line\n-\n```"
	v, dropped, err := Build(reply, p)
	if err != nil {
		t.Fatal(err)
	}
	m := Markup(v)
	want := "TGoogle\nHGoogle\nF1\tSearch\nA2\tSearch by voice\n"
	if m != want {
		t.Errorf("markup\n%q, want\n%q", m, want)
	}
	// Search button, 999, 3 as a box, 2 as a link, 3 again, the nonsense
	if dropped != 6 {
		t.Errorf("dropped %d, want 6", dropped)
	}
	if b, _ := v.Choice(1); b.Target != 2 {
		t.Errorf("choice 1 is page number %d, want 2", b.Target)
	}
	if _, ok := v.Choice(3); ok {
		t.Error("choice 3 should not exist")
	}
}

func TestBuildPasswordsAndPrices(t *testing.T) {
	p := &Page{Title: "Sign in", Targets: []Target{{1, "password", "Password"}, {2, "link", "Thing"}}}
	p.Targets = append(p.Targets, Target{3, "button", "Buy Now"})
	v, dropped, err := Build("KIND form\nF 1 | Password\nI 2 | Thing | | Ships tomorrow from the warehouse\nA 3 | Buy Now\nA 3: Buy Now\n", p)
	if err != nil {
		t.Fatal(err)
	}
	if dropped != 3 || len(v.Blocks) != 1 {
		t.Fatalf("password or Buy Now not dropped: %d dropped, %+v", dropped, v.Blocks)
	}
	if b := v.Blocks[0]; b.Price != "" || b.Detail != "Ships tomorrow from the warehouse" {
		t.Errorf("words in the price should move to the detail: %+v", b)
	}
	if _, _, err := Build("KIND other\n", p); err == nil {
		t.Error("an empty view should be an error (the raw page is shown instead)")
	}
}

func TestReplyWithoutBars(t *testing.T) {
	var got []replyBlock
	for _, l := range []string{"A 11: Add to cart", "I [12] Thing", "A twelve | x"} {
		_, b, _ := parseLine(l)
		got = append(got, b)
	}
	if got[0].Target != 11 || got[0].Text != "Add to cart" || got[1].Target != 12 || !got[2].Bad {
		t.Errorf("%+v", got)
	}
}

// Streaming: lines come out as soon as they are checked, the title
// first, a divider only once something follows it.
func TestBuilderStreams(t *testing.T) {
	p := load(t, "google.com.json")
	b := newBuilder(p)
	var got []string
	for _, l := range []string{"KIND search", "-", "F 2 | Search", "-", "A 999 | Nothing", "A 3 | Voice", "-"} {
		got = append(got, b.add(l)...)
	}
	want := "TGoogle|F1\tSearch|-|A2\tVoice"
	if strings.Join(got, "|") != want {
		t.Errorf("got %q, want %q", strings.Join(got, "|"), want)
	}
	v, err := b.done()
	if err != nil || Markup(v) != strings.ReplaceAll(want, "|", "\n")+"\n" {
		t.Errorf("view %q, %v", Markup(v), err)
	}
}

func TestSections(t *testing.T) {
	p := load(t, "en.wikipedia.org-wiki-IBM-PCjr.json")
	v, dropped, err := Build("KIND article\nTITLE IBM PCjr\nP The PCjr.\nS Hardware\nS Video\nS Software\nS No such heading\n", p)
	if err != nil {
		t.Fatal(err)
	}
	if dropped != 2 { // Video is a part of Hardware
		t.Errorf("dropped %d, want 2", dropped)
	}
	sec := Section(p, v, 1)
	if len(sec) < 10 || Plain(sec[0]) != "Hardware" {
		t.Fatalf("section 1: %d lines, starts %q", len(sec), Plain(sec[0]))
	}
	for _, l := range sec {
		if isHeading(l) && Plain(l) == "Software" {
			t.Fatal("section Hardware runs on into Software")
		}
		if linkNum.MatchString(Plain(l)) {
			t.Fatalf("a page number left in a section: %q", Plain(l))
		}
	}
	// (subsections like Video stay inside Hardware)
	found := false
	for _, l := range sec {
		found = found || Plain(l) == "Video"
	}
	if !found {
		t.Error("subsection Video missing from Hardware")
	}
}

func TestRenderMarkup(t *testing.T) {
	m := "TAmazon\nHAmazon\nI1\tA very long product title that goes on and on about keyboards and switches and colors\t4.5 stars\t$19.99\nA2\tNext page\nA3\tSort\nF4\tSearch\nS5\tHistory\nXfuture block type\n"
	lines := RenderMarkup(m, Width)
	var all []string
	for _, l := range lines {
		s := Plain(l)
		if len(s) > Width {
			t.Errorf("line over %d columns: %q", Width, s)
		}
		all = append(all, s)
	}
	got := strings.Join(all, "\n")
	for _, want := range []string{" 1 A very long", "$19.99", "   4.5 stars", "[2] Next page  [3] Sort", " 4 Search: [", " 5 > History"} {
		if !strings.Contains(got, want) {
			t.Errorf("missing %q in\n%s", want, got)
		}
	}
	if strings.Count(got, "Amazon") != 1 {
		t.Errorf("the title shown twice:\n%s", got)
	}
}
