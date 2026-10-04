// Package term draws CodeJr's screen on a VT100-class terminal (MS-Kermit's
// emulator) using only 7-bit ASCII and escape sequences, so nothing depends
// on the PCjr's code page or Kermit's character-set settings.
//
// Layout (80x24; Kermit keeps line 25 for its own status line):
//
//	1      header
//	2..21  transcript (VT100 scroll region)
//	22     divider
//	23     input line
//	24     status bar
package term

import (
	"bytes"
	"fmt"
	"io"
	"strings"
)

const (
	Width = 80

	rowHeader = 1
	rowTop    = 2
	rowBottom = 21
	rowDiv    = 22
	rowInput  = 23
	rowStatus = 24

	// Wrap one short of the right edge so writing the last column never
	// triggers the terminal's own autowrap.
	wrapAt = Width - 1
	keep   = rowBottom - rowTop + 1 // transcript lines kept for redraws
)

// Style is a transcript color.
type Style int

const (
	Plain  Style = iota // Claude's replies
	User                // what you typed
	Note                // tool activity, file transfers
	Warn                // errors
	Banner              // welcome text
)

// Every color sets its background explicitly. MS-Kermit fills cleared and
// scrolled lines with its own default otherwise (gray on a real PCjr).
var sgr = map[Style]string{
	Plain:  "\x1b[0;37;40m",
	User:   "\x1b[0;1;33;40m",
	Note:   "\x1b[0;36;40m",
	Warn:   "\x1b[0;1;31;40m",
	Banner: "\x1b[0;1;37;40m",
}

const (
	headerSGR = "\x1b[0;1;37;44m"
	statusSGR = "\x1b[0;30;46m"
	promptSGR = "\x1b[0;1;32;40m"
	inputSGR  = "\x1b[0;1;37;40m"
	baseSGR   = "\x1b[0;37;40m"
	decLines  = "\x1b(0" // DEC special graphics: q is a horizontal line
	asciiSet  = "\x1b(B"
)

type line struct {
	style Style
	text  string
}

// Screen tracks what is on the terminal so it only sends what changed:
// at 2400 baud a full repaint takes about 8 seconds.
type Screen struct {
	w     io.Writer
	title string

	cur     line   // transcript line being written
	pending []rune // current word, held back until we know whether it fits
	style   Style
	lines   []line // finished transcript lines, newest last
	atText  bool   // terminal cursor is at the end of cur

	status string
	prompt string
	input  []rune
	echo   bool // echo typing; off when the modem already echoes it
}

func New(w io.Writer, title string) *Screen {
	return &Screen{w: w, title: title, prompt: "> ", echo: true}
}

func (s *Screen) out(parts ...string) {
	var b bytes.Buffer
	for _, p := range parts {
		b.WriteString(p)
	}
	s.w.Write(b.Bytes())
}

func at(row, col int) string { return fmt.Sprintf("\x1b[%d;%dH", row, col) }

// Redraw paints the whole screen. Use it on connect and after anything
// that may have disturbed the terminal.
func (s *Screen) Redraw() {
	var b strings.Builder
	b.WriteString(baseSGR + asciiSet + "\x1b[r\x1b[2J")
	b.WriteString(at(rowHeader, 1) + headerSGR + pad(" "+s.title, Width) + baseSGR)
	b.WriteString(fmt.Sprintf("\x1b[%d;%dr", rowTop, rowBottom))
	// Bottom-align the history: the line being written is always on the
	// last row of the scroll region, where new text appears.
	shown := s.lines
	if len(shown) > keep-1 {
		shown = shown[len(shown)-(keep-1):]
	}
	row := rowBottom - len(shown)
	for _, l := range shown {
		b.WriteString(at(row, 1) + sgr[l.style] + l.text)
		row++
	}
	b.WriteString(at(rowBottom, 1) + sgr[s.cur.style] + s.cur.text)
	b.WriteString(at(rowDiv, 1) + "\x1b[0;34;40m" + decLines + strings.Repeat("q", Width) + asciiSet + baseSGR)
	s.out(b.String())
	// Leave the cursor at the bottom of the transcript so new text scrolls.
	s.atText = false
	s.drawStatus()
	s.drawInput()
	s.moveToText()
}

func pad(s string, n int) string {
	if len(s) >= n {
		return s[:n]
	}
	return s + strings.Repeat(" ", n-len(s))
}

// moveToText puts the cursor where transcript text continues. The
// transcript always grows at the bottom row of the scroll region.
func (s *Screen) moveToText() {
	if s.atText {
		return
	}
	s.out(at(rowBottom, len(s.cur.text)+1), sgr[s.style])
	s.atText = true
}

