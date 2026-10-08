# Sopwith26

Sopwith 2 with our changes. `apps/sopwith` is the faithful port of the 1984
game and stays that way; this is the same source built with `SOPWITH26`
defined (`build.sh` here, or `just sopwith26`), so a speed fix made there
reaches both and the two can always be compared frame for frame.

    SOP26               the title screen, then any key to play
    SOP26 -c            straight into a game against the computer
    SOP26 -s -q         straight into a single-player game, sound off

On the PCjr it lives in `C:\CODEJR\PROGS\SOP26` (`just deploy-sopwith26`).
A DOS name holds eight letters, hence `SOP26`.

## What is different from Sopwith 2

| Change | Why |
|---|---|
| The title screen shows the keys and says "Press any key to play" (`C` plays against the computer) | The original asked two questions before every game |
| No keyboard question: keys are always read as held or released | The "non-IBM keyboard" choice was for 1984 clones and only sees taps. `-j` still picks a joystick |
| No network or serial game on the menu; `-m` and `-a` say so and stop | They were never in the released source: the menu entries led to empty routines |
| `P` pauses. Esc pauses too, and Esc again there quits | One slip on Esc shouldn't end a game. Ctrl-Break (Fn then B on a PCjr) still quits the original's way |
| A top ten, kept in `SOP26.HI` beside the program: three initials, score, level reached, date. Shown after every game and with `T` on the title screen; the best score is on the title screen | |
| `1` to `7` on the title screen (or `-g0` to `-g6`) pick the starting level, and losing all lives returns to it, not to the first | There are 7 levels: each win moves up one and makes everything fly faster; from the second the enemy's buildings shoot back |
| When a frame is late the next one's drawing is left out, so the game keeps its pace through an explosion | The original's limiter can only slow the game down. See `apps/sopwith/SPEED.md` |

The game itself (the world, the planes, the scoring) is the same: with the
same keys, both builds put the same picture on the screen at frame 1000.

Sopwith26's own code is `sop26.c` here, included by `sopwith2.c` when
`SOPWITH26` is defined. `SOP26.HI` is a text file, one score a line; lines
that don't read as a score are skipped.

The licence (`apps/sopwith/license.txt`) travels with it: the on-screen
copyright stays, changed files are marked, the source is here, and nothing
is charged. The title screen says "The 1984 game, changed in 2026".

## Testing

The test mode is `apps/sopwith`'s (see its README): `SOP26 @1000FP -s -q`
flies the scripted battle. Letters after the number, for the parts that
need someone at the keyboard:

| | |
|---|---|
| `@5T` | save the title screen and exit |
| `@300U -s` | pause at frame 300, save the screen with the notice on it |
| `@300G -s` | end the game at frame 300 with a score of 300, "type" CJ, save the score screen |
| `@2500FK -s` | leave out two frames' drawing in three; the last screen must match a run without `K` |

On the real PCjr: the 1000-frame battle with the limiter on (2009 ticks,
drawing left out on 19 frames, screen identical to the emulator's).
Emulator only so far: the title screen, pause, the top ten and its file,
the starting level. Nobody has yet pressed P or typed initials on the real
keyboard.

## Ideas, not done

- More speed in busy scenes: see "What is left" in `apps/sopwith/SPEED.md`.
- The PCjr's three-voice sound chip, so the engine doesn't cut out when a
  gun fires.
- A recorded game playing itself behind the title screen.
- Colours: a PCjr can show any 4 of its 16 colours in this screen mode at
  no cost in speed, and 16 at once only in a mode that takes twice the
  memory and twice the drawing.
- Two players through CodeJr's line or a Gipity room, and a high score
  table. Both need the game to stay exactly repeatable, which it now is.
