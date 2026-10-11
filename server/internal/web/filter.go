package web

import (
	"bufio"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

// The page filter: a small, fast model on the laptop reads a page's
// numbered text and picks out what matters (a search box, ten results,
// an article's lead and its sections, five actions). Every choice it
// offers must point at a real numbered element of the page; the rest is
// dropped, so it can never invent a click.

// View is a page as the filter laid it out.
type View struct {
	Kind   string // search, results, list, article, product, form, other
	Title  string
	Blocks []Block
}

// Block is one piece of a view.
type Block struct {
	Type   byte   // 'H' heading, 'P' text, 'I' item, 'A' action, 'F' field, 'S' section, '-' divider
	Text   string // the heading, text, item title, label, or section heading
	Detail string // an item's one line of detail
	Price  string // an item's price (or points, or date)
	Target int    // the page's number for I, A and F
	N      int    // the view's own number for I, A, F and S
}

// Choice is the view's numbered block n.
func (v *View) Choice(n int) (Block, bool) {
	for _, b := range v.Blocks {
		if b.N == n && n > 0 {
			return b, true
		}
	}
	return Block{}, false
}

// Stats is what one filtering cost.
type Stats struct {
	Seconds   float64
	InTokens  int
	OutTokens int
	CostUSD   float64
	First     float64 // seconds until the first line could be shown
	Cached    bool
	Dropped   int    // blocks thrown out because they pointed at nothing real
	Reply     string // what the model wrote (for tuning: chromejr -try)
}

func (s Stats) String() string {
	if s.Cached {
		return "cached"
	}
	return fmt.Sprintf("first line %.1fs, all %.1fs, %d tokens in, %d out, $%.4f, %d dropped", s.First, s.Seconds, s.InTokens, s.OutTokens, s.CostUSD, s.Dropped)
}

// Filterer runs the filter with headless Claude Code (`claude -p`), which
// uses the laptop's Claude subscription: no API key needed.
type Filterer struct {
	Bin     string        // default "claude"
	Model   string        // default "haiku"
	Timeout time.Duration // default 40 s

	mu    sync.Mutex
	cache map[string]*View
}

// maxInput is how much of a page's text the model is given. Wikipedia's
// IBM PCjr article is 53 KB; the lead and the headings are what matter.
const maxInput = 60000

// PageText is the page as the model reads it: its address and title,
// then its lines with the numbers on them.
func PageText(p *Page) string {
	var b strings.Builder
	fmt.Fprintf(&b, "URL: %s\nTITLE: %s\n\n", p.URL, p.Title)
	for _, l := range p.Lines {
		t := Plain(l)
		if b.Len()+len(t) > maxInput {
			// Past the cap only the headings still go: an article's
			// sections can all be offered.
			if !isHeading(l) {
				continue
			}
		}
		if isHeading(l) {
			if l[0].Style == 'g' {
				b.WriteString("### ")
			} else {
				b.WriteString("## ")
			}
		}
		b.WriteString(t)
		b.WriteByte('\n')
	}
	return b.String()
}

func isHeading(l []Seg) bool {
	for _, s := range l {
		if strings.TrimSpace(s.Text) != "" && s.Style != 'h' && s.Style != 'g' {
			return false
		}
	}
	return len(l) > 0
}

// Filter lays a page out as a View. Each line of its Junior markup goes
// to show as soon as it is ready: the model's reply is read as it is
// written, so the top of a page is on the way while the rest is still
// being written. The same page (address and text) is laid out once and
// then comes from the cache. show may be nil.
func (f *Filterer) Filter(ctx context.Context, p *Page, show func(line string)) (*View, Stats, error) {
	if show == nil {
		show = func(string) {}
	}
	in := PageText(p)
	sum := sha256.Sum256([]byte(in))
	key := hex.EncodeToString(sum[:])
	f.mu.Lock()
	v := f.cache[key]
	f.mu.Unlock()
	if v != nil {
		for _, l := range strings.Split(strings.TrimSuffix(Markup(v), "\n"), "\n") {
			show(l)
		}
		return v, Stats{Cached: true}, nil
	}

	start := time.Now()
	b := newBuilder(p)
	var first time.Duration
	reply, st, err := f.ask(ctx, in, func(line string) {
		for _, m := range b.add(line) {
			if first == 0 {
				first = time.Since(start)
			}
			show(m)
		}
	})
	st.Seconds = time.Since(start).Seconds()
	st.First = first.Seconds()
	st.Reply = reply
	st.Dropped = b.dropped
	if err != nil {
		return nil, st, err
	}
	v, err = b.done()
	if err != nil {
		return nil, st, err
	}
	f.mu.Lock()
	if f.cache == nil {
		f.cache = map[string]*View{}
	}
	f.cache[key] = v
	f.mu.Unlock()
	return v, st, nil
}

// ask runs the model once, calls line with each line of its reply as it
// arrives, and returns the whole reply.
func (f *Filterer) ask(ctx context.Context, page string, line func(string)) (string, Stats, error) {
	bin, model, timeout := f.Bin, f.Model, f.Timeout
	if bin == "" {
		bin = "claude"
	}
	if model == "" {
		model = "haiku"
	}
	if timeout == 0 {
		timeout = 40 * time.Second
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	cmd := exec.CommandContext(ctx, bin, "-p",
		"--model", model,
		// Low effort: no thinking. A page takes 2 to 3 s instead of 8 to 11.
		"--effort", "low",
		"--system-prompt", filterPrompt,
		"--tools", "",
		"--no-session-persistence",
		"--strict-mcp-config",
		// No settings and no CLAUDE.md from this laptop: only the page.
		"--setting-sources", "",
		"--output-format", "stream-json", "--verbose", "--include-partial-messages")
	cmd.Dir = os.TempDir()
	cmd.Stdin = strings.NewReader(page)
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	cmd.Cancel = func() error { return syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM) }
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return "", Stats{}, err
	}
	if err := cmd.Start(); err != nil {
		return "", Stats{}, fmt.Errorf("page filter: %w", err)
	}
	var text, part strings.Builder
	var r *streamResult
	sc := bufio.NewScanner(stdout)
	sc.Buffer(make([]byte, 1<<20), 16<<20)
	for sc.Scan() {
		var m streamMsg
		if json.Unmarshal(sc.Bytes(), &m) != nil {
			continue
		}
		switch m.Type {
		case "stream_event":
			if m.Event == nil || m.Event.Delta == nil || m.Event.Delta.Type != "text_delta" {
				continue
			}
			t := m.Event.Delta.Text
			text.WriteString(t)
			for {
				i := strings.IndexByte(t, '\n')
				if i < 0 {
					part.WriteString(t)
					break
				}
				part.WriteString(t[:i])
				line(part.String())
				part.Reset()
				t = t[i+1:]
			}
		case "result":
			r = &m.streamResult
		}
	}
	if part.Len() > 0 {
		line(part.String())
	}
	err = cmd.Wait()
	if ctx.Err() != nil {
		return text.String(), Stats{}, fmt.Errorf("the page filter took over %s", timeout)
	}
	if r == nil {
		msg := strings.TrimSpace(stderr.String())
		if msg == "" && err != nil {
			msg = err.Error()
		}
		return text.String(), Stats{}, fmt.Errorf("page filter: %s", lastLine(msg))
	}
	st := Stats{InTokens: r.Usage.Input + r.Usage.CacheRead + r.Usage.CacheWrite, OutTokens: r.Usage.Output, CostUSD: r.CostUSD}
	if r.IsError {
		return text.String(), st, fmt.Errorf("page filter: %s", short(r.Result, 100))
	}
	return text.String(), st, nil
}

