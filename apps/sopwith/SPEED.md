# Making Sopwith 2 faster on a real IBM PCjr

A record of every speed change made to this port of Sopwith 2 (David L.
Clark, BMB Compuscience, 1984; source reverse-engineered by Andrew Jenner
and released in 2000), what each one bought, and how each was shown not to
change the game. Written October 2026.

The machine is a real IBM PCjr (8088, 4.77 MHz) with a jr-IDE card. The
game is built with Open Watcom 2, 8088 code only, small memory model.

## The short version

| | Frames a second on the real PCjr, limiter off |
|---|---|
| Straight port of the released source | not measured there (8.1 in the emulator) |
| Row and shift tables, sprite rows in assembly | 10.5 |
| Whole sprite and ground line in assembly | 13.3 |
| Collisions without drawing, score written once a frame and without the BIOS | 15.5 |
| Unrolled sprite routines, pairs that can't matter left untested | 16.8 |

The game wants 9.1 frames a second (one frame every two ticks of the PC's
18.2 Hz clock). A frame that takes longer than 110 ms simply runs late, so
what a player feels is not the average but the slow frames. Counting those
became the real measure.

Every change below leaves the game itself untouched: with the same keys,
the screen at frame 1000 is the same 16 KB, byte for byte, as the straight
port's, in the emulator and on the PCjr.

## How it was measured

**A pilot that flies itself.** `SOPWITH2 @1000FP -s -i -k -q` (or
`SET SOPTEST=1000FP`) starts a game with a fixed random seed and a scripted
pilot: full throttle, take off, climb and dive in a wave, fire every 7th
frame, bomb every 23rd. After 1000 frames it writes the screen to
`SHOT.BIN` and the timings to `LOG.TXT` and exits. `F` turns the frame
limiter off, `P` times each phase of the frame. The pilot is clumsy: it
crashes about 25 times in 1000 frames, so the battle has plenty of
explosions, and plenty of respawns (see "What the test overstates").

**The clock.** Timer channel 0 is put in mode 2 and read with the BIOS tick
count: 0.84 microsecond steps. Two traps, both found on the real machine:

- If the count has just restarted but the tick interrupt hasn't been
  counted yet (it arrived while interrupts were off for the reading), the
  clock appears to step back 55 ms. The reading now asks the interrupt
  controller whether a tick is waiting (`out 20h,0Ah`, `in 20h`, bit 0)
  and adds it.
- A count of exactly 0 means "just restarted" (it is 65536), not "nearly
  done". Rare, about once in 65,536 readings, which at 80 readings a frame
  is once or twice a run: it showed up as one frame "3600110 ms" long.

**The real machine, not only the emulator.** `just pcjr-run` sends the
build to the PCjr over its serial line, runs it there and brings back the
log; `just pcjr-fetch` brings back the screen. DOSBox at `cycles=fixed 400`
is the stand-in between real runs, and it is wrong in ways that mattered:

| What | DOSBox says | The PCjr says |
|---|---|---|
| Game logic (multiplies, long arithmetic) | 12.7 ms a frame | 18.5 ms |
| One score written through the BIOS | 3.3 ms | 22.7 ms |
| Memory handed to the program | zeros | whatever ran before |

DOSBox charges every instruction the same, runs BIOS calls as native code,
and clears memory. Each of those hid something.

**Proving a change is harmless.** Three checks, used throughout:

1. The screen at frames 330, 1000 and 2500 must match the previous build's.
2. Where one routine replaces another, a checking build (`-dCJ_CHECKHIT`)
   runs both on every call and counts disagreements in the log.
3. The PCjr's screen at frame 1000 must match the emulator's.

One early check passed on stale files: the script left the previous run's
screen in place when the game hung, and "identical" was reported for a
build that never ran. It then hung the real PCjr. The script now deletes
its outputs first. A check that can't fail isn't one.

## The changes, in order

Emulator figures are DOSBox at 400 cycles, 1000 frames, limiter off.

### 1. Row table (emulator: 8.1 to 11.0 fps, with 2)

The screen is CGA mode 4: 320x200, 2 bits a pixel, even lines in one 8 KB
bank and odd lines in another. The original worked out every pixel's
address with `((199-y)>>1)*80` plus the bank, in every sprite row, pixel
and ground call. An 8088 multiply takes over 100 clock cycles. Now there is
a 200-entry table of row offsets for each of the two buffer layouts (the
screen, and the off-screen buffer whose banks are 1000h apart).

### 2. Shift tables for sprites

