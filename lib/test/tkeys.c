/* TKEYS: the key layer (jrkeys.h) against scripted scan codes, with the
 * failures a PCjr keyboard produces: a release that never arrives, a tap
 * shorter than a tick. Needs no screen and ends by itself. */
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
	l = jk_add(KEY_LEFT, KEY_A, 1, 90);
	r = jk_add(KEY_RIGHT, KEY_D, 1, 90);
	f = jk_add(KEY_SPACE, 0, 0, 30);
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
	jk_poll();
	key_feed(KEY_SPACE, 0);
	jk_tick();
	check(jk_down(f) && jk_hit(f), "a tap counts for one tick");
	jk_tick();
	check(!jk_down(f), "and only one");

	/* the release of right is lost while left is worked */
	key_heal = 0; /* the layer alone, without the interrupt's own repair */
	key_feed(KEY_RIGHT, 1);
	ticks(3);
	key_feed(KEY_LEFT, 1);
	jk_tick();
	check(jk_best(1) == l, "the newest press of a group wins");
	key_feed(KEY_LEFT, 0); /* right's release never comes */
	jk_tick();
	check(key_down[KEY_RIGHT], "key_down still has right as down");
	check(!jk_down(r) && !jk_down(l), "letting go of left frees the group");
	key_feed(KEY_RIGHT, 1); /* pressed again: key_down doesn't change */
	jk_tick();
	check(jk_down(r) && jk_hit(r), "a press on a stuck key is seen");
	key_feed(KEY_RIGHT, 0);
	jk_tick();
	check(!jk_down(r), "and its release");

	/* a lost release with nothing else pressed: the limit ends it */
	key_feed(KEY_SPACE, 1);
	ticks(30);
	check(jk_down(f), "down up to the limit");
	jk_tick();
	check(!jk_down(f) && key_down[KEY_SPACE], "past the limit it counts as let go");
	key_feed(KEY_SPACE, 1);
	jk_tick();
	check(jk_down(f) && jk_hit(f), "until it is pressed again");
	key_feed(KEY_SPACE, 0);
	jk_tick();

	/* the interrupt's own repair, for games that read key_down */
	key_heal = 1;
	key_feed(KEY_RIGHT, 1);
	key_feed(KEY_LEFT, 1);
	key_feed(KEY_LEFT, 0);
	check(!key_down[KEY_RIGHT] && !key_down[KEY_LEFT], "key_heal: an arrow's release frees the opposite arrow");
	key_feed(KEY_A, 1);
	key_feed(KEY_D, 1);
	key_feed(KEY_D, 0);
	check(key_down[KEY_A], "key_heal leaves other keys alone");
	key_feed(KEY_A, 0);

	/* a keyboard that repeats: a key that stops repeating was let go */
	ticks(2);
	key_feed(KEY_RIGHT, 1);
	for (i = 0; i < 4; i++) {
		ticks(5);
		key_feed(KEY_RIGHT, 1);
	}
	jk_tick();
	check(jk_repeats, "repeats noticed");
	check(jk_down(r), "down while it repeats");
	ticks(JK_QUIET + 1);
	check(!jk_down(r), "quiet for long enough counts as let go");

	dbg_log("TKEYS done: %d failures", fails);
	printf("TKEYS done: %d failures\n", fails);
	return fails != 0;
}
