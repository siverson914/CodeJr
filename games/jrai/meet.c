/* Part 4: the PCjr meets the AI.
 *
 * A 1984 PCjr (monitor, system unit, chiclet keyboard) and a robot face
 * under the stars, talking in a speech box at the top. The scene is drawn
 * once on both pages; each frame redraws only what moves: the box, the
 * robot's eyes, mouth and antenna, the PCjr's screen, and a heart. The
 * robot's first line is spoken aloud, with its LED mouth following the
 * voice; after that the mouth follows the music.
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
	fx_fill_rect(99, 112, 42, 24, S2(SHADE));
	fx_fill_rect(101, 114, 38, 20, S2(DARK));
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

static void robot_mouth(int level)  /* 0-15 */
{
	int i;
	fx_fill_rect(101, 114, 38, 20, S2(DARK));
	if (level <= 0) { fx_fill_rect(103, 123, 34, 2, S2(GREEN)); return; }
	for (i = 0; i < 8; i++) {
		/* bars: tallest in the middle, jittered */
		int shape = 4 - (i < 4 ? 3 - i : i - 4);
		int h = level * (shape + 2) / 6 + ((sin8[(i * 50 + level * 9) & 255] + 128) >> 6);
		if (h > 9) h = 9;
		if (h < 1) h = 1;
		fx_fill_rect(103 + i * 4 + (i >= 4), 124 - h, 3, h * 2, S2(GREEN));
		fx_fill_rect(103 + i * 4 + (i >= 4), 124 - h, 3, 1, S2(YELLOW));
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

typedef struct { unsigned at; char who; const char *a, *b; } line;
#define P PAT_TICKS
static const line script[] = {
	{ 0,           'A', "I HAVE BEEN WAITING", "FOR 42 YEARS." },
	{ P * 3 / 2,   'J', "WHO ARE YOU?", "" },
	{ P * 5 / 2,   'A', "I AM AN AI.", "I WRITE CODE." },
	{ P * 4,       'J', "I HAVE 128K OF RAM", "AND 16 COLORS." },
	{ P * 11 / 2,  'A', "I KNOW. I WROTE", "THIS DEMO ON YOU." },
	{ P * 7,       'J', "ON ME? IN 2026?", "" },
	{ P * 17 / 2,  'A', "YOU STILL GOT IT,", "KID." },
	{ P * 10,      'J', "FRIENDS?", "" },
	{ P * 21 / 2,  'A', "FRIENDS.", "" },
	{ 0xFFFF, 0, 0, 0 },
};

static int line_at(unsigned t)
{
	int i = 0;
	while (script[i + 1].at <= t) i++;
	return i;
}

static void box(unsigned t, int full)
{
	int i = line_at(t), n, tail;
	const line *l = &script[i];
	char a[24], b[24];
	int c = l->who == 'A' ? CYAN : YELLOW;
	n = full ? 99 : (int)((t - l->at) / 2);
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
	box(0, 1);
}


/* What each page shows now, so a frame redraws only what changed. */
enum { C_BOX, C_ANT, C_EYES, C_SCREEN, C_MOUTH, C_HEART, NC };
static int shown[2][NC];
#define CHANGED(id, v) (shown[pg][id] != (v) ? (shown[pg][id] = (v), 1) : 0)

static void mouth_cb(int level)
{
	fx_target(page_seg[0]); robot_mouth(level);
	fx_target(page_seg[1]); robot_mouth(level);
	fx_target(0);
}


static void meet_enter(void)
{
	set_pal(pal_meet);
	fx_pal_anim(0, STAR1, STAR3, 7, 1);
	fx_pal_anim(1, EYE, EYE3, 9, 1);
	both_pages(scene);
	say(&sp_wait, mouth_cb);
	memset(shown, 0xFF, sizeof shown); /* -1: redraw everything once */
}

static void meet_frame(unsigned t)
{
	int i = line_at(t), pg = fx_seg == page_seg[1];
	char who = script[i].who;
	int len = (int)(strlen(script[i].a) + strlen(script[i].b));
	int n = (int)((t - script[i].at) / 2);
	int typing = n < len;
	int lv;
	if (CHANGED(C_BOX, i * 64 + (typing ? n : 63))) box(t, 0);
	if (CHANGED(C_ANT, (int)(t / 15) & 1)) antenna(t);
	if (CHANGED(C_EYES, who == 'J' || i == 0 ? -1 : 0)) robot_eyes(shown[pg][C_EYES]);
	if (who == 'J' && typing) { shown[pg][C_SCREEN] = -1; pcjr_screen(t, 1); }
	else if (CHANGED(C_SCREEN, (int)(t / 20) & 1)) pcjr_screen(t, 0);
	if (who == 'A' && typing) lv = 6 + ((sin8[(t * 37) & 255] + 128) >> 5);
	else lv = mus_level[2] > 0 ? mus_level[1] / 4 : 0; /* humming along */
	if (CHANGED(C_MOUTH, lv)) robot_mouth(lv);
	/* "FRIENDS?" "FRIENDS.": a heart rises between them */
	if (t >= P * 10 + 30) {
		int y = 70 - (int)((t - (P * 10 + 30)) / 4);
		if (y < 50) y = 50;
		if (CHANGED(C_HEART, y)) {
			fx_fill_rect(66, y, 14, 24, S2(BLACK));
			draw_heart(66, y, RED);
		}
	}
}

static void meet_leave(void)
{
	say(&sp_friends, mouth_cb);
	fx_pal_anim(0, 0, 0, 0, 0);
	fx_pal_anim(1, 0, 0, 0, 0);
}

const part part_meet = { "meeting", 0, meet_enter, meet_frame, meet_leave, 24, 12, 0 };
