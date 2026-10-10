/* JRTEST: self-running test mode for JRGFX games. See jrtest.h and
 * TESTING.md. Open Watcom, 16-bit DOS, large model, 8088, no floating
 * point.
 *
 * Everything that comes from the command line is hostile: numbers
 * saturate (num), the script is copied with a cap (jt_set_script), key
 * names are compared without being copied, and the one sprintf here
 * (shot_name) prints a number that is at most 99 into 16 bytes.
 */
#include <stdio.h>
#include <string.h>
#include "jrtest.h"

int jt_on, jt_test, jt_n, jt_bench;
unsigned jt_demo;
char jt_script[JT_SCRIPT_MAX + 1];
unsigned jt_limit = 3000;
unsigned jt_ticks, jt_frames;
int jt_fails, jt_checks;
void (*jt_cmd_hook)(int c);

static unsigned shot_at[JT_SHOTS];
static int nshots;            /* entries of shot_at                       */
static int shot_n;            /* SHOT<n>.PPM files written                */
static int begun, ended;
static unsigned long t_start; /* tmr_ms at jt_begin                       */
static unsigned long t_skip;  /* ms spent writing screenshots             */
static unsigned long t_run;   /* ms of the run, set when it ends          */

/* script player */
static const char *sp;                 /* next character of jt_script      */
static unsigned left;                  /* ticks left of the current group  */
static unsigned char held[JT_KEYS];    /* scan codes the script holds down */
static int nheld;
static int sdone;                      /* the script has ended             */

/* What a single letter presses. Letter keys by default, except L R U D
 * (arrows) and J (space bar): the letters the games' own scripts used. */
