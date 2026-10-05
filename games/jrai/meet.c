/* Part 4: the PCjr meets the AI.
 *
 * A 1984 PCjr (monitor, system unit, chiclet keyboard) and a robot face
 * under the stars, talking in a speech box at the top. The scene is drawn
 * once on both pages; each frame redraws only what moves: the box, the
 * robot's eyes, mouth and antenna, the PCjr's screen, and a heart. Every
 * line is spoken (voice.c): while the robot talks its LED mouth follows
 * the voice, while the PCjr talks a scope on its screen does.
 */
#include <dos.h>
#include <string.h>
#include "jrai.h"

/* logical colors */
enum { BLACK, STAR1, STAR2, STAR3, CASE, SHADE, DARK, WHITE, NAVY, GREEN,
	EYE, EYE2, EYE3, YELLOW, CYAN, RED };
static const unsigned char pal_meet[16] = {
	0, 8, 7, 15,   /* stars (rotating) */
	7, 8, 0, 15,   /* case, shade, (unused), white */
	1, 10,         /* PCjr screen, its text */
	3, 11, 15,     /* robot eye glow (rotating) */
	14, 11, 12 };  /* yellow, light cyan, light red */

#define S2(c) FX_SOLID2(c)

static void ellipse(int cx, int cy, int rx, int ry, unsigned pat2)
{
	int xy[32], i;
	for (i = 0; i < 16; i++) {
		xy[i * 2] = cx + (sin8[(i * 16 + 64) & 255] * rx + 63) / 127;
		xy[i * 2 + 1] = cy + (sin8[i * 16] * ry + 63) / 127;
	}
	fx_poly(16, xy, pat2);
}

/* ---- the PCjr ------------------------------------------------------------ */

static void draw_pcjr(void)
{
	int x, y;
	/* monitor */
	fx_fill_rect(8, 94, 58, 54, S2(CASE));
	fx_fill_rect(8, 146, 58, 2, S2(SHADE));
	fx_fill_rect(12, 98, 50, 42, S2(SHADE));
	fx_fill_rect(15, 101, 44, 36, S2(NAVY));
	fx_fill_rect(56, 142, 4, 2, S2(GREEN));          /* power light */
	fx_fill_rect(30, 148, 14, 3, S2(SHADE));         /* stand */
	/* system unit */
	fx_fill_rect(4, 151, 66, 18, S2(CASE));
	fx_fill_rect(4, 167, 66, 2, S2(SHADE));
	fx_fill_rect(9, 155, 26, 2, S2(DARK));           /* diskette slot */
	fx_fill_rect(9, 159, 6, 3, S2(SHADE));           /* drive button */
	fx_fill_rect(42, 154, 10, 4, S2(DARK));          /* cartridge slots */
	fx_fill_rect(55, 154, 10, 4, S2(DARK));
	fx_fill_rect(42, 162, 4, 2, S2(NAVY));           /* IBM blue */
	/* the chiclet keyboard (no cable: it's infrared) */
	fx_fill_rect(10, 175, 54, 14, S2(CASE));
	fx_fill_rect(10, 188, 54, 2, S2(SHADE));
	for (y = 177; y < 187; y += 3)
		for (x = 12 + ((y / 3) & 1); x < 61; x += 4)
			fx_fill_rect(x, y, 2, 2, S2(SHADE));
}

/* while its line is spoken: a scope trace as tall as the voice is loud */
static void pcjr_talk(unsigned phase, int level)
{
	unsigned char far *v = (unsigned char far *)MK_FP(fx_seg, 0);
	int b, y, lv = level > 15 ? 15 : level;
	for (y = 101; y < 137; y++) _fmemset(v + jr_rowoff[y] + 8, NAVY * 0x11, 21);
	for (b = 9; b < 28; b++) {
		y = 118 + sin8[(b * 24 + phase * 16) & 255] * lv / 127;
		v[jr_rowoff[y] + b] = v[jr_rowoff[y + 1] + b] = GREEN * 0x11;
	}
}

