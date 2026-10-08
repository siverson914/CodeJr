package session

import (
	"bytes"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"strconv"
	"strings"
)

// Scripted keys and screenshots for a program run on the real PCjr (see
// tick_hook in client/codejr.c).

// A key script is plain text: ordinary characters are keys, and braces
// hold a pause in seconds or a named key:
//
//	{2}q            wait 2 seconds, press q
//	{1.5}{SPACE}{3}{ESC}
//	abc{ENTER}
//
// Names: ENTER ESC SPACE TAB BKSP UP DOWN LEFT RIGHT HOME END PGUP PGDN
// INS DEL F1 to F10.

type scriptKey struct{ delay, scan, ascii byte }

// XT (set 1) scan codes, which is what the PCjr's BIOS hands to INT 9.
var charScan = map[rune]byte{
	'1': 2, '2': 3, '3': 4, '4': 5, '5': 6, '6': 7, '7': 8, '8': 9, '9': 10, '0': 11, '-': 12, '=': 13,
	'q': 16, 'w': 17, 'e': 18, 'r': 19, 't': 20, 'y': 21, 'u': 22, 'i': 23, 'o': 24, 'p': 25, '[': 26, ']': 27,
	'a': 30, 's': 31, 'd': 32, 'f': 33, 'g': 34, 'h': 35, 'j': 36, 'k': 37, 'l': 38, ';': 39, '\'': 40, '`': 41,
	'\\': 43, 'z': 44, 'x': 45, 'c': 46, 'v': 47, 'b': 48, 'n': 49, 'm': 50, ',': 51, '.': 52, '/': 53, ' ': 57,
}

var namedKey = map[string][2]byte{ // scan, ASCII
	"ENTER": {28, 13}, "ESC": {1, 27}, "SPACE": {57, 32}, "TAB": {15, 9}, "BKSP": {14, 8},
	"UP": {72, 0}, "DOWN": {80, 0}, "LEFT": {75, 0}, "RIGHT": {77, 0},
	"HOME": {71, 0}, "END": {79, 0}, "PGUP": {73, 0}, "PGDN": {81, 0}, "INS": {82, 0}, "DEL": {83, 0},
	"F1": {59, 0}, "F2": {60, 0}, "F3": {61, 0}, "F4": {62, 0}, "F5": {63, 0},
	"F6": {64, 0}, "F7": {65, 0}, "F8": {66, 0}, "F9": {67, 0}, "F10": {68, 0},
}

// maxKeys is what one request can carry (and the client's table holds).
const maxKeys = 30

// parseKeys turns a script into the client's form: for each key, the
// pause before it in units of 2 timer ticks (about 0.11 s), its scan code
// and its ASCII code. A pause too long for one byte becomes a key with
// scan code 0, which only waits.
func parseKeys(script string) ([]scriptKey, error) {
	var out []scriptKey
	wait := 0.0 // seconds owed before the next key
	emit := func(scan, ascii byte) {
		units := int(math.Round(wait * 18.2 / 2))
		for units > 255 {
			out = append(out, scriptKey{255, 0, 0})
			units -= 255
		}
		out = append(out, scriptKey{byte(units), scan, ascii})
		wait = 0
	}
	rs := []rune(script)
	for i := 0; i < len(rs); i++ {
		c := rs[i]
		if c != '{' {
			lower := c
			if c >= 'A' && c <= 'Z' {
				lower = c + 32
			}
			scan, ok := charScan[lower]
			if !ok || c > 126 {
				return nil, fmt.Errorf("no key for %q in the key script", string(c))
			}
			emit(scan, byte(c))
			continue
		}
		end := i + 1
		for end < len(rs) && rs[end] != '}' {
			end++
		}
		if end == len(rs) {
			return nil, errors.New("a { in the key script has no }")
		}
		word := strings.ToUpper(string(rs[i+1 : end]))
		i = end
		if k, ok := namedKey[word]; ok {
			emit(k[0], k[1])
		} else if secs, err := strconv.ParseFloat(word, 64); err == nil && secs >= 0 && secs <= 120 {
			wait += secs
		} else {
			return nil, fmt.Errorf("{%s} in the key script is neither a pause in seconds nor a key name", word)
		}
	}
	if len(out) > maxKeys {
		return nil, fmt.Errorf("the key script has %d keys and pauses; %d is the most", len(out), maxKeys)
	}
	return out, nil
}

