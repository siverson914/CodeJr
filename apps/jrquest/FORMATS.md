# JRQ formats: the contract

Everything in `tools/`, `js/` and later `dos/` is written against this
file. If code and this file disagree, fix one of them in the same change.
`ENGINE.md` says why; this says exactly what.

Binary formats are AGI version 2 (interpreter 2.917 behavior) as described
in the public "AGI Specifications" (Lance Ewing, Peter Kelly and others;
mirrored on the AGI wiki and inside ScummVM's and NAGI's sources). When
this file is silent, that document decides. Where we differ on purpose it
is marked **JRQ**.

All multi-byte numbers are little-endian unless marked BE.

## Layout

```
apps/jrquest/
  js/commands.js     opcode tables (done)
  js/pic.js          picture decoder and rasterizer (shared by tools and interpreter)
  js/                the interpreter (no DOM) and the browser shell
  tools/             Node command-line tools, no npm dependencies
  game/              the game's source text
  build/             tool output (not committed)
  test/
```

Plain JavaScript ES modules, Node 26, no build step, no dependencies.
Interpreter modules must not touch `window`, `document`, `fs` or `process`.

## Game source (`game/`)

| File | Becomes |
|---|---|
| `game.json` | `{ "id": "JRQ", "name": "...", "version": "0.1", "ext": 1 }` |
| `words.txt`, `words/*.txt` | `WORDS.TOK` (the files are read as one, `words.txt` first, then `words/` in name order) |
| `objects.txt` | `OBJECT` |
| `logic/NNN_name.lgc` | logic NNN |
| `pic/NNN_name.pic` | picture NNN |
| `view/NNN_name.view` | view NNN |
| `sound/NNN_name.snd` | sound NNN |
| `demo/N.txt` | `build/demo/N.txt`, copied as it is: a recorded play. See "Recorded play" |

NNN is the resource number in decimal, 0 to 255. The name is for people.

### `words.txt`

One group a line: `number: word, word, word`. Words are lowercase and may
contain spaces ("pc junior"). `#` starts a comment. Group 0 holds words
the parser drops ("a", "the", "my", "please"). Group 1 is "anyword".
Group 9999 is "rol" (rest of line). Example:

```
0: a, an, the, my, please, at, to
1: anyword
20: look, examine, read, check, l
21: take, get, grab, pick up
100: code, program, source
9999: rol
```

### `objects.txt`

Inventory items, one a line, in number order from 0: `room: name`.
Room 0 is nowhere, 255 is carried. Line 0 is always `0: ?`. An optional
first line `max: N` sets the animated object limit (default 16). `#`
at the start of a line or after a space starts a comment. Equal names
(the `?` entries) are stored once in `OBJECT`.

### `*.pic`

One command a line. `#` comments. Numbers are decimal. x is 0 to 159, y
is 0 to 167.

| Line | Meaning |
|---|---|
| `color N` / `color off` | visual pen color 0 to 15, or stop drawing on the visual layer |
| `pri N` / `pri off` | priority pen 0 to 15, or stop drawing on the priority layer |
| `line x y x y [x y ...]` | absolute line through the points (opcode F6) |
| `rel x y dx dy [dx dy ...]` | relative line, each step -7 to 7 (F7) |
| `ycorner x y y x y x ...` | corner line starting with a vertical move (F4) |
| `xcorner x y x y x y ...` | corner line starting with a horizontal move (F5) |
| `fill x y [x y ...]` | flood fill at each point (F8) |
| `pen SHAPE SIZE [splatter]` | SHAPE `circle` or `rect`, SIZE 0 to 7 (F9) |
| `plot x y [x y ...]` | pen plot (FA) |
| `arc cx cy rx ry [mask]` | **JRQ** ellipse, mask 0 to 255, default 255 (FB) |
| `pattern N [color2]` | **JRQ** fill pattern 0 to 15 and second color (FC, FD) |
| `rect x1 y1 x2 y2` | tool sugar: an outline as one `line` |
| `poly x y x y x y ...` | tool sugar: a closed outline |
| `circle cx cy r [mask]` | tool sugar: an arc that looks round on screen (rx = 0.6 r, because pixels are wide) |
| `box x1 y1 x2 y2` | tool sugar: outline plus a fill at its center |

The tool may add more sugar later as long as it compiles to the opcodes
above.

### `*.view`

```
desc "Sir Junior"            # optional
loop 0                       # loops in order from 0
cel 8 14 trans=0             # width height, transparent color (hex digit)
........
..7777..
.7FFFF7.
```

Each cel row is exactly `width` characters: a hex digit 0 to F for a
color, `.` for transparent. `loop 1 mirror 0` declares loop 1 as loop 0
flipped left to right and has no cels of its own. `trans=` may be left
out (0 then). A digit equal to the cel's transparent color is an error:
write `.`. A loop can be mirrored by one other loop only, and only loops
0 to 7 can be mirrored (the cel header has 3 bits for it). Blank lines
and comments are not allowed between the rows of a cel.

### `*.lgc`

The usual AGI logic syntax (as AGI Studio and WinAGI accept it):