static void pcjr_screen(unsigned t, int talking)
{
	static const char *const faces[3] = { "C>", "C>", "C>" };
	fx_fill_rect(15, 101, 44, 36, S2(NAVY));
	if (talking) {
		/* a little oscilloscope: the PCjr is talking */
		int x, y0 = 119;
		for (x = 17; x < 57; x += 2) {
			int y = y0 + sin8[(x * 12 + t * 16) & 255] / 16;
			fx_fill_rect(x, y, 2, 2, S2(GREEN));
		}
	} else {
		fx_text(17, 104, faces[0], GREEN);
		if ((t / 20) & 1) fx_fill_rect(33, 110, 6, 2, S2(GREEN));
	}
}

/* ---- the robot ----------------------------------------------------------- */

#define HX0 88
#define HX1 152
#define HY0 50
#define HY1 150

static void draw_robot(void)
{
	int y;
	/* ears */
	fx_fill_rect(83, 82, 6, 30, S2(SHADE));
	fx_fill_rect(151, 82, 6, 30, S2(SHADE));
	fx_fill_rect(80, 92, 3, 10, S2(CASE));
	fx_fill_rect(157, 92, 3, 10, S2(CASE));
	/* head: a metal box with bevelled corners */
	{
		int xy[16] = { HX0 + 4, HY0, HX1 - 4, HY0, HX1, HY0 + 6, HX1, HY1 - 6,
			HX1 - 4, HY1, HX0 + 4, HY1, HX0, HY1 - 6, HX0, HY0 + 6 };
		fx_poly(8, xy, FX_DITH(CASE, SHADE));
	}
	fx_fill_rect(HX0 + 4, HY0 + 2, HX1 - HX0 - 8, 2, S2(WHITE));      /* shine */
	/* forehead plate */
	fx_fill_rect(106, 56, 28, 10, S2(SHADE));
	fx_text(112, 57, "AI", CYAN);
	/* eye sockets */
	ellipse(105, 84, 8, 13, S2(DARK));
	ellipse(135, 84, 8, 13, S2(DARK));
	/* mouth frame */
	fx_fill_rect(100, 112, 40, 24, S2(SHADE));
	fx_fill_rect(102, 114, 36, 20, S2(DARK));
	/* bolts */
	for (y = HY0 + 10; y < HY1 - 6; y += 30) {
		fx_fill_rect(HX0 + 3, y, 2, 2, S2(WHITE));
		fx_fill_rect(HX1 - 5, y, 2, 2, S2(WHITE));
	}
	/* neck */
	fx_fill_rect(108, HY1, 24, 12, S2(SHADE));
	for (y = HY1 + 2; y < HY1 + 12; y += 3) fx_fill_rect(108, y, 24, 1, S2(DARK));
	fx_fill_rect(92, HY1 + 12, 56, 6, FX_DITH(CASE, SHADE));
}

static void robot_eyes(int look)  /* look: -1 left (at the PCjr), 0 ahead */
{
	ellipse(105, 84, 6, 10, S2(EYE));
	ellipse(135, 84, 6, 10, S2(EYE));
	fx_fill_rect(103 + look * 3, 80, 4, 6, S2(WHITE));
	fx_fill_rect(133 + look * 3, 80, 4, 6, S2(WHITE));
}

/* The LED mouth, written straight into fx_seg (no library calls: it's
 * redrawn between chunks of speech, so it has to be quick). 8 bars, one
 * byte (2 pixels) wide, in the dark area bytes 51-68, rows 114-133. */
static void robot_mouth(int level)  /* 0-15 */
{
	static const unsigned char col[8] = { 1, 3, 5, 7, 10, 12, 14, 16 };
	unsigned char far *v = (unsigned char far *)MK_FP(fx_seg, 0);
	int h[8], i, y;
	for (i = 0; i < 8; i++) {
		/* bars: tallest in the middle, jittered */
		int shape = 4 - (i < 4 ? 3 - i : i - 4);
		h[i] = level * (shape + 2) / 6 + ((sin8[(i * 50 + level * 9) & 255] + 128) >> 6);
		if (h[i] > 9) h[i] = 9;
		if (h[i] < 1) h[i] = 1;
	}
	for (y = 114; y < 134; y++) {
		unsigned char far *r = v + jr_rowoff[y] + 51;
		_fmemset(r, DARK * 0x11, 18);
		if (level <= 0) {
			if (y == 123 || y == 124) _fmemset(r + 1, GREEN * 0x11, 16);
			continue;
		}
		for (i = 0; i < 8; i++)
			if (y >= 124 - h[i] && y < 124 + h[i])
				r[col[i]] = (unsigned char)((y == 124 - h[i] ? YELLOW : GREEN) * 0x11);
	}
}

