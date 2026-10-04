/* Part 5: the finale. A big sine scroller over raster bars and a
 * parallax starfield, the logo on top, and a VU meter of the four sound
 * voices at the bottom.
 *
 * The scroller is 2x3-scaled 8x8 font, one byte (2 pixels) per font
 * column, so every byte column of the screen is one font column with its
 * own height on the wave. Its colors come from rows of palette entries
 * 1-6, which the retrace interrupt rotates: rainbow stripes for free.
 */
#include <dos.h>
#include <string.h>
#include "jrai.h"

static const char msg[] =
	"          PCjr MEETS AI ...  CODE, MUSIC AND WORDS BY CODEJR: CLAUDE ON A LAPTOP, "
	"TALKING TO THIS PCjr THROUGH A WIFI MODEM AT 9600 BAUD ...  8088 AT 4.77 MHZ, "
	"16 COLORS, 3 VOICES AND NOISE ...  THE TUNNEL IS 43K OF CODE THAT WROTE ITSELF ...  "
	"THE PLASMA COSTS NO CPU AT ALL ...  THE VOICE IS SAM, 1982 ...  "
	"GREETINGS TO 8088 MPH, AREA 5150, RIQ, MIKE BRUTMAN, IBM BOCA RATON, "
	"AND STEVE, WHO ASKED FOR BEYOND EPIC ...  PCjr + AI = FRIENDS FOREVER          ";

enum { SC1 = 1, SC6 = 6, ST1, ST2, ST3, RED, LRED, YEL, BLUE, LBLUE, WHITE };
static const unsigned char pal_fin[16] = {
	0, 12, 14, 10, 11, 9, 13,  /* scroller rainbow (rotating) */
	8, 7, 15,                  /* stars */
	4, 12, 14,                 /* red bar */
	1, 9, 15 };                /* blue bar, white */

#define NSTARS 48
static struct { unsigned x; unsigned char y, sp, c; } star[NSTARS];

#define TOP 40   /* rows above stay as drawn by enter() */

static void logo(void)
{
	fx_clear(0);
	big_text(80 - big_width("PCjr+AI", 2) / 2, 6, "PCjr+AI", 2, 3, FX_SOLID2(WHITE), BLUE);
}

static void scroll_enter(void)
{
	int i;
	set_pal(pal_fin);
	fx_pal_anim(0, SC1, SC6, 4, 1);
	both_pages(logo);
	rnd_seed(2026);
	for (i = 0; i < NSTARS; i++) {
		star[i].x = rnd() % 160 << 6;
		star[i].y = (unsigned char)(TOP + rnd() % (200 - TOP - 12));
		star[i].sp = (unsigned char)(1 + i % 3);
		star[i].c = (unsigned char)(ST1 + i % 3);
	}
}

static void bar(int y, const unsigned *rows, int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (y + i >= TOP && y + i < 188) fx_fill_rect(0, y + i, 160, 1, rows[i]);
}

static void scroller(unsigned t)
{
	unsigned char far *v = (unsigned char far *)MK_FP(fx_seg, 0);
	unsigned sx = (t * 3) & ~1u;
	int bx;
	for (bx = 0; bx < 80; bx++) {
		unsigned x = bx * 2 + sx;
		unsigned ci = x >> 4;
		int ch, fc, r, y0;
		const unsigned char *g;
		if (ci >= sizeof msg - 1) continue;
		ch = msg[ci];
		if (ch < 32 || ch > 127) continue;
		g = jr_font + (ch - 32) * 8;
		fc = (x & 15) >> 1;
		y0 = 112 + sin8[(bx * 3 + t * 2) & 255] * 30 / 127 + sin8[(bx * 7 + t) & 255] * 8 / 127;
		for (r = 0; r < 8; r++) {
			if (g[r] & (0x80 >> fc)) {
				int y = y0 + r * 3, k;
				for (k = 0; k < 3; k++, y++) {
					unsigned char c = (unsigned char)(SC1 + ((y >> 2) % 6));
					v[jr_rowoff[y] + bx] = (unsigned char)(c * 0x11);
				}
			}
		}
	}
}

static void vu(void)
{
	int i;
	for (i = 0; i < 4; i++) {
		int w = mus_level[i] * 36 / 15;
		fx_fill_rect(i * 40 + 2, 192, w, 6, FX_DITH(LBLUE, WHITE));
	}
}

static void scroll_frame(unsigned t)
{
	static const unsigned redbar[11] = {
		FX_SOLID2(RED), FX_DITH(RED, LRED), FX_SOLID2(LRED), FX_DITH(LRED, YEL), FX_SOLID2(YEL),
		FX_SOLID2(WHITE), FX_SOLID2(YEL), FX_DITH(LRED, YEL), FX_SOLID2(LRED), FX_DITH(RED, LRED), FX_SOLID2(RED) };
	static const unsigned bluebar[11] = {
		FX_SOLID2(BLUE), FX_DITH(BLUE, LBLUE), FX_SOLID2(LBLUE), FX_DITH(LBLUE, WHITE), FX_SOLID2(WHITE),
		FX_SOLID2(WHITE), FX_SOLID2(WHITE), FX_DITH(LBLUE, WHITE), FX_SOLID2(LBLUE), FX_DITH(BLUE, LBLUE), FX_SOLID2(BLUE) };
	int i;
	fx_fill_rect(0, TOP, 160, 200 - TOP, FX_SOLID2(0));
	/* stars, three speeds */
	for (i = 0; i < NSTARS; i++) {
		star[i].x -= star[i].sp * 24;
		if (star[i].x >= 160u << 6) star[i].x += 160u << 6;
		fx_pset(star[i].x >> 6, star[i].y, star[i].c);
	}
	/* raster bars */
	bar(118 + sin8[(t * 2) & 255] * 60 / 127, bluebar, 11);
	bar(118 + sin8[(t * 2 + 85) & 255] * 60 / 127, redbar, 11);
	bar(118 + sin8[(t * 3 + 170) & 255] * 50 / 127, bluebar, 11);
	scroller(t);
	vu();
	if (t >= 16 * PAT_TICKS) {
		big_text(80 - big_width("THE END", 2) / 2, 96, "THE END", 2, 3, FX_SOLID2(WHITE), 0);
	}
}

static void scroll_leave(void)
{
	fx_pal_anim(0, 0, 0, 0, 0);
}

const part part_scroll = { "finale", 0, scroll_enter, scroll_frame, scroll_leave, 36, 17 };