```
#include "defines.h"
#define ego o0
#message 1 "The gate is shut."

if (new.room) {            // a flag name from a define tests isset
  load.pic(v0); draw.pic(v0); show.pic();
}
if (said("read", "code") && !f40) {
  print("You read all of it.");
  set(f40);
  v3 += 8;
}
if ((said("look") || said("look", "room"))) { print(m1); }
return();
```

- Arguments: `v`N, `f`N, `o`N, `i`N, `s`N, `w`N, `c`N, `m`N, or a plain
  number. Names come from `#define`.
- A quoted string where a message is expected becomes a message (same
  text reuses the same number). `said` arguments are words from
  `words.txt` in quotes, or group numbers.
- An inventory argument may be the object's name in quotes.
- Tests: `&&`, `||` (only inside one pair of parentheses, as AGI's OR
  block allows), `!`. Sugar: `v1 == 3`, `v1 != v2`, `<`, `>`, `<=`, `>=`,
  `f5` alone, `!f5`. Statements: `v1 = 3;` `v1 = v2;` `v1 += 2;`
  `v1 -= v2;` `v1 *= 2;` `v1 /= 2;` `v1++;` `v1--;`
- `else`, `goto(label);`, `label:`, `//` and `/* */` comments.
- Message text escapes: `\"`, `\n`, `\\`. The interpreter expands `%v12`,
  `%m3`, `%s1`, `%w1`, `%g4`, `%0` (object name) at print time.
- Every logic ends in `return();`. The compiler adds one if missing.

What the compiler does where the list above leaves room:

- Types are strict. `set(5)` and `set(v5)` are errors, and so is `v5`
  alone as a test (only a flag is a test by itself). The types of each
  command's arguments are in `tools/argtypes.js`.
- A `#define` gives a name to one token: `f5`, `12`, `"text"` or another
  defined name. It must come before its first use. A name is looked up
  as a define only where an argument is expected, so a define may share
  its name with a command (`new.room`). `#include` paths are relative to
  the file that has the `#include`.
- `#message` numbers are kept. Quoted strings take the lowest free
  numbers after every `#message` in the file and its includes is known.
  Using `mN` with no message N is an error. Two strings side by side
  (`"one " "two"`) are one string.
- `||` works with or without the extra parentheses, and `!` may stand
  before a parenthesized group: the compiler rewrites it (De Morgan).
  What cannot be stored is `&&` inside `||`: that is an error.
- `if` and `else` take one statement or a `{ }` block. `goto label;`
  works as well as `goto(label);`, `return;` as well as `return();`.
- An inventory name in quotes must match `objects.txt` exactly, case
  included. `said` words are lowercased first.
- `wN` compiles to the byte N as written.

The compiler also writes, per logic, `build/said/NNN.json`: the list of
distinct `said` patterns in the order they first appear. Each is
`{ "groups": [20, 100], "words": ["look", "code"] }`: the word-group
numbers, and the first word `words.txt` lists for each group (so
`"anyword"` for 1 and `"rol"` for 9999). The AI parser will use it.

### `*.snd`

Notes as text, a voice at a time. `#` starts a comment.

```
tempo 120                  # quarter notes a minute, default 120, once, first
voice 1                    # voices 1 to 3 are tones
vol 12                     # 0 silent to 15 loudest, default 15; may change mid-voice
c4/4 e4/4 g4/4. r/8 | f#4/2 bb3/2
voice noise                # the fourth voice
r/2 w1/4 p0/4
```

- A note is `pitch/length`: pitch `c d e f g a b`, then `#` or `b` for
  sharp or flat, then the octave (`a4` is 440 Hz; the chip reaches down
  to `a2`). Length is `1 2 4 8 16 32` for a whole note down to a 32nd,
  with a `.` after it for half as long again. `r/4` is a rest.
- Noise voice: `w0` to `w3` is white noise, `p0` to `p3` periodic; 0 is
  the highest of the three fixed rates and 3 takes its rate from voice 3.
- `|` is a bar line for the reader and does nothing. Notes of the same
  pitch run together: put a short rest between them to hear two.
- A voice that is not written is silent. Each voice is written once.

The tool keeps each voice's position exactly and rounds only the note
boundaries to 1/60 s, so voices never drift apart.

## Built files (`build/`)

`LOGDIR`, `PICDIR`, `VIEWDIR`, `SNDDIR`, `VOL.0`, `WORDS.TOK`, `OBJECT`
as below, plus `game.json` (copied), `said/` and `demo/` (recorded
plays, copied). `pack.js` writes all
256 entries of each directory file, and puts the resources in `VOL.0`
back to back: logics, pictures, views, sounds, each in number order.

### Directory files

256 entries of 3 bytes: `vol << 4 | offset >> 16`, `offset >> 8`,
`offset`. Three bytes of 0xFF mean "no such resource". The file may be
shorter than 256 entries.

### VOL.n

Each resource: `12 34`, vol number byte, length word, then the data.

### LOGIC

```
word   offset of the message section, counted from byte 2
...    code
byte   number of messages
word   offset of the end of the message section, from the byte after the count
word   one offset per message (0 = no such message), from the byte after the count
...    message texts, each ending in 0
```

