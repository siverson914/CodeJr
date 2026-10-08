# Sopwith 2, rebuilt for the PCjr

David L. Clark's Sopwith 2 (BMB Compuscience, 1984-85), built from source
with this project's tools, as a base to fix, speed up and extend. The
original program already runs on a PCjr; this is about being able to
change it.

    SOPWITH2            asks for the game mode and the keyboard
    SOPWITH2 -s -i -k   straight into a single-player game, keys held down properly
    SOPWITH2 -s -i -k -q   the same, with the sound off

Keys: `X` faster, `Z` slower, `,` climb, `/` dive, `.` flip, Space fire, `B`
bomb, `H` home, `S` sound, Ctrl-Break quit. The arrow keys also climb, dive
and change speed.

This folder is the faithful one: the original's menus, look and game, with
a modern build and faster drawing. `apps/sopwith26` builds the same source
with `SOPWITH26` defined, which is where changes to how the game looks or
plays go (inside `#ifdef SOPWITH26`). Speed work that leaves the game
identical is shared by both.

## Where the source came from

- `orig/` is the Sopwith 2 source exactly as released: reverse-engineered
  from the 1984 program by Andrew Jenner in 1999-2000 and released with
  David Clark, who later put it under the GNU GPL as well. Fetched from
  <https://github.com/Retro-Jack/sopwith-genx> (its first commit is the
  release, untouched).
- The working copy starts from that repository's version, which adds the
  arrow keys (Retro Jack, 24 September 2026, 12 lines).
- `license.txt` is the licence that came with the source and must travel
  with the program: keep the on-screen copyright, mark changed files with
  what changed and when, ship the source, charge nothing. Every change made
  here is marked `CodeJr:` in the source.

The released source was written for Turbo C 2.0 and the A86 assembler,
neither of which this project has.

## What the port changed (October 2026)