// Print appends text to the transcript in a style, word-wrapping at 79
// columns. Words are held until a space or newline arrives, so streamed
// text wraps cleanly; call Flush when a reply ends.
func (s *Screen) Print(st Style, text string) {
	if st != s.style {
		s.flushWord()
		s.style = st
		if s.atText {
			s.out(sgr[st])
		}
	}
	for _, r := range ToASCII(text) {
		switch r {
		case '\n':
			s.flushWord()
			s.newline()
		case ' ':
			s.flushWord()
			if len(s.cur.text) < wrapAt {
				s.emit(" ")
			}
		default:
			s.pending = append(s.pending, r)
			if len(s.pending) >= wrapAt {
				s.flushWord()
			}
		}
	}
}

// Println is Print plus a newline.
func (s *Screen) Println(st Style, text string) { s.Print(st, text+"\n") }

// Flush writes any held-back word.
func (s *Screen) Flush() { s.flushWord() }

// EndBlock finishes the current line if it has text.
func (s *Screen) EndBlock() {
	s.flushWord()
	if s.cur.text != "" {
		s.newline()
	}
}

func (s *Screen) flushWord() {
	if len(s.pending) == 0 {
		return
	}
	w := string(s.pending)
	s.pending = s.pending[:0]
	if len(s.cur.text)+len(w) > wrapAt {
		s.cur.text = strings.TrimRight(s.cur.text, " ")
		s.newline()
	}
	s.emit(w)
}

func (s *Screen) emit(t string) {
	s.moveToText()
	if s.cur.text == "" {
		s.cur.style = s.style
	}
	s.cur.text += t
	s.out(t)
}

func (s *Screen) newline() {
	s.moveToText()
	s.out("\r\n") // at the bottom margin this scrolls the region up
	s.lines = append(s.lines, s.cur)
	if len(s.lines) > 200 {
		s.lines = s.lines[len(s.lines)-200:]
	}
	s.cur = line{style: s.style}
}

// Status sets the bottom status bar.
func (s *Screen) Status(msg string) {
	if msg == s.status {
		return
	}
	s.status = msg
	s.drawStatus()
}

func (s *Screen) drawStatus() {
	// 79 columns: writing column 80 of the last row can scroll some terminals.
	s.out("\x1b7", at(rowStatus, 1), statusSGR, pad(" "+ToASCII(s.status), Width-1), baseSGR, "\x1b8")
	if s.atText {
		s.out(sgr[s.style])
	}
}

// ProgressBar renders a fixed-width bar for the status line.
func ProgressBar(done, total, width int) string {
	if total <= 0 {
		total = 1
	}
	if done > total {
		done = total
	}
	n := done * width / total
	return "[" + strings.Repeat("#", n) + strings.Repeat(".", width-n) + "]"
}

// The input line. The terminal cursor lives here while you type.

const inputRoom = Width - 3 // prompt plus one spare column

// SetInput replaces the input line's contents.
func (s *Screen) SetInput(text string) {
	s.input = []rune(text)
	s.drawInput()
}

// Input returns what is typed so far.
func (s *Screen) Input() string { return string(s.input) }

// SetEcho turns echoing of typed characters on or off.
func (s *Screen) SetEcho(on bool) { s.echo = on }

// Type adds one character at the end of the input.
func (s *Screen) Type(r rune) {
	s.input = append(s.input, r)
	if !s.echo && s.cursorAtInput() && len(s.input) <= inputRoom {
		return // the modem already put it on screen
	}
	if len(s.input) > inputRoom || !s.cursorAtInput() {
		s.drawInput()
		return
	}
	s.out(string(r))
}

// Backspace removes the last character.
func (s *Screen) Backspace() {
	if len(s.input) == 0 {
		return
	}
	s.input = s.input[:len(s.input)-1]
	if len(s.input) >= inputRoom || !s.cursorAtInput() {
		s.drawInput()
		return
	}
	if !s.echo {
		s.out(" \b") // the modem echoed the backspace; erase the character
		return
	}
	s.out("\b \b")
}

func (s *Screen) cursorAtInput() bool { return !s.atText }

func (s *Screen) drawInput() {
	shown := s.input
	if len(shown) > inputRoom {
		shown = shown[len(shown)-inputRoom:]
	}
	s.out(at(rowInput, 1), inputSGR, "\x1b[2K", promptSGR, s.prompt, inputSGR, string(shown))
	s.atText = false
}

// FocusInput moves the cursor back to the input line for typing.
func (s *Screen) FocusInput() {
	s.flushWord()
	if s.atText {
		s.drawInput()
	}
}