The message texts (not the offsets) are XORed with the repeating key
`Avis Durgan`, starting at the first text byte. The key runs on through
the whole text area, ending zeros included; it does not restart for each
message. A logic with no messages still has the count byte (0) and the
end word (2).

Code bytes: `FF` starts and ends an `if` test list, and after the closing
`FF` comes a word: how many bytes to skip when the test is false. `FE` is
an unconditional jump with a signed word offset (this is `else` and
`goto`). Inside a test list `FD` negates the next test and `FC ... FC`
brackets an OR group. Test and action opcodes are in `js/commands.js`.

### PICTURE

A byte stream. Bytes F0 and above are opcodes; everything below is an
argument of the current opcode. `FF` ends the picture.

| Op | Arguments |
|---|---|
| F0 | color: set visual color, enable visual drawing |
| F1 | none: disable visual drawing |
| F2 | color: set priority color, enable priority drawing |
| F3 | none: disable priority drawing |
| F4 | x y, then y x y x ...: corner line, vertical first |
| F5 | x y, then x y x y ...: corner line, horizontal first |
| F6 | x y pairs: absolute line |
| F7 | x y, then one byte a step: bit 7 x sign, bits 6 to 4 dx, bit 3 y sign, bits 2 to 0 dy |
| F8 | x y pairs: fill |
| F9 | one byte: bit 5 splatter, bit 4 rectangle (else circle), bits 2 to 0 size |
| FA | per plot: a texture byte first if splatter is on, then x y |
| FB | **JRQ** arcs: groups of cx cy rx ry mask |
| FC | **JRQ** one byte: fill pattern 0 to 15 |
| FD | **JRQ** one byte: second color for patterns |

A picture starts with both layers disabled, pattern 0, second color 0.
The buffer is 160x168 bytes: priority in the high 4 bits, visual in the
low 4. It starts as priority 4, visual 15 (white) everywhere: byte 0x4F.

**Line (JRQ: defined in integers, so two interpreters agree exactly).**
From (x1,y1) to (x2,y2): `dx = |x2-x1|`, `dy = |y2-y1|`, `sx`, `sy` = the
signs. If `dx >= dy`: `err = dx >> 1`; plot (x,y) and repeat dx times:
`x += sx; err -= dy; if (err < 0) { y += sy; err += dx; }` then plot.
Otherwise the same with the roles of x and y swapped (`err = dy >> 1`).
Both endpoints are plotted. Real AGI data draws within a pixel of
Sierra's interpreter but not always identically; that is accepted.

**Plot.** Writing a pixel sets the low 4 bits if visual drawing is on and
the high 4 bits if priority drawing is on. Points outside 0..159, 0..167
are skipped.

**Fill.** If visual drawing is on, the fill replaces white: it spreads
over 4-connected pixels whose visual color is 15, and does nothing if the
visual color is 15 or the start pixel isn't white. If only priority
drawing is on, it spreads over pixels whose priority is 4 and does
nothing if the priority color is 4. A filled pixel is plotted (both
layers if both are on). **JRQ:** with pattern N from 1 to 15, the visual
color of each filled pixel is the pen color where the pattern bit is 1
and the second color where it is 0. Neither may be 15. Patterns are 4x4,
bit `(y & 3) * 4 + (x & 3)` of this table, bit 0 first:

```
 0: FFFF solid          8: 0F0F horizontal stripes, 1 px
 1: A5A5 checker (50%)  9: 5555 vertical stripes, 1 px
 2: A0A0 25% a         10: 8421 diagonal down
 3: A000 12%           11: 1248 diagonal up
 4: FAFA 75% a         12: F000 horizontal, 1 in 4
 5: FFFA 87%           13: 1111 vertical, 1 in 4
 6: A050 25% b         14: 8FFF brick a
 7: 5A5A checker inv   15: 9669 weave
```

**Arc (JRQ).** Midpoint ellipse centered (cx,cy) with radii rx, ry (each
0 to 119), integer math only. Each computed point (dx,dy) with dx,dy >= 0
is plotted in the octants enabled by the mask: the quadrant is chosen by
the signs, and within a quadrant the octant by whether the point came
from the "x-major" or "y-major" half of the algorithm. Bits: 0 = east to
north-east (x right, y up, steep side), then counter-clockwise: 1 =
north-east to north, 2 = north to north-west, 3 = north-west to west, 4 =
west to south-west, 5 = south-west to south, 6 = south to south-east, 7 =
south-east to east. `js/pic.js` is the reference for the exact points.
Every argument byte must be below 0xF0, so cx and cy are 0 to 239 and a
mask of 0xF0 or more is written as two or three arcs with smaller masks
(the compiler does this).

**Pen and plot (F9, FA).** As in the AGI Specifications (circle tables
and splatter bitmap). Not needed for checkpoint 2.

### VIEW

```
byte 0, byte 1    unused (1, 1)
byte              number of loops
word              offset of the description (0 = none)
word * loops      offset of each loop from the start of the view
loop:  byte count of cels, then a word per cel: offset from the loop start
cel:   byte width, byte height, byte (mirror bits << 4 | transparent color)
       rows: bytes of (color << 4 | run length), 0 ends the row
       (trailing transparent pixels may be left out)
```

