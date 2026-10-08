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
| Esc quits, as well as Ctrl-Break (Fn then B on a PCjr) | |

The game itself (the world, the planes, the scoring, the speed) is the
same: with the same keys, both builds put the same picture on the screen at
frame 1000.

The licence (`apps/sopwith/license.txt`) travels with it: the on-screen
copyright stays, changed files are marked, the source is here, and nothing
is charged. The title screen says "The 1984 game, changed in 2026".

## Testing

The test mode is `apps/sopwith`'s (see its README): `SOP26 @1000FP -s -q`
flies the scripted battle, `SOP26 @5T` saves the title screen to `SHOT.BIN`
and exits.

Tested in the emulator only: the title screen, any key to start, and the
1000-frame battle. Not yet run on the real PCjr.

## Ideas, not done

- More speed in busy scenes: drawing debris is now the largest part of a
  slow frame.
- Colours: a PCjr can show any 4 of its 16 colours in this screen mode at
  no cost in speed, and 16 at once only in a mode that takes twice the
  memory and twice the drawing.
- Two players through CodeJr's line or a Gipity room, and a high score
  table. Both need the game to stay exactly repeatable, which it now is.
