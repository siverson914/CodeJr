# JRQ: the engine

Checkpoint 1 draft, 2026-10-05. Nothing is built yet. The game itself (map,
puzzles, script, facts) is in `DESIGN.md`. This file is the engine: what it
is, what we add to AGI, how it is built and tested.

## The idea in one paragraph

A game is only data. One set of resource files plays on two interpreters:
a JavaScript one (browser and Node, written first, the reference) and an
8088 one (the PCjr, written second, must match the first frame for frame).
The data format is Sierra's AGI version 2, as documented by fans, with a
few opcodes added in slots Sierra left empty. We write every line of both
interpreters ourselves from the public format documents. No Sierra code or
data goes in the repo.

## What we keep from AGI v2, unchanged

| Thing | AGI v2 | Notes |
|---|---|---|
| Screen | 160x200 picture, 16 colors, double-wide pixels, with 40-column text over it | So the PCjr runs in 320x200 (BIOS mode 9, 32 KB of video memory) and doubles the picture's pixels, as Sierra did. Mode 8 would give 20-column text |
| Layout | status line, 160x168 picture, 4 text lines | 40x25 text grid over it |
| Pictures | a list of drawing commands, 1 to 3 KB a room | two layers: what you see, and priority |
| Priority | bands 4 to 15 set who is in front | 0 block, 1 conditional block, 2 trigger, 3 water |
| Views | sprites: loops of cels, run-length coded, one clear color | Sir Junior walks in 4 directions |
| Logic | bytecode: `if (tests) { actions }`, 256 byte variables, 256 flags | logic 0 runs every cycle, plus the room's |
| Objects | 16 or more on screen, each with position, loop, cel, priority, motion | wander, follow, move to, step size, cycle time |
| Words | `WORDS.TOK`: words in groups, synonyms share a number | `said()` tests match group numbers |
| Inventory | `OBJECT` file | room number 255 means "carried" |
| Sound | 3 tone voices and noise, as note lists | the format is the PCjr's sound chip, as is |
| Files | `LOGDIR`, `PICDIR`, `VIEWDIR`, `SNDDIR`, `VOL.0` | uncompressed, as in v2 |
| Save and restore | the whole state to a file | on both platforms |

Why keep it: it was designed for this exact machine, it is small, and it
is written down in detail, so nobody has to guess how a command behaves.
A real AGI game's data (Steve's own King's Quest disk, if he has it as
files) is then a free test for the interpreters. That is a test only: no
Sierra data in the repo, and none shipped.

## What we add

Old data never uses these, so real AGI data still runs. Our game says in
its header that it needs them.

### Picture opcodes (AGI uses 0xF0 to 0xFA and 0xFF; 0xFB to 0xFE are free)

| Opcode | Name | Arguments | What it does |
|---|---|---|---|
| 0xFB | arc | cx, cy, rx, ry, octant mask | Ellipse or any part of one, by the midpoint method: adds, subtracts and shifts only, no multiply in the loop. A full circle is mask 0xFF. Draws on both layers like a line does |
| 0xFC | fill pattern | pattern number | Sets how the next fills paint: 0 solid (AGI's), 1 to 15 a two-color dither from a fixed table (checker, 25%, 75%, stripes, brick, and so on). The second color comes from 0xFD |
| 0xFD | second color | color | The dither's other color |
| 0xFE | reserved | | Must not appear. Kept for a later need |

Dithered fills are what Sierra added in its next engine to get past 16
flat colors. On a PCjr composite monitor or TV they blend into new
shades; on an RGB monitor they read as texture. Fill boundaries work on
the solid outline as before, so a dithered area can still be filled
against.

Arcs are also allowed on the priority layer, so a round hill hides Sir
Junior correctly.

### Logic commands (numbers above AGI's last, starting at 200)

| Command | What it does |
|---|---|
| `speak(n)` | Plays speech clip n and waits. In the browser, an audio file; on the PCjr, a SAY clip from the hard disk. With no clip, it does nothing, so the text box is always the real line |
| `talk(n)` | Opens a live conversation using role prompt n (the Mirror). Sets a flag when it ends. With no link, sets a different flag at once, so logic can play the written ending |
| `link.status(v)` | Puts 0 (no link), 1 (trying) or 2 (connected) in a variable, for the status line and for jokes |
| `score.event(n)` | One place to add points and note which things were done, for the ending's summary to the Mirror |

That is the whole extension. Everything else in the game is plain AGI.

### The parser, with an AI behind it

Puzzles are deterministic. The AI never decides what happens. It only
turns a sentence the game didn't understand into one it does.

1. The player types a line. The classic parser looks each word up in
   `WORDS.TOK`. If every word is known and some `said()` test matches,
   that is the end of it: instant, no AI.
2. On a miss (an unknown word, or nothing matched), the interpreter asks
   its host to resolve the line. It sends the typed text and the list of
   sentences that could match right now. The logic compiler makes that
   list for each room from the `said()` tests, so the question is closed:
   "which of these 40 sentences did the player mean, or none?"
3. The host answers with one sentence from the list, or none. The
   interpreter runs it through the classic parser as if typed. "None"
   gives the usual "I don't understand".
4. Answers are saved (typed text, room, answer) so the same phrase is
   instant next time. The saved list ships with the game, so common
   phrases never need the AI at all.

The host is the only part that differs:

| Where | Host |
|---|---|
| Browser | a small server function that calls a fast model |
| PCjr | the game dials the CodeJr server (the address in `CODEJR.CFG`) with the client's own link code and sends a `Resolve` message |
| No link | none: the classic parser alone. The game is fully playable |

While it waits, the input line shows a spinner and the line "Thinking.
People watch a little spinner." (Demo 2's joke, used as the real status.)
Typing stays live, as the project rules ask.

Voice input is browser only: the browser's speech recognition produces
text, and the text goes down the same path.

### The live ending

`talk()` sends the host a role prompt, the game's facts (score, what the
player did, how they died, how many times the AI parser had to help), and
then each line the player types. The reply comes back as text for the
game's message box, wrapped to 40 columns, a page at a time. The role
prompt is in `DESIGN.md`. On the PCjr this is the same CodeJr server and
the same `claude -p` that runs the rest of the project, with a different
prompt and no tools.