Mirror byte: bit 7 set means the cel data is shared with a mirrored loop,
and bits 6 to 4 give the loop number the data is stored the right way
round for. Drawing it in any other loop flips it left to right.

### WORDS.TOK

26 BE words: the offset of the first word starting with each letter (0 =
none). Then the words in alphabetical order: a byte saying how many
leading characters are shared with the previous word, then the rest of
the characters XORed with 0x7F, the last one with bit 7 also set, then
the group number as a BE word. A zero byte ends the file.

### OBJECT

The whole file is XORed with `Avis Durgan`. Before that: a word (offset
of the first name, counted from byte 3, which is also the size of the
entry table), a byte (maximum animated objects), then 3 bytes per item:
a word offset of its name from byte 3, a byte for its starting room.
Then the names, each ending in 0.

### SOUND

Four words: the offset of each voice's notes (three tone voices, one
noise). Each note is 5 bytes: duration word in 1/60 s, two bytes that are
the PCjr sound chip's frequency bytes, one byte that is its attenuation
byte. `FF FF` ends a voice.

Byte by byte, for voice v (0 to 3) and a 10-bit tone divisor d (the tone
is 111861 / d Hz): `00dddddd` (the high 6 bits), then `1vv0dddd` (the low
4), then `1vv1aaaa` (attenuation, 0 loudest, 15 silent). The noise voice
has a 0 in the first byte and `11100wrr` in the second: w = 1 white, 0
periodic, rr the rate. A rest is a note with attenuation 15. A player
sends the second byte to the chip first (it is the latch byte), then the
first; for the noise voice it sends the second byte only, because a data
byte after a noise latch would overwrite the noise setting.

## The interpreter module (`js/`)

```js
import { Interpreter } from './agi.js';
const agi = new Interpreter(resources, host);
agi.cycle();                 // one interpreter cycle
agi.screen                   // Uint8Array(160*200), one color index a pixel
agi.keyDown(code) / agi.keyUp(code) / agi.typeChar(ch) / agi.enter()
agi.snapshot() / agi.restore(obj)   // full state as a plain object
agi.hash()                   // 32-bit FNV-1a of the state bytes below, for tests
agi.pending                  // true while the game waits on a host promise
agi.delay                    // variable 10, for the shell's timer
```

`resources` is `{ logdir, picdir, viewdir, snddir, vols: [Uint8Array],
words, object, game }` (the built files as bytes, `game` the parsed
`game.json`).

`host` is an object the shell provides; every member is optional:

| Member | Use |
|---|---|
| `random()` | integer 0 to 255. Default: a seeded generator, so tests repeat |
| `time()` | seconds since start, for the clock variables. Default: cycles / 20 |
| `sound(events)` / `stopSound()` | play a decoded sound resource |
| `resolve(text, patterns)` | the AI parser: returns a promise of a sentence or null |
| `speak(n)`, `talk(n, facts, io)` | speech clip and live talk: promises. See "Interpreter rules" |
| `link()` | 0 no link, 1 trying, 2 connected, for `link.status`. Default 0 |
| `quit()`, `shake(n)` | told when the game ends, and for `shake.screen` |
| `save(slot, obj)` / `load(slot)` | saved games |
| `log(text)` | debug |

The screen is 160x200: text row r covers screen rows 8r to 8r+7, and the
picture's row 0 is at screen row 8 times the picture's top text row (1
by default, so rows 8 to 175). Forty columns of 8-pixel characters need
320 pixels, so **text is drawn at 320x200** over the doubled picture:
`agi.screen` holds the picture and views, and `agi.text` is a
40x25 grid of `{ ch, fg, bg }` cells (bg -1 = transparent, picture shows
through) that the shell draws on top with its own 8x8 font. Message
windows are text cells with a white background and a red border drawn in
box characters, as Sierra's looked.

The 16 colors are the IBM palette: 000000 0000AA 00AA00 00AAAA AA0000
AA00AA AA5500 AAAAAA 555555 5555FF 55FF55 55FFFF FF5555 FF55FF FFFF55
FFFFFF.

### Timing

One `cycle()` is one interpreter cycle. The shell calls it 20 times a
second divided by variable 10 (the delay: 1 fastest, 2 normal). Blocking
commands (`print`, `get.string`, `pause`, `talk`) do not block the
caller: `cycle()` returns at once with the game in a waiting state, and
later calls do nothing to the game until the wait ends (Enter, a key,
a promise). The shell keeps calling `cycle()` and drawing.

## Tools (`tools/`)

Each is runnable as `node tools/NAME.js` and importable.

