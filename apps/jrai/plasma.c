/* Part 1: palette plasma and the title.
 *
 * The plasma is computed once, in full 160x200, as indexes 1-14 into a
 * cyclic rainbow. The retrace interrupt then rotates palette entries 1-14,
 * which moves every pixel on the screen for the cost of 28 port writes a
 * frame: the CPU draws nothing at all. The title is drawn on top in
 * colors 0 and 15, which don't rotate.
 */
#include <dos.h>
#include <string.h>
#include "jrai.h"

static unsigned buf; /* the plasma, in page layout */

static void plasma_prep(void)
{
	/* color for a plasma value v (-508..508) at each of the 4 dither
	 * thresholds: 48 levels of v, 14 colors, a 2x2 ordered dither between
	 * neighbouring levels */
	static const unsigned char bayer[2][2] = { { 0, 2 }, { 3, 1 } };
	static unsigned char ctab[4][1024];
	static signed char sx[160];
	static unsigned dx2[160];
	int x, y, k;
	if (!sin8[64]) for (x = 0; x < 256; x++) sin8[x] = (signed char)(fix_sin(x * 4) * 127 >> 16);
	buf = jr_alloc_high(0x400);
	if (!buf) return;
	for (k = 0; k < 4; k++)
		for (x = 0; x < 1024; x++) {
			unsigned u = (unsigned)x * 3, lv = u >> 6;
			if (((u >> 4) & 3) > k) lv++;
			ctab[k][x] = (unsigned char)(1 + lv % 14);
		}
	for (x = 0; x < 160; x++) {
		int dx = (x - 80) * 5 / 3; /* a pixel is 5/3 as wide as it is tall */
		dx2[x] = (unsigned)(dx * dx);
		sx[x] = sin8[(x * 5) & 255];
	}
	for (y = 0; y < 200; y++) {
		/* jr_rowoff isn't set yet: this runs before graphics start */
		unsigned char far *row = (unsigned char far *)MK_FP(buf, (y & 1) * 0x2000 + (y >> 1) * 80);
		int dy = y - 100, sy = sin8[(y * 3) & 255] + 512;
		unsigned dy2 = (unsigned)(dy * dy);
		const unsigned char *c0 = ctab[bayer[y & 1][0]], *c1 = ctab[bayer[y & 1][1]];
		for (x = 0; x < 160; x += 2) {
			/* the radial term: dx2 + dy2 < 27700, so 16 bits do */
			int v0 = sx[x] + sy + sin8[(x * 4 + y * 2) & 255] + sin8[((dx2[x] + dy2) >> 6) & 255];
			int v1 = sx[x + 1] + sy + sin8[(x * 4 + 4 + y * 2) & 255] + sin8[((dx2[x + 1] + dy2) >> 6) & 255];
			*row++ = (unsigned char)((c0[v0] << 4) | c1[v1]);
		}
	}
}

/* cyclic rainbows over entries 1-14 (0 black and 15 white stay put) */
static const unsigned char pal_cool[16] = { 0, 1, 1, 9, 9, 11, 3, 3, 11, 9, 1, 8, 1, 9, 11, 15 };
static const unsigned char pal_hot[16] = { 0, 1, 9, 11, 3, 2, 10, 14, 6, 4, 12, 13, 5, 8, 1, 15 };

static int stage;


static void draw_stage(void)
{
	fx_copy_rect(fx_seg, buf, 0, 0, 159, 199);
	if (stage >= 1) big_text(80 - big_width("PCjr", 4) / 2, 14, "PCjr", 4, 7, FX_SOLID2(15), 0);
	if (stage >= 2) big_text(80 - big_width("MEETS", 2) / 2, 86, "MEETS", 2, 3, FX_SOLID2(15), 0);
	if (stage >= 3) big_text(80 - big_width("AI", 8) / 2, 116, "AI", 8, 8, FX_SOLID2(15), 0);
	if (stage >= 4) {
		fx_fill_rect(0, 186, 160, 14, FX_SOLID2(0));
		center_text(189, "by CodeJr, 2026", 15); /* 20 letters at most */
	}
}

static void plasma_enter(void)
{
	stage = 0;
	set_pal(pal_cool);
	if (buf) both_pages(draw_stage);
	fx_pal_anim(0, 1, 14, 3, 1);
	voice_say("JHELLO", 0);
}

static int redraw; /* pages still showing the old stage */

static void plasma_frame(unsigned t)
{
	int want = t < 2 * PAT_TICKS ? 0 : t < 3 * PAT_TICKS ? 1 : t < 4 * PAT_TICKS ? 2 : t < 6 * PAT_TICKS ? 3 : 4;
	if (t >= 4 * PAT_TICKS && pal[2] != pal_hot[2]) {
		/* the drums come in: hot colors, rotating the other way, faster */
		set_pal(pal_hot);
		fx_pal_anim(0, 1, 14, 2, -1);
	}
	if (want != stage) {
		stage = want; redraw = 2;
		if (stage == 3 && buf) {
			/* "AI" lands: the music drops out for the announcer */
			both_pages(draw_stage);
			redraw = 0;
			voice_say("JTITLE", 0);
		}
	}
	if (redraw && buf) { draw_stage(); redraw--; }
}

static void plasma_leave(void)
{
	fx_pal_anim(0, 0, 0, 0, 0);
}

const part part_plasma = { "plasma", plasma_prep, plasma_enter, plasma_frame, plasma_leave, 0, 8, 0 };