static unsigned char letter_sc[26] = {
	0x1E, 0x30, 0x2E, KEY_DOWN, 0x12, 0x21, 0x22, 0x23, 0x17,      /* A-I */
	KEY_SPACE, 0x25, KEY_LEFT, 0x32, 0x31, 0x18, 0x19, 0x10,       /* J-Q */
	KEY_RIGHT, 0x1F, 0x14, KEY_UP, 0x2F, 0x11, 0x2D, 0x15, 0x2C    /* R-Z */
};
/* The letter keys themselves, for {A}..{Z}. */
static const unsigned char alpha_sc[26] = {
	0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
	0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,
	0x15, 0x2C
};
static const struct { const char *name; unsigned char sc; } names[] = {
	{"ESC", KEY_ESC}, {"ENTER", KEY_ENTER}, {"SPACE", KEY_SPACE},
	{"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT},
	{"RIGHT", KEY_RIGHT}, {"CTRL", KEY_CTRL}, {"ALT", KEY_ALT},
	{"SHIFT", KEY_LSHIFT}, {"TAB", 0x0F}, {"BKSP", 0x0E}
};

static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int digit(int c) { return c >= '0' && c <= '9'; }

/* Decimal number at *p, moved past ALL its digits; never more than max. */
static unsigned num(const char **p, unsigned max)
{
	unsigned long v = 0;
	while (digit(**p)) {
		if (v <= max) v = v * 10 + (unsigned)(**p - '0');
		(*p)++;
	}
	return v > max ? max : (unsigned)v;
}

/* Does a start with the word w (upper case), in any case? Returns the
 * rest of a, or 0. */
static const char *word(const char *a, const char *w)
{
	for (; *w; a++, w++)
		if (up(*a) != *w) return 0;
	return a;
}

static int all_digits(const char *a)
{
	if (!*a) return 0;
	for (; *a; a++)
		if (!digit(*a)) return 0;
	return 1;
}

/* ---- Arguments --------------------------------------------------------- */

int jt_set_script(const char *s)
{
	unsigned n = s ? strlen(s) : 0;
	int cut = n > JT_SCRIPT_MAX;
	if (cut) n = JT_SCRIPT_MAX;
	if (n) memcpy(jt_script, s, n);
	jt_script[n] = 0;
	sp = jt_script; left = 0; sdone = 0;
	if (cut) {
		jt_fails++;
		dbg_log("JT FAIL: script cut at %d characters", JT_SCRIPT_MAX);
	}
	return cut ? -1 : 0;
}

void jt_map(int letter, int scancode)
{
	letter = up(letter);
	if (letter >= 'A' && letter <= 'Z' && scancode > 0 && scancode < 128)
		letter_sc[letter - 'A'] = (unsigned char)scancode;
}

int jt_args(int argc, char **argv)
{
	int i, o = 1, cut = 0;
	const char *a, *r;
	jt_test = jt_n = jt_bench = nshots = 0;
	jt_demo = 0;
	jt_set_script(0);
	for (i = 1; i < argc; i++) {
		a = argv[i];
		r = 0;
		if (*a == '/' || *a == '-') {
			if ((r = word(a + 1, "TEST")) != 0 && (*r == 0 || *r == ':')) {
				jt_test = 1;
				if (*r == ':') { r++; jt_n = (int)num(&r, 32767); }
				else if (i + 1 < argc && all_digits(argv[i + 1])) {
					r = argv[++i];
					jt_n = (int)num(&r, 32767);
				}
				/* the next argument is the script unless it is a switch */
				if (i + 1 < argc && argv[i + 1][0] != '/' && argv[i + 1][0] != '-')
					cut = jt_set_script(argv[++i]);
			} else if ((r = word(a + 1, "DEMO")) != 0 && (*r == 0 || *r == ':')) {
				jt_demo = 300;
				if (*r == ':') { r++; jt_demo = num(&r, 60000u); }
			} else if ((r = word(a + 1, "BENCH")) != 0 && (*r == 0 || *r == ':')) {
				jt_bench = 1;
				if (*r == ':') { r++; jt_bench = (int)num(&r, 32767); }
			} else if ((r = word(a + 1, "SHOT:")) != 0) {
				while (*r) {
					if (digit(*r)) {
						unsigned v = num(&r, 60000u);
						if (nshots < JT_SHOTS) shot_at[nshots++] = v;
					} else r++;   /* commas, and anything else */
				}
			} else r = 0;
		}
		if (!r) argv[o++] = argv[i];   /* not ours: the game's */
	}
	jt_on = jt_test || jt_demo || jt_bench;
	if (jt_on) {
		remove("LOG.TXT");
		/* the cut was logged into the old file: say it again */
		if (cut) dbg_log("JT FAIL: script cut at %d characters", JT_SCRIPT_MAX);
	}
	if (o < argc) argv[o] = 0;
	return o;
}

/* ---- Key script -------------------------------------------------------- */

static void script_error(const char *what, int c)
{
	jt_fails++;
	dbg_log("JT FAIL: script: %s '%c' at %d", what, c >= 32 && c < 127 ? c : '?',
	        (int)(sp - jt_script));
}

/* A key name: sp is just past the '{'. Leaves sp past the '}' (or at the
 * end) and returns the scan code, 0 if the name is not known. */
static int named_key(void)
{
	const char *s = sp, *r;
	int i, sc = 0, len = 0;
	while (sp[len] && sp[len] != '}') len++;
	sp += len;
	if (*sp) sp++;
	if (len == 1 && up(*s) >= 'A' && up(*s) <= 'Z') return alpha_sc[up(*s) - 'A'];
	if (len == 1 && digit(*s)) return *s == '0' ? 0x0B : *s - '0' + 1;
	if (*s == '#' && len > 1) {                /* {#59}: scan code in decimal */
		r = s + 1;
		sc = (int)num(&r, 128);
		return r == s + len && sc < 128 ? sc : 0;
	}
	if (up(*s) == 'F' && len > 1) {            /* F1-F10 are 3Bh-44h */
		r = s + 1;
		sc = (int)num(&r, 99);
		return r == s + len && sc >= 1 && sc <= 10 ? 0x3A + sc : 0;
	}
	for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
		if ((r = word(s, names[i].name)) != 0 && r == s + len) return names[i].sc;
	return 0;
}

static int in_set(const unsigned char *set, int n, int sc)
{
	while (n-- > 0)
		if (set[n] == sc) return 1;
	return 0;
}

/* Makes `want` the keys held: lets go of the others first, then presses
 * the new ones. A key in both is left alone, so it stays down. */
static void hold(const unsigned char *want, int n)
{
	int i;
	for (i = 0; i < nheld; i++)
		if (!in_set(want, n, held[i])) key_feed(held[i], 0);
	for (i = 0; i < n; i++)
		if (!in_set(held, nheld, want[i])) key_feed(want[i], 1);
	if (n) memcpy(held, want, n);
	nheld = n;
}

/* Reads the next group and holds its keys. Every pass of the loop takes
 * at least one character, so it ends whatever the script holds. */
static void script_next(void)
{
	unsigned char want[JT_KEYS];
	int n, any, c, sc;
	for (;;) {
		while (*sp == ' ' || *sp == ',' || *sp == '\t') sp++;
		if (!*sp) { hold(want, 0); sdone = 1; return; }
		n = any = 0;
		for (;;) {
			c = up(*sp);
			if (c == '!') {
				sp++;
				if (*sp) { if (jt_cmd_hook) jt_cmd_hook(up(*sp)); sp++; }
				any = 1;
				continue;
			}
			if (c >= 'A' && c <= 'Z') { sp++; sc = letter_sc[c - 'A']; }
			else if (c == '{') {
				sp++;
				sc = named_key();
				if (!sc) script_error("unknown key before", *sp ? *sp : '$');
			} else break;
			any = 1;
			if (sc && !in_set(want, n, sc)) {
				if (n < JT_KEYS) want[n++] = (unsigned char)sc;
				else script_error("more than 8 keys before", *sp ? *sp : '$');
			}
		}
		if (digit(*sp)) left = num(&sp, 60000u);
		else if (n) left = 1;                  /* a tap */
		else {
			left = 0;
			if (!any) { script_error("bad character", *sp); sp++; }
		}
		if (left) { hold(want, n); return; }
	}
}

/* ---- Run --------------------------------------------------------------- */

static unsigned long now_ms(void) { return tmr_ms() - t_start - t_skip; }

static int shot_file(const char *name)
{
	unsigned long t = tmr_ms();
	int r = -1;
	if (fx_seg) { fx_begin(); r = fx_screenshot(name); }   /* the page shown */
	else if (gfx_mode) r = gfx_screenshot(name);           /* the back buffer */
	if (r) dbg_log("JT shot: %s failed at tick %u", name, jt_ticks);
	else dbg_log("JT shot: %s at tick %u", name, jt_ticks);
	t_skip += tmr_ms() - t;
	return r;
}

int jt_shot(void)
{
	char name[16];   /* "SHOT99.PPM" is 10 characters */
	if (shot_n >= 99) return -1;
	sprintf(name, "SHOT%d.PPM", ++shot_n);
	return shot_file(name);
}

void jt_begin(void)
{
	if (begun) return;
	begun = 1;
	if (!sp) jt_set_script(0);
	if (jt_limit > 60000u) jt_limit = 60000u;
	if (jt_on)
		dbg_log("JT start: test %d n %d demo %u shots %d script %s",
		        jt_test, jt_n, jt_demo, nshots, jt_script[0] ? jt_script : "-");
	t_skip = 0;
	t_start = tmr_ms();
}

void jt_frame(void) { jt_frames++; }

static unsigned fps10_of(unsigned frames, unsigned long ms)
{
	/* frames <= 65535, so frames * 10000 fits in 32 bits */
	return ms ? (unsigned)((unsigned long)frames * 10000UL / ms) : 0;
}

unsigned jt_fps10(void)
{
	if (!begun) return 0;
	return fps10_of(jt_frames ? jt_frames : jt_ticks, ended ? t_run : now_ms());
}

static void finish(void)
{
	unsigned f, fps;
	if (ended) return;
	ended = 1;
	t_run = now_ms();
	hold(held, 0);                 /* let go of everything */
	if (jt_on) shot_file("SHOT.PPM");
	f = jt_frames ? jt_frames : jt_ticks;
	fps = fps10_of(f, t_run);
	dbg_log("JT end: %u frames in %lu ms = %u.%u fps", f, t_run, fps / 10, fps % 10);
}

int jt_tick(void)
{
	int i;
	if (!jt_on) return 0;
	if (ended) return 1;
	if (!begun) jt_begin();
	for (i = 0; i < nshots; i++)
		if (shot_at[i] == jt_ticks) jt_shot();
	if (jt_script[0]) {
		if (!left) script_next();
		if (sdone) { finish(); return 1; }
		left--;
	}
	if (jt_demo && jt_ticks >= jt_demo) { finish(); return 1; }
	if (jt_ticks >= jt_limit) {
		jt_fails++;
		dbg_log("JT FAIL: limit of %u ticks reached", jt_limit);
		finish();
		return 1;
	}
	jt_ticks++;
	return 0;
}

int jt_check(int cond, const char *what)
{
	jt_checks++;
	if (!cond) {
		jt_fails++;
		dbg_log("JT FAIL: %s", what ? what : "?");
	}
	return cond;
}

int jt_end(void)
{
	if (!jt_on) return 0;
	if (!begun) jt_begin();
	finish();
	dbg_log("JT done: %d failures (%d checks)", jt_fails, jt_checks);
	return jt_fails > 255 ? 255 : jt_fails < 0 ? 255 : jt_fails;
}