| Tool | Command line |
|---|---|
| `pic.js` | `pic.js in.pic out.bin [--png out.png] [--pri pri.png] [--lint]` |
| `view.js` | `view.js in.view out.bin [--png sheet.png]` |
| `logic.js` | `logic.js in.lgc out.bin --words game/words.txt --objects game/objects.txt [--said out.json] [--list]` (`--list` prints the disassembly) |
| `words.js` | `words.js game/words.txt build/WORDS.TOK` |
| `objects.js` | `objects.js game/objects.txt build/OBJECT` |
| `snd.js` | `snd.js in.snd out.bin` |
| `pack.js` | `pack.js game build`: runs all of the above over `game/`, writes `build/`. `--html out.html` also writes one self-contained page (interpreter, shell and resources inlined) |
| `play.js` | `play.js build --script test/x.txt --png-dir out [--every N]`: headless run |
| `png.js` | library: `writePng(path, width, height, indexedPixels, palette)` |
| `lint.js` | `lint.js a.pic [b.pic ...] [--pri-fill 45] [--vis-fill 60] [--white 3]`: checks pictures (below). Exits 1 on an error, 0 on warnings alone. `pic.js --lint` runs the same checks on its one file |
| `mkwalk.mjs` | `mkwalk.mjs build test/x.src test/x.txt [--demo game/demo/1.txt] [--pace "12 2 1 4"] [--png-dir out]`: plays a source script with macros (`goto x y`, `exit left`, `say text`, `dismiss`, `waitbox n`, `pace`, `slowtype`, `zuckchat`) against the built game and writes the plain `play.js` script it turned into. `--demo` also writes the copy without `shot`, `expect` and `hash` lines |
| `serve.js` | `serve.js [--port 4861] [--build build] [--host 127.0.0.1] [--cache file.json]`: the game in a browser with the AI parser and the live talk. See "Local server" |
| `review.js` | `review.js build build/review.html [--game game]`: one self-contained page for reviewing the whole game. See "The review page" |

`play.js` script lines: `N` cycles to run, `key up|down|left|right|enter|esc|f1..`,
`type some text` (types it and presses Enter), `char x` (types it, no Enter), `shot name` (writes a
PNG), `hash` (prints the state hash), `expect text` (fails unless the
text is on screen).

### Recorded play

"Let the AI play" on the title screen is `demo.play(1)`: the host
restarts the game and plays `build/demo/1.txt`, which `pack.js` copies
from `game/demo/1.txt` (and `bundle.js` inlines as `demo/1.txt`). The
file is a `play.js` script, so any interpreter that can run one can play
it; the 8088 interpreter will read the same file.

- **Where it comes from.** `game/demo/1.txt` is generated, not written:
  `node tools/mkwalk.mjs build test/showcase.src test/showcase.txt --demo
  game/demo/1.txt`. `test/showcase.txt` is the same script with its
  `shot`, `expect` and `hash` lines, and the tests fail if the two differ
  in anything else.
- **Lines a demo may contain.** `N` (run N cycles), `key NAME` (`up down
  left right home end pgup pgdn center enter esc tab backspace space f1`
  to `f10`), `char TEXT` (type it, no Enter; leading and trailing spaces
  are dropped, so a space is `key space`), `type TEXT` (type it and press
  Enter), `# comment`, blank. Nothing else: `pack.js` refuses a demo with
  any other line. Plain ASCII, one line each, `\n` ends.
- **How it is played.** Lines are done in order. `key`, `char` and `type`
  happen at once, before the next cycle. `N` calls the interpreter's
  cycle N times, at the game's own rate (20 a second divided by v10).
  A call made while a message box, the inventory or a `get.string` is up
  counts as one of the N and passes the same time, but does nothing: the interpreter's cycle counter, clock, random
  generator, animation and sound flags all stand still until the wait
  ends. That is how the script holds a box on screen long enough to read.
  (The title menu and the death screen poll `have.key()`, so there the
  game does keep cycling; the counts cover that too.)
- **It always starts from power-on.** A new interpreter, every variable
  and flag clear, the default seed (0x1234) for the random generator, the
  clock counted from cycles, and a host with no `random`, `time`,
  `resolve`, `talk`, `link`, `save`, `load` or `demo`. With those the
  same file gives the same game on every machine, to the last pixel (the
  state hash at the end is the test). Change anything in the game that
  moves a person, a timer or a random roll and the demo must be made
  again: `mkwalk.mjs` walks by looking, the finished file does not.
- **In the browser.** `js/demo.js` (no DOM) parses the file and feeds one
  cycle at a time; `js/shell.js` shows "The AI is playing. Press Esc to
  stop." under the picture, hides the on-screen keys, and ignores the
  keyboard except Esc (or a tap on the picture), which starts an
  ordinary game. When the script runs out the last screen stays up with
  "The AI has finished. Press Esc to play it yourself."
- **Pacing** is in the file, as cycle counts: `pace BASE PER GAP BEAT` in
  the source makes `mkwalk.mjs` hold each message box BASE cycles plus
  one for every PER characters in it, type commands one character every
  GAP cycles, and wait BEAT cycles before typing.

`lint.js` reports `file:line: error|warning: text`. The line comes from
`compilePicMap(text)` in `pic.js`, which returns the same bytes as
`compilePic` plus `lines[i]`, the 1-based source line of byte i.
Errors: a fill that changes more than 45% of the priority layer or 60%
of the visual layer (a leak; the message has the point and the
percent), a fill that changes nothing (with the reason), a point off
the picture, anything the compiler refuses. Warnings: white left on the
visual layer in a region of more than 3 pixels (its bounding box), and
a priority 2 trigger line that is one row thick for 3 or more pixels
(the hero moves 2 rows a step).

## Local server

`node tools/serve.js` serves the unbundled game at
`http://127.0.0.1:4861/` and answers the two host calls that need a
model. It listens on 127.0.0.1 unless `--host` says otherwise.

