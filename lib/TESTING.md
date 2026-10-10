# Testing a JRGFX game: the self-running mode (jrtest)

CodeJr cannot press a key or look at the screen. Its loop is: build, run
the program headless in DOSBox (`run`), read what came back, then run it
on the real PCjr (`run_on_pcjr`). So every game needs a mode that plays
itself and **ends by itself**: a program that hangs on the real PCjr can
only be stopped by a person at the PCjr.

`jrtest.h` (functions `jt_*`, part of the JRGFX library) is that mode's
common part: the arguments, a key script, screenshots, the clock, checks
and the log lines. The game supplies its tick, its frame and what to check.

**Status: emulator only.** Everything here has run in headless DOSBox
(`lib/test/tjt.c`). Nothing in jrtest has run on the real PCjr yet.

## The pattern

A complete program. The test part is the five `jt_` lines:

```c
#include "jrtest.h"                      /* brings jrgfx.h */
static int x = 80;
int main(int argc, char **argv)
{
	argc = jt_args(argc, argv);          /* takes /TEST /DEMO /SHOT /BENCH */
	if (fx_init(0)) return 1;
	key_init();
	jt_begin();                          /* start line, clock */
	while (!key_down[KEY_ESC] && !jt_tick()) {   /* jt_tick: keys, shots, the end */
		if (key_down[KEY_LEFT]) x--;     /* the game's tick: the same code */
		if (key_down[KEY_RIGHT]) x++;    /* a person's keys go through */
		fx_begin(); fx_clear(0); fx_fill_rect(x, 98, 4, 4, FX_SOLID2(14)); fx_flip();
	}
	jt_check(x == 100, "dot at 100");    /* true after: GAME /TEST R30,L10 */
	return jt_end();                     /* 0 = every check passed */
}
```

Rules that make it work:

- Call `jt_tick()` once per game tick, **before** the tick. When no test
  argument was given it returns 0 and does nothing, so the same loop is the
  normal game.
- Do not write a second input path for tests. The script presses keys with
  `key_feed()`, which is what the keyboard interrupt calls: `key_down[]`,
  `key_makes[]` and the `jrkeys.h` layer (`jk_down`, `jk_hit`) all see a
  scripted key exactly like a real one.
- In test mode run the ticks as fast as they come (no waiting for the
  clock), so a run is the same every time. Seed the random numbers with a
  constant when `jt_on` is set.
- Call `jt_end()` while the graphics are still up (it may take the final
  screenshot), and return its value from `main`.
- If frames and ticks are different things in the game, call `jt_frame()`
  once per frame drawn; the end line then counts frames.
- Anything the game wants to say goes to LOG.TXT with `jt_log(fmt, ...)`
  (which is `dbg_log`). Log little inside the loop: each line opens and
  closes the file, which is slow on the PCjr's disk and counts in the time.

## Arguments

`/` or `-`, any case. `jt_args` removes the ones it knows from `argv` and
returns the new `argc`; everything else is left for the game.

| Argument | Effect |
|---|---|
| `/TEST` | `jt_test = 1` |
| `/TEST:n` | and `jt_n = n` (a level, a scene; the game decides) |
| `/TEST n script` | the older games' form; `n` and `script` are both optional. An argument made only of digits is `n`; the next one, unless it starts with `/` or `-`, is the script |
| `/DEMO:n` | `jt_demo = n`: end after n ticks (`/DEMO` alone: 300) |
| `/SHOT:a,b,c` | SHOT1.PPM at tick a, SHOT2.PPM at tick b, ... (16 at most). Only with one of the others |
| `/BENCH` `/BENCH:n` | `jt_bench = n` (1 without n). What gets timed is up to the game |

Any of `/TEST`, `/DEMO`, `/BENCH` sets `jt_on` and deletes the old LOG.TXT.
A game with a built-in script sets it after `jt_args`:
`if (jt_test && !jt_script[0]) jt_set_script("D20 R10 U20");`

A run ends when the script ends, when the `/DEMO` count is reached, or when
the game leaves its loop. If none of those happens, `jt_tick` ends it after
`jt_limit` ticks (3000; set it after `jt_args` if a test needs more, 60000
at most) and counts a failure. So `/TEST` with no script and no `/DEMO`
runs to the limit and fails: give it one of them. A test of a title screen
is the script `40` (wait 40 ticks).

## Key script

Groups of keys, each followed by the number of ticks to hold them:

```
R30 RJ20 U40 20 J {ENTER}5 !W
```

| Piece | Meaning |
|---|---|
| `R30` | right arrow down for 30 ticks |
| `RJ20` | right and jump together for 20 ticks. Right stays down from the group before: no release in between |
| `20` | nothing held for 20 ticks |
| `J` | no number: 1 tick (a tap) |
| `{ENTER}5` | a key by name |
| `!W` | calls `jt_cmd_hook('W')`, takes no time: the game's own test commands (win the level, set up a board) |

