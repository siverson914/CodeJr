/* TJT: the test harness (jrtest.h) used the way a game uses it. A dot on
 * the fast path moves 1 pixel per tick while LEFT or RIGHT is down. The
 * key script drives it through key_feed, two screenshots are taken, and
 * the dot's final place is checked.
 *   TJT              (no arguments) runs the built-in test, same as:
 *   TJT /TEST:1 R30 10 L10 J !B {RIGHT}5 /SHOT:15,40
 *   TJT /TEST:2      the harness's own edges (arguments, bad scripts)
 * Ends with "JT done: 0 failures" in LOG.TXT and exit code 0. */
#include <stdio.h>
#include <string.h>
#include "../jrtest.h"

static int x = 80, taps, tap_was, bonus;

static void cmd(int c) { if (c == 'B') bonus++; }

static void tick(void)
{
	if (key_down[KEY_LEFT] && x > 0) x--;
	if (key_down[KEY_RIGHT] && x < 159) x++;
	if (key_down[KEY_SPACE] && !tap_was) taps++;
	tap_was = key_down[KEY_SPACE];
}

static void frame(void)
{
	char s[24];   /* "X=" + 3 digits + " TICK " + 5 digits = 17 */
	fx_begin();
	fx_clear(FX_SOLID2(1));
	fx_fill_rect(x - 2, 98, 5, 5, FX_SOLID2(14));
	sprintf(s, "X=%d TICK %u", x, jt_ticks);
	fx_text(4, 4, s, 15);
	fx_flip();
	jt_frame();
}

/* /TEST:2: things a game never does on purpose. Runs before the real
 * arguments are read (jt_args starts LOG.TXT again), so the results wait
 * in e[] and are checked at the end. */
static int e[5];
static void edges(void)
{
	static char long_script[300];
	static char *av[] = { "TJT", "-test", "7", "r5", "KEEP", "/demo:99999999",
	                      "/Shot:1,,2,x3", "/BENCHX", "/bench:4", 0 };
	int ac, f;
	ac = jt_args(9, av);
	e[0] = ac == 3 && !strcmp(av[1], "KEEP") && !strcmp(av[2], "/BENCHX");
	e[1] = jt_n == 7 && !strcmp(jt_script, "r5");
	e[2] = jt_demo == 60000u && jt_bench == 4;
	memset(long_script, 'R', sizeof long_script - 1);
	f = jt_fails;
	e[3] = jt_set_script(long_script) == -1 && strlen(jt_script) == JT_SCRIPT_MAX;
	e[4] = jt_fails == f + 1;
	jt_fails = f;   /* that failure was wanted */
}

int main(int argc, char **argv)
{
	static char *def[] = { "TJT", "/TEST:1", "R30 10 L10 J !B {RIGHT}5", "/SHOT:15,40", 0 };
	int bad, edge;
	if (argc < 2) { argc = 4; argv = def; }
	argc = jt_args(argc, argv);
	edge = jt_n == 2;
	if (edge) { edges(); argc = jt_args(4, def); }
	if (fx_init(0)) { dbg_log("fx_init failed %d", gfx_err); printf("no graphics\n"); return 1; }
	key_init();
	jt_cmd_hook = cmd;
	dbg_log("TJT pages %d irq %d args left %d", fx_pages, fx_irq, argc - 1);
	jt_begin();
	while (!key_down[KEY_ESC] && !jt_tick()) { tick(); frame(); }
	/* R30 10 L10 J !B {RIGHT}5: 80 + 30 - 10 + 5 = 105, in 56 ticks */
	jt_check(x == 105, "dot at 105");
	jt_check(jt_ticks == 56, "56 ticks");
	jt_check(taps == 1 && bonus == 1, "one tap, one !B");
	jt_check(!key_down[KEY_RIGHT] && !key_down[KEY_LEFT] && !key_down[KEY_SPACE], "keys let go");
	dbg_log("TJT x %d ticks %u taps %d bonus %d fps10 %u", x, jt_ticks, taps, bonus, jt_fps10());
	if (edge) {
		jt_check(e[0], "edges: unknown arguments kept");
		jt_check(e[1], "edges: -test 7 r5");
		jt_check(e[2], "edges: demo saturates, bench:4");
		jt_check(e[3], "edges: long script cut at 255");
		jt_check(e[4], "edges: the cut counts as a failure");
	}
	bad = jt_end();
	gfx_done();
	printf("TJT: %d failures\n", bad);
	return bad;
}