static void antenna(unsigned t)
{
	fx_fill_rect(119, 42, 2, 8, S2(SHADE));
	ellipse(120, 40, 3, 4, S2(((t / 15) & 1) ? RED : SHADE));
}

/* ---- stars, the box, the heart ------------------------------------------- */

static void stars(void)
{
	int i;
	rnd_seed(1984); /* the same stars on both pages */
	for (i = 0; i < 70; i++) {
		unsigned r = rnd();
		fx_pset(r % 160, 52 + (r >> 8) % 130, 1 + (i % 3));
	}
}

/* The dialog. Each line is typed into the box, then the music drops out
 * and the line is spoken (NAME.SND), then the music picks up again. */
typedef struct { char who; const char *a, *b, *clip; } line;
static const line script[] = {
	{ 'A', "I HAVE BEEN WAITING", "FOR 42 YEARS.", "JM1" },
	{ 'J', "WHO ARE YOU?", "", "JM2" },
	{ 'A', "I AM AN AI.", "I WRITE CODE.", "JM3" },
	{ 'J', "I HAVE 128K OF RAM", "AND 16 COLORS!", "JM4" },
	{ 'A', "I KNOW. I WROTE", "THIS DEMO ON YOU.", "JM5" },
	{ 'J', "ON ME? IN 2026?", "", "JM6" },
	{ 'A', "YOU STILL GOT IT,", "KID.", "JM7" },
	{ 'J', "FRIENDS?", "", "JM8" },
	{ 'A', "FRIENDS.", "FOREVER.", "JM9" },
};
#define NLINES (sizeof script / sizeof script[0])
#define HEART_LINE 8   /* the heart rises from this line on */

static void box(int i, int n) /* line i, its first n letters */
{
	const line *l = &script[i];
	char a[24], b[24];
	int c = l->who == 'A' ? CYAN : YELLOW, tail;
	strncpy(a, l->a, sizeof a); strncpy(b, l->b, sizeof b);
	a[23] = b[23] = 0;
	if (n < (int)strlen(a)) { a[n] = 0; b[0] = 0; }
	else if (n - (int)strlen(a) < (int)strlen(b)) b[n - strlen(a)] = 0;
	fx_fill_rect(0, 0, 160, 39, S2(BLACK));
	fx_fill_rect(2, 2, 156, 28, S2(c));
	fx_fill_rect(4, 4, 152, 24, S2(BLACK));
	tail = l->who == 'A' ? 130 : 36;
	{
		int xy[6];
		xy[0] = tail; xy[1] = 29; xy[2] = tail + 10; xy[3] = 29;
		xy[4] = l->who == 'A' ? tail + 2 : tail + 8; xy[5] = 38;
		fx_poly(3, xy, S2(c));
	}
	fx_text(8, 7, a, c);
	fx_text(8, 18, b, c);
}

static const unsigned char heart[6] = { 0x6C, 0xFE, 0xFE, 0x7C, 0x38, 0x10 };

static void draw_heart(int x, int y, int c)
{
	int r, k;
	for (r = 0; r < 6; r++)
		for (k = 0; k < 7; k++)
			if (heart[r] & (0x80 >> k)) fx_fill_rect(x + k * 2, y + r * 3, 2, 3, S2(c));
}

/* ---- the part ------------------------------------------------------------ */

static void scene(void)
{
	fx_clear(0);
	stars();
	draw_pcjr();
	draw_robot();
	robot_eyes(-1);
	robot_mouth(0);
	antenna(0);
	pcjr_screen(0, 0);
	box(0, 0);
}


/* What each page shows now, so a frame redraws only what changed. */
enum { C_BOX, C_ANT, C_EYES, C_SCREEN, C_MOUTH, C_HEART, NC };
static int shown[2][NC];
#define CHANGED(id, v) (shown[pg][id] != (v) ? (shown[pg][id] = (v), 1) : 0)

