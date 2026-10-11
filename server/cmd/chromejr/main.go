// Command chromejr is ChromeJr's laptop half: it runs a real browser and
// shows its pages as numbered text to a PCjr that dials in, or, with
// -text, to this terminal (for trying sites out without a PCjr).
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"sync"

	"codejr/internal/web"
)

type termScreen struct{ color bool }

func (t termScreen) Text(style byte, s string) {
	if !t.color {
		fmt.Print(s)
		return
	}
	code := map[byte]string{'h': "1;37", 'l': "33", 'n': "36", 'c': "36"}[style]
	if code == "" {
		fmt.Print(s)
		return
	}
	fmt.Printf("\x1b[%sm%s\x1b[0m", code, s)
}
func (termScreen) Status(s string) { fmt.Fprintln(os.Stderr, "["+s+"]") }

func main() {
	home, _ := os.UserHomeDir()
	listen := flag.String("listen", ":4850", "address the PCjr's modem dials")
	profile := flag.String("profile", filepath.Join(home, ".local", "share", "chromejr", "profile"), "the browser's own folder (cookies, logins)")
	bin := flag.String("browser", "", "the browser to run (default: found by itself)")
	show := flag.Bool("show", false, "open a real browser window (to sign in to a site by hand)")
	text := flag.Bool("text", false, "browse from this terminal: commands on standard input, pages on standard output")
	color := flag.Bool("color", false, "with -text: colors")
	save := flag.String("save", "", "with -text: keep every page read in this folder, as JSON")
	nofilter := flag.Bool("nofilter", false, "show pages as numbered text, without the page filter")
	model := flag.String("model", "haiku", "the model that lays pages out")
	try := flag.Bool("try", false, "lay out saved pages (the files named, from -save) and print them; no browser")
	baud := flag.Int("baud", 9600, "serial speed until the client says its own")
	flag.Parse()

	var filter *web.Filterer
	if !*nofilter {
		filter = &web.Filterer{Model: *model}
	}
	if *try {
		tryPages(filter, *color, flag.Args())
		return
	}

	if err := os.MkdirAll(*profile, 0o700); err != nil {
		log.Fatal(err)
	}
	b, err := web.Open(*profile, *bin, *show)
	if err != nil {
		log.Fatal(err)
	}
	defer b.Close()

	if *text {
		s := &web.Session{B: b, Out: termScreen{*color}, Logf: log.Printf, SaveDir: *save, Filter: filter}
		for _, a := range flag.Args() {
			s.Command(a)
		}
		sc := bufio.NewScanner(os.Stdin)
		for sc.Scan() {
			if sc.Text() == "q" || sc.Text() == "/exit" {
				break
			}
			fmt.Printf("\n>>> %s\n", sc.Text())
			s.Command(sc.Text())
		}
		return
	}

	l, err := net.Listen("tcp", *listen)
	if err != nil {
		log.Fatal(err)
	}
	log.Printf("ChromeJr listening on %s, browser profile %s", l.Addr(), *profile)
	var one sync.Mutex // one browser, one tab: one caller at a time
	for {
		conn, err := l.Accept()
		if err != nil {
			log.Fatal(err)
		}
		go func() {
			one.Lock()
			defer one.Unlock()
			web.Serve(b, filter, conn, *baud, conn.RemoteAddr().String())
		}()
	}
}

// tryPages lays out saved pages, for tuning the page filter without a
// browser or a PCjr: the view as the PCjr would draw it, its Junior
// markup, and what it cost.
func tryPages(f *web.Filterer, color bool, files []string) {
	if f == nil {
		log.Fatal("-try needs the page filter (no -nofilter)")
	}
	scr := termScreen{color}
	for _, file := range files {
		p, err := web.LoadPage(file)
		if err != nil {
			log.Fatal(err)
		}
		fmt.Printf("\n######## %s (%d lines, %d numbered)\n", file, len(p.Lines), len(p.Targets))
		v, st, err := f.Filter(context.Background(), p)
		if err != nil {
			fmt.Printf("FAILED (%s): %v\n", st, err)
			if os.Getenv("SHOW_MARKUP") != "" {
				fmt.Print(st.Reply)
			}
			continue
		}
		m := web.Markup(v)
		for _, l := range web.RenderMarkup(m, web.Width) {
			for _, s := range l {
				scr.Text(s.Style, s.Text)
			}
			fmt.Println()
		}
		fmt.Printf("---- %s, %s, %d bytes of markup\n", v.Kind, st, len(m))
		if os.Getenv("SHOW_MARKUP") != "" {
			fmt.Print(m)
		}
		if os.Getenv("SHOW_REPLY") != "" {
			fmt.Print("---- the model wrote:\n" + st.Reply + "\n")
		}
	}
}