| File | Change |
|---|---|
| `owcompat.h` (new) | Open Watcom stand-ins for the Turbo C calls the game uses (`inportb`, `outportb`, `farmalloc`, the `_DS` pseudo-register, `delay`) |
| `sopsys.asm` (new) | the timer and keyboard interrupt handlers, rewritten from `sopasm.asm` for WASM. They now run the game's routine on a private stack, and the keyboard one keeps AL, where a PCjr's INT 9 gets its scan code |
| `sopsys.c` (new) | the rest of `sopasm.asm` in C: BIOS text calls, far memory copy and fill, the status-bar fill |
| `sopwith2.c` | the variables the interrupts change are `volatile` (Watcom's optimizer would otherwise turn "wait for the next tick" into an endless loop); the PCjr scan code; a fix where the option parser took the address of an argument; the speed work and the test hook below |

`sopasm.asm` itself is no longer used; it stays in `orig/`.

## Speed

`SPEED.md` is the full account: every change, what it bought on the real
PCjr, what the emulator got wrong, and how each change was shown to leave
the game identical. The tables here are the summary.

Measured in DOSBox (`machine=pcjr`, `cycles=fixed 400`, the project's rough
stand-in for a PCjr; the real machine will differ) on a scripted battle of
1000 frames with the frame limiter off:

| | Frames a second | Screen | Collisions | Game logic |
|---|---|---|---|---|
| Straight port | 8.1 | 80.1 ms | 26.0 ms | 12.9 ms |
| Row table and shift tables in C | 11.0 | 52.3 ms | 24.3 ms | 12.7 ms |
| Sprite rows in assembly | 13.6 | 40.2 ms | 18.6 ms | 12.7 ms |
| Tighter ground loop | 14.4 | 36.5 ms | 18.6 ms | 12.7 ms |
| Whole sprite and ground line in assembly (`sopgfx.asm`) | 17.4 | 26.2 ms | 16.9 ms | 12.7 ms |
| Sprites compared directly for collisions (`cj_hit`) | 19.8 | 26.2 ms | 9.8 ms | 12.7 ms |
| Crash test without drawing, debris pairs skipped, score once a frame | 20.0 | 26.4 ms | 8.6 ms | 12.7 ms |
| Sprite routines for 2 and 4 bytes a row, pairs that can't matter left untested | 21.9 | 22.8 ms | 7.3 ms | 12.7 ms |

The game aims for 9.1 frames a second (every second timer tick). The
straight port fell short of that in a busy scene; now there is headroom.

What was slow, and what replaced it:

- **A multiply for every pixel address.** `((199-y)>>1)*80` plus the bank,
  in every sprite, pixel and ground call. Now a 200-entry row table per
  buffer layout.
- **A variable shift for every sprite byte.** An 8088 shifts one bit at a
  time. Now tables of every byte shifted by 0 to 3 pixels.
- **The colour mask.** Drawing a sprite in the second colour looked up
  `blut[]` for every byte. A pixel is 2 bits and sprites shift by whole
  pixels, so the mask can be worked into the tables in advance: a sprite
  byte is now one lookup, one OR and one XOR, in a `#pragma aux` loop
  (`cj_putrow`, and `cj_bltrow` for the collision test).
- **The ground line**, about 700 pixel calls a frame while scrolling. Now
  one loop that walks the screen byte and pixel mask along.
- Tried and dropped: redrawing only the ground columns that changed. While
  the plane moves, nearly all of them do.

Where the time still goes: the game logic and the collision test's
bookkeeping are plain C, and each sprite still has C around its rows.

## On the real PCjr

`just pcjr-run SOPWITH SOPWITH2.EXE @1000FP -s -i -k -q` runs the scripted
battle on the connected PCjr and brings back `LOG.TXT`;
`just pcjr-fetch 'SOPWITH\SHOT.BIN'` brings back the screen it saved.

| Build | Frames a second | Game logic | Screen | Collisions |
|---|---|---|---|---|
| Sprite rows in assembly, tight C ground loop (1000 frames) | 10.5 | 18.1 ms | 49.6 ms | 24.9 ms |
| Whole sprite and ground line in assembly (`sopgfx.asm`), same 1000 frames | 13.3 | 18.2 ms | 31.5 ms | 22.7 ms |
| Collisions without drawing (`cj_hit`, `cj_crash`) | 14.9 | 18.5 ms | 31.8 ms | 13.6 ms |
| Score written once a frame, and not through the BIOS | 15.5 | 18.6 ms | 31.8 ms | 11.2 ms |
| Sprite routines for 2 and 4 bytes a row, pairs that can't matter left untested | 16.8 | 18.5 ms | 27.5 ms | 9.8 ms |

With the limiter on, 1000 frames take 2033 ticks on the PCjr (2000 would be
full speed); Sopwith26, which leaves out a frame's drawing when it is
behind, takes 2009. A frame with an explosion's debris in it takes 78 ms
(94 before the last row), one with none 48 ms.

The screens it saved at frames 300 and 1000 match the emulator's byte for
byte, and two runs in a row give the same numbers. Next to DOSBox at 400
cycles the real machine is slower in the C parts (logic 1.4 times,
collisions 1.3, screen 1.2): DOSBox charges a multiply or a shift the same
as an add.

The game clears its object table at startup. The original didn't, and
debris never sets the `inverted` field that the collision code reads to
decide whether debris scores, so on the real PCjr the same scripted battle
ended on a different score from run to run (-750, then -1950), depending on
what the last program left in memory. DOSBox hands out zeroed memory and
never showed it.

## Testing

`SET SOPTEST=n` before starting, or `@n` as the first argument (`SOPWITH2 @1000FP -s -i -k -q`), makes the game fly itself (a scripted
pilot: take off, climb and dive, shoot and bomb) with a fixed random seed,
and after n frames save the screen (`SHOT.BIN`) and its speed (`LOG.TXT`)
and exit. `F` after the number turns the frame limiter off, `P` adds the
time per phase. The same game unfolds every time, at any emulator speed, so
two builds can be compared byte for byte:

```sh
apps/sopwith/build.sh
apps/sopwith/shot.sh /tmp/a.png 1000 400 FP        # screenshot, /tmp/a.bin, speed and phases
apps/sopwith/shot.sh /tmp/b.png 1000 20000 F OLD.EXE   # another build, same game
cmp /tmp/a.bin /tmp/b.bin
```

The optimized build matches the straight port at frames 330, 1000 and 2500.

With `P`, the log also counts the frames that took too long: how many
finished under 55, 110 and 165 ms and over (110 ms is two timer ticks, the
game's full speed, so anything above is a slowdown the player sees), where
the time went in the slow ones, and the slowest frame. In DOSBox at 400
cycles, 54 of the 1000 frames were slow and collisions took 70 ms of each
(16 ms on average over all frames): explosion debris is what slows the game.

So the collision test no longer draws. The original drew one sprite in a
scratch corner, XORed the other in while watching for pixels already set,
and wiped the corner: three passes over memory for every pair of objects
whose boxes overlap. `cj_hit` compares the same bytes straight from the two
sprites. With that, 28 of the 1000 frames are slow, none over 165 ms, and
the slowest is 122 ms (was 182). What is left in a slow frame is mostly
drawing the debris (64 ms of screen). Emulator figures; not yet measured on
the real PCjr.

A build with `-dCJ_CHECKHIT` runs both tests on every pair and counts the
disagreements in `LOG.TXT`: none in 11,624 pairs over 6000 frames. A single
pixel (a bullet) still goes the original's way. `@5T` (Sopwith26 only)
saves the title screen and exits.

`shot.sh` deletes its output files before it runs. It didn't at first, and
a build that hung left the previous build's screen in place, which then
"matched": a broken build was sent to the real PCjr on the strength of it.

## Known gaps

- The record and playback options work again: `-hNAME` records a game's
  keys to a file, `-vNAME` flies it back. The released source wrote a
  one-word header and skipped two on reading, so a recording never played
  to its end, and it passed far memory to `fread`, which this memory model
  can't do. The file is now two words (random seed, level), then a frame
  number and the keys for each change. A 1000-frame flight is 1.3 KB and
  plays back to the identical screen. A recording holds up to 15,000 key
  changes. Emulator-tested only.
- The multiplayer and serial modes are stubs in the released source.
- The original's compiler warnings (unused parameters and the like) are left
  alone, so this one build doesn't use `-wx -we`.
