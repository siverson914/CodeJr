/* Part 2: fire, with words burning in it.
 *
 * The classic fire: every cell becomes the average of the cells below it,
 * minus a little, so heat rises and cools. 80x25 cells, each one byte wide
 * (2 pixels) and 8 rows tall: tall cells make tall flames, and halve the
 * work. Each cell is drawn as a checkerboard of two colors (even rows one
 * byte, odd rows the other), which turns the PCjr's 5 fire colors into 11
 * shades. The two inner loops are 8088 code (#pragma aux): the simulation
 * is 4 byte adds and an XLAT per cell (heat is 0-63, so a sum of 4 fits in
 * a byte); the output writes two cells per word, 8 stores per pair, and
 * skips pairs whose shades this page already shows.
 */
#include <dos.h>
#include <string.h>
#include "jrai.h"

#define W 80
#define H 25              /* cells are 1 byte (2 pixels) wide, 8 rows tall */
/* heat rows 0..H-1 are shown; H and H+1 are the burning bed. Heat is 0-63,
 * so the sum of four neighbours fits in a byte. */
static unsigned char heat[H + 3][W];
static unsigned char cool[256];           /* sum of 4 -> new heat         */
static unsigned char tab[96];             /* heat -> shade; shade -> even, odd-row byte */
static unsigned char prev[2][H][W];       /* shades each page shows */
static unsigned char noise[256];
static unsigned text[H * W];              /* burning letters: their cells */
static int ntext;
static int word_i = -1;

/* (heat, cool and lut are in one segment: these set DS to it) */
/* one row of the simulation: dst[x] = cool[b[x-1] + b[x] + b[x+1] + b[x+W]],
 * two cells at a time: the four word adds can't carry from one cell into
 * the other, since each sum is at most 4 x 63 = 252. n = pairs. */
extern void fire_row(unsigned seg, unsigned dst, unsigned below, unsigned n, unsigned tab);
#pragma aux fire_row = \
	"push ds" "push es" "pop ds" \
	"FR1: mov ax,[si-1]" "add ax,[si]" "add ax,[si+1]" "add ax,[si+80]" \
	"xlat" "xchg al,ah" "xlat" "xchg al,ah" "stosw" "inc si" "inc si" "loop FR1" \
	"pop ds" \
	parm [es] [di] [si] [cx] [bx] modify [ax cx si di];

/* one row of cells to the screen: 4 lines in bank 0, 4 in bank 1. Each
 * pair of cells is turned into shades (tab[0..63]); if this page already
 * shows those shades (prev), nothing is written. Otherwise the even-row
 * bytes (tab[64..79]) go to bank 0 and the odd-row bytes (tab[80..95])
 * to bank 1. */
extern void fire_out(unsigned vseg, unsigned voff, unsigned src, unsigned tab, unsigned dseg, unsigned prev);
#pragma aux fire_out = \
	"push ds" "push bp" "mov ds,ax" "mov bp,dx" "mov cx,40" \
	"FO1: lodsw" \
	"xlat" "xchg al,ah" "xlat" "xchg al,ah" \
	"cmp ax,ds:[bp]" "je FO2" \
	"mov ds:[bp],ax" "mov dx,ax" "add bx,64" \
	"xlat" "xchg al,ah" "xlat" "xchg al,ah" \
	"mov es:[di],ax" "mov es:[di+80],ax" "mov es:[di+160],ax" "mov es:[di+240],ax" \
	"mov ax,dx" "add bx,16" \
	"xlat" "xchg al,ah" "xlat" "xchg al,ah" "sub bx,80" \
	"mov es:[di+2000h],ax" "mov es:[di+2050h],ax" "mov es:[di+20A0h],ax" "mov es:[di+20F0h],ax" \
	"FO2: add di,2" "add bp,2" "loop FO1" \
	"pop bp" "pop ds" \
	parm [es] [di] [si] [bx] [ax] [dx] modify [ax bx cx dx si di];

static void fire_prep(void)
{
	/* 11 shades: black, red, brown, light red, yellow, white, and the
	 * checkerboards between neighbours */
	static const unsigned char ramp[6] = { 0, 4, 6, 12, 14, 15 };
	int i;
	for (i = 0; i < 64; i++) tab[i] = (unsigned char)(i * 11 / 64);
	for (i = 0; i < 11; i++) {
		int a = ramp[i >> 1], b = ramp[(i + 1) >> 1];
		tab[64 + i] = FX_PAIR(a, b);
		tab[80 + i] = FX_PAIR(b, a);
	}
	/* cooling: the average, minus about 2.5 (cells are tall, so cool fast) */
	for (i = 0; i < 256; i++) {
		int v = i / 4 - 2 - (i & 1);
		cool[i] = (unsigned char)(v < 0 ? 0 : v);
	}
	for (i = 0; i < 256; i++) noise[i] = (unsigned char)rnd();
}

static void set_word(const char *w)
{
	int len = (int)strlen(w), x0 = (W - len * 16) / 2, y0 = 4, i, r, c;
	ntext = 0;
	for (i = 0; i < len; i++) {
		const unsigned char *g = jr_font + (w[i] - 32) * 8;
		for (r = 0; r < 16; r++)
			for (c = 0; c < 16; c++)
				if (g[r / 2] & (0x80 >> (c / 2))) {
					int x = x0 + i * 16 + c, y = y0 + r;
					text[ntext++] = y * W + x;
				}
	}
}

static void fire_enter(void)
{
	static const unsigned char ident[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	set_pal(ident);
	memset(heat, 0, sizeof heat);
	word_i = -1;
	fx_clear(0); fx_flip(); fx_begin(); fx_clear(0); fx_flip(); fx_begin();
	memset(prev, 0, sizeof prev); /* both pages are black: shade 0 */
}

static void step(unsigned t)
{
	static unsigned char k;
	int x, y;
	unsigned char *src = heat[H];
	unsigned char hi = 63, lo = 10;
	if (mus_beat) { lo = 50; mus_beat = 0; } /* kick drum: the fire roars */
	if (t > 8 * PAT_TICKS - 60) hi = lo = 0;  /* the end of the part: it dies */
	for (x = 0; x < W; x++) {
		unsigned char r = noise[k++];
		src[x] = r & 3 ? hi : lo;
		src[x + W] = r & 0x30 ? hi : lo;
	}
	k += 37;
	for (y = 0; y < H; y++)
		fire_row(FP_SEG(heat), FP_OFF(heat[y]) + 1, FP_OFF(heat[y + 1]) + 1, (W - 2) / 2, FP_OFF(cool));
	/* the letters burn: their cells stay hot */
	if (word_i >= 0 && t < 8 * PAT_TICKS - 90) {
		unsigned char *h = heat[0];
		const unsigned *c = text;
		unsigned char j = k;
		int n;
		for (n = ntext; n; n--) h[*c++] = (unsigned char)(48 + (noise[j++] & 15));
	}
}

static void draw(void)
{
	int r, pg = fx_seg == page_seg[1];
	for (r = 0; r < H; r++)
		fire_out(fx_seg, r * 320, FP_OFF(heat[r]), FP_OFF(tab), FP_SEG(heat), FP_OFF(prev[pg][r]));
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

const part part_fire = { "fire", fire_prep, fire_enter, fire_frame, 0, 8, 8, 1 };