/* While a line is spoken nothing flips, so only the page on screen needs
 * the mouth: the one fx_seg isn't. */
static unsigned shown_page(void)
{
	return fx_seg == page_seg[0] ? page_seg[1] : page_seg[0];
}

static void mouth_cb(int level) /* the robot talks: its LED mouth */
{
	int lv = level > 15 ? 15 : level;
	fx_target(shown_page()); robot_mouth(lv); fx_target(0);
}

static unsigned jr_phase;
static void jr_cb(int level) /* the PCjr talks: a scope on its screen */
{
	jr_phase += 3;
	fx_target(shown_page()); pcjr_talk(jr_phase, level); fx_target(0);
}

static int cur, spoken;            /* the line, and whether it has been said */
static unsigned t_line, t_said;    /* when it started, when it was said */

static void meet_enter(void)
{
	set_pal(pal_meet);
	fx_pal_anim(0, STAR1, STAR3, 7, 1);
	fx_pal_anim(1, EYE, EYE3, 9, 1);
	both_pages(scene);
	memset(shown, 0xFF, sizeof shown); /* -1: redraw everything once */
	cur = 0; spoken = 0; t_line = 0;
}

/* both pages show this line in full, then it's spoken */
static void say_line(int i)
{
	const line *l = &script[i];
	int k;
	for (k = 0; k < 2; k++) {
		fx_target(page_seg[k]);
		box(i, 99);
		robot_eyes(l->who == 'J' || i == 0 ? -1 : 0);
		shown[k][C_BOX] = -1; shown[k][C_EYES] = -1; shown[k][C_MOUTH] = -1; shown[k][C_SCREEN] = -1;
	}
	fx_target(0);
	if (!voice_say(l->clip, l->who == 'A' ? mouth_cb : jr_cb))
		t_said += (unsigned)(strlen(l->a) + strlen(l->b)) * 4; /* no clip: time to read it */
}

static void meet_frame(unsigned t)
{
	const line *l = &script[cur];
	int pg = fx_seg == page_seg[1];
	char who = l->who;
	int len = (int)(strlen(l->a) + strlen(l->b));
	int n = (int)((t - t_line) / 2);
	int typing = n < len;
	int lv;
	if (!typing && !spoken && t - t_line >= (unsigned)len * 2 + 10) {
		t_said = t;
		say_line(cur);
		spoken = 1;
		return;
	}
	if (spoken && t - t_said >= 45) {
		if (cur + 1 < (int)NLINES) { cur++; spoken = 0; t_line = t; return; }
		if (t - t_said >= 150) { part_done = 1; return; }
	}
	if (CHANGED(C_BOX, cur * 64 + (typing ? n : 63))) box(cur, typing ? n : 99);
	if (CHANGED(C_ANT, (int)(t / 15) & 1)) antenna(t);
	if (CHANGED(C_EYES, who == 'J' || cur == 0 ? -1 : 0)) robot_eyes(shown[pg][C_EYES]);
	if (who == 'J' && typing) { shown[pg][C_SCREEN] = -1; pcjr_screen(t, 1); }
	else if (CHANGED(C_SCREEN, (int)(t / 20) & 1)) pcjr_screen(t, 0);
	lv = mus_level[2] > 0 ? mus_level[1] / 4 : 0; /* humming along to the music */
	if (CHANGED(C_MOUTH, lv)) robot_mouth(lv);
	/* "FRIENDS?" "FRIENDS.": a heart rises between them */
	if (cur >= HEART_LINE) {
		static unsigned t_heart;
		int y;
		if (cur == HEART_LINE && !spoken && t == t_line) t_heart = t;
		y = 70 - (int)((t - t_heart) / 4);
		if (y < 50) y = 50;
		if (CHANGED(C_HEART, y)) {
			fx_fill_rect(66, y, 14, 24, S2(BLACK));
			draw_heart(66, y, RED);
		}
	}
}

static void meet_leave(void)
{
	fx_pal_anim(0, 0, 0, 0, 0);
	fx_pal_anim(1, 0, 0, 0, 0);
}

const part part_meet = { "meeting", 0, meet_enter, meet_frame, meet_leave, 24, 30, 0 };