| Request | Answer |
|---|---|
| `GET /`, `GET /NAME.js` | `js/index.html` and the `js/` modules |
| `GET /build/NAME` | the built files. Nothing else on disk is served |
| `GET /api/ping` | `{ "ok": true, "server": "jrq", "resolve": true, "talk": true, "feedback": true }` |
| `POST /api/resolve` | body `{ "text": "peek out the window", "patterns": [{ "groups": [20, 101], "text": "look window" }] }` (the patterns as `host.resolve` gets them; plain strings also work). Answer `{ "sentence": "look window" }` or `{ "sentence": null }`, with `"cached": true` when it came from the saved answers and `"error": "timeout"` (or another reason) when the model could not be asked |
| `POST /api/talk` | body `{ "prompt": 1, "facts": { ... }, "history": [{ "role": "player" or "mirror", "text": "..." }], "text": "the player's line" }`. Answer `{ "reply": "..." }`, or status 502 with `{ "reply": null, "error": "..." }` |

Both POSTs need `content-type: application/json` and, if the browser
sends an `Origin`, it must be the server's own address: each call runs a
model on the laptop owner's account.

**Resolve.** The model is asked which one of the sentences the player
meant, or NONE. Its answer counts only if it is one of the sentences
word for word (case and punctuation aside). Where a sentence has
`anyword` or `rol`, the answer may put words there, but only words the
player typed. A pattern made only of `anyword` and `rol` is left out.
Anything else is `null`. Answers, `null` included, are saved in
`.cache/resolve.json` beside the build folder (or `--cache`), keyed by
the cleaned text and a hash of the sentence list, and looked up before
the model is asked. Failures and timeouts are not saved. Limit: 10 s.

**Talk.** `prompt` is the number from `talk(n)`. The text is
`prompts.json` at the top of `apps/jrquest/` (`{ "1": "...", "facts": {
... } }`), which only the server reads. `{name}` in a prompt is looked
up in `"facts"`: `"score"`, `"maxScore"`, `"aiHelps"`, `"inventory"`,
`"minutes"`, `"room"` are members of the facts object; `"v41"` is a
variable, `"v35+1980"` a variable plus a number, `"s3"` a string, and
`{ "flag": 73, "yes": "...", "no": "..." }` one of two texts. A name
that is not there reads "unknown". The reply is one line of printable
ASCII: quotes, dashes and accents are replaced, the rest is dropped.
Limit: 45 s.

**The model** is `claude -p` (Claude Code, headless) with no tools,
started in an empty temporary folder: `--restricted --strict-mcp-config
--tools "" --permission-prompts none --no-session-persistence
--disable-slash-commands --output-format json --system-prompt TEXT`,
plus `--model haiku` for resolve. No API key is used.

**The page.** `shell.js` asks `GET /api/ping` (not from a file or the
one-page build, which make no requests). If the answer is the one
above it sets `host.resolve`, `host.talk` and `host.link` (2, or 0
after a request that could not be sent) from `js/remote.js`. `talk`
loops: `io.ask`, POST with the history so far, `io.print` the reply a
window at a time, until an empty line, Esc, `bye` or `goodbye`. It
adds `minutes` (since the page loaded) to the facts, and gives `false`
when the server cannot answer. The on-screen keys get a Mic button
when the browser has `SpeechRecognition`; what it hears is sent as a
typed line.

## The review page

`node tools/review.js build build/review.html` writes one HTML file with
everything inlined (pictures as PNG data URIs), for the owner to read
the game and comment on it. It reads the built files for pictures,
views, words and screenshots, `game/logic/*.lgc` for the text,
`GAMEPLAN.md` for the map, exits and briefs, and `DESIGN.md` for the
puzzle chain table. If a document cannot be read the page says so.

- **Overview:** the map as thumbnails, the puzzle chain, totals.
- **Each room** (the title, then 1 to 16): a screenshot taken by playing
  headless (`debug` at the date prompt, `tp` N, greeting boxes closed),
  with the bare picture and the priority layer behind a toggle; its
  exits and brief; its sprites (views N*10+20 to N*10+29 and any the
  logic loads); and its script.
- **The script** is every `print`, `print.at`, `display`, `set.string`
  and input prompt in source order, each under the conditions it sits
  in, read from the `.lgc` source: `said("look","bed")` is LOOK BED,
  `isset(gateOpen)` is "when gateOpen", an `else` is "otherwise" (with
  the opposite spelled out when the condition is one test), the init
  block is "on entering". `// ---- text ----` comments become headings.
  `score += n` shows as points; `get`, `drop`, token costs and deaths
  show as notes. Text reached by `goto` is listed at its label with
  where the jumps come from.
- **Shared text:** logics 0, 80, 83, 84, 90, 99 and 104, the same way.
- **All sprites, the vocabulary, a search box** over every line.

