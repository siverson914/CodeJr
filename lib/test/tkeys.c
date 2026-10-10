/* TKEYS: the key layer (jrkeys.h) against scripted scan codes that
 * behave as the real PCjr keyboard was measured to (apps/keytest): every
 * press has its release, and a held key repeats, the newest one only.
 * A held key must read as held for as long as it is held. Needs no
 * screen and ends by itself. */
#include <stdio.h>
#include "../jrgfx.h"
#include "../jrkeys.h"

static int fails;

static void check(int ok, const char *what)
{
	if (!ok) {
		fails++;
		dbg_log("FAIL: %s", what);
	}
}

static void ticks(int n)
{
	while (n-- > 0)
		jk_tick();
}

int main(void)
{
	int l, r, f, i;

	remove("LOG.TXT");
	key_init();
	jk_init();
	l = jk_add(KEY_LEFT, KEY_A, 1, 0);
	r = jk_add(KEY_RIGHT, KEY_D, 1, 0);
	f = jk_add(KEY_SPACE, 0, 0, 0);
	check(l == 0 && r == 1 && f == 2, "three keys added");

	/* plain press and release */
	key_feed(KEY_RIGHT, 1);
	jk_tick();
	check(jk_down(r) && jk_hit(r) && jk_best(1) == r, "right is down and was hit");
	jk_tick();
	check(jk_down(r) && !jk_hit(r), "held: down, not hit again");
	key_feed(KEY_RIGHT, 0);
	jk_tick();
	check(!jk_down(r), "let go");

	/* a tap that is over before the tick still counts, once */
	key_feed(KEY_SPACE, 1);
	key_feed(KEY_SPACE, 0);
	jk_tick();
	check(jk_down(f) && jk_hit(f), "a tap counts for one tick");
	jk_tick();
	check(!jk_down(f) && !jk_hit(f), "and only one");

	/* two taps inside one tick are one tick's worth, not lost and not two */
	key_feed(KEY_SPACE, 1);
	key_feed(KEY_SPACE, 0);
	key_feed(KEY_SPACE, 1);
	key_feed(KEY_SPACE, 0);
	jk_tick();
	check(jk_hit(f), "two quick taps are seen");
	jk_tick();
	check(!jk_down(f), "and then it is up");

	/* held for a long time, with the keyboard repeating it: held, and
	 * the repeats are not new presses */
	key_feed(KEY_RIGHT, 1);
	jk_tick();
	for (i = 0; i < 400; i++) {
		if (i % 6 == 5)
			key_feed(KEY_RIGHT, 1); /* a repeat */
		jk_tick();
		if (!jk_down(r) || jk_hit(r))
			break;
	}
	check(i == 400, "a key held for 400 ticks stays down and is hit once");
	check(jk_repeats, "the repeats were noticed");

	/* right held, left tapped: left wins while it is down, right is
	 * still held afterwards (it no longer repeats: that means nothing) */
	key_feed(KEY_LEFT, 1);
	jk_tick();
	check(jk_down(l) && jk_down(r) && jk_best(1) == l, "the newest press of a group wins");
	key_feed(KEY_LEFT, 0);
	jk_tick();
	check(!jk_down(l) && jk_down(r) && jk_best(1) == r, "letting go of left leaves right held");
	ticks(200);
	check(jk_down(r), "and it stays held with no repeats at all");
	key_feed(KEY_RIGHT, 0);
	jk_tick();
	check(!jk_down(r) && jk_best(1) == -1, "until it is let go");

	/* the second key of a pair */
	key_feed(KEY_A, 1);
	jk_tick();
	check(jk_down(l) && jk_hit(l), "A is left's second key");
	key_feed(KEY_A, 0);
	jk_tick();
	check(!jk_down(l), "and its release");

	dbg_log("TKEYS done: %d failures", fails);
	printf("TKEYS done: %d failures\n", fails);
	return fails != 0;
}
