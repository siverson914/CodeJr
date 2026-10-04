/* Drawing helpers shared by the parts. */
#include <string.h>
#include "jrai.h"

unsigned char pal[16];
signed char sin8[256];
unsigned page_seg[2];

void set_pal(const unsigned char *p16)
{
	memcpy(pal, p16, 16);
	fx_pal_set(pal);
}

void pages_find(void)
{
	int i;
	if (!sin8[64]) for (i = 0; i < 256; i++) sin8[i] = (signed char)(fix_sin(i * 4) * 127 >> 16);
	page_seg[0] = fx_seg;
	fx_flip(); fx_begin();
	page_seg[1] = fx_seg;
	fx_flip(); fx_begin();
}

void both_pages(void (*draw)(void))
{
	draw(); fx_flip(); fx_begin();
	draw(); fx_flip(); fx_begin();
}

int big_width(const char *s, int sx)
{
	return (int)strlen(s) * 8 * sx;
}

void big_text(int x, int y, const char *s, int sx, int sy, unsigned pat2, int shadow)
{
	int pass;
	for (pass = shadow < 0 ? 1 : 0; pass < 2; pass++) {
		const char *p;
		int cx = x, d = pass ? 0 : (sx > 1 ? sx / 2 + 1 : 1);
		unsigned pt = pass ? pat2 : FX_SOLID2(shadow);
		for (p = s; *p; p++, cx += 8 * sx) {
			const unsigned char *g;
			int r, c;
			if (*p < 32 || *p > 127) continue;
			g = jr_font + (*p - 32) * 8;
			for (r = 0; r < 8; r++) {
				unsigned char bits = g[r];
				c = 0;
				while (bits) {
					/* runs of set bits as one rectangle */
					if (bits & 0x80) {
						int n = 0;
						while (bits & 0x80) { n++; bits <<= 1; }
						fx_fill_rect(cx + c * sx + d, y + r * sy + d, n * sx, sy, pt);
						c += n;
					} else { bits <<= 1; c++; }
				}
			}
		}
	}
}

void center_text(int y, const char *s, int c)
{
	fx_text(80 - (int)strlen(s) * 4, y, s, c);
}

static unsigned long seed = 0x1984A126UL;
void rnd_seed(unsigned long s) { seed = s; }

unsigned rnd(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (unsigned)(seed >> 16);
}