// The parts of Claude Code's stream-json that the filter reads.
type streamMsg struct {
	Type  string `json:"type"`
	Event *struct {
		Delta *struct {
			Type string `json:"type"`
			Text string `json:"text"`
		} `json:"delta"`
	} `json:"event"`
	streamResult
}

type streamResult struct {
	Result  string  `json:"result"`
	IsError bool    `json:"is_error"`
	CostUSD float64 `json:"total_cost_usd"`
	Usage   struct {
		Input      int `json:"input_tokens"`
		CacheRead  int `json:"cache_read_input_tokens"`
		CacheWrite int `json:"cache_creation_input_tokens"`
		Output     int `json:"output_tokens"`
	} `json:"usage"`
}

func lastLine(s string) string {
	if i := strings.LastIndexByte(s, '\n'); i >= 0 {
		return s[i+1:]
	}
	return s
}

// The model replies in lines much like Junior markup, which takes about
// half the tokens JSON would (and tokens are time):
//
//	KIND results
//	TITLE pcjr at DuckDuckGo
//	H a heading     P text     S a section's heading     -
//	I 12 | title | detail | price
//	A 12 | label
//	F 12 | label
//
// where 12 is the page's own number. Lines it can't read are dropped.
type replyBlock struct {
	T                   byte
	Text, Detail, Price string
	Target              int
	Bad                 bool // a line that could not be read
}

