/* JRGFX: graphics, input, timing and sound for the IBM PCjr.
 * See jrgfx.h for the interface and STATUS.md for notes and sources.
 * Build: wcl -bt=dos -ml -0 -ox (8088 only, large model).
 */
#include <dos.h>
#include <conio.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "jrgfx.h"

extern const unsigned char jr_font[96 * 8];
extern const long jr_qsin[257];

int gfx_w, gfx_h;
unsigned char far *gfx_back;
int gfx_mode;
int gfx_err;
unsigned char gfx_pal[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };

static unsigned back_seg;          /* DOS block holding the back buffer   */
static unsigned vram_seg = 0xB800; /* where gfx_present writes             */
int gfx_force_claim;               /* test: take the mode 9 claim path   */
unsigned gfx_vseg;                 /* same, exported for tests             */
static unsigned vram_own;          /* DOS block we hold for mode 9, or 0   */
static unsigned bpr;               /* video bytes per row                  */
static unsigned bank_shift;        /* 1 (2 banks) or 2 (4 banks)           */
unsigned jr_rowoff[200];           /* video offset of each row             */

/* ---- DOS memory helpers ------------------------------------------------ */

static unsigned dos_alloc(unsigned paras)
{
	union REGS r;
	r.h.ah = 0x48; r.x.bx = paras;
	intdos(&r, &r);
	return r.x.cflag ? 0 : r.x.ax;
}

static void dos_free(unsigned seg)
{
	union REGS r; struct SREGS s;
	segread(&s);
	r.h.ah = 0x49; s.es = seg;
	intdosx(&r, &r, &s);
}

static unsigned first_mcb(void)
{
	union REGS r; struct SREGS s;
	segread(&s);
	r.h.ah = 0x52;
	intdosx(&r, &r, &s);
	return *(unsigned far *)MK_FP(s.es, r.x.bx - 2);
}

/* Owns a 32 KB block of conventional memory at exactly segment `want`
 * (32K-aligned, in the low 128K), using DOS first-fit allocation and
 * throw-away blocks to fill the holes below it. Returns the segment or 0. */
static unsigned claim_window(unsigned want, unsigned paras)
{
	unsigned dummies[24];
	int nd = 0, i;
	unsigned got = 0;
	for (;;) {
		/* lowest free block */
		unsigned m = first_mcb(), lo = 0, losize = 0;
		for (;;) {
			unsigned char far *p = (unsigned char far *)MK_FP(m, 0);
			unsigned owner = *(unsigned far *)(p + 1), size = *(unsigned far *)(p + 3);
			if (p[0] != 'M' && p[0] != 'Z') { lo = 0; break; }
			if (owner == 0) { lo = m + 1; losize = size; break; }
			if (p[0] == 'Z') break;
			m = m + 1 + size;
		}
		if (!lo || lo > want || nd >= 23) break;
		if (lo + losize < want + paras) {
			/* free block entirely below or not covering: plug it */
			if (!(dummies[nd] = dos_alloc(losize))) break;
			nd++;
			continue;
		}
		if (lo < want) {
			if (want - lo < 2) break; /* cannot place an MCB there */
			if (!(dummies[nd] = dos_alloc(want - lo - 1))) break;
			nd++;
		}
		got = dos_alloc(paras);
		if (got != want) { if (got) dos_free(got); got = 0; }
		break;
	}
	for (i = 0; i < nd; i++)
		dos_free(dummies[i]);
	return got;
}

/* True if no DOS block that a program could use overlaps [seg, seg+paras). */
static int region_reserved(unsigned seg, unsigned paras);

/* For jrfast.c: the same helpers, plus hooks so the fast path can retarget
 * gfx_present and clean up in gfx_done. */
unsigned jr_claim(unsigned want, unsigned paras) { return claim_window(want, paras); }
int jr_reserved(unsigned seg, unsigned paras) { return region_reserved(seg, paras); }
void jr_dos_free(unsigned seg) { dos_free(seg); }
void (*jr_done_hook)(void);
void (*jr_present_hook)(void);
void jr_set_vseg(unsigned seg) { vram_seg = seg; gfx_vseg = seg; }

static int region_reserved(unsigned seg, unsigned paras)
{
	unsigned m = first_mcb();
	unsigned long end = (unsigned long)seg + paras;
	for (;;) {
		unsigned char far *p = (unsigned char far *)MK_FP(m, 0);
		unsigned owner = *(unsigned far *)(p + 1), size = *(unsigned far *)(p + 3);
		unsigned long b0, b1;
		if (p[0] != 'M' && p[0] != 'Z') return 0;
		b0 = m; b1 = (unsigned long)m + 1 + size;
		if (b0 < end && b1 > seg && owner != 8)
			return 0;
		if (p[0] == 'Z') return 1;
		m = m + 1 + size;
	}
}

/* ---- Mode setting ------------------------------------------------------ */

static void bios_mode(unsigned char m)
{
	union REGS r;
	r.h.ah = 0; r.h.al = m;
	int86(0x10, &r, &r);
}

