/* Part 5: the finale. A big sine scroller over raster bars and a
 * parallax starfield, the logo on top, and a VU meter of the four sound
 * voices at the bottom.
 *
 * The scroller is 2x4-scaled 8x8 font, one byte (2 pixels) per font
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

static unsigned char rowc[200]; /* scroller color byte for each row */

static unsigned roff[200];               /* jr_rowoff, near */
static unsigned char rowfill[200];       /* this frame's byte for each row */
static unsigned char colmask[sizeof msg * 8]; /* the message, one font column a byte */

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
	for (i = 0; i < 200; i++) {
		rowc[i] = (unsigned char)((SC1 + (i >> 2) % 6) * 0x11);
		roff[i] = jr_rowoff[i];
	}
	for (i = 0; i < (int)sizeof colmask; i++) {
		int ch = msg[i >> 3], r;
		unsigned char m = 0;
		if (ch > 32 && ch < 128)
			for (r = 0; r < 8; r++)
				if (jr_font[(ch - 32) * 8 + r] & (0x80 >> (i & 7))) m |= (unsigned char)(0x80 >> r);
		colmask[i] = m;
	}
	rnd_seed(2026);
	for (i = 0; i < NSTARS; i++) {
		star[i].x = rnd() % 160 << 6;
		star[i].y = (unsigned char)(TOP + rnd() % (200 - TOP - 12));
		star[i].sp = (unsigned char)(1 + i % 3);
		star[i].c = (unsigned char)(ST1 + i % 3);
	}
}

extern void fillw(unsigned seg, unsigned off, unsigned n, unsigned v);
#pragma aux fillw = "rep stosw" parm [es] [di] [cx] [ax] modify [cx di];


/* A bar is a run of row colors; rows not in a bar are black. */
static void bar(int y, const unsigned *rows, int n)
{
	int i;
	for (i = 0; i < n * 2; i++) { /* each shade two rows tall */
		int r = y + i;
		if (r >= TOP && r < 188) {
			unsigned p = rows[i >> 1];
			rowfill[r] = (unsigned char)(r & 1 ? p >> 8 : p);
		}
	}
}

static void fill_rows(void)
{
	int y;
	for (y = TOP; y < 200; y++) fillw(fx_seg, roff[y], 40, rowfill[y] * 0x101u);
}

#define COLROW(k) { v[ro[k] + bx] = rc[k]; v[ro[k + 1] + bx] = rc[k + 1]; v[ro[k + 2] + bx] = rc[k + 2]; v[ro[k + 3] + bx] = rc[k + 3]; }

static void scroller(unsigned t)
{
	unsigned char far *v = (unsigned char far *)MK_FP(fx_seg, 0);
	unsigned first = ((t * 4) & ~1u) >> 1; /* message column at the left edge */
	int bx;
	for (bx = 0; bx < 80; bx++) {
		unsigned idx = first + bx;
		unsigned char m;
		const unsigned *ro;
		const unsigned char *rc;
		int y;
		if (idx >= sizeof colmask) break;
		m = colmask[idx];
		if (!m) continue;
		y = 104 + ((sin8[(bx * 2 + t * 3) & 255] * 13) >> 6);
		ro = roff + y; rc = rowc + y;
		if (m & 0x80) COLROW(0)
		if (m & 0x40) COLROW(4)
		if (m & 0x20) COLROW(8)
		if (m & 0x10) COLROW(12)
		if (m & 0x08) COLROW(16)
		if (m & 0x04) COLROW(20)
		if (m & 0x02) COLROW(24)
		if (m & 0x01) COLROW(28)
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
	memset(rowfill + TOP, 0, 200 - TOP);
	/* raster bars, then every row below the logo in one pass */
	bar(108 + sin8[(t * 2) & 255] * 60 / 127, bluebar, 11);
	bar(108 + sin8[(t * 2 + 85) & 255] * 60 / 127, redbar, 11);
	bar(108 + sin8[(t * 3 + 170) & 255] * 50 / 127, bluebar, 11);
	fill_rows();
	/* stars, three speeds */
	{
		unsigned char far *v = (unsigned char far *)MK_FP(fx_seg, 0);
		for (i = 0; i < NSTARS; i++) {
			unsigned x;
			star[i].x -= star[i].sp * 24;
			if (star[i].x >= 160u << 6) star[i].x += 160u << 6;
			x = star[i].x >> 6;
			/* the row was just filled: no need to keep the other nibble */
			if (rowfill[star[i].y]) continue; /* behind a bar */
			v[roff[star[i].y] + (x >> 1)] = (unsigned char)(x & 1 ? star[i].c : star[i].c << 4);
		}
	}
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

const part part_scroll = { "finale", 0, scroll_enter, scroll_frame, scroll_leave, 36, 17, 1 };
