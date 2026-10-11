package web

import (
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
	Cached    bool
	Dropped   int    // blocks thrown out because they pointed at nothing real
	Reply     string // what the model wrote (for tuning: chromejr -try)
}

func (s Stats) String() string {
	if s.Cached {
		return "cached"
	}
	return fmt.Sprintf("%.1fs, %d tokens in, %d out, $%.4f, %d dropped", s.Seconds, s.InTokens, s.OutTokens, s.CostUSD, s.Dropped)
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
			b.WriteString("## ")
		}
		b.WriteString(t)
		b.WriteByte('\n')
	}
	return b.String()
}

func isHeading(l []Seg) bool {
	for _, s := range l {
		if strings.TrimSpace(s.Text) != "" && s.Style != 'h' {
			return false
		}
	}
	return len(l) > 0
}

// Filter lays a page out as a View. The same page (address and text) is
// laid out once and then comes from the cache.
func (f *Filterer) Filter(ctx context.Context, p *Page) (*View, Stats, error) {
	in := PageText(p)
	sum := sha256.Sum256([]byte(in))
	key := hex.EncodeToString(sum[:])
	f.mu.Lock()
	if v := f.cache[key]; v != nil {
		f.mu.Unlock()
		return v, Stats{Cached: true}, nil
	}
	f.mu.Unlock()

	start := time.Now()
	reply, st, err := f.ask(ctx, in)
	st.Seconds = time.Since(start).Seconds()
	if err != nil {
		return nil, st, err
	}
	st.Reply = reply
	v, dropped, err := Build(reply, p)
	st.Dropped = dropped
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

// ask runs the model once and returns its reply text.
func (f *Filterer) ask(ctx context.Context, page string) (string, Stats, error) {
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
		"--output-format", "json")
	cmd.Dir = os.TempDir()
	cmd.Stdin = strings.NewReader(page)
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	cmd.Cancel = func() error { return syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM) }
	var stdout, stderr bytes.Buffer
	cmd.Stdout, cmd.Stderr = &stdout, &stderr
	err := cmd.Run()
	if ctx.Err() != nil {
		return "", Stats{}, fmt.Errorf("the page filter took over %s", timeout)
	}
	var r struct {
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
	if jerr := json.Unmarshal(stdout.Bytes(), &r); jerr != nil {
		if err == nil {
			err = jerr
		}
		msg := strings.TrimSpace(stderr.String())
		if msg == "" {
			msg = err.Error()
		}
		return "", Stats{}, fmt.Errorf("page filter: %s", lastLine(msg))
	}
	st := Stats{InTokens: r.Usage.Input + r.Usage.CacheRead + r.Usage.CacheWrite, OutTokens: r.Usage.Output, CostUSD: r.CostUSD}
	if r.IsError {
		return "", st, fmt.Errorf("page filter: %s", short(r.Result, 100))
	}
	return r.Result, st, nil
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
type reply struct {
	Kind, Title string
	Blocks      []replyBlock
}

type replyBlock struct {
	T                   byte
	Text, Detail, Price string
	Target              int
	Bad                 bool // a line that could not be read
}

func parseReply(text string) reply {
	var r reply
	for _, line := range strings.Split(text, "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "```") {
			continue
		}
		word, rest, _ := strings.Cut(line, " ")
		rest = strings.TrimSpace(rest)
		switch word {
		case "KIND":
			r.Kind = strings.ToLower(rest)
			continue
		case "TITLE":
			r.Title = rest
			continue
		case "H", "P", "S":
			r.Blocks = append(r.Blocks, replyBlock{T: word[0], Text: rest})
			continue
		case "-":
			r.Blocks = append(r.Blocks, replyBlock{T: '-'})
			continue
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
			r.Blocks = append(r.Blocks, b)
			continue
		}
		r.Blocks = append(r.Blocks, replyBlock{Bad: true})
	}
	return r
}

// Build checks the model's reply against the page and numbers what is
// left. It returns how many lines were dropped.
func Build(text string, p *Page) (*View, int, error) {
	r := parseReply(text)
	if r.Kind == "" && len(r.Blocks) == 0 {
		return nil, 0, errors.New("the page filter's reply is empty")
	}
	headings := map[string]bool{}
	for _, l := range p.Lines {
		if isHeading(l) {
			headings[norm(Plain(l))] = true
		}
	}
	v := &View{Kind: r.Kind, Title: oneLine(r.Title)}
	if v.Title == "" {
		v.Title = oneLine(p.Title)
	}
	dropped, n, choices := 0, 0, 0
	used := map[int]bool{}
	for _, b := range r.Blocks {
		blk := Block{Type: b.T, Text: oneLine(b.Text), Detail: oneLine(b.Detail), Price: oneLine(b.Price), Target: b.Target}
		if len(blk.Price) > 16 {
			// words, not a price: they belong with the detail
			blk.Detail = strings.TrimPrefix(blk.Detail+", "+blk.Price, ", ")
			blk.Price = ""
		}
		if (blk.Type == 'A' || blk.Type == 'I') && buyButton.MatchString(blk.Text) {
			dropped++ // spends money: only in the raw view (w), on purpose
			continue
		}
		if blk.Type == 'A' && hasField(r) && sendButton.MatchString(blk.Text) {
			dropped++ // a box's Search button: Enter does that
			continue
		}
		if blk.Type == '-' && (len(v.Blocks) == 0 || v.Blocks[len(v.Blocks)-1].Type == '-') {
			continue // a divider with nothing above it
		}
		ok := !b.Bad
		switch b.T {
		case 'H', 'P', '-':
		case 'I', 'A':
			t, found := p.Find(b.Target)
			ok = found && (t.Kind == "link" || t.Kind == "button" || t.Kind == "check" || t.Kind == "image") && !used[b.Target]
		case 'F':
			t, found := p.Find(b.Target)
			// Never a password: what the PCjr types crosses the house
			// network unencrypted.
			ok = found && (t.Kind == "field" || t.Kind == "select") && !used[b.Target]
		case 'S':
			blk.Target = 0
			ok = headings[norm(b.Text)]
		default:
			ok = false
		}
		if ok && blk.Type != '-' && blk.Text == "" {
			ok = false
		}
		if !ok {
			dropped++
			continue
		}
		switch blk.Type {
		case 'I', 'A', 'F', 'S':
			n++
			blk.N = n
			choices++
			if blk.Target > 0 {
				used[blk.Target] = true
			}
		}
		v.Blocks = append(v.Blocks, blk)
	}
	for len(v.Blocks) > 0 && v.Blocks[len(v.Blocks)-1].Type == '-' {
		v.Blocks = v.Blocks[:len(v.Blocks)-1]
	}
	if choices == 0 && len(v.Blocks) < 2 {
		return nil, dropped, errors.New("the page filter found nothing to show")
	}
	return v, dropped, nil
}

var footnote = regexp.MustCompile(`^\s*(\[[a-z0-9 ]+\]|edit)\s*$`)

var buyButton = regexp.MustCompile(`(?i)\b(buy now|place (your )?order|pay now|complete (your )?purchase|proceed to checkout|checkout)\b`)

var sendButton = regexp.MustCompile(`(?i)^(search|go|submit|send|ok)$`)

func hasField(r reply) bool {
	for _, b := range r.Blocks {
		if b.T == 'F' {
			return true
		}
	}
	return false
}

func oneLine(s string) string {
	return strings.TrimSpace(strings.Join(strings.Fields(s), " "))
}

// norm is a heading as compared: no link numbers, no case, no spaces at
// the ends.
func norm(s string) string {
	s = linkNum.ReplaceAllString(s, "")
	return strings.ToLower(oneLine(strings.TrimPrefix(strings.TrimSpace(s), "## ")))
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
