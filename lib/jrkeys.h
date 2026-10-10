/* JRKEYS: game keys that survive the PCjr keyboard's lost scan codes.
 * Part of JRGFX; include this next to jrgfx.h and the build links it.
 *
 * The PCjr keyboard sends a scan code when a key goes down and another
 * when it comes up, and now and then one is lost. A lost release leaves
 * key_down[] set forever: the piece slides to the wall, the player runs
 * into it. Games that are played with held keys should read them through
 * this layer, not from key_down[]:
 *
 *  - A tap shorter than a game tick is still seen, for one tick.
 *  - A press is seen even on a "stuck" key, so pressing it again works.
 *  - A key down longer than its limit counts as let go until it is
 *    pressed again.
 *  - Keys in the same group (left/right): the newest press wins, and
 *    letting go of one lets go of them all. A release is nearly always
 *    lost while another key is pressed or let go at the same moment.
 *  - If the keyboard sends repeats while a key is held (the PCjr's does:
 *    after 0.6 s, then every 96 ms, for the newest key only), the newest
 *    key counts as let go once it stops repeating. Found out by itself,
 *    never assumed.
 *
 * Use:
 *   key_init(); jk_init();
 *   kl = jk_add(KEY_LEFT,  KEY_A, 1, 90);     group 1, 90 ticks at most
 *   kr = jk_add(KEY_RIGHT, KEY_D, 1, 90);     (0 = no second key)
 *   kf = jk_add(KEY_SPACE, KEY_ENTER, 0, 30); group 0 = no group
 *   main loop: jk_poll();      each game tick: jk_tick();
 *   jk_down(kl)   held this tick (a tap counts for one tick)
 *   jk_hit(kl)    went down this tick
 *   jk_best(1)    newest held key of group 1 (-1: of any group), or -1
 * A limit of 0 means no limit (a throttle, a fire button held on purpose).
 * Test scripts press keys with key_feed(), which this layer sees like the
 * real keyboard.
 *
 * Where this came from: written by CodeJr on 2026-10-09 for Xonix Junior
 * and Junris after left and right "got stuck"; moved into the library the
 * same day. Its theory was that codes are lost when two arrive close
 * together. apps/keytest on the real PCjr that evening did NOT show that:
 * 400 codes in text mode, 16 ms apart at the closest, none lost. It did
 * show that the keyboard repeats: a held key sends its press again after
 * 0.6 s and then every 96 ms, and only the newest held key does. Whether
 * codes are lost while a game runs is KEYTEST G's question.
 */
#ifndef JRKEYS_H
#define JRKEYS_H

#include "jrgfx.h"

#define JK_MAX   16
#define JK_QUIET 60    /* ticks without a repeat = let go, once repeats are seen */

void jk_init(void);                 /* forgets all keys; call after key_init */
int  jk_add(int sc, int sc2, int grp, int max); /* a key's number, or -1 if full */
void jk_poll(void);                 /* often, from the main loop: it is cheap */
void jk_tick(void);                 /* once per game tick, before reading keys */
int  jk_down(int i);
int  jk_hit(int i);
int  jk_best(int grp);
void jk_done(void);                 /* nothing to undo; kept for older games */
extern int jk_repeats;              /* 1 once the keyboard was seen to repeat */

#endif