static void set_pages(unsigned char crt, unsigned char cpu)
{
	union REGS r;
	r.h.ah = 5; r.h.al = 0x83; r.h.bh = crt; r.h.bl = cpu;
	int86(0x10, &r, &r);
}

/* Allocates with DOS "last fit" so the block comes from the top of memory:
 * above 128K on an expanded PCjr, where the CPU does not wait for video. */
static unsigned dos_alloc_high(unsigned paras)
{
	union REGS r;
	unsigned old, seg;
	r.x.ax = 0x5800; intdos(&r, &r); old = r.x.ax;
	r.x.ax = 0x5801; r.x.bx = 2; intdos(&r, &r);
	seg = dos_alloc(paras);
	r.x.ax = 0x5801; r.x.bx = old; intdos(&r, &r);
	return seg;
}

unsigned jr_alloc_high(unsigned paras) { return dos_alloc_high(paras); }

int gfx_init(int mode)
{
	unsigned y, w = 0;
	static int reg;
	if (mode != GFX_160x200 && mode != GFX_320x200) { gfx_err = 1; return -1; }
	if (gfx_mode) gfx_done();
	if (!reg) { reg = 1; atexit(gfx_done); }
	if (mode == GFX_320x200 && (gfx_force_claim || !region_reserved(0x1800, 0x800))) {
		/* Mode 9 needs 32 KB of video RAM in the low 128K. The BIOS uses
		 * pages 6-7 (1800:0) but DOS (POST sets 40:13 to 112K) only keeps
		 * 1C00-1FFF out of its pool, so 1800-1BFF may be in use. Claim our
		 * own free 32K-aligned window instead (before anything else
		 * allocates low memory). */
		static const unsigned win[2] = { 0x1000, 0x0800 };
		int i;
		for (i = 0; i < 2 && !w; i++)
			w = claim_window(win[i], 0x800);
		if (!w) { gfx_err = 3; return -1; }
		vram_own = w;
	}
	if (!(back_seg = dos_alloc_high(0x1000))) {
		if (vram_own) { dos_free(vram_own); vram_own = 0; }
		gfx_err = 2; return -1;
	}
	gfx_back = (unsigned char far *)MK_FP(back_seg, 0);
	for (y = 0; y < 256; y++) /* XLAT table for jr_pack */
		gfx_back[64000u + y] = (unsigned char)(y << 4);
	gfx_w = mode == GFX_160x200 ? 160 : 320;
	gfx_h = 200;

	if (mode == GFX_160x200) {
		bios_mode(8);
		vram_seg = 0xB800; bpr = 80; bank_shift = 1;
	} else {
		if (w) {
			/* The BIOS clears 1800-1FFF on the mode set: preserve 1800-1BFF. */
			_fmemcpy(gfx_back, MK_FP(0x1800, 0), 16384u);
			bios_mode(9);
			_fmemcpy(MK_FP(0x1800, 0), gfx_back, 16384u);
			_fmemset(MK_FP(w, 0), 0, 32768u);
			set_pages((unsigned char)(w >> 10), (unsigned char)(w >> 10));
			vram_seg = w;
		} else {
			bios_mode(9);
			vram_seg = 0x1800;
		}
		/* B800 reaches only the first 16K of a 32K mode: write to the
		 * physical segment instead (IBM BIOS listing, SET_MODE notes). */
		bpr = 160; bank_shift = 2;
	}
	_fmemset(gfx_back, 0, 64000u);
	gfx_mode = mode;
	gfx_vseg = vram_seg;
	for (y = 0; y < 200; y++)
		jr_rowoff[y] = ((y & ((1 << bank_shift) - 1)) << 13) + (y >> bank_shift) * bpr;
	for (y = 0; y < 16; y++)
		gfx_pal[y] = (unsigned char)y;
	return 0;
}

void gfx_done(void)
{
	if (!gfx_mode) return;
	if (jr_done_hook) jr_done_hook();
	/* Back to text, the way that is known to leave a clean DOS screen on
	 * the real PCjr. One BIOS mode set after a game that wrote the page
	 * register directly left the prompt garbled (black with lines) until
	 * the user typed MODE 80, which is just a second mode set. So: put
	 * the gate array's index/data latch back in its index state, then
	 * set the mode twice. Not reproducible in DOSBox. */
	inp(0x3DA);
	bios_mode(3);
	bios_mode(3);
	if (vram_own) { dos_free(vram_own); vram_own = 0; }
	if (back_seg) { dos_free(back_seg); back_seg = 0; gfx_back = 0; }
	gfx_mode = 0;
}

void gfx_free_back(void)
{
	if (back_seg) { dos_free(back_seg); back_seg = 0; gfx_back = 0; }
}

/* ---- Drawing ----------------------------------------------------------- */

void gfx_clear(unsigned char c)
{
	_fmemset(gfx_back, c, (unsigned)gfx_w * gfx_h);
}