// parseShot reads the screenshot option: "" none, "last" the latest one
// before the program ended (taken every second), or seconds after start.
func parseShot(s string) (byte, error) {
	s = strings.ToLower(strings.TrimSpace(s))
	switch s {
	case "", "0", "no", "none":
		return 0, nil
	case "last", "end":
		return 255, nil
	}
	n, err := strconv.Atoi(s)
	if err != nil || n < 1 || n > 250 {
		return 0, errors.New(`screenshot: give seconds after the start (1 to 250) or "last"`)
	}
	return byte(n), nil
}

// The PCjr's 16 colors, in the order of its palette registers at reset.
var rgbi = [16]color.RGBA{
	{0, 0, 0, 255}, {0, 0, 170, 255}, {0, 170, 0, 255}, {0, 170, 170, 255},
	{170, 0, 0, 255}, {170, 0, 170, 255}, {170, 85, 0, 255}, {170, 170, 170, 255},
	{85, 85, 85, 255}, {85, 85, 255, 255}, {85, 255, 85, 255}, {85, 255, 255, 255},
	{255, 85, 85, 255}, {255, 85, 255, 255}, {255, 255, 85, 255}, {255, 255, 255, 255},
}

// DecodeShot turns the client's screenshot ("JS", mode, page byte, length,
// then the video memory) into a 640x400 PNG. Colors are the standard 16:
// the PCjr's palette registers can't be read back, so a program that
// changed them shows in the wrong colors.
func DecodeShot(b []byte) ([]byte, string, error) {
	if len(b) < 6 || b[0] != 'J' || b[1] != 'S' {
		return nil, "", errors.New("not a screenshot")
	}
	mode, mem := b[2], b[6:]
	if n := int(b[4]) | int(b[5])<<8; n < len(mem) {
		mem = mem[:n]
	}
	at := func(off int) byte {
		if off < len(mem) {
			return mem[off]
		}
		return 0
	}
	var w, h int
	var pixel func(x, y int) byte
	var what string
	switch mode {
	case 8: // 160x200, 16 colors: 2 banks of rows, 4 bits a pixel
		w, h, what = 160, 200, "160x200, 16 colors"
		pixel = func(x, y int) byte {
			v := at((y&1)*0x2000 + (y>>1)*80 + x/2)
			if x&1 == 0 {
				return v >> 4
			}
			return v & 15
		}
	case 9: // 320x200, 16 colors: 4 banks
		w, h, what = 320, 200, "320x200, 16 colors"
		pixel = func(x, y int) byte {
			v := at((y&3)*0x2000 + (y>>2)*160 + x/2)
			if x&1 == 0 {
				return v >> 4
			}
			return v & 15
		}
	case 4, 5: // 320x200, 4 colors: 2 banks, 2 bits a pixel
		w, h, what = 320, 200, "320x200, 4 colors"
		pal := [4]byte{0, 11, 13, 15}
		pixel = func(x, y int) byte {
			v := at((y&1)*0x2000 + (y>>1)*80 + x/4)
			return pal[v>>(6-2*uint(x&3))&3]
		}
	case 6: // 640x200, 2 colors
		w, h, what = 640, 200, "640x200, 2 colors"
		pixel = func(x, y int) byte {
			if at((y&1)*0x2000+(y>>1)*80+x/8)>>(7-uint(x&7))&1 != 0 {
				return 15
			}
			return 0
		}
	case 10: // 640x200, 4 colors: 4 banks
		w, h, what = 640, 200, "640x200, 4 colors"
		pal := [4]byte{0, 11, 13, 15}
		pixel = func(x, y int) byte {
			v := at((y&3)*0x2000 + (y>>2)*160 + x/4)
			return pal[v>>(6-2*uint(x&3))&3]
		}
	default:
		return nil, "", fmt.Errorf("no decoder for video mode %d", mode)
	}
	img := image.NewRGBA(image.Rect(0, 0, 640, 400))
	for y := 0; y < 400; y++ {
		for x := 0; x < 640; x++ {
			img.SetRGBA(x, y, rgbi[pixel(x*w/640, y*h/400)&15])
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		return nil, "", err
	}
	return buf.Bytes(), fmt.Sprintf("mode %d (%s)", mode, what), nil
}