// parseLine reads one line of the reply: key is "KIND" or "TITLE" (with
// the value in b.Text) or "" for a block; ok is false for a line that
// says nothing (blank, a code fence).
func parseLine(line string) (key string, b replyBlock, ok bool) {
	line = strings.TrimSpace(line)
	if line == "" || strings.HasPrefix(line, "```") {
		return "", b, false
	}
	word, rest, _ := strings.Cut(line, " ")
	rest = strings.TrimSpace(rest)
	switch word {
	case "KIND", "TITLE":
		return word, replyBlock{Text: rest}, true
	case "H", "P", "S":
		return "", replyBlock{T: word[0], Text: rest}, true
	case "-":
		return "", replyBlock{T: '-'}, true
	case "I", "A", "F":
		f := strings.Split(rest, "|")
		if len(f) == 1 {
			// "A 11 Add to cart" or "A 11: Add to cart": no bars
			num, label, _ := strings.Cut(rest, " ")
			f = []string{strings.TrimRight(num, ":.)"), label}
		}
		for i := range f {
			f[i] = strings.TrimSpace(f[i])
		}
		n, err := strconv.Atoi(strings.Trim(f[0], "[]{}<>()#:"))
		if err != nil || len(f) < 2 {
			break
		}
		b := replyBlock{T: word[0], Target: n, Text: f[1]}
		if len(f) > 2 {
			b.Detail = f[2]
		}
		if len(f) > 3 {
			b.Price = f[3]
		}
		return "", b, true
	}
	return "", replyBlock{Bad: true}, true
}

// builder checks the model's reply against the page one line at a time
// and numbers what is left.
type builder struct {
	p        *Page
	headings map[string]byte   // a heading's words: its style, 'h' or 'g'
	parent   map[string]string // a smaller heading's main heading above it
	sections map[string]bool   // the sections the view offers so far
	used     map[int]bool
	v        View
	titled   bool // the title line has gone out
	divider  bool // a divider waits for something below it
	field    bool // the view has a text box
	read     bool // the reply said anything at all
	n        int
	dropped  int
}

func newBuilder(p *Page) *builder {
	b := &builder{p: p, headings: map[string]byte{}, parent: map[string]string{}, sections: map[string]bool{}, used: map[int]bool{}}
	main := ""
	for _, l := range p.Lines {
		if isHeading(l) {
			h := norm(Plain(l))
			if b.headings[h] != 'h' {
				b.headings[h] = l[0].Style
			}
			if l[0].Style == 'h' {
				main = h
			} else if _, ok := b.parent[h]; !ok {
				b.parent[h] = main
			}
		}
	}
	return b
}