**Comments.** Every room, screenshot, sprite, puzzle and line has a
comment button. A comment is `{ id, target, kind, group, order, label,
quote, trigger, text, time, done, updated }`. Targets stay the same
across rebuilds: `L2:1a2b3c4d` (logic 2, the first 8 hex digits of the
SHA-1 of the line's source text), `room:2`, `shot:2`, `view:12`,
`puzzle:5`, `logic:104`, `game`. `quote` and `trigger` are kept with
the comment so a line can be found after it moves.

Served by `tools/serve.js` at `/admin`, the page keeps comments on the
server:

| Request | Answer |
|---|---|
| `GET /admin` | `build/review.html` |
| `GET /api/feedback` | `{ "comments": [ ... ] }` |
| `POST /api/feedback` | body: a comment. A new id adds it (needs `target` and `text`); a known id changes the members sent (`{ "id": "...", "done": true }`). Answer `{ "comment": { ... } }`, or 400 `{ "error": "..." }` with nothing written |
| `DELETE /api/feedback/ID` | `{ "ok": true }`, or 404 |

Same guards as the other calls (JSON content type, local Host, own
Origin). The text is at most 4000 characters, the quote 2000, and there
are at most 2000 comments. They are stored in
`apps/jrquest/review/feedback.json` (`{ "version": 1, "comments": [] }`,
written whole to a temporary file and renamed), and every change also
writes `review/feedback.md`, the same comments as a report grouped by
room. A `feedback.json` that cannot be parsed is never overwritten: the
call fails with 500.

Opened from a file the page makes no requests and keeps comments in
`localStorage`, saying so at the top; "Copy all as text" and "Download
JSON" get them out. Served, it offers to upload any comments it finds
in `localStorage` that the server lacks.

## JRQ extension actions

Opcodes 200 and up. Plain AGI data never contains them.

| Op | Command | What it does |
|---|---|---|
| 200 | `speak(n)` | play speech clip n and wait. No clip or no host: nothing |
| 201 | `talk(n)` | live conversation with role prompt n |
| 202 | `link.status(v)` | v = 0 no link, 1 trying, 2 connected |
| 203 | `score.event(n)` | note event n for the ending's summary |
| 205 | `demo.play(n)` | ask the host to restart the game and play recorded script n (`host.demo(n)`). With no such host member, sets flag 27 |
| 204 | `str.date(s, v)` | parse string s as a date the way the DOS `DATE` command does, and set four variables starting at v: result, month, day, years since 1980 |

`str.date`: the string must be `m-d-y` or `m/d/y` (also `.`), each part
decimal digits, month 1 to 12, day 1 to 31 (no per-month check). A year
of 1980 to 2099 is accepted; a two-digit year is accepted only from 80
to 99 (meaning 1980 to 1999), which is why "26" is an invalid date on
real DOS. Result 1 = valid (the other three variables are set), 0 =
invalid (they are left alone). Leading and trailing spaces are ignored.

## Interpreter rules (JRQ)

What `js/agi.js` does where the AGI Specifications are silent, vague or
need floating point. The 8088 interpreter must do the same.

**Modules.** `agi.js` (the class, the cycle, input, waits, snapshot),
`ops.js` (logic executor, tests, actions), `anim.js` (objects),
`screen.js` (text grid, windows, %-codes), `parser.js`, `resources.js`
(decoders), `font.js`, `render.js` (screen + text grid to a 320x200
frame), `util.js`, and `shell.js` + `index.html` for the browser.

**Keys.** `keyDown(code)` takes ASCII (13 Enter, 27 Esc, 8 Backspace, 9
Tab) or scan code << 8 for the rest (arrows 0x4800 up, 0x5000 down,
0x4B00 left, 0x4D00 right, F1 to F10 0x3B00 to 0x4400): the same number
`set.key` builds from its two bytes. `agi.js` exports them as `KEY`.
Printable characters go through `typeChar`. `keyUp` only matters after
`hold.key`.

**Input never waits for a cycle, and is never dropped.** A typed
character, Backspace and Enter change the input line (or the
`get.string` field) inside the call, so the shell can draw at once.
Enter on a line puts the line in a queue and clears the input line; the
cycle takes one queued line a cycle. While a message window is up,
typing still goes to the input line and Enter or Esc closes the window.
With `prevent.input` (or on a text screen) typed characters are only
keys for `have.key`. `have.key` is true if a key arrived this cycle that
no dialog or wait used up.

**Text cells.** `agi.text[row][col]` is `{ ch, fg, bg }` with `ch` a
code page 437 character code (box characters 179, 191, 192, 196, 217,
218). Over a picture `display` writes black on white when the
background attribute is not 0, else the foreground on black; on a text
screen both colors are as given. `clear.lines` is black, or white over
a picture when its color is not 0. `show.pic` makes the cells over the
picture transparent again.

**Message windows.** Text is wrapped to the width (30 unless `print.at`
gives one, 38 at most): a line breaks at the last space at or before the
limit, a longer word is cut at the limit, `\n` starts a new line, and
more than 19 lines are cut at 19. The text starts at row
`((20 - lines - 1) >> 1) + 1` and column `(40 - width) >> 1`, with the
border one cell outside it. Enter or Esc closes it. If variable 21 is
not 0 the window also closes by itself after `v21 * 10` cycles and v21
is set to 0. With flag 15 set `print` does not wait.

**Clock.** Variables 11 to 14 count seconds, minutes, hours, days from
`host.time()`; the default is `cycles / 20` rounded down, whatever
variable 10 says.

**Random.** `host.random()`, or `seed = (seed * 25173 + 13849) & 0xFFFF`
starting from 0x1234, giving `seed >> 8`. `random(a, b, v)` is
`a + r % (b - a + 1)`. `wander` picks direction `r % 9` for
`6 + r % 45` steps (or until blocked). A blocked `follow.ego` picks
direction `1 + r % 8` for `step + r % (d - step + 1)` steps, `d` being
half the distance, or `step` steps if `d < step`.

**Objects.** The table has (the OBJECT file's byte) + 1 objects. Loop
from direction is automatic for views with 2 or 3 loops (right 0, left
1) and with exactly 4 loops (right 0, left 1, down 2, up 3), as in
2.917; views with more are left alone. A fixed-priority object sorts
at y `base + trunc((p - 5) * (168 - base) / 10)`, base 48 unless
`set.pri.base` changed it; the band of row y is 4 below the base, else
`5 + ((y - base) * 10 / (168 - base) | 0)`, at most 15. `fix position`
spirals left, down, right, up with legs growing by one, and gives up
(object left where it was) once a leg passes 200. A bad loop or cel
number is taken as 0. `div.n` and `div.v` by zero give 0. `get.num`
keeps the low 8 bits. Flag 1 is set when no pixel of ego was drawn.

**Sound.** `host.sound(events)` gets `{ t, voice, dur, div, att }`
sorted by `t` (1/60 s; `div` is the chip's 10-bit divisor, tone =
111860 / div Hz; for voice 3 it is the 3-bit noise control; `att` 0 is
loud, 15 silent). The done flag does not come from the host: every
cycle takes `3 * v10` sixtieths off the sound's length, and the flag is
set when none is left, sound on or off.

**Saved games.** `save.game` and `restore.game` ask for a slot (keys 1
to 9, Esc cancels) and call `host.save(slot, obj)` (false = failed) and
`host.load(slot)` (null = none); either may return a promise. A restored
game starts at the top of a cycle with flag 12 set for that cycle.

**The AI parser.** With `host.resolve`, a typed line is a miss when it
has a word that is not in WORDS.TOK, or when all its words are known but
no `said` pattern in the loaded logics (logic 0, the room, anything
from `load.logics`) could accept them. A miss is not shown to the
logics. The host is called with the text and the patterns as
`[{ groups: [20, 21], text: "examine door" }]` (the alphabetically first
word of each group; 1 is "anyword", 9999 "rol"). The game stands still
while it waits, the row under the input line shows a spinner and
`game.json`'s `"thinking"` text (default "Thinking..."), and typing
stays live. A string answer is parsed as if typed; null (or a failure)
sends the original line. Either way the line is not asked about again.
Without `host.resolve` every line goes straight to the logics.

**speak, talk, link.status, score.event.** `speak(n)` waits on
`host.speak(n)`; no member, no wait. `talk(n)` with no `host.talk` sets
flag 29 ("no link") and goes on. Otherwise it waits on
`host.talk(n, facts, io)` and then sets flag 28 ("talk ended"), or 29
if the promise fails or gives `false`. `facts` is `{ n, prompt (message
n of the logic, or null), score, maxScore, room, events, aiHelps,
inventory, vars, flags, strings }`. `io.print(text)` shows a message
window and resolves when it is closed; `io.ask(prompt)` reads a line on
the input row and resolves with it (null on Esc). `score.event(n)`
notes event n once, in order (`facts.events`); if `game.json` has
`"events": { "n": points }` the points are added to variable 3 that
first time.

**snapshot, restore.** A plain JSON-safe object: variables, flags, the
object table and its save-unders, the picture buffer, the screen, the
text grid (run-length coded, base64), the logic call stack and any
wait. A wait on a host promise cannot be saved: after `restore` it ends
as if the host had answered nothing.

**hash.** FNV-1a (offset 0x811C9DC5, prime 0x01000193) over these
bytes, in order: variables (256), flags (256, each 0 or 1), inventory
rooms (256), strings 0 to 23 (40 bytes each, zero padded), every object
(33 fields as 16-bit little-endian, in the order of `OBJ_FIELDS` in
`anim.js`), the picture buffer (26880), the screen (32000), the text
grid (3 bytes a cell: character, foreground, background + 1), then
horizon, player control, block on, block x1 y1 x2 y2, graphics mode,
picture shown, picture top row, input row, status row, status on, input
on, text foreground, text background, cursor character, the input line
(40 bytes, zero padded), the wait type (0 none, 1 window, 2 any key, 3
text field, 4 inventory, 5 menu, 6 slot, 7 host) and the random seed
(2 bytes). With `host.time` set, variables 11 to 14 are hashed as 0 so
a wall clock cannot reach the hash.

**Steps larger than one pixel (JRQ).** An object with step size n moves
one pixel at a time, n times, testing control lines, blocks and other
objects at each pixel, and stops at the last allowed one. A trigger line
crossed on the way sets flag 3 for that cycle. An edge reached on the
way ends the step there. Sierra's interpreter tested only the landing
spot. If the very first pixel is refused the object stays put, as in
AGI.
