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
	static const unsigned char bayer[2][2] = { { 0, 2 }, { 3, 1 } };
	int x, y;
	if (!sin8[64]) for (x = 0; x < 256; x++) sin8[x] = (signed char)(fix_sin(x * 4) * 127 >> 16);
	buf = jr_alloc_high(0x400);
	if (!buf) return;
	for (y = 0; y < 200; y++) {
		unsigned char far *row = (unsigned char far *)MK_FP(buf, jr_rowoff[y]);
		int dy = y - 100, sy = sin8[(y * 3) & 255];
		long dy2 = (long)dy * dy;
		for (x = 0; x < 160; x++) {
			int dx = (x - 80) * 5 / 3;
			int v = sin8[(x * 5) & 255] + sy + sin8[((x * 2 + y) * 2) & 255]
				+ sin8[(unsigned)(((long)dx * dx + dy2) >> 6) & 255];
			unsigned u = (unsigned)(v + 512) * 3;
			unsigned lv = u >> 6;
			unsigned char c;
			if (((u >> 4) & 3) > bayer[y & 1][x & 1]) lv++;
			c = (unsigned char)(1 + lv % 14);
			if (x & 1) row[x >> 1] |= c;
			else row[x >> 1] = (unsigned char)(c << 4);
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
	if (stage >= 4) center_text(190, "a PCjr demo by CodeJr", 15);
}

static void plasma_enter(void)
{
	stage = 0;
	set_pal(pal_cool);
	if (buf) both_pages(draw_stage);
	fx_pal_anim(0, 1, 14, 3, 1);
	say(&sp_hello, 0);
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
	if (want != stage) { stage = want; redraw = 2; }
	if (redraw && buf) { draw_stage(); redraw--; }
}

static void plasma_leave(void)
{
	fx_pal_anim(0, 0, 0, 0, 0);
}

const part part_plasma = { "plasma", plasma_prep, plasma_enter, plasma_frame, plasma_leave, 0, 8 };
