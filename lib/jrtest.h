/* JRTEST: the self-running test mode every JRGFX game needs.
 * Part of JRGFX; include this next to jrgfx.h. Guide: lib/TESTING.md.
 *
 * CodeJr cannot press keys or see the screen. It builds a game, runs it
 * headless in DOSBox and then on the real PCjr, and reads back LOG.TXT
 * and the *.PPM screenshots. So a game needs a mode that plays itself
 * from a script and ends by itself. This is the common part of that mode:
 * the arguments, the key script, the screenshots, the clock, the checks
 * and the log lines, the same in every game.
 *
 * Use (the whole pattern):
 *   argc = jt_args(argc, argv);         takes /TEST /DEMO /SHOT /BENCH
 *   fx_init(0); key_init();
 *   jt_begin();
 *   while (!quit && !jt_tick()) { tick(); frame(); }
 *   jt_check(score > 0, "scored");
 *   return jt_end();                    0 when every check passed
 * Without one of the arguments jt_on is 0 and jt_tick() only returns 0.
 *
 * Arguments (/ or -, any case):
 *   /TEST            test mode: jt_on = jt_test = 1
 *   /TEST:n          the same, and jt_n = n (a level, a scene...)
 *   /TEST n script   the forms the older games take: both are optional,
 *   /TEST script     and an argument made only of digits is n
 *   /DEMO:n          end after n ticks (/DEMO alone: 300)
 *   /SHOT:a,b,c      SHOT1.PPM at tick a, SHOT2.PPM at tick b... (16 at most)
 *   /BENCH[:n]       jt_bench = n (1 without n); what it times is the game's
 *
 * Key script: groups of keys, each followed by how many ticks to hold it.
 *   R30 RJ20 U40     right for 30 ticks, right and jump for 20, up for 40
 *   20               nothing held for 20 ticks (a wait)
 *   J                no number: 1 tick (a tap)
 *   {ENTER}5 {F1}    keys by name; a letter in braces is that letter's key
 *   !W               calls jt_cmd_hook('W'): the game's own test commands
 * Single letters: L R U D are the arrows, J is the space bar, any other
 * letter is its own key (jt_map changes that). Names: ESC ENTER SPACE UP
 * DOWN LEFT RIGHT CTRL ALT SHIFT TAB BKSP F1-F10 A-Z 0-9 and #n (scan code
 * n in decimal). Spaces and commas separate groups and mean nothing else.
 * A key in two groups in a row stays down between them; put a wait
 * between two groups to press it twice ("J 1 J"). A group holds at most
 * 8 keys and 60000 ticks; a script is cut (and the test failed) after 255
 * characters. Keys are pressed with key_feed(), so key_down[], the press
 * counters and the jrkeys.h layer all see them like the real keyboard.
 *
 * Log lines, the same from every game:
 *   JT start: test 1 n 2 demo 0 shots 2 script R30
 *   JT shot: SHOT1.PPM at tick 10
 *   JT FAIL: dot at 92
 *   JT end: 150 frames in 33151 ms = 4.5 fps
 *   JT done: 0 failures (3 checks)
 */
#ifndef JRTEST_H
#define JRTEST_H

#include "jrgfx.h"

#define JT_SHOTS      16   /* ticks in /SHOT: at most                    */
#define JT_SCRIPT_MAX 255  /* characters of script kept                  */
#define JT_KEYS       8    /* keys held at once                          */

extern int jt_on;          /* 1: the program must run and end by itself   */
extern int jt_test;        /* /TEST given                                 */
extern int jt_n;           /* the n of /TEST:n or /TEST n, else 0         */
extern unsigned jt_demo;   /* /DEMO:n: end after n ticks; 0 = no count    */
extern int jt_bench;       /* /BENCH[:n]                                  */
extern char jt_script[JT_SCRIPT_MAX + 1]; /* the key script, "" if none   */
extern unsigned jt_limit;  /* a run never lasts more ticks (3000)         */
extern unsigned jt_ticks;  /* ticks so far (calls of jt_tick that went on) */
extern unsigned jt_frames; /* frames so far (jt_frame)                    */
extern int jt_fails, jt_checks;

/* Takes the arguments above out of argv and returns the new argc, so the
 * game's own argument loop only meets what is left. With one of them it
 * also deletes the old LOG.TXT. Call it first in main. */
int  jt_args(int argc, char **argv);

/* The script, for a game that has a default or builds one ("" or 0: none).
 * Returns 0, or -1 if it was cut at JT_SCRIPT_MAX (counted as a failure). */
int  jt_set_script(const char *s);

/* What a single letter in a script presses (scan code, KEY_*). */
void jt_map(int letter, int scancode);

/* Called for "!c" in a script, with c in upper case: a game's own test
 * commands (win the level, set up a board). Null by default. */
extern void (*jt_cmd_hook)(int c);

/* Logs the start line and starts the clock. Call it when the game is set
 * up and about to run; jt_tick does it if you did not. */
void jt_begin(void);

/* Once per game tick, before the tick. Presses and releases the script's
 * keys, takes the screenshots that are due, and returns nonzero when the
 * run is over: the script ended, the /DEMO count is reached, or jt_limit
 * ticks have passed (logged, and counted as a failure: a test must end by
 * its script or count, the limit is only there so that nothing can hang).
 * At the end it saves SHOT.PPM, lets go of every key and logs "JT end".
 * It goes on returning nonzero afterwards. Returns 0 when jt_on is 0. */
int  jt_tick(void);

/* Once per frame drawn, if frames and ticks are not the same thing. If it
 * is never called, the end line counts ticks. */
void jt_frame(void);

/* Screenshot of what is on screen now, as SHOT<n>.PPM with the next free
 * n (fx_screenshot on the fast path, after waiting for the flip; else
 * gfx_screenshot, the back buffer). Returns 0, or -1 (text mode, disk).
 * The time it takes is not counted in the end line. */
int  jt_shot(void);

/* Frames per second so far, times 10 (45 = 4.5 fps). */
unsigned jt_fps10(void);

/* Counts a check; logs "JT FAIL: what" if cond is 0. Returns cond. */
int  jt_check(int cond, const char *what);

/* Ends the run if jt_tick has not (the game stopped by itself), logs
 * "JT done: N failures (M checks)" and returns N (255 at most), for use
 * as the exit code. Call it while the graphics are still up. */
int  jt_end(void);

#define jt_log dbg_log     /* one line to LOG.TXT, "T=<ms> text" */

#endif