// add reads one line of the reply and returns the lines of Junior markup
// it makes ready to show (often none, sometimes the title too).
func (b *builder) add(line string) []string {
	key, rb, ok := parseLine(line)
	if !ok {
		return nil
	}
	b.read = true
	var out []string
	title := func() {
		if !b.titled {
			if b.v.Title == "" {
				b.v.Title = oneLine(b.p.Title)
			}
			out = append(out, "T"+field(b.v.Title))
			b.titled = true
		}
	}
	switch key {
	case "KIND":
		b.v.Kind = strings.ToLower(rb.Text)
		return nil
	case "TITLE":
		if !b.titled {
			b.v.Title = oneLine(rb.Text)
			title()
		}
		return out
	}
	blk := Block{Type: rb.T, Text: oneLine(rb.Text), Detail: oneLine(rb.Detail), Price: oneLine(rb.Price), Target: rb.Target}
	if len(blk.Price) > 16 {
		// words, not a price: they belong with the detail
		blk.Detail = strings.TrimPrefix(blk.Detail+", "+blk.Price, ", ")
		blk.Price = ""
	}
	if blk.Type == '-' {
		b.divider = len(b.v.Blocks) > 0 // shown once something comes below it
		return nil
	}
	ok = !rb.Bad
	switch rb.T {
	case 'H', 'P':
	case 'I', 'A':
		t, found := b.p.Find(rb.Target)
		ok = found && (t.Kind == "link" || t.Kind == "button" || t.Kind == "check" || t.Kind == "image") && !b.used[rb.Target]
		// Spending money is only in the raw view (w), on purpose; and
		// a box's Search button is what Enter does.
		if buyButton.MatchString(blk.Text) || (rb.T == 'A' && b.field && sendButton.MatchString(blk.Text)) {
			ok = false
		}
	case 'F':
		t, found := b.p.Find(rb.Target)
		// Never a password: what the PCjr types crosses the house
		// network unencrypted.
		ok = found && (t.Kind == "field" || t.Kind == "select") && !b.used[rb.Target]
	case 'S':
		blk.Target = 0
		// A smaller heading is part of the section above it: offered
		// only when that one isn't (a blog's main heading is its date).
		h := norm(rb.Text)
		lv := b.headings[h]
		ok = lv == 'h' || (lv == 'g' && !b.sections[b.parent[h]])
		if ok {
			b.sections[h] = true
		}
	default:
		ok = false
	}
	if ok && blk.Text == "" {
		ok = false
	}
	if !ok {
		b.dropped++
		return nil
	}
	title()
	if b.divider {
		b.v.Blocks = append(b.v.Blocks, Block{Type: '-'})
		out = append(out, "-")
		b.divider = false
	}
	switch blk.Type {
	case 'I', 'A', 'F', 'S':
		b.n++
		blk.N = b.n
		if blk.Target > 0 {
			b.used[blk.Target] = true
		}
		b.field = b.field || blk.Type == 'F'
	}
	b.v.Blocks = append(b.v.Blocks, blk)
	return append(out, blockMarkup(blk))
}

// done is the finished view, or why there is none.
func (b *builder) done() (*View, error) {
	if !b.read {
		return nil, errors.New("the page filter's reply is empty")
	}
	if b.n == 0 && len(b.v.Blocks) < 2 {
		return nil, errors.New("the page filter found nothing to show")
	}
	v := b.v
	if v.Title == "" {
		v.Title = oneLine(b.p.Title)
	}
	return &v, nil
}

// Build checks a whole reply against the page (Filter does the same a
// line at a time). It returns how many lines were dropped.
func Build(text string, p *Page) (*View, int, error) {
	b := newBuilder(p)
	for _, l := range strings.Split(text, "\n") {
		b.add(l)
	}
	v, err := b.done()
	return v, b.dropped, err
}

var footnote = regexp.MustCompile(`^\s*(\[[a-z0-9 ]+\]|edit)\s*$`)

var buyButton = regexp.MustCompile(`(?i)\b(buy now|place (your )?order|pay now|complete (your )?purchase|proceed to checkout|checkout)\b`)

var sendButton = regexp.MustCompile(`(?i)^(search|go|submit|send|ok)$`)

func oneLine(s string) string {
	return strings.TrimSpace(strings.Join(strings.Fields(s), " "))
}

// norm is a heading as compared: no link numbers, no case, no spaces at
// the ends.
func norm(s string) string {
	s = linkNum.ReplaceAllString(s, "")
	return strings.ToLower(oneLine(strings.TrimLeft(strings.TrimSpace(s), "# ")))
}

// Section is the part of the page under the view's section n: from its
// heading to the next heading the view offers, with the page's own
// numbers taken out (they mean nothing in the view).
func Section(p *Page, v *View, n int) [][]Seg {
	b, ok := v.Choice(n)
	if !ok || b.Type != 'S' {
		return nil
	}
	others := map[string]bool{}
	for _, o := range v.Blocks {
		if o.Type == 'S' && o.N != n {
			others[norm(o.Text)] = true
		}
	}
	want := norm(b.Text)
	var out [][]Seg
	in := false
	for _, l := range p.Lines {
		if isHeading(l) {
			h := norm(Plain(l))
			if in && others[h] {
				break
			}
			if !in && h == want {
				in = true
			}
		}
		if !in {
			continue
		}
		var segs []Seg
		for _, s := range l {
			switch s.Style {
			case 'n':
				continue
			case 'l':
				if footnote.MatchString(s.Text) {
					continue // [1]: a footnote's mark (or Wikipedia's "edit")
				}
				s.Style = 'p'
			case 'c':
				s.Text = linkNum.ReplaceAllString(s.Text, "")
			}
			segs = append(segs, s)
		}
		out = append(out, segs)
	}
	return out
}