void gfx_pset(int x, int y, unsigned char c)
{
	if ((unsigned)x < (unsigned)gfx_w && (unsigned)y < (unsigned)gfx_h)
		gfx_back[(unsigned)y * gfx_w + x] = c;
}

unsigned char gfx_pget(int x, int y)
{
	if ((unsigned)x < (unsigned)gfx_w && (unsigned)y < (unsigned)gfx_h)
		return gfx_back[(unsigned)y * gfx_w + x];
	return 0;
}

void gfx_hline(int x0, int x1, int y, unsigned char c)
{
	if ((unsigned)y >= (unsigned)gfx_h) return;
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (x0 < 0) x0 = 0;
	if (x1 >= gfx_w) x1 = gfx_w - 1;
	if (x0 > x1) return;
	_fmemset(gfx_back + (unsigned)y * gfx_w + x0, c, x1 - x0 + 1);
}

/* Vertical run: n pixels of color c, stride bx+1. Unrolled by 4. */
extern void jr_vfill_asm(unsigned char far *p, unsigned n, unsigned skip, unsigned char c);
#pragma aux jr_vfill_asm = \
	"test cl,1" "jz V2" \
	"stosb" "add di,bx" \
	"V2:" "test cl,2" "jz V4" \
	"stosb" "add di,bx" "stosb" "add di,bx" \
	"V4:" "shr cx,1" "shr cx,1" "jcxz V9" \
	"V8:" \
	"stosb" "add di,bx" "stosb" "add di,bx" \
	"stosb" "add di,bx" "stosb" "add di,bx" \
	"loop V8" \
	"V9:" \
	parm [es di] [cx] [bx] [al] modify [cx di];

static void jr_vfill(unsigned char far *p, unsigned n, unsigned skip, unsigned char c)
{
	jr_vfill_asm(p, n, skip, c);
}

/* Textured vertical run: n pixels, pixel = tex[v >> 8] with v in 8.8
 * fixed point, v += dv per row; the texture index wraps at 256. One
 * routine per row stride so the stride is an immediate. ~50 clocks/pixel. */
extern void jr_vtex160(unsigned char far *p, const unsigned char far *tex,
                        unsigned n, unsigned v, unsigned dv);
#pragma aux jr_vtex160 = \
	"push ds" "mov ds,bx" "mov bx,ax" \
	"test cl,1" "jz T160a" \
	"mov al,dh" "xlat" "stosb" "add di,159" "add dx,si" \
	"T160a:" "shr cx,1" "jcxz T160z" \
	"T160b:" \
	"mov al,dh" "xlat" "stosb" "add di,159" "add dx,si" \
	"mov al,dh" "xlat" "stosb" "add di,159" "add dx,si" \
	"loop T160b" \
	"T160z:" "pop ds" \
	parm [es di] [bx ax] [cx] [dx] [si] modify [ax cx dx di];

extern void jr_vtex320(unsigned char far *p, const unsigned char far *tex,
                        unsigned n, unsigned v, unsigned dv);
#pragma aux jr_vtex320 = \
	"push ds" "mov ds,bx" "mov bx,ax" \
	"test cl,1" "jz T320a" \
	"mov al,dh" "xlat" "stosb" "add di,319" "add dx,si" \
	"T320a:" "shr cx,1" "jcxz T320z" \
	"T320b:" \
	"mov al,dh" "xlat" "stosb" "add di,319" "add dx,si" \
	"mov al,dh" "xlat" "stosb" "add di,319" "add dx,si" \
	"loop T320b" \
	"T320z:" "pop ds" \
	parm [es di] [bx ax] [cx] [dx] [si] modify [ax cx dx di];

void gfx_vtex(int x, int y0, int y1, const unsigned char far *tex, long v, long dv)
{
	int n;
	unsigned char far *p;
	if ((unsigned)x >= (unsigned)gfx_w || y1 < y0) return;
	if (y0 < 0) { v += dv * -y0; y0 = 0; }
	if (y1 >= gfx_h) y1 = gfx_h - 1;
	if (y0 > y1) return;
	n = y1 - y0 + 1;
	p = gfx_back + (unsigned)y0 * gfx_w + x;
	if (gfx_w == 160) jr_vtex160(p, tex, n, (unsigned)(v >> 8), (unsigned)(dv >> 8));
	else              jr_vtex320(p, tex, n, (unsigned)(v >> 8), (unsigned)(dv >> 8));
}

void gfx_vline(int x, int y0, int y1, unsigned char c)
{

	unsigned char far *p;
	unsigned w = gfx_w;
	int n;
	if ((unsigned)x >= (unsigned)gfx_w) return;
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	if (y0 < 0) y0 = 0;
	if (y1 >= gfx_h) y1 = gfx_h - 1;
	if (y0 > y1) return;
	p = gfx_back + (unsigned)y0 * w + x;
	n = y1 - y0 + 1;
	jr_vfill(p, n, w - 1, c);
}