A sprite can start at any of 4 pixel positions inside a byte, so every
sprite byte was shifted by 0, 2, 4 or 6 bits: `v<<(8-2*s)`. An 8088 shifts
one bit at a time, in a loop. Now there are tables, for each of the 4
positions, of every byte value already shifted: the part that lands in this
screen byte and the part that spills into the next.

The original also looked up a mask (`blut[]`, 3 in every pixel that isn't
background) for each byte when drawing in the second plane's colour or
testing a collision. Because a pixel is 2 bits and sprites shift by whole
pixels, the shifted-in and shifted-out parts never share a pixel, so
`blut[a|b]` equals `blut[a]|blut[b]` and the masks can be put in the tables
too. Six 256-byte pages per shift, each block on a 256-byte boundary so
assembly can pick an entry by loading BL alone.

### 3. Sprite rows in assembly (11.0 to 13.6)

One row of a sprite became an inline loop: load the sprite byte, look up
its shifted form, OR in what spilled from the previous byte, keep this
byte's spill, XOR onto the screen. Eight instructions a byte.

### 4. The ground line (13.6 to 14.4, then to 17.4 with 5)

The ground is a line across all 320 columns, drawn by flipping pixels, and
it is erased and redrawn whenever the screen scrolls: about 700 calls to
`putpixel` a frame, each with its own multiply. Now one routine walks the
screen byte and the pixel mask along, and steps up and down the row table
as the line climbs and falls: about three instructions a pixel.

Tried and dropped: redrawing only the columns whose height changed. While
the plane is moving nearly all of them change, and the test cost more than
it saved.

### 5. Whole sprite and whole ground line in one assembly call (14.4 to 17.4; PCjr 10.5 to 13.3)

With rows in assembly, the C around each row (the bank arithmetic, the
clipping tests, the call) cost more than the row. `sopgfx.asm` has the
whole sprite (`cj_blit`) and the whole ground line (`cj_ground`).

This is the change that hung the PCjr. `#pragma aux cj_blit parm [es] [di]
...` on an external routine: Open Watcom silently drops ES from the list
and moves every later argument along one register. No warning. It was
found by reading the call in the disassembly. ES in a parameter list is
fine for inline `#pragma aux` code, which is why the row routines had
worked. The segment now travels in a variable.

### 6. Collisions without drawing (17.4 to 19.8; collisions 16.0 to 9.8 ms)

To test whether two objects touch, the original drew the first in a
scratch corner of the off-screen buffer, XORed the second in at its offset
while collecting any pixels already set, then cleared the corner. Three
passes over memory for every pair of objects whose boxes overlap, and an
explosion makes many pairs.

`cj_hit` gets the same answer from the sprite bytes directly, using the
shift and mask tables from step 2: the first sprite as it would have been
drawn at x=15, ANDed with the mask of the second at its offset, row by row
over the rows where they overlap. One detail had to be kept: a sprite drawn
in the second plane's colour has its colour-3 pixels come out as 0, so
those pixels can't be hit. That is the original's behaviour.

The checking build ran both tests on 19,622 pairs over 8000 frames: no
disagreements. A single-pixel object (a bullet) still uses the original
method.

### 7. The ground-crash test without drawing (collisions' crash tests 2.6 to 0.7 ms a frame)

The same idea for "has a plane or bomb touched the ground": the original
drew the sprite in the scratch corner and looked at one pixel per column,
at the ground's height. `cj_crash` reads that pixel from the sprite.
Covered by the same checking build.

### 8. Debris against debris, turned away early

Two pieces of debris never collide: it is the first thing the pair test
checks. An explosion throws up to 15 pieces from one spot, which is about a
hundred pairs a frame, each paying for a call to learn that. The loop now
skips them itself.

### 9. The score, once a frame (PCjr: frames over 165 ms, 20 to 4)

Found only on the real machine. The slowest frame there spent 189 ms in
"collisions" where the emulator showed 41. The score is written through
the BIOS (teletype output in a graphics mode), which costs 22.7 ms a time
on the PCjr and almost nothing in DOSBox. Debris from one explosion can
score six times in a frame, and each time the number was written again.

Now a changed score is noted and written once, at the end of the frame. In
the 1000-frame battle the score changed 130 times and was written 44 times.
The picture is the same because the number is padded to the same width
every time.

### 10. The score, without the BIOS (22.7 to 6.3 ms a write)

The six characters are drawn straight into screen memory from the ROM's
8x8 character table at F000:FA6E, each font byte doubled into 2-bit pixels
through a 16-entry table and masked with the colour. The PCjr keeps its
characters at the same address as the PC, and the result matches what its
BIOS draws: the screen comparison passes on both machines.

### 11. Keeping the pace when a frame is late (Sopwith26 only)

