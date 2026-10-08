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

Measured in DOSBox (`machine=pcjr`, `cycles=fixed 400`, the project's rough
stand-in for a PCjr; the real machine will differ) on a scripted battle of
1000 frames with the frame limiter off:

| | Frames a second | Screen | Collisions | Game logic |
|---|---|---|---|---|
| Straight port | 8.1 | 80.1 ms | 26.0 ms | 12.9 ms |
| Row table and shift tables in C | 11.0 | 52.3 ms | 24.3 ms | 12.7 ms |
| Sprite rows in assembly | 13.6 | 40.2 ms | 18.6 ms | 12.7 ms |
| Tighter ground loop | 14.4 | 36.5 ms | 18.6 ms | 12.7 ms |

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

## Testing

Nothing here has run on the real PCjr yet.

`SET SOPTEST=n` before starting makes the game fly itself (a scripted
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

## Known gaps

- The record and playback options (`-h`, `-v`) are in the source but don't
  work in this build: recording only writes when a key changes, and the
  buffer handling assumed a different memory model. Untouched.
- The multiplayer and serial modes are stubs in the released source.
- The original's compiler warnings (unused parameters and the like) are left
  alone, so this one build doesn't use `-wx -we`.