/* Cohen-Sutherland clip to the screen. Returns 0 if nothing is visible. */
static int outcode(long x, long y)
{
	int c = 0;
	if (x < 0) c |= 1; else if (x >= gfx_w) c |= 2;
	if (y < 0) c |= 4; else if (y >= gfx_h) c |= 8;
	return c;
}

static int clip_line(long *x0, long *y0, long *x1, long *y1)
{
	int c0 = outcode(*x0, *y0), c1 = outcode(*x1, *y1);
	long xmax = gfx_w - 1, ymax = gfx_h - 1;
	for (;;) {
		long x, y;
		int c;
		if (!(c0 | c1)) return 1;
		if (c0 & c1) return 0;
		c = c0 ? c0 : c1;
		if (c & 8)      { y = ymax; x = *x0 + (*x1 - *x0) * (ymax - *y0) / (*y1 - *y0); }
		else if (c & 4) { y = 0;    x = *x0 + (*x1 - *x0) * (0 - *y0) / (*y1 - *y0); }
		else if (c & 2) { x = xmax; y = *y0 + (*y1 - *y0) * (xmax - *x0) / (*x1 - *x0); }
		else            { x = 0;    y = *y0 + (*y1 - *y0) * (0 - *x0) / (*x1 - *x0); }
		if (c == c0) { *x0 = x; *y0 = y; c0 = outcode(x, y); }
		else         { *x1 = x; *y1 = y; c1 = outcode(x, y); }
	}
}

void gfx_line(int ax, int ay, int bx, int by, unsigned char c)
{
	long x0 = ax, y0 = ay, x1 = bx, y1 = by;
	int dx, dy, sx, err, n;
	unsigned char far *p;
	int step;
	if (ay == by) { gfx_hline(ax, bx, ay, c); return; }
	if (ax == bx) { gfx_vline(ax, ay, by, c); return; }
	if (!clip_line(&x0, &y0, &x1, &y1)) return;
	dx = (int)(x1 - x0); dy = (int)(y1 - y0);
	sx = 1;
	if (dx < 0) { dx = -dx; sx = -1; }
	step = gfx_w;
	if (dy < 0) { dy = -dy; step = -step; }
	p = gfx_back + (unsigned)y0 * gfx_w + (int)x0;
	if (dx >= dy) {
		err = dx >> 1;
		for (n = dx + 1; n; n--) {
			*p = c; p += sx;
			if ((err -= dy) < 0) { err += dx; p += step; }
		}
	} else {
		err = dy >> 1;
		for (n = dy + 1; n; n--) {
			*p = c; p += step;
			if ((err -= dx) < 0) { err += dy; p += sx; }
		}
	}
}

void gfx_rect(int x, int y, int w, int h, unsigned char c)
{
	if (w <= 0 || h <= 0) return;
	gfx_hline(x, x + w - 1, y, c);
	gfx_hline(x, x + w - 1, y + h - 1, c);
	gfx_vline(x, y, y + h - 1, c);
	gfx_vline(x + w - 1, y, y + h - 1, c);
}

void gfx_fill_rect(int x, int y, int w, int h, unsigned char c)
{
	unsigned char far *p;
	int x1 = x + w, y1 = y + h;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > gfx_w) x1 = gfx_w;
	if (y1 > gfx_h) y1 = gfx_h;
	if (x >= x1 || y >= y1) return;
	w = x1 - x;
	p = gfx_back + (unsigned)y * gfx_w + x;
	for (; y < y1; y++, p += gfx_w)
		_fmemset(p, c, w);
}

void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, unsigned char c)
{
	int xy[6];
	xy[0] = x0; xy[1] = y0; xy[2] = x1; xy[3] = y1; xy[4] = x2; xy[5] = y2;
	gfx_fill_poly(3, xy, c);
}

/* Convex polygon: every edge is walked once with a 16.16 DDA (one
 * division per edge) into per-row left/right bounds, then each row is one
 * hline. Vertex pixels are included, so a shape matches its gfx_line
 * outline. Vertices may be off screen (|coords| < 16000). */
static int pl[200], pr[200];