- Single letters: `L R U D` are the arrow keys and `J` is the space bar.
  Every other letter is its own key (`W A S Z X` ...). `jt_map('Z', KEY_CTRL)`
  changes what a letter presses.
- Names in braces: `ESC ENTER SPACE UP DOWN LEFT RIGHT CTRL ALT SHIFT TAB
  BKSP`, `F1` to `F10`, `A` to `Z` (the letter key itself: `{L}` is L, not
  left), `0` to `9`, and `#n` for scan code n in decimal (`{#59}` is F1).
- Spaces and commas separate groups and mean nothing else. On a command
  line use commas, since a space starts a new argument: `GAME /TEST R30,L10`.
- Upper and lower case are the same.
- To press a key twice, put a wait between: `J 1 J`. `J J` is one press
  held for 2 ticks.
- Limits: 8 keys in a group, 60000 ticks in a group, 255 characters in a
  script. A longer script is cut and the test failed. An unknown key or a
  bad character is logged as a failure and skipped; the run goes on.
- `key_heal` (jrgfx) frees the opposite arrow when an arrow is released,
  for scripted keys too: after `LR10 R10` right is up as well. Scripts
  that hold both arrows of a pair are not worth writing.

## Log lines

The same from every game, so results can be read (and grepped) the same way:

```
T=16 JT start: test 1 n 1 demo 0 shots 2 script R30 10 L10 J !B {RIGHT}5
T=834 JT shot: SHOT1.PPM at tick 15
T=2653 JT shot: SHOT.PPM at tick 56
T=2655 JT end: 56 frames in 892 ms = 62.7 fps
T=2660 JT done: 0 failures (4 checks)
```

- `JT FAIL: what` for each failed `jt_check`, each script error and the
  tick limit. `JT done: 0 failures` is the line that says the test passed;
  the exit code is the number of failures (255 at most).
- `JT end` counts from `jt_begin` to the end of the run, without the time
  spent writing screenshots. No floating point: the rate is in tenths.
- SHOT.PPM is always the last screen. SHOTn.PPM are the `/SHOT` ticks, in
  the order given, and any `jt_shot()` the game calls. On the fast path
  (`fx_init`) a screenshot is the page on screen, read from video memory
  after the flip; otherwise it is the back buffer (`gfx_screenshot`). In
  text mode there is none (logged as failed, not counted as a failure).

## Where it fits in the loop

1. `build GAME`: the server links the library, jrtest included, when the
   program includes one of its headers.
2. `run GAME GAME.EXE /TEST:1 R30,J,L20 /SHOT:10,40` (headless DOSBox):
   returns what it printed, LOG.TXT and the PPMs as pictures. Look for
   `JT done: 0 failures` and look at the pictures.
3. `run_on_pcjr` with the same arguments: the program plays the same
   script on the real machine and ends by itself. LOG.TXT comes back; the
   `JT end` line is the real frame rate. The PPMs stay on the PCjr's disk
   (`fetch` one if needed); `run_on_pcjr`'s own `screenshot` argument is
   the other way to get a picture. Its `keys` argument is not needed with a
   script, and should not be mixed with one.

In the library itself: `lib/build.sh` builds `test/TJT.EXE` with the other
tests. To run it by hand:

```
lib/build.sh && d=$(mktemp -d) && lib/emu.sh $d 3000 lib/test/TJT.EXE /TEST:2 && cat $d/LOG.TXT
```

(`/TEST:2` adds the checks of the argument reader; with no arguments TJT
runs its built-in script only.)

## What is emulator-only

All of it. In particular, not yet seen on the real PCjr:

- Any jrtest run at all: the arguments as PC DOS 2.1 passes them, the
  script, the checks.
- Screenshot time. Writing a 192 KB PPM took about 0.6 to 1 s in DOSBox; on
  the PCjr's disk it will take far longer. It is left out of the `JT end`
  time, but a run with many `/SHOT` ticks will be slow, and the `run` tool
  returns 3 pictures at most (SHOT.PPM is one of them). Two shots is
  plenty.
- The frame rates. DOSBox's timing is wrong; only a `JT end` line from the
  real machine says how fast a game is.
- The tick limit as a guard. 3000 ticks at 4 frames per second is over 12
  minutes on the real machine: keep scripts short there. The limit stops a
  loop that keeps calling `jt_tick`; it cannot stop a game that is stuck
  inside its own tick.
- Scripted keys next to the real keyboard. A key pressed on the real
  keyboard during a test also reaches the game.