Not a speedup, but the change a player notices most. The original's frame
limiter can only hold the game back; a frame over 110 ms just makes the
game run slow for that moment. Sopwith26 keeps count of how far behind the
clock it is, and when it is two ticks behind it runs the next frame's game
logic but leaves out its drawing (never more than two frames running).
Game logic is about 18 ms of a frame and drawing about 32 ms, more in an
explosion, so one skipped drawing recovers most of a late frame.

It needed care in three places, because the game erases a sprite by
XORing it again at its remembered old position:

- After a frame that wasn't drawn, the "old" position, sprite and
  visibility of every object must not be updated: they still describe what
  is on the screen.
- Objects deleted in a skipped frame are still erased in it, because their
  slots are reused in the next.
- The screen's copy of the ground line is marked stale so the next drawn
  frame redraws it even if the screen isn't scrolling by then.

The test: a mode that leaves out two frames' drawing in every three for
2500 frames. The final screen is identical to a run that drew everything,
which shows both that the erase bookkeeping holds and that the game itself
doesn't depend on drawing.

On the real PCjr with the limiter on, 1000 frames should take 2000 ticks:

| | Ticks for 1000 frames | Frames not drawn |
|---|---|---|
| Sopwith 2 (faithful) | 2047 | 0 |
| Sopwith26 | 2009 | 19 |

### 12. What debris costs, measured (no speedup: a counter)

The player's complaint was that the game slows in explosions. Rather than
time each piece of debris (timing every object had cost 10 ms a frame),
each frame's three phase times are added to a bucket chosen by how many
pieces of debris are alive. No extra clock readings. On the real PCjr:

| Debris alive | Frames | Game logic | Screen | Collisions | Whole frame |
|---|---|---|---|---|---|
| none | 447 | 17.2 ms | 27.8 ms | 7.0 ms | 52 ms |
| 1 to 7 | 406 | 21.2 ms | 31.4 ms | 10.4 ms | 63 ms |
| 8 to 15 | 147 | 20.7 ms | 46.1 ms | 26.8 ms | 94 ms |

An explosion's worth of debris adds 42 ms: 20 in collisions, 18 in drawing
and only 3.5 in moving the pieces. Rewriting debris movement in assembly
had been on the list; this took it off.

### 13. Sprite routines for the two common widths (PCjr: screen 27.8 to 24.4 ms, 46.1 to 38.2 with debris)

Sprites are 8 pixels wide (debris, bombs, birds: 2 bytes a row) or 16
(planes, buildings: 4 bytes). The general loop from step 5 spent as much
on its row bookkeeping (a loop counter in memory, saving and restoring the
screen pointer, a test for clipping) as on a 2-byte row. `cj_blit` now
branches once to a routine for exactly 2 or 4 bytes a row: no inner loop,
the spilled pixels carried in AH, the row count in DX, fixed offsets from
DI. A sprite clipped at the right edge still takes the general loop.

### 14. Pairs that can't matter (PCjr: collisions with debris 26.8 to 19.5 ms)

Counting the collision tests by the two objects' types showed that 1269 of
the battle's 1770 pair tests were a plane against debris. A plane that is
shot down falls through its own debris and was tested against every piece,
every frame, and nothing can come of it: a falling plane reacts only to
the ground, and debris only scores off a plane that is flying or caught
fire that same frame.

`cj_noeffect` turns such pairs away before the pixel test: debris against a
falling plane, a building, a cow or smoke. For each, the collision handler
was read to confirm that neither side does anything, and that nothing
earlier in the same pass could change that before the pair's turn came.
(Debris whose `inverted` field is set is the exception, and is tested as
before.)

One thing made this less simple than it looks. The original lists at most
199 collisions a frame and silently drops the rest, so leaving pairs out
could let a later pair onto the list that the original would have dropped.
The pass therefore counts the pairs it leaves out as if each had hit, and
if that count could have filled the list it runs the whole pass again with
nothing left out. The list is the original's in every case.

Check: a build with the skipping off (`-dCJ_NOSKIP`) gives the same screen
at frames 330, 1000, 2500 and 6000 of the single-player battle, at 2500 of
a game against the computer, and at 12000 of one on level 5.

### 15. The fast score at respawn too (PCjr: a respawn frame 100 to about 85 ms)

Every score write now takes the direct path from step 10, including the
ones in the full status redraw after a crash.

### 16. A different way to show debris (Sopwith26; not a speedup, as it turned out)