void gfx_fill_poly(int n, const int *xy, unsigned char c)
{
	int i, y, ymin, ymax;
	if (n < 2) return;
	ymin = ymax = xy[1];
	for (i = 1; i < n; i++) {
		if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
		if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
	}
	if (ymin < 0) ymin = 0;
	if (ymax >= gfx_h) ymax = gfx_h - 1;
	if (ymin > ymax) return;
	for (y = ymin; y <= ymax; y++) { pl[y] = 32767; pr[y] = -32767; }
	for (i = 0; i < n; i++) {
		int ax = xy[2 * i], ay = xy[2 * i + 1];
		int j = i + 1 == n ? 0 : i + 1;
		int bx = xy[2 * j], by = xy[2 * j + 1];
		int y0, y1;
		long x, dx;
		if (ay > by) { int t; t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
		if (by < ymin || ay > ymax) continue;
		if (ay == by) {
			if (ax > bx) { int t = ax; ax = bx; bx = t; }
			if (ax < pl[ay]) pl[ay] = ax;
			if (bx > pr[ay]) pr[ay] = bx;
			continue;
		}
		dx = ((long)(bx - ax) << 16) / (by - ay);
		x = ((long)ax << 16) + 0x8000L;
		y0 = ay; y1 = by;
		if (y0 < ymin) { x += dx * (ymin - y0); y0 = ymin; }
		if (y1 > ymax) y1 = ymax;
		for (y = y0; y <= y1; y++, x += dx) {
			int xi = (int)(x >> 16);
			if (xi < pl[y]) pl[y] = xi;
			if (xi > pr[y]) pr[y] = xi;
		}
	}
	for (y = ymin; y <= ymax; y++)
		if (pl[y] <= pr[y])
			gfx_hline(pl[y], pr[y], y, c);
}

/* Copies n bytes except those equal to the key. ~25 clocks/pixel. */
extern void jr_keyrow(unsigned char far *d, const unsigned char far *s, unsigned n, unsigned char key);
#pragma aux jr_keyrow = \
	"push ds" "mov ds,dx" \
	"K1:" "lodsb" "cmp al,ah" "je K2" "mov es:[di],al" \
	"K2:" "inc di" "loop K1" \
	"pop ds" \
	parm [es di] [dx si] [cx] [ah] modify [al cx si di];

void gfx_blit(int x, int y, int w, int h, const unsigned char far *pix, int key)
{
	int sx0 = 0, sy0 = 0, cw, ch, r;
	if (x < 0) { sx0 = -x; x = 0; }
	if (y < 0) { sy0 = -y; y = 0; }
	cw = w - sx0; ch = h - sy0;
	if (x + cw > gfx_w) cw = gfx_w - x;
	if (y + ch > gfx_h) ch = gfx_h - y;
	if (cw <= 0 || ch <= 0) return;
	for (r = 0; r < ch; r++) {
		const unsigned char far *s = pix + (unsigned)(sy0 + r) * w + sx0;
		unsigned char far *d = gfx_back + (unsigned)(y + r) * gfx_w + x;
		if (key < 0)
			_fmemcpy(d, s, cw);
		else
			jr_keyrow(d, s, cw, (unsigned char)key);
	}
}

void gfx_text(int x, int y, const char *s, unsigned char c)
{
	for (; *s; s++, x += 8) {
		unsigned ch = (unsigned char)*s;
		const unsigned char *g;
		int r, b;
		if (ch < 32 || ch > 127) ch = '?';
		g = jr_font + (ch - 32) * 8;
		if (x >= 0 && y >= 0 && x + 8 <= gfx_w && y + 8 <= gfx_h) {
			unsigned char far *p = gfx_back + (unsigned)y * gfx_w + x;
			for (r = 0; r < 8; r++, p += gfx_w) {
				unsigned char bits = g[r];
				if (!bits) continue;
				if (bits & 0x80) p[0] = c;
				if (bits & 0x40) p[1] = c;
				if (bits & 0x20) p[2] = c;
				if (bits & 0x10) p[3] = c;
				if (bits & 0x08) p[4] = c;
				if (bits & 0x04) p[5] = c;
				if (bits & 0x02) p[6] = c;
				if (bits & 0x01) p[7] = c;
			}
		} else {
			for (r = 0; r < 8; r++)
				for (b = 0; b < 8; b++)
					if (g[r] & (0x80 >> b)) gfx_pset(x + b, y + r, c);
		}
	}
}

/* ---- Video ------------------------------------------------------------- */

void gfx_vsync(void)
{
	while (inp(0x3DA) & 8) ;
	while (!(inp(0x3DA) & 8)) ;
}

/* Packs 2*n back-buffer bytes (one pixel each) into n video bytes, left
 * pixel in the high nibble. DS must be the back buffer's segment: the
 * shift table (i << 4) lives at offset 64000 of it, for XLAT. ~40 clocks
 * per output byte on an 8088, bus bound. n must be a multiple of 8. */
extern void jr_pack_asm(unsigned char far *dst, const unsigned char far *src, unsigned n8);
#pragma aux jr_pack_asm = \
	"push ds" \
	"mov ds,dx" \
	"mov bx,64000" \
	"L1:" \
	"lodsw" "xlat" "or al,ah" "stosb" \
	"lodsw" "xlat" "or al,ah" "stosb" \
	"lodsw" "xlat" "or al,ah" "stosb" \
	"lodsw" "xlat" "or al,ah" "stosb" \
	"lodsw" "xlat" "or al,ah" "stosb" \
	"lodsw" "xlat" "or al,ah" "stosb" \
	"lodsw" "xlat" "or al,ah" "stosb" \
	"lodsw" "xlat" "or al,ah" "stosb" \
	"loop L1" \
	"pop ds" \
	parm [es di] [dx si] [cx] modify [ax bx cx si di];

void jr_pack(unsigned char far *dst, const unsigned char far *src, unsigned n)
{
	jr_pack_asm(dst, src, n >> 3);
}

void gfx_present_rows(int y0, int y1)
{
	int y;
	unsigned w = gfx_w;
	if (!gfx_mode) return;
	if (jr_present_hook) jr_present_hook();
	if (y0 < 0) y0 = 0;
	if (y1 >= gfx_h) y1 = gfx_h - 1;
	for (y = y0; y <= y1; y++)
		jr_pack((unsigned char far *)MK_FP(vram_seg, jr_rowoff[y]),
		        gfx_back + (unsigned)y * w, bpr);
}

void gfx_present(void)
{
	gfx_present_rows(0, gfx_h - 1);
}

void gfx_palette(int index, int rgbi)
{
	index &= 15;
	gfx_pal[index] = (unsigned char)(rgbi & 15);
	if (!gfx_mode) return;
	gfx_vsync();
	_disable();
	inp(0x3DA);                       /* reset the gate array flip-flop */
	outp(0x3DA, 0x10 + index);
	outp(0x3DA, rgbi & 15);
	inp(0x3DA);
	outp(0x3DA, 0);                   /* re-enable video (see STATUS.md) */
	inp(0x3DA);                       /* leave the latch expecting an index */
	_enable();
}

static const unsigned char rgb16[16][3] = {
	{0x00,0x00,0x00},{0x00,0x00,0xAA},{0x00,0xAA,0x00},{0x00,0xAA,0xAA},
	{0xAA,0x00,0x00},{0xAA,0x00,0xAA},{0xAA,0x55,0x00},{0xAA,0xAA,0xAA},
	{0x55,0x55,0x55},{0x55,0x55,0xFF},{0x55,0xFF,0x55},{0x55,0xFF,0xFF},
	{0xFF,0x55,0x55},{0xFF,0x55,0xFF},{0xFF,0xFF,0x55},{0xFF,0xFF,0xFF},
};

int gfx_screenshot(const char *path)
{
	FILE *f;
	int x, y, sx = gfx_w == 160 ? 2 : 1;
	static unsigned char row[320 * 3];
	if (!gfx_back || !(f = fopen(path, "wb"))) return -1;
	fprintf(f, "P6\n%d %d\n255\n", gfx_w * sx, gfx_h);
	for (y = 0; y < gfx_h; y++) {
		unsigned char far *p = gfx_back + (unsigned)y * gfx_w;
		unsigned char *o = row;
		for (x = 0; x < gfx_w; x++) {
			const unsigned char *c = rgb16[gfx_pal[p[x] & 15]];
			int k;
			for (k = 0; k < sx; k++) { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o += 3; }
		}
		if (fwrite(row, 1, o - row, f) != (size_t)(o - row)) { fclose(f); return -1; }
	}
	return fclose(f) ? -1 : 0;
}

/* ---- Fixed point ------------------------------------------------------- */

fix fix_sin(int ang)
{
	ang &= 1023;
	if (ang < 256)  return jr_qsin[ang];
	if (ang < 512)  return jr_qsin[512 - ang];
	if (ang < 768)  return -jr_qsin[ang - 512];
	return -jr_qsin[1024 - ang];
}

fix fix_cos(int ang)
{
	return fix_sin(ang + 256);
}

fix fix_sqrt(fix a)
{
	/* isqrt(a << 16), bit by bit: 24 result bits */
	unsigned long rem = 0, root = 0, v = (unsigned long)a;
	int i;
	if (a <= 0) return 0;
	for (i = 0; i < 24; i++) {
		root <<= 1;
		rem = (rem << 2) | (v >> 30);
		v <<= 2;
		root++;
		if (root <= rem) { rem -= root; root++; }
		else root--;
	}
	return (fix)(root >> 1);
}

/* (a * b) >> 16, signed, from four 16x16 MULs (bits 16..47 of the
 * 64-bit product), rounded down like >> 16. About 3x faster than the long long library call. */
extern long jr_mul(long a, long b);
#pragma aux jr_mul = \
	"mov di,dx" "xor di,cx" "push di" \
	"test dx,dx" "jns M1" "neg dx" "neg ax" "sbb dx,0" \
	"M1:" "test cx,cx" "jns M2" "neg cx" "neg bx" "sbb cx,0" \
	"M2:" "mov si,ax" "mov di,dx" \
	"mov ax,si" "mul cx" "push ax" "push dx" \
	"mov ax,di" "mul cx" "mov cx,ax" \
	"mov ax,di" "mul bx" "mov di,ax" "add cx,dx" \
	"mov ax,si" "mul bx" "mov si,ax" \
	"add dx,di" "adc cx,0" \
	"pop ax" "pop bx" \
	"add dx,bx" "adc cx,ax" \
	"mov ax,dx" "mov dx,cx" \
	"pop di" "test di,di" "jns M3" "neg dx" "neg ax" "sbb dx,0" \
	"test si,si" "jz M3" "sub ax,1" "sbb dx,0" \
	"M3:" \
	parm [dx ax] [cx bx] value [dx ax] modify [bx cx si di];

fix fix_mul(fix a, fix b)
{
	return jr_mul(a, b);
}

/* Unsigned (a << 16) / b for a in dx:ax, b in cx:bx. A b of 17+ bits is
 * shifted right (with the 48-bit dividend) until it fits 16 bits, then
 * three DIVs. Saturates to 7FFFFFFFh. */
extern unsigned long jr_udiv(unsigned long a, unsigned long b);
#pragma aux jr_udiv = \
	"xor si,si" \
	"D1:" "test cx,cx" "jz D2" \
	"shr cx,1" "rcr bx,1" "shr dx,1" "rcr ax,1" "rcr si,1" "jmp D1" \
	"D2:" "mov cx,ax" "mov ax,dx" "xor dx,dx" "div bx" "mov di,ax" \
	"mov ax,cx" "div bx" "mov cx,ax" \
	"mov ax,si" "div bx" "mov dx,cx" \
	"test di,di" "jnz D3" "test dx,dx" "jns D4" \
	"D3:" "mov dx,7FFFh" "mov ax,0FFFFh" \
	"D4:" \
	parm [dx ax] [cx bx] value [dx ax] modify [bx cx si di];

/* (a << 16) / b, truncated toward zero, saturating at +-7FFFFFFFh. Exact
 * when |b| < 1.0 (65536); for larger b the divisor keeps its top 16 bits
 * (relative error < 2^-15). */
fix fix_div(fix a, fix b)
{
	unsigned long q;
	int neg = (a < 0) != (b < 0);
	if (b == 0) return a < 0 ? -0x7FFFFFFFL : 0x7FFFFFFFL;
	q = jr_udiv(a < 0 ? -(unsigned long)a : (unsigned long)a,
	            b < 0 ? -(unsigned long)b : (unsigned long)b);
	return neg ? -(fix)q : (fix)q;
}

/* ---- Keyboard ---------------------------------------------------------- */

volatile unsigned char key_down[128];
volatile unsigned char key_makes[128];
volatile unsigned key_seq;
static void (__interrupt __far *old_kb)(void);
static int kb_on;
static int is_pcjr;

/* The PCjr's keyboard reaches INT 9 through the NMI handler and the INT 48h
 * translator, which hands INT 9 the 83-key scan code in AL. DOSBox and
 * other PCs deliver it in port 60h. Record the key, empty the BIOS
 * type-ahead buffer (so it never fills and beeps), then chain to the BIOS,
 * which does the EOI, Ctrl-Alt-Del and the PCjr's own housekeeping. */

/* One scan code, from the interrupt or from key_feed. A press is counted
 * as well as recorded, so a tap that is over before the program looks
 * still shows, and so do the keyboard's repeats of a held key. */
static void kb_code(unsigned char sc)
{
	unsigned char k = sc & 0x7F;
	if (sc == 0xE0 || sc == 0xE1)
		return;
	if (!(sc & 0x80)) {
		key_down[k] = 1;
		key_makes[k]++;
		key_seq++;
		return;
	}
	key_down[k] = 0;
}

void key_feed(int scancode, int down)
{
	_disable();
	kb_code((unsigned char)((scancode & 0x7F) | (down ? 0 : 0x80)));
	_enable();
}

static void __interrupt __far kb_isr(union INTPACK r)
{
	kb_code(is_pcjr == 1 ? r.h.al : (unsigned char)inp(0x60));
	*(unsigned short far *)MK_FP(0x40, 0x1C) = *(unsigned short far *)MK_FP(0x40, 0x1A);
	_chain_intr(old_kb);
}

void key_init(void)
{
	if (kb_on) return;
	memset((void *)key_down, 0, sizeof key_down);
	memset((void *)key_makes, 0, sizeof key_makes);
	if (!is_pcjr) is_pcjr = *(unsigned char far *)MK_FP(0xF000, 0xFFFE) == 0xFD ? 1 : 2;
	if (kb_on == 0 && !old_kb) atexit(key_done);
	old_kb = _dos_getvect(9);
	_dos_setvect(9, kb_isr);
	kb_on = 1;
}

void key_done(void)
{
	if (!kb_on) return;
	_dos_setvect(9, old_kb);
	kb_on = 0;
	*(unsigned short far *)MK_FP(0x40, 0x1C) = *(unsigned short far *)MK_FP(0x40, 0x1A);
}

/* ---- Timer ------------------------------------------------------------- */

/* The BIOS runs PIT channel 0 in mode 3 (square wave) at 18.2 Hz, where the
 * counter steps by 2 and wraps twice per tick. tmr_init switches it to mode
 * 2 (rate generator) with the same divisor: the interrupt rate and the BIOS
 * clock are unchanged, but the counter now falls linearly once per tick, so
 * BIOS ticks * 65536 + (65536 - count) is a 1.19 MHz clock. No new ISR. */
static int tmr_on;
static unsigned long t0;

static unsigned long pit_now(void)
{
	unsigned long ticks;
	unsigned cnt;
	unsigned char lo, hi, irr;
	_disable();
	outp(0x43, 0x00);                 /* latch channel 0 */
	lo = (unsigned char)inp(0x40);
	hi = (unsigned char)inp(0x40);
	ticks = *(unsigned long far *)MK_FP(0x40, 0x6C);
	outp(0x20, 0x0A);                 /* read the 8259 IRR */
	irr = (unsigned char)inp(0x20);
	_enable();
	cnt = (unsigned)hi << 8 | lo;
	/* A tick that wrapped but hasn't been counted by INT 8 yet. */
	if ((irr & 1) && cnt > 0x8000u) ticks++;
	if (tmr_on != 1) /* mode 3: counter runs twice per tick, use ticks only */
		return ticks << 16;
	return (ticks << 16) + (unsigned)(-(int)cnt);
}

void tmr_init(void)
{
	if (tmr_on == 1) return;
	if (!tmr_on) atexit(tmr_done);
	_disable();
	outp(0x43, 0x34);                 /* ch 0, lo/hi, mode 2 */
	outp(0x40, 0);
	outp(0x40, 0);
	_enable();
	tmr_on = 1;
	t0 = pit_now();
}

void tmr_done(void)
{
	if (tmr_on != 1) return;
	_disable();
	outp(0x43, 0x36);                 /* ch 0, lo/hi, mode 3 (BIOS default) */
	outp(0x40, 0);
	outp(0x40, 0);
	_enable();
	tmr_on = 2;
}

/* 1193182 Hz input: ms = counts * 1000 / 1193182 ~= counts * 55 / 65629.
 * Use counts / 1193.182 via (counts >> 4) * 4096 / 4773 (good to 0.01%). */
unsigned long tmr_ms(void)
{
	unsigned long d;
	if (!tmr_on) tmr_init();
	d = pit_now() - t0;
	/* d * 1000 / 1193182 without overflow: d < 2^32 means < 1 hour at full
	 * precision. Split into high and low parts. */
	return (d / 1193182UL) * 1000UL + (d % 1193182UL) * 1000UL / 1193182UL;
}

/* Raw 1.193182 MHz PIT counts since tmr_init: no division, so it costs
 * only the port reads (a few dozen instructions, vs ~1 ms for tmr_ms). */
unsigned long tmr_ticks(void)
{
	if (!tmr_on) tmr_init();
	return pit_now() - t0;
}

/* ---- Sound ------------------------------------------------------------- */

static int snd_on;
static unsigned char old61;

static void sn_write(unsigned char b)
{
	outp(0xC0, b);
}

void snd_init(void)
{
	int v;
	static int reg;
	if (snd_on) return;
	if (!reg) { reg = 1; atexit(snd_done); }
	old61 = (unsigned char)inp(0x61);
	outp(0x61, (old61 & 0x9F) | 0x60); /* audio mux: bits 5-6 = 11, SN76496 */
	for (v = 0; v < 4; v++) sn_write((unsigned char)(0x9F | (v << 5)));
	snd_on = 1;
}

void snd_done(void)
{
	int v;
	if (!snd_on) return;
	for (v = 0; v < 4; v++) sn_write((unsigned char)(0x9F | (v << 5)));
	/* Put back the audio mux bits only. Bit 2 of this port is the PCjr's
	 * text/graphics select, and snd_init usually ran in graphics mode:
	 * writing the old byte back after gfx_done turned the text screen
	 * into lines on the real machine. */
	outp(0x61, (inp(0x61) & 0x9F) | (old61 & 0x60));
	snd_on = 0;
}

void snd_tone(int voice, unsigned hz, int vol)
{
	unsigned n;
	if (!snd_on) snd_init();
	voice &= 3; if (voice == 3) return;
	if (vol <= 0 || hz == 0) { sn_write((unsigned char)(0x9F | (voice << 5))); return; }
	if (vol > 15) vol = 15;
	n = (unsigned)(3579545UL / 32 / hz);
	if (n > 1023) n = 1023;
	if (n < 1) n = 1;
	sn_write((unsigned char)(0x80 | (voice << 5) | (n & 15)));
	sn_write((unsigned char)(n >> 4));
	sn_write((unsigned char)(0x90 | (voice << 5) | (15 - vol)));
}

void snd_noise(int type, int vol)
{
	if (!snd_on) snd_init();
	if (vol <= 0) { sn_write(0xFF); return; }
	if (vol > 15) vol = 15;
	sn_write((unsigned char)(0xE0 | (type & 7)));
	sn_write((unsigned char)(0xF0 | (15 - vol)));
}

/* ---- Debug log --------------------------------------------------------- */

void dbg_log(const char *fmt, ...)
{
	FILE *f = fopen("LOG.TXT", "a");
	va_list ap;
	if (!f) return;
	fprintf(f, "T=%lu ", tmr_ms());
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}
