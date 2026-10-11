// Package web is ChromeJr's laptop half: a real browser (headless
// Chromium, driven over its debugging protocol) whose pages are turned
// into numbered text for a PCjr to show, and whose links, buttons and
// form fields the PCjr works by number.
package web

import (
	"context"
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/chromedp/chromedp"
)

//go:embed extract.js
var extractJS string

// Seg is a run of text in one style: 'p' plain, 'h' heading, 'g' a
// smaller heading (a part inside the one above), 'l' link
// text, 'n' a link's number, 'c' a control (field, button, picture).
type Seg struct {
	Style byte
	Text  string
}

// Target is something on the page that has a number.
type Target struct {
	N     int    `json:"n"`
	Kind  string `json:"kind"` // link button field password check select image
	Label string `json:"label"`
}

// Page is one page as text.
type Page struct {
	Title   string
	URL     string
	Lines   [][]Seg
	Targets []Target
	Whole   bool // everything on the page, not only its main content
}

// Browser is one Chromium with one tab.
type Browser struct {
	ctx    context.Context
	cancel context.CancelFunc
}

// Open starts Chromium. profile is its user data folder (cookies and
// logins live there, so they last); bin is the browser to run ("" finds
// one); show opens a real window, for signing in to a site by hand.
func Open(profile, bin string, show bool) (*Browser, error) {
	opts := append(chromedp.DefaultExecAllocatorOptions[:],
		chromedp.UserDataDir(profile),
		chromedp.WindowSize(1100, 1400),
		// The stock headless user agent says "HeadlessChrome", which
		// some sites turn away.
		chromedp.UserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36"),
		chromedp.Flag("disable-blink-features", "AutomationControlled"),
		// Back from the back-forward cache sends no load event, and
		// NavigateBack waited 30 s for one (then 20 more in settle).
		chromedp.Flag("disable-features", "BackForwardCache"),
	)
	if bin != "" {
		opts = append(opts, chromedp.ExecPath(bin))
	}
	if show {
		opts = append(opts, chromedp.Flag("headless", false))
	}
	actx, acancel := chromedp.NewExecAllocator(context.Background(), opts...)
	ctx, cancel := chromedp.NewContext(actx)
	b := &Browser{ctx: ctx, cancel: func() { cancel(); acancel() }}
	if err := chromedp.Run(ctx); err != nil { // starts the browser
		b.cancel()
		return nil, fmt.Errorf("starting the browser: %w", err)
	}
	return b, nil
}

func (b *Browser) Close() { b.cancel() }

func (b *Browser) run(d time.Duration, actions ...chromedp.Action) error {
	ctx, cancel := context.WithTimeout(b.ctx, d)
	defer cancel()
	return chromedp.Run(ctx, actions...)
}

// settle gives a page that builds itself with scripts a moment after the
// load event.
func (b *Browser) settle() {
	b.run(20*time.Second, chromedp.WaitReady("body", chromedp.ByQuery))
	time.Sleep(1200 * time.Millisecond)
}

// Address makes a URL of what someone typed: "example.com", a full URL,
// or anything else, which becomes a search.
func Address(s string) string {
	s = strings.TrimSpace(s)
	if strings.Contains(s, "://") {
		return s
	}
	if !strings.ContainsAny(s, " ") && strings.Contains(s, ".") {
		return "https://" + s
	}
	return SearchURL(s)
}

// SearchURL is a search for words. DuckDuckGo's plain HTML page: it
// works without scripts and reads well as text.
func SearchURL(words string) string {
	return "https://html.duckduckgo.com/html/?q=" + url.QueryEscape(words)
}

// Go loads a URL.
func (b *Browser) Go(u string) error {
	err := b.run(45*time.Second, chromedp.Navigate(u))
	b.settle()
	return err
}

func (b *Browser) Back() error {
	err := b.run(30*time.Second, chromedp.NavigateBack())
	b.settle()
	return err
}

func (b *Browser) Reload() error {
	err := b.run(45*time.Second, chromedp.Reload())
	b.settle()
	return err
}

