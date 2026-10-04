/* Part 2: fire, with words burning in it.
 *
 * The classic fire: every cell becomes the average of the cells below it,
 * minus a little, so heat rises and cools. 80x50 cells, each one byte wide
 * (2 pixels) and 4 rows tall. Each cell is drawn as a checkerboard of two
 * colors (even rows one byte, odd rows the other), which turns the PCjr's
 * 5 fire colors into 11 shades. Even rows live in bank 0 and odd rows in
 * bank 1, so a cell is 2 bytes in each bank, and rows that are black on
 * this page already are skipped.
 */
#include <dos.h>
#include <string.h>
#include "jrai.h"

#define W 80
#define H 50
static unsigned char heat[H + 3][W];
static unsigned char lutE[64], lutO[64];  /* heat -> even-row byte, odd-row byte */
static unsigned char dec[256];            /* cooling noise */
static unsigned char text[H][W / 8];      /* burning letters, 1 bit a cell */
static unsigned char blank[2][H];         /* row drawn black on page i */
static int word_i = -1;

static void fire_prep(void)
{
	/* 11 shades: black, red, brown, light red, yellow, white, and the
	 * checkerboards between neighbours */
	static const unsigned char ramp[6] = { 0, 4, 6, 12, 14, 15 };
	int i;
	for (i = 0; i < 64; i++) {
		int s = i * 11 / 64;          /* 0..10 */
		int a = ramp[s >> 1], b = ramp[(s + 1) >> 1];
		lutE[i] = FX_PAIR(a, b);
		lutO[i] = FX_PAIR(b, a);
	}
	for (i = 0; i < 256; i++) dec[i] = (unsigned char)((rnd() & 7) < 3 ? 1 : (rnd() & 7) == 0 ? 2 : 0);
}

static void set_word(const char *w)
{
	int len = (int)strlen(w), x0 = (W - len * 16) / 2, y0 = 12, i, r, c;
	memset(text, 0, sizeof text);
	for (i = 0; i < len; i++) {
		const unsigned char *g = jr_font + (w[i] - 32) * 8;
		for (r = 0; r < 24; r++)
			for (c = 0; c < 16; c++)
				if (g[r / 3] & (0x80 >> (c / 2))) {
					int x = x0 + i * 16 + c, y = y0 + r;
					text[y][x >> 3] |= (unsigned char)(0x80 >> (x & 7));
				}
	}
}

static void fire_enter(void)
{
	static const unsigned char ident[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	set_pal(ident);
	memset(heat, 0, sizeof heat);
	memset(blank, 0, sizeof blank);
	word_i = -1;
	fx_clear(0); fx_flip(); fx_begin(); fx_clear(0); fx_flip(); fx_begin();
	memset(blank, 1, sizeof blank);
}

static void step(unsigned t)
{
	static unsigned k;
	int x, y;
	unsigned char *src = heat[H];
	int burst = mus_beat; /* kick drum: the fire roars */
	mus_beat = 0;
	for (x = 0; x < W; x++) {
		unsigned r = rnd();
		src[x] = (unsigned char)((r & 3) ? 63 : burst ? 63 : 8);
		src[x + W] = (unsigned char)((r & 0x30) ? 63 : 20);
	}
	/* the ends of the part die down */
	if (t > 8 * PAT_TICKS - 60) for (x = 0; x < 2 * W; x++) src[x] >>= 2;
	for (y = 0; y < H; y++) {
		unsigned char *p = heat[y], *b = heat[y + 1], *c = heat[y + 2];
		for (x = 1; x < W - 1; x++) {
			unsigned s = (unsigned)(b[x - 1] + b[x] + b[x + 1] + c[x]) >> 2;
			unsigned char d = dec[k++ & 255];
			p[x] = (unsigned char)(s > d ? s - d : 0);
		}
	}
	/* the letters burn: their cells stay hot */
	if (word_i >= 0 && t < 8 * PAT_TICKS - 90) {
		for (y = 0; y < H; y++) {
			unsigned char *m = text[y], *p = heat[y];
			for (x = 0; x < W / 8; x++) {
				unsigned char bits = m[x];
				int i = x * 8;
				while (bits) {
					if (bits & 0x80) p[i] = (unsigned char)(48 + (rnd() & 15));
					bits <<= 1; i++;
				}
			}
		}
	}
}

static void draw(void)
{
	int r, x, pg = fx_seg == page_seg[1];
	for (r = 0; r < H; r++) {
		unsigned char *p = heat[r];
		unsigned far *e = (unsigned far *)MK_FP(fx_seg, r * 160);
		unsigned far *o = (unsigned far *)MK_FP(fx_seg, 0x2000 + r * 160);
		unsigned char any = 0;
		for (x = 0; x < W; x++) any |= p[x];
		if (!any && blank[pg][r]) continue;
		blank[pg][r] = (unsigned char)!any;
		for (x = 0; x < W; x += 2) {
			unsigned a = p[x], b = p[x + 1];
			unsigned we = lutE[a] | (lutE[b] << 8), wo = lutO[a] | (lutO[b] << 8);
			e[0] = we; e[40] = we; e++;
			o[0] = wo; o[40] = wo; o++;
		}
	}
}

static void fire_frame(unsigned t)
{
	static const char *const words[4] = { "1984", "2026", "PCjr", "+ AI" };
	int w = (int)(t / (2 * PAT_TICKS));
	if (w > 3) w = 3;
	if (t > PAT_TICKS / 2 && w != word_i) { word_i = w; set_word(words[w]); }
	step(t);
	draw();
}

const part part_fire = { "fire", fire_prep, fire_enter, fire_frame, 0, 8, 8 };
