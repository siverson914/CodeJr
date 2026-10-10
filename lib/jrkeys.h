/* JRKEYS: keys for games. Part of JRGFX; include this next to jrgfx.h
 * and the build links it.
 *
 * key_down[] says what is held right now. A game that looks once per
 * tick needs two things more, and this layer adds exactly those:
 *
 *  - A tap that is over before the tick still counts, for one tick
 *    (at 5 to 10 frames a second a quick tap is easily shorter than a
 *    frame).
 *  - Of the keys in a group (left/right) that are held together, the
 *    one pressed last is the one that counts (jk_best).
 *
 * A held key is held for as long as the finger is on it, whatever else
 * is pressed or let go meanwhile. Nothing here guesses that a key was
 * let go.
 *
 * Use:
 *   key_init(); jk_init();
 *   kl = jk_add(KEY_LEFT,  KEY_A, 1, 0);      group 1
 *   kr = jk_add(KEY_RIGHT, KEY_D, 1, 0);      (0 = no second key)
 *   kf = jk_add(KEY_SPACE, KEY_ENTER, 0, 0);  group 0 = no group
 *   main loop: jk_poll();      each game tick: jk_tick();
 *   jk_down(kl)   held this tick (a tap counts for one tick)
 *   jk_hit(kl)    pressed since the last tick (the keyboard's own
 *                 repeats of a held key are not presses)
 *   jk_best(1)    the key of group 1 pressed last among those held
 *                 (-1: of any group), or -1
 * Test scripts press keys with key_feed(), which this layer sees like
 * the real keyboard.
 *
 * History, because the first version of this did much more. It was
 * written (by CodeJr, 2026-10-09) on the theory that the PCjr keyboard
 * loses a key's release code now and then, and it repaired that: a hold
 * limit per key, letting go of one key of a group let go of all, a key
 * that stopped repeating counted as let go. Then apps/keytest measured
 * the real keyboard (731 codes, text mode and under game conditions):
 * it loses nothing. Every one of those repairs could only drop a key
 * that was really held, so they are gone. jk_add still takes the old
 * fourth argument (the hold limit) and ignores it, so older sources
 * compile.
 */
#ifndef JRKEYS_H
#define JRKEYS_H

#include "jrgfx.h"

#define JK_MAX   16

void jk_init(void);                 /* forgets all keys; call after key_init */
int  jk_add(int sc, int sc2, int grp, int unused); /* a key's number, or -1 if full */
void jk_poll(void);                 /* often, from the main loop: it is cheap */
void jk_tick(void);                 /* once per game tick, before reading keys */
int  jk_down(int i);
int  jk_hit(int i);
int  jk_best(int grp);
void jk_done(void);                 /* nothing to undo; kept for older games */
extern int jk_repeats;              /* 1 once the keyboard was seen to repeat a held key */

#endif