## Tools (laptop, plain JavaScript on Node, no build step)

Everything the game is made of is a text file an agent can write and a
person can read. The tools turn text into AGI resources.

| Tool | Input | Output |
|---|---|---|
| `pic` | `room.pic`: one drawing command a line (`color 2`, `line 0 100 159 100`, `arc 80 40 30 12`, `dither 3 2 10`, `fill 20 150`), with a `pri` section | PICTURE resource, plus a PNG of each layer for review |
| `view` | `jr.view`: cels as text grids, one hex digit a pixel | VIEW resource, plus a contact sheet PNG |
| `logic` | `room.lgc`: AGI's C-like source (`if (said("take","key")) { get("key"); }`), with names for variables and flags | LOGIC resource, plus the room's list of accepted sentences |
| `words` | `words.txt`: one group a line, synonyms together | `WORDS.TOK` |
| `snd` | `tune.snd`: notes as text, three voices and noise | SOUND resource |
| `pack` | all of the above | `VOL.0` and the four directory files |
| `play` | a script of keys and typed lines | runs the JS interpreter headless, writes PNG frames and a state hash per cycle |
| `walk` | the whole game | plays the full solution from a script and fails the build if the game can't be won, or if any room has a sentence nothing answers |

`pic` has a lint pass: every fill must be closed (a leak that floods the
screen fails the build), and every room must have a walkable path between
its exits.

## The two interpreters

### JavaScript (first)

One module with no DOM in it: resources in, a 160x200 byte buffer and a
sound event list out, one `cycle()` at a time. A thin browser shell draws
the buffer to a canvas (scaled, with an optional composite-monitor look),
plays sound with WebAudio square waves, and takes keys. The same module
runs headless in Node for tests.

### 8088 (second)

C with Open Watcom (8088, large model, `-0 -wx`), assembly for the hot
loops, linked with JRGFX's fast path.

| Part | Plan |
|---|---|
| Picture | drawn into a 160x168 buffer, one byte a pixel: priority in the high half, color in the low half, as Sierra did. 26,880 bytes |
| Hot loops in assembly | line, arc, fill, cel draw with priority test, buffer to screen |
| Screen | mode 9 (320x200). JRGFX's fast path is mode 8 only, so this needs its own doubled-pixel copy loop. Only changed rectangles go to video memory |
| Room change | the picture is drawn from its commands the first time, and the finished buffer is saved to the hard disk, so later visits load 27 KB instead of redrawing. Stock King's Quest redrew every time |
| Memory | all logic and views for the game loaded at start. It is under 200 KB, and this PCjr has 640 KB, with the code above the slow low 128 KB |
| Timing | the game cycle runs off the timer, 20 a second at normal speed, the same as AGI's default |
| Sound | JRGFX's sound calls, driven from the timer interrupt |
| Speech | SAY's player, run between cycles, clips on the hard disk |
| Link | `client/` serial and link code, reused as is: RTS off, one frame in flight |
| Input | every key shows at once. A line typed while the game is busy waits and is not lost |

### How "frame for frame" is tested

Both interpreters can run from a script (keys and typed lines, each
tagged with a cycle number, random numbers from a fixed seed) and write,
every N cycles, a checksum of the picture buffer, the screen, and all
variables, flags and object tables. The test plays the full winning
script on both (the 8088 one in headless DOSBox) and compares checksums.
A mismatch names the first cycle that differs and dumps both screens.

This is an emulator test. It proves the two interpreters agree. It does
not prove speed, sound, palette or the keyboard on the real PCjr: those
need Steve at the machine, as always.

## Numbers to hit

| Thing | Target | How we'll know |
|---|---|---|
| Game cycle on the real PCjr | 20 a second with 4 objects moving | a counter on the status line (debug key) |
| Room change, first visit | under 3 s | timed in the log |
| Room change, later visits | under 1 s | timed in the log |
| Classic parser reply | at once | |
| AI parser reply on the PCjr | under 6 s, with the spinner moving | server log and client counter |
| Whole game on disk | under 400 KB without speech | `pack` prints it |

These are targets, not promises. DOSBox can't judge the first three.

## Where it lives

```
apps/jrquest/
  DESIGN.md  ENGINE.md      these two
  tools/                    pic, view, logic, words, snd, pack, play, walk
  js/                       the reference interpreter and the browser shell
  dos/                      the 8088 interpreter
  game/                     the game's source: rooms/, views/, logic/, sound/, words.txt
  test/                     scripts and the checksum comparison
```

The engine (tools, js, dos) knows nothing about this game, so the next
game is a new `game/` folder.

## Build order

1. **Tools and JS interpreter, two rooms.** Checkpoint 2: Steve plays two
   screens in the browser and judges the look and feel.
2. **The whole game in the browser,** with the walkthrough test, the AI
   parser and the live ending. Checkpoint 3.
3. **The 8088 interpreter,** matched to the JS one in the emulator.
4. **On the real PCjr:** speed, sound, palette, keyboard, the modem link.
5. **Browser hosting on a private link.** Asked about first.

Steps 1 and 2 can use parallel agents against the shared formats: one on
the interpreter, one on the tools, one each on art, logic and sound.