// Read turns the page as it is now into text: its main content, or with
// whole everything on it (menus, side bars, the footer).
func (b *Browser) Read(whole bool) (*Page, error) {
	var raw string
	js := strings.Replace(extractJS, "%WHOLE%", fmt.Sprint(whole), 1)
	if err := b.run(30*time.Second, chromedp.Evaluate(js, &raw)); err != nil {
		return nil, fmt.Errorf("reading the page: %w", err)
	}
	var j struct {
		Title   string        `json:"title"`
		URL     string        `json:"url"`
		Lines   [][][2]string `json:"lines"`
		Targets []Target      `json:"targets"`
		Whole   bool          `json:"whole"`
	}
	if err := json.Unmarshal([]byte(raw), &j); err != nil {
		return nil, err
	}
	p := &Page{Title: j.Title, URL: j.URL, Targets: j.Targets, Whole: j.Whole}
	for _, l := range j.Lines {
		var segs []Seg
		for _, s := range l {
			st := byte('p')
			if s[0] != "" {
				st = s[0][0]
			}
			segs = append(segs, Seg{st, s[1]})
		}
		p.Lines = append(p.Lines, segs)
	}
	return p, nil
}

func sel(n int) string { return fmt.Sprintf(`[data-cj="%d"]`, n) }

// Find is the numbered thing on the page last read.
func (p *Page) Find(n int) (Target, bool) {
	for _, t := range p.Targets {
		if t.N == n {
			return t, true
		}
	}
	return Target{}, false
}

// Press follows a link, presses a button or flips a checkbox, then waits
// for whatever that sets off.
func (b *Browser) Press(n int) error {
	// A real click at the element, as a person's would be: scripts that
	// listen for mouse events see it, and a plain link is followed.
	err := b.run(20*time.Second,
		chromedp.ScrollIntoView(sel(n), chromedp.ByQuery),
		chromedp.Click(sel(n), chromedp.ByQuery, chromedp.NodeVisible))
	if err != nil {
		// Covered or off screen: ask the element itself.
		err = b.run(10*time.Second, chromedp.Evaluate(fmt.Sprintf(`document.querySelector('%s').click()`, sel(n)), nil))
	}
	b.settle()
	return err
}

// Fill types text into a field (replacing what was there). enter also
// presses Enter, which sends most search boxes and logins.
func (b *Browser) Fill(n int, text string, enter bool) error {
	keys := text
	if enter {
		keys += "\r"
	}
	err := b.run(20*time.Second,
		chromedp.ScrollIntoView(sel(n), chromedp.ByQuery),
		chromedp.SetValue(sel(n), "", chromedp.ByQuery),
		chromedp.SendKeys(sel(n), keys, chromedp.ByQuery))
	if enter {
		b.settle()
	}
	return err
}

// Choose picks the option of a select box whose text contains want.
func (b *Browser) Choose(n int, want string) error {
	var ok bool
	js := fmt.Sprintf(`(() => { const s = document.querySelector('%s'); const w = %q.toLowerCase();
		for (const o of s.options) if (o.text.toLowerCase().includes(w)) { s.value = o.value;
			s.dispatchEvent(new Event('input', {bubbles: true})); s.dispatchEvent(new Event('change', {bubbles: true})); return true; }
		return false; })()`, sel(n), want)
	if err := b.run(10*time.Second, chromedp.Evaluate(js, &ok)); err != nil {
		return err
	}
	if !ok {
		return errors.New("no choice there matches " + want)
	}
	b.settle()
	return nil
}

// Shot is a PNG of one numbered element (a picture), or of the visible
// page when n is 0.
func (b *Browser) Shot(n int) ([]byte, error) {
	var png []byte
	if n == 0 {
		return png, b.run(20*time.Second, chromedp.CaptureScreenshot(&png))
	}
	return png, b.run(20*time.Second,
		chromedp.ScrollIntoView(sel(n), chromedp.ByQuery),
		chromedp.Screenshot(sel(n), &png, chromedp.ByQuery))
}

// SavePage writes a page as JSON into dir, named after its address, and
// returns the file's path.
func SavePage(dir string, p *Page) (string, error) {
	name := p.URL
	if u, err := url.Parse(p.URL); err == nil {
		name = u.Host + u.Path
		if u.RawQuery != "" {
			name += "_" + u.RawQuery
		}
	}
	name = strings.Trim(slug.ReplaceAllString(strings.TrimPrefix(name, "www."), "-"), "-")
	if len(name) > 80 {
		name = name[:80]
	}
	b, err := json.MarshalIndent(p, "", " ")
	if err != nil {
		return "", err
	}
	f := filepath.Join(dir, name+".json")
	return f, os.WriteFile(f, b, 0o644)
}

var slug = regexp.MustCompile(`[^A-Za-z0-9.]+`)

// LoadPage reads a page SavePage wrote.
func LoadPage(file string) (*Page, error) {
	b, err := os.ReadFile(file)
	if err != nil {
		return nil, err
	}
	p := &Page{}
	return p, json.Unmarshal(b, p)
}