The idea was that fifteen 8 by 8 sprites could be shown as fifteen small
blocks (3 screen rows of 1 or 2 bytes, against 8 rows of 3) with single
pixel sparks added for effect, and come out both prettier and cheaper. The
game's own debris stays as it is, since it can hit things and score; only
the screen update's draw call is swapped.

Measured on the real PCjr, a frame with an explosion in it:

| | Screen time |
|---|---|
| Original debris sprites | 39.2 ms |
| Blocks and 16 to 28 sparks an explosion | 40.4 ms |
| Blocks and 12 to 20 sparks, pixel masks from a table | 37.3 ms |

The sparks cost what the blocks saved. Each spark is a little C: a
structure, a far pointer, and at first a shift by a variable count, which
an 8088 does a bit at a time. With fewer sparks and the shift replaced by
a 4-entry table it comes out slightly ahead.

It was taken out again. Seen on the real screen, the original's chunks
looked better, and without a speed gain there was nothing to weigh against
that. The red flash that came with it (one palette register write when a
building goes, no drawing) stayed.

Because the picture now differs from the faithful build's on purpose, the
screen comparison no longer applies. The log gained a state number (a sum
over every object's position, type, status and score): it is the same
with the new explosions, with the old, and in the faithful build, at
frames 1000, 2500 and 6000.

## Where the PCjr stands now

1000 frames of the scripted battle, limiter off, real PCjr:

| Build | Frames a second | Game logic | Screen | Collisions | Frames over 110 ms | Over 165 ms |
|---|---|---|---|---|---|---|
| Sprite rows in assembly (step 4) | 10.5 | 18.1 ms | 49.6 ms | 24.9 ms | not counted | not counted |
| Whole sprite and ground in assembly (step 5) | 13.3 | 18.2 ms | 31.5 ms | 22.7 ms | not counted | not counted |
| Collisions without drawing (steps 6 to 8) | 14.9 | 18.5 ms | 31.8 ms | 13.6 ms | 98 | 20 |
| Score once a frame (step 9) | 15.4 | 18.5 ms | 31.8 ms | 11.8 ms | 81 | 4 |
| Score without the BIOS (step 10) | 15.5 | 18.6 ms | 31.8 ms | 11.2 ms | 67 | 2 |
| Unrolled sprites, pairs that can't matter (steps 13 to 15) | 16.8 | 18.5 ms | 27.5 ms | 9.8 ms | 38 | 0 |

A frame with 8 to 15 pieces of debris flying went from 94 ms to 78 ms, and
one with none from 52 ms to 48 ms. With the limiter on, 1000 frames take
2033 ticks in the faithful build (2047 before these steps) and 2009 in
Sopwith26, which now leaves out 12 drawings where it left out 19.

Of the screen's time, 9.7 ms is the ground line and the rest is about 15
sprites a frame. Of collisions', about 4 ms is pair tests and 0.9 ms crash
tests; the rest is walking the object list.

### What the test overstates

A build that timed each object's update (`-dCJ_TYPEPROF`; the timing
itself costs 10 ms a frame on the PCjr, so it is not in normal builds)
showed that every update over 10 ms was the player's own plane, at 100 ms,
28 times in 1000 frames. Those are respawns: after a crash the whole
status area is rebuilt (16 KB of screen cleared and copied, the map and
gauges redrawn). The scripted pilot crashes far more often than a person
does, so about 28 of the slow frames are respawns, where a tenth of a
second's pause is not noticed. After steps 13 to 15 that is most of them:
28 of 38.

## A bug found by measuring

Comparing the PCjr's screen with the emulator's at frame 1000 showed the
same scene with a different score, and three runs on the PCjr gave three
scores. The game's object table comes from `malloc` and was never cleared;
debris never sets its `inverted` field; and the collision code reads that
field to decide whether debris scores. DOSBox hands out zeroed memory, so
the emulator always agreed with itself. It is in the 1984 source. The
table is now cleared at startup, and the PCjr gives the emulator's screen
every time.

This matters beyond tidiness: the game's design (for its 1984 network
play) is that two machines given the same keys stay in step. They would
not have.

## What is left

- **Drawing.** Still the largest part of a frame. A sprite that starts on
  a byte boundary in the first plane's colour could be XORed a word at a
  time, with no table lookups.
- **The collision test in assembly.** `cj_hit` is C, and a flying plane
  among debris still needs it.
- **Game logic.** 18.6 ms on the PCjr against 12.7 in the emulator points
  at multiplies and 32-bit arithmetic (each object's position is a 32-bit
  fixed-point number; the enemy's aiming looks 10 moves ahead with a
  distance-squared in each). Not yet profiled on the real machine in a way
  that doesn't disturb it.
- **Respawn.** 100 ms to rebuild the status area.
