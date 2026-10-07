/* JRCUBE: a light-shaded solid over a neon grid, a glowing orb circling
 * it and casting a real-time shadow, all timed to a synthwave song. IBM
 * PCjr, mode 8 (160x200x16), built on the JRGFX fast path.
 *
 * The show (patterns of the song, 1.87 s each; music.h has the sections):
 * a cube that morphs to an octahedron, back, to a tetrahedron and back;
 * a glass floor that reflects it in the drop; then it bursts into its six
 * faces, which tumble, each casting its own shadow, and fly back together.
 *
 * Tricks, cheapest first:
 * - The whole scene except the moving things is a static picture (bg) in
 *   RAM. Each frame restores only the bytes the hidden page drew last
 *   time (fx_track / fx_restore): no clear, no full copy.
 * - The grid scrolls by palette rotation in the retrace IRQ
 *   (fx_pal_anim); beats flash palette entries (fx_pal_flash). Nothing
 *   is redrawn for either.
 * - The solid is the intersection of a cube and the two tetrahedra that
 *   make an octahedron; genshape.py precomputes every morph step, always convex,
 *   so back-face culling and the shadow (a convex hull of the corners
 *   projected from the orb) stay exact. The shadow is copied from a
 *   darkened copy of the floor (fx_poly_src), so the grid shows through.
 * - Faces are lit per corner (diffuse, Blinn highlight, a fill light from
 *   the camera), interpolated down the rows and drawn as a 2x2 ordered
 *   dither between ramp colors (fx_poly_rows). The reflection uses the
 *   same rows checkered with the floor color.
 * - /SPLIT: the sky gets its own 16 colors (a sunset and a striped sun)
 *   by switching the palette at the horizon (fx_split).
 *
 *   JRCUBE            run until Esc, Space or Enter (F: fps counter,
 *                     E: neon edges on/off)
 *   JRCUBE /SPLIT     the 32-color sky (needs the real PCjr to see)
 *   JRCUBE /QUIET     no sound
 *   JRCUBE /BENCH:n   n frames, then log the frame rate to LOG.TXT
 *   JRCUBE /PROF      with /BENCH: time per stage to LOG.TXT
 *   JRCUBE /AT:p      the pictures start at pattern p (tests; the music doesn't)
 *   JRCUBE /GIF:n     n frames at fixed 1/15 s steps, each saved as Fnnn.PPM
 *   JRCUBE /VIDEO:n   the same, plus the sound chip log SND.BIN (video.sh)
 *   JRCUBE /NOEDGE    start without the edges
 *   JRCUBE /ONEPAGE   tests: as if there were no second video page
 *   JRCUBE /RUN:n     tests: the normal mode for n frames
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <malloc.h>
#include "jrgfx.h"
#include "music.h"
#include "shapes.h"

extern const unsigned char jr_font[96 * 8];

/* ---- Scene constants (world units; y up, camera looks along +z) -------- */

#define CAMH 330          /* camera height                               */
#define SS   6085         /* camera pitch (21.8 deg down): sin, cos in 2.14 */
#define CC   15212
#define FY   210          /* focal lengths in pixels (pixels are 2:1 wide) */
#define FX   126
#define HY0  104          /* screen row of the view axis                 */
#define HORIZON 20        /* = HY0 - FY * tan(pitch)                     */
#define SPLIT_ROW 18      /* /SPLIT: first row with the floor palette    */
#define VIEW_Y1 189       /* last 3D row; the scroller is below          */
#define CZ   460          /* solid center                                */
#define CY   104
#define CS   52           /* cube half size (as in genshape.py)          */
#define LR   170          /* light orbit radius                          */
#define LY   300          /* light orbit height, +-LYA                   */
#define LYA  40
#define GRID 64           /* floor grid spacing; lines every GRID/4      */
#define SONG_TICKS ((unsigned long)SONG_PATS * PAT_TICKS)

/* Logical palette. 2-5 and 6-9 are the scrolling grid lines (lit floor,
 * shadowed floor); the IRQ rotates them in step. With /SPLIT the sky
 * shows 2-9 as its own sunset colors (sky_pal). */
enum { C_BLACK, C_BLUE, C_G0, C_G1, C_G2, C_G3, C_S0, C_S1, C_S2, C_S3,
       C_VLINE, C_SVLINE, C_CYAN, C_LCYAN, C_WHITE, C_YELLOW };
static const unsigned char pal[16] = { 0, 1, 13, 1, 1, 1, 5, 0, 0, 0, 13, 5, 3, 11, 15, 14 };
/* the sky's names for 2-9 */
enum { K_RED = 2, K_LRED, K_ORANGE, K_GOLD, K_PURPLE, K_DRED, K_DPURP, K_PINK };
static const unsigned char sky_pal[16] = { 0, 1, 4, 12, 6, 14, 5, 4, 5, 13, 13, 5, 3, 11, 15, 14 };
static int split_on;

/* Fades: each hardware color one step darker (CGA RGBI order) */
static const unsigned char darker[16] = { 0, 0, 0, 1, 0, 1, 4, 8, 0, 1, 2, 3, 4, 5, 6, 7 };

static void dim(unsigned char *p, const unsigned char *src, int steps)
{
	int i, k;
	for (i = 0; i < 16; i++) {
		unsigned char c = src[i];
		for (k = 0; k < steps; k++) c = darker[c];
		p[i] = c;
	}
}

static void pal_fade(int steps)   /* steps 0 (full) to 3 (black) */
{
	unsigned char p[16];
	dim(p, pal, steps);
	fx_pal_set(p);
	if (split_on) { dim(p, sky_pal, steps); fx_split(SPLIT_ROW, p); }
}

static int sin14[1024];   /* sin * 16384 */
#define SIN(a) sin14[(a) & 1023]
#define COS(a) sin14[((a) + 256) & 1023]

static unsigned bg, shbuf;      /* background and its shadowed copy    */

/* /PROF: time per stage, summed over the run (tmr_ticks) */
#define NPROF 9
static int prof;
static unsigned long pt[NPROF], plast;
static const char *pname[NPROF] = { "math", "wait", "restore", "shadow", "faces", "edges", "orb+strip", "m:faces", "m:shadow" };
static void mark(int i)
{
	unsigned long t;
	if (!prof) return;
	t = tmr_ticks();
	pt[i] += t - plast;
	plast = t;
}

/* ---- Shading ramps: 2x2 ordered dither between neighbouring colors ----- */

#define NRAMP 5
static const unsigned char ramp_cols[NRAMP][6] = {
	{ C_BLACK, C_CYAN, C_LCYAN, C_WHITE, 255 },           /* chrome   */
	{ C_BLACK, C_SVLINE, C_VLINE, C_WHITE, 255 },         /* magenta  */
	{ C_BLACK, C_SVLINE, C_YELLOW, C_WHITE, 255 },        /* sunset   */
	{ C_BLACK, C_SVLINE, C_CYAN, C_LCYAN, C_WHITE, 255 }, /* violet   */
	{ C_BLACK, C_BLUE, C_LCYAN, C_WHITE, 255 },           /* electric */
};
/* which ramp each of the 14 planes uses (genshape.py's plane order) */
static const unsigned char plane_ramp[14] = { 0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4 };
/* [ramp][0-255]: even-row byte, [ramp][256-511]: odd-row byte, by shade.
 * refl: the same, dimmer, checkered with the floor (the glass floor) */
static unsigned char __near ramp[NRAMP][512], refl[NRAMP][512];

/* n row patterns at SS:DI from shade I (8.8, DX) stepping dI (SI) per
 * row, through the ramp at SS:BX; the first row is even if AX is 0.
 * XLAT picks the byte; INC/DEC BH flips between the even and odd half. */
extern void rows_asm(unsigned dst, unsigned I, unsigned dI, unsigned n, unsigned tab, unsigned odd);
#pragma aux rows_asm = \
	"push ds" "push es" "push ss" "pop ds" "push ss" "pop es" \
	"or ax,ax" "jz RA1" "inc bh" "mov al,dh" "xlat" "stosb" "add dx,si" "dec bh" "dec cx" \
	"RA1:" "shr cx,1" "pushf" "jcxz RA3" \
	"RA2:" "mov al,dh" "xlat" "stosb" "add dx,si" "inc bh" \
	"mov al,dh" "xlat" "stosb" "add dx,si" "dec bh" "loop RA2" \
	"RA3:" "popf" "jnc RA4" "mov al,dh" "xlat" "stosb" \
	"RA4:" "pop es" "pop ds" \
	parm [di] [dx] [si] [cx] [bx] [ax] modify [ax cx dx si di];

static void make_ramps(void)
{
	int r, i;
	for (r = 0; r < NRAMP; r++) {
		unsigned char *e = ramp[r], *o = ramp[r] + 256;
		int n = 0, levels;
		while (ramp_cols[r][n + 1] != 255) n++;   /* n intervals */
		levels = n * 4 + 1;
		for (i = 0; i < 256; i++) {
			int l = (i * levels) >> 8, seg = l >> 2, k = l & 3;
			unsigned char a, b;
			if (seg >= n) { seg = n - 1; k = 4; }
			a = ramp_cols[r][seg]; b = ramp_cols[r][seg + 1];
			switch (k) {
			case 0: e[i] = FX_PAIR(a, a); o[i] = FX_PAIR(a, a); break;
			case 1: e[i] = FX_PAIR(b, a); o[i] = FX_PAIR(a, a); break;
			case 2: e[i] = FX_PAIR(b, a); o[i] = FX_PAIR(a, b); break;
			case 3: e[i] = FX_PAIR(b, b); o[i] = FX_PAIR(a, b); break;
			default: e[i] = FX_PAIR(b, b); o[i] = FX_PAIR(b, b); break;
			}
		}
		for (i = 0; i < 256; i++) {   /* every other pixel the floor */
			int j = i;
			refl[r][i] = (unsigned char)((e[j] & 0xF0) | C_BLUE);
			refl[r][256 + i] = (unsigned char)((C_BLUE << 4) | (o[j] & 0x0F));
		}
	}
}

/* ---- Math -------------------------------------------------------------- */

/* 16 x 16 -> 32 signed multiply and 32 / 16 divide, one instruction
 * each (C would call the 32-bit library routines). idiv32's quotient
 * must fit in an int. */
extern long imul16(int a, int b);
#pragma aux imul16 = "imul dx" parm [ax] [dx] value [dx ax];
extern int idiv32(long a, int b);
#pragma aux idiv32 = "idiv bx" parm [dx ax] [bx] value [ax] modify [dx];
/* (a * b) >> 14, >> 13 and >> 8 for results that fit an int: multiply,
 * then shift the 32-bit product left and keep the high word (a 32-bit
 * shift right would be a loop on the 8088) */
extern int mul14(int a, int b);
#pragma aux mul14 = "imul dx" "shl ax,1" "rcl dx,1" "shl ax,1" "rcl dx,1" parm [ax] [dx] value [dx] modify [ax];
extern int mul13(int a, int b);
#pragma aux mul13 = "imul dx" "shl ax,1" "rcl dx,1" "shl ax,1" "rcl dx,1" "shl ax,1" "rcl dx,1" parm [ax] [dx] value [dx] modify [ax];
extern int mul8(int a, int b);
#pragma aux mul8 = "imul dx" "mov al,ah" "mov ah,dl" parm [ax] [dx] value [ax] modify [dx];

/* square root from a table: shift into 4096-16383, look up (about 1%) */
static unsigned sqt[256];   /* sqrt(64 i + 32) * 16 */
static void make_sqrt(void)
{
	unsigned i, r = 0;
	unsigned long sq = 0;
	for (i = 0; i < 256; i++) {
		unsigned long v = (64UL * i + 32) * 256;
		while (sq + 2 * r + 1 <= v) { sq += 2 * r + 1; r++; }
		sqt[i] = r;
	}
}
static unsigned fsqrt(unsigned long v)
{
	int e = 0;
	while (v >= 16384) { v >>= 2; e++; }
	return (sqt[(unsigned)v >> 6] << e) >> 4;
}

typedef struct { int x, y, z; } v3;
typedef struct { int sx, sy; } p2;

/* n / d clamped to +-16000 (d > 0), so the IDIV can't overflow */
static int cdiv(long n, int d)
{
	int hi = (int)(n >> 16), q = d >> 2;   /* |n| < (d / 4) * 65536 is safe */
	if (hi >= q) return 16000;
	if (hi < -q) return -16000;
	return idiv32(n, d);
}

/* world point to screen; returns 0 if too close to the camera */
static int project(int X, int Y, int Z, int *sx, int *sy)
{
	int dy = Y - CAMH, zp = mul14(Z, CC) - mul14(dy, SS), yp;
	if (zp < 64) return 0;
	yp = mul14(dy, CC) + mul14(Z, SS);
	*sx = cdiv(imul16(80, zp) + imul16(X, FX), zp);
	*sy = cdiv(imul16(HY0, zp) - imul16(yp, FY), zp);
	return 1;
}

static int depth(int Y, int Z) { return mul14(Z, CC) - mul14(Y - CAMH, SS); }

/* floor z seen at screen row y (y > HORIZON) */
static long floor_z(int y)
{
	long k = HY0 - y;
	return (long)CAMH * ((long)CC * FY + k * SS) / ((long)SS * FY - k * CC);
}

/* 0-256 eased: slow start and end (smoothstep), x 0-256 */
static int ease(int x)
{
	long v;
	if (x <= 0) return 0;
	if (x >= 256) return 256;
	v = (long)x * x * (768 - 2 * x);
	return (int)(v >> 16);
}

/* ---- Background: sky, mountains, grid floor ---------------------------- */

static void bpset(unsigned seg, int x, int y, int c)
{
	unsigned char far *p;
	if ((unsigned)x >= 160 || (unsigned)y >= 200) return;
	p = (unsigned char far *)MK_FP(seg, jr_rowoff[y] + (x >> 1));
	*p = (unsigned char)(x & 1 ? (*p & 0xF0) | c : (*p & 0x0F) | (c << 4));
}

static unsigned rs = 12345;
static int rnd(int n) { rs ^= rs << 7; rs ^= rs >> 9; rs ^= rs << 8; return (int)(rs % (unsigned)n); }

/* 2x2 ordered dither row pattern between a and b, k/4 of b */
static unsigned dith(int a, int b, int k, int y)
{
	static const unsigned char m[2][2] = { {1, 3}, {4, 2} }; /* thresholds, 1-4 */
	int l = k >= m[y & 1][0] ? b : a, r = k >= m[y & 1][1] ? b : a;
	return FX_PAIR(l, r);
}

/* clips a line to columns 0-159 (rows are already on screen) */
static int clip_x(int *ax, int *ay, int *bx, int *by)
{
	int k;
	for (k = 0; k < 2; k++) {
		int lim = k ? 159 : 0;
		int ao = k ? *ax > lim : *ax < lim, bo = k ? *bx > lim : *bx < lim;
		if (ao && bo) return 0;
		if (ao) { *ay += (int)((long)(*by - *ay) * (lim - *ax) / (*bx - *ax)); *ax = lim; }
		else if (bo) { *by += (int)((long)(*ay - *by) * (lim - *bx) / (*ax - *bx)); *bx = lim; }
	}
	return 1;
}

static void draw_floor(unsigned seg, int shadow)
{
	int y, j, prev = 999, yfar, k, ring[4];
	int base = shadow ? C_BLACK : C_BLUE, g0 = shadow ? C_S0 : C_G0, vl = shadow ? C_SVLINE : C_VLINE;
	long z0 = floor_z(VIEW_Y1), z;
	fx_target(seg);
	/* horizontal lines every GRID/4 use entry g0 + (k & 3); they stop
	 * where the lit ones come closer than 3 rows: haze from there up */
	for (k = 0; ; k++) {
		int sx, sy;
		z = (z0 / (GRID / 4) + 1) * (GRID / 4) + (long)k * (GRID / 4);
		project(0, 0, (int)z, &sx, &sy);
		if (k >= 4 && ring[k & 3] - sy < 3) break;   /* lit lines < 3 rows apart */
		ring[k & 3] = prev = sy;
	}
	yfar = prev;
	for (y = HORIZON + 1; y <= VIEW_Y1; y++) {
		int p;
		if (y < yfar) {   /* light magenta -> magenta -> base, 2x2 dithered */
			int l = (y - HORIZON - 1) * 9 / (yfar - HORIZON - 1);
			p = l < 4 ? dith(C_VLINE, C_SVLINE, l, y) : dith(C_SVLINE, base, l - 4 > 4 ? 4 : l - 4, y);
		} else p = FX_SOLID(base);
		fx_hspan(y, 0, 159, (unsigned char)p);
	}
	for (k = 0; ; k++) {
		int sx, sy;
		z = (z0 / (GRID / 4) + 1) * (GRID / 4) + (long)k * (GRID / 4);
		project(0, 0, (int)z, &sx, &sy);
		if (sy < yfar) break;
		fx_hspan(sy, 0, 159, FX_SOLID(g0 + (int)((z / (GRID / 4)) & 3)));
	}
	/* converging lines, up to the haze */
	for (j = -24; j <= 24; j++) {
		int ax, ay, bx, by;
		long X = (long)j * GRID;
		project((int)X, 0, (int)floor_z(VIEW_Y1), &ax, &ay);
		project((int)X, 0, (int)floor_z(yfar), &bx, &by);
		if (clip_x(&ax, &ay, &bx, &by)) fx_line(ax, ay, bx, by, vl);
	}
	fx_target(0);
}

static void draw_sky(unsigned seg)
{
	int y, i, x, minh = split_on ? 4 : 1;
	fx_target(seg);
	for (y = 0; y <= HORIZON; y++) {
		unsigned p;
		if (!split_on) {
			if (y < 4) p = FX_SOLID(C_BLACK);
			else if (y < 8) p = dith(C_BLACK, C_BLUE, y - 4, y);
			else if (y < 11) p = FX_SOLID(C_BLUE);
			else if (y < 15) p = dith(C_BLUE, C_SVLINE, y - 11, y);
			else p = dith(C_SVLINE, C_VLINE, (y - 15) * 4 / 5, y);
		} else {   /* sunset, in the sky's own colors */
			if (y < 2) p = FX_SOLID(C_BLACK);
			else if (y < 6) p = dith(C_BLACK, K_DPURP, y - 2, y);
			else if (y < 10) p = dith(K_DPURP, K_DRED, y - 6, y);
			else if (y < 14) p = dith(K_DRED, K_PINK, y - 10, y);
			else p = dith(K_PINK, K_ORANGE, y - 14 > 4 ? 4 : y - 14, y);
		}
		fx_hspan(y, 0, 159, (unsigned char)p);
	}
	if (split_on) {   /* a striped sun setting behind the mountains */
		for (y = 2; y <= HORIZON; y++) {
			int dy = HORIZON - y, w2 = 324 - dy * dy, hw, c;   /* radius 18 rows */
			if (w2 <= 0) continue;
			if (dy < 9 && (dy % 3) == 1) continue;              /* the stripes */
			hw = (int)(fsqrt((unsigned long)w2 * 64) * 12 / 80); /* a wide sun: x radius = rows * 1.2 */
			c = dy > 13 ? K_GOLD : dy > 9 ? K_ORANGE : dy > 5 ? K_LRED : K_RED;
			fx_hspan(y, 80 - hw, 79 + hw, FX_SOLID(c));
		}
	}
	for (i = 0; i < 40; i++) {
		x = rnd(160); y = rnd(12);
		bpset(seg, x, y, i & 3 || split_on ? C_WHITE : C_G0 + (i & 12) / 4);
	}
	/* mountains: a jagged line, black below it, neon edge on top */
	{
		int h = 3 + minh, xx;
		for (xx = 0; xx < 160; xx += 4) {
			int nh = minh + rnd(5) + (xx > 40 && xx < 70 ? 3 : 0) + (xx > 110 && xx < 130 ? 3 : 0), t;
			for (t = 0; t < 4; t++) {
				int hh = h + (nh - h) * t / 4, yy;
				for (yy = HORIZON - hh + 1; yy <= HORIZON; yy++) bpset(seg, xx + t, yy, C_BLACK);
				bpset(seg, xx + t, HORIZON - hh, C_VLINE);
			}
			h = nh;
		}
	}
	fx_hspan(HORIZON, 0, 159, FX_SOLID(C_VLINE));
	fx_target(0);
}

/* ---- Scroller ---------------------------------------------------------- */

static const char msg[] =
	"     *** JRCUBE ***   SHADOWS, LIGHT AND MUSIC IN REAL TIME ON AN IBM PCjr ... "
	"MADE WITH CODEJR ...      ";
#define SROWS 8
#define SCROLL_Y 191
static unsigned char far *strip;
static unsigned sw;   /* bytes per strip row */

static void make_strip(void)
{
	static const unsigned char rowc[SROWS] = { C_WHITE, C_WHITE, C_LCYAN, C_LCYAN, C_CYAN, C_VLINE, C_VLINE, C_SVLINE };
	int n = (int)strlen(msg), i, r, k;
	sw = (unsigned)n * 4 + 80;
	strip = (unsigned char far *)_fmalloc((unsigned long)sw * SROWS);
	if (!strip) return;
	_fmemset(strip, 0, sw * SROWS);
	for (i = 0; i < n; i++) {
		unsigned ch = (unsigned char)msg[i];
		const unsigned char *g = jr_font + ((ch < 32 || ch > 127 ? '?' : ch) - 32) * 8;
		for (r = 0; r < SROWS; r++) {
			unsigned char bits = g[r], c = rowc[r];
			unsigned char two[4];   /* 2 font bits -> 1 packed byte */
			unsigned char far *p = strip + (unsigned)r * sw + i * 4;
			two[0] = 0; two[1] = c; two[2] = (unsigned char)(c << 4); two[3] = (unsigned char)(c * 0x11);
			for (k = 0; k < 4; k++, bits <<= 2) p[k] = two[bits >> 6];
		}
	}
	/* the last 80 bytes repeat the start, so any window is contiguous */
	for (r = 0; r < SROWS; r++)
		_fmemcpy(strip + (unsigned)r * sw + n * 4, strip + (unsigned)r * sw, 80);
}

static void draw_strip(unsigned pos)
{
	int r;
	if (!strip) return;
	pos %= sw - 80;
	for (r = 0; r < SROWS; r++)
		_fmemcpy(MK_FP(fx_seg, jr_rowoff[SCROLL_Y + r]), strip + (unsigned)r * sw + pos, 80);
	fx_dirty(0, SCROLL_Y, 159, SCROLL_Y + SROWS - 1);   /* one-page mode copies only dirty areas */
}

/* ---- The orb ----------------------------------------------------------- */

#define NORB 3
static fx_sprite *orb[NORB], *flare;
static int orbw[NORB] = { 18, 14, 10 }, orbh[NORB] = { 34, 26, 19 };
static unsigned char orbpix[NORB][18 * 34], flarepix[26 * 46];
#define FLAREW 26
#define FLAREH 46

/* glow pool under the orb (on the floor, so flattened) and two trail
 * sparks; same shading rule, different shapes */
static fx_sprite *pool[NORB], *spark[2];
static int poolw[NORB] = { 26, 20, 14 }, poolh[NORB] = { 9, 7, 5 };
static int sparkw[2] = { 6, 4 }, sparkh[2] = { 11, 7 };
static unsigned char poolpix[NORB][26 * 9], sparkpix[2][6 * 11];

/* w x h glow: d is the squared distance from the center, 4096 at the edge */
static fx_sprite *glow(unsigned char *pix, int w, int h, int kind)
{
	int x, y;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			int dx = (2 * x + 1 - w) * 64 / w, dy = (2 * y + 1 - h) * 64 / h;
			unsigned d = dx * dx + dy * dy;
			int c, ck = (x + y) & 1;
			if (kind == 0) {        /* orb */
				if (d < 500) c = C_WHITE;
				else if (d < 1300) c = ck ? C_WHITE : C_YELLOW;
				else if (d < 2100) c = C_YELLOW;
				else if (d < 2900) c = ck ? C_YELLOW : C_VLINE;
				else if (d < 4096) c = ck ? 0 : C_VLINE;   /* halo: half see-through */
				else c = 0;
			} else if (kind == 1) { /* pool of light on the floor */
				if (d < 900) c = ck ? C_WHITE : C_VLINE;
				else if (d < 2200) c = C_VLINE;
				else if (d < 4096) c = ck ? 0 : C_VLINE;
				else c = 0;
			} else if (kind == 2) { /* spark */
				if (d < 1200) c = C_YELLOW;
				else if (d < 4096) c = ck ? 0 : C_VLINE;
				else c = 0;
			} else {                /* flare: the orb on a kick, with rays */
				int ray = (dx > -5 && dx < 5) || (dy > -3 && dy < 3);
				if (d < 300) c = C_WHITE;
				else if (d < 700) c = ck ? C_WHITE : C_YELLOW;
				else if (d < 1100) c = C_YELLOW;
				else if (d < 1700) c = ck ? C_YELLOW : C_WHITE;
				else if (ray && d < 4096) c = ck ? C_WHITE : 0;
				else if (d < 2600) c = ck ? 0 : C_VLINE;
				else c = 0;
			}
			pix[y * w + x] = (unsigned char)c;
		}
	return fx_compile(pix, w, h, 0);
}

static void make_orbs(void)
{
	int s;
	for (s = 0; s < NORB; s++) {
		orb[s] = glow(orbpix[s], orbw[s], orbh[s], 0);
		pool[s] = glow(poolpix[s], poolw[s], poolh[s], 1);
	}
	for (s = 0; s < 2; s++) spark[s] = glow(sparkpix[s], sparkw[s], sparkh[s], 2);
	flare = glow(flarepix, FLAREW, FLAREH, 3);
}

/* ---- The solid --------------------------------------------------------- */

#define MAXV SHAPE_MAXV
static v3 cv[MAXV];       /* corners, world             */
static p2 cp[MAXV];       /* corners, screen            */
static p2 rp[MAXV];       /* corners mirrored in the floor, screen */
static int nv;
static int rm[3][3];      /* rotation, 2.14             */
static v3 ax3[3];         /* rotated half-axes (length CS), for the pieces */
static v3 ctr, lt;        /* center, light              */
static int cur_cz = CZ;   /* the center's z this frame  */
static v3 ldir[MAXV];     /* toward the light, unit * 256 */
static v3 hv[MAXV];       /* half vector per corner, length 256 */
static v3 vdir;           /* toward the camera, length 256 */
static unsigned char spec_tab[129];

/* the current shape, from shapes.c */
static const signed char __far *shp;
static int sh_nf, sh_ne;
static const signed char __far *sh_v, *sh_f, *sh_e;

static void use_shape(int morph, int step)
{
	const signed char __far *p;
	if (step < 0) step = 0;
	if (step > SHAPE_STEPS) step = SHAPE_STEPS;
	p = shp = shape_data + shape_off[morph * (SHAPE_STEPS + 1) + step];
	nv = p[0]; sh_nf = p[1]; sh_ne = p[2];
	sh_v = p + 3;
	sh_f = sh_v + 3 * nv;
	p = sh_f;
	{ int f; for (f = 0; f < sh_nf; f++) p += 2 + p[1]; }
	sh_e = p;
}

static void make_spec(void)
{
	int i;
	for (i = 0; i <= 128; i++) {   /* 255 * (i / 128) ^ 24, by squaring in 0.16 */
		unsigned long x = (unsigned long)i << 9, x2, x4, x8, x16;
		if (i == 128) { spec_tab[i] = 255; continue; }
		x2 = x * x >> 16; x4 = x2 * x2 >> 16; x8 = x4 * x4 >> 16; x16 = x8 * x8 >> 16;
		spec_tab[i] = (unsigned char)((x16 * x8 >> 16) * 255 >> 16);
	}
}

/* R = Rz(a3) * Ry(a2) * Rx(a1); the center bobs */
static void rotation(int a1, int a2, int a3, int bob)
{
	int sa = SIN(a1), ca = COS(a1), sb = SIN(a2), cb = COS(a2), sc = SIN(a3), cc = COS(a3);
	int ccsb = mul14(cc, sb), scsb = mul14(sc, sb), i;
	rm[0][0] = mul14(cc, cb);
	rm[0][1] = mul14(ccsb, sa) - mul14(sc, ca);
	rm[0][2] = mul14(ccsb, ca) + mul14(sc, sa);
	rm[1][0] = mul14(sc, cb);
	rm[1][1] = mul14(scsb, sa) + mul14(cc, ca);
	rm[1][2] = mul14(scsb, ca) - mul14(cc, sa);
	rm[2][0] = -sb;
	rm[2][1] = mul14(cb, sa);
	rm[2][2] = mul14(cb, ca);
	for (i = 0; i < 3; i++) {   /* column i is axis i */
		ax3[i].x = mul14(rm[0][i], CS);
		ax3[i].y = mul14(rm[1][i], CS);
		ax3[i].z = mul14(rm[2][i], CS);
	}
	ctr.x = 0; ctr.y = CY + bob; ctr.z = cur_cz;
}

static void solid_geometry(int mirror)
{
	int i;
	const signed char __far *v = sh_v;
	for (i = 0; i < nv; i++, v += 3) {
		int x = v[0], y = v[1], z = v[2];
		cv[i].x = ctr.x + mul14(rm[0][0], x) + mul14(rm[0][1], y) + mul14(rm[0][2], z);
		cv[i].y = ctr.y + mul14(rm[1][0], x) + mul14(rm[1][1], y) + mul14(rm[1][2], z);
		cv[i].z = ctr.z + mul14(rm[2][0], x) + mul14(rm[2][1], y) + mul14(rm[2][2], z);
		project(cv[i].x, cv[i].y, cv[i].z, &cp[i].sx, &cp[i].sy);
		if (mirror) project(cv[i].x, -cv[i].y, cv[i].z, &rp[i].sx, &rp[i].sy);
	}
}

static void light_at(int t, v3 *l)
{
	int a = t * 5;
	l->x = mul14(LR, COS(a));
	l->z = cur_cz + mul14(LR, SIN(a));
	l->y = LY + mul14(LYA, SIN(a));
}

/* unit vector * 256 (len = its length, > 64) */
static void unit(v3 *o, int x, int y, int z, unsigned len)
{
	int inv = idiv32(1L << 21, (int)len);
	o->x = mul13(x, inv);
	o->y = mul13(y, inv);
	o->z = mul13(z, inv);
}

static long sq3(int x, int y, int z) { return imul16(x, x) + imul16(y, y) + imul16(z, z); }

/* per corner, on first use in a frame: direction to the light and the
 * Blinn half vector (both unit * 256); one table square root and one
 * divide each */
static unsigned char lit[MAXV];

static void view_light(void)
{
	int vx = -ctr.x, vy = CAMH - ctr.y, vz = -ctr.z;     /* view dir, from the center */
	unit(&vdir, vx, vy, vz, fsqrt(sq3(vx, vy, vz)));
	memset(lit, 0, sizeof lit);
}

static void corner_light(int i)
{
	int dx = lt.x - cv[i].x, dy = lt.y - cv[i].y, dz = lt.z - cv[i].z, hx, hy, hz;
	unsigned hl;
	lit[i] = 1;
	unit(&ldir[i], dx, dy, dz, fsqrt(sq3(dx, dy, dz)));
	hx = ldir[i].x + vdir.x; hy = ldir[i].y + vdir.y; hz = ldir[i].z + vdir.z;
	hl = fsqrt(sq3(hx, hy, hz));
	if (hl > 64) unit(&hv[i], hx, hy, hz, hl);
	else hv[i].x = hv[i].y = hv[i].z = 0;
}

static int dot8(const v3 *a, const v3 *b) /* (a . b) >> 8 */
{
	return mul8(a->x, b->x) + mul8(a->y, b->y) + mul8(a->z, b->z);
}

/* light at corner v of a face with unit normal n (* 256): 0-255.
 * Ambient, a dim fill light from the camera, the orb (diffuse) and its
 * highlight (Blinn, exponent 24). */
static int shade(const v3 *n, int v)
{
	int d, s, f, i;
	if (!lit[v]) corner_light(v);
	d = dot8(n, &ldir[v]); s = dot8(n, &hv[v]) >> 1; f = dot8(n, &vdir);
	i = 24 + (f > 0 ? f / 5 : 0);
	if (d > 0) i += (d * 5) >> 3;
	if (s > 0) i += spec_tab[s > 128 ? 128 : s];
	return i > 255 ? 255 : i;
}

/* plane id -> world unit normal (* 256): a rotated axis, or a rotated
 * (+-1, +-1, +-1) / sqrt(3) for the tetrahedron planes */
static void plane_normal(int pid, v3 *n)
{
	static const signed char tn[8][3] = {
		{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1},
		{-1, -1, -1}, {-1, 1, 1}, {1, -1, 1}, {1, 1, -1},
	};
	if (pid < 6) {
		int a = pid >> 1, s = pid & 1 ? 1 : -1;
		n->x = s * (rm[0][a] >> 6); n->y = s * (rm[1][a] >> 6); n->z = s * (rm[2][a] >> 6);
	} else {
		const signed char *t = tn[pid - 6];
		int k, x = 0, y = 0, z = 0;
		for (k = 0; k < 3; k++) {
			x += t[k] * rm[0][k]; y += t[k] * rm[1][k]; z += t[k] * rm[2][k];
		}
		n->x = mul14(x, 148); n->y = mul14(y, 148); n->z = mul14(z, 148);
	}
}

/* Visible faces: polygon, row patterns (shade from the top corner's light
 * to the bottom one's), all before the page is free. */
#define MAXDRAW 16
static int ndraw, dn[MAXDRAW], dxy[MAXDRAW][2 * SHAPE_MAXFV];
static unsigned char __near drow[MAXDRAW][200];
static unsigned vis_f;   /* bit per face of the shape */

/* the reflection fades out over REFL_ROWS rows below its top (refl_y0) */
#define REFL_ROWS 32
static int refl_y0;
static int flat_shade;   /* the pieces: one light value per face */

static void add_face(const signed char __far *f, const v3 *nu, const p2 *pts, int r, int mirror)
{
	int n = f[1], k, top = 0, bot = 0, ytop, ybot, I, dI, *xy;
	if (ndraw >= MAXDRAW) return;
	xy = dxy[ndraw];
	for (k = 0; k < n; k++) {
		const p2 *p = &pts[f[2 + k]];
		xy[2 * k] = p->sx; xy[2 * k + 1] = p->sy;
		if (p->sy < xy[2 * top + 1]) top = k;
		if (p->sy > xy[2 * bot + 1]) bot = k;
	}
	ytop = xy[2 * top + 1]; ybot = xy[2 * bot + 1];
	I = shade(nu, f[2 + top]) << 7;
	if (mirror) {   /* fading: the light at the top, 0 at refl_y0 + REFL_ROWS */
		int yend = refl_y0 + REFL_ROWS;
		if (ytop >= yend) return;
		I += 64 << 7;   /* the underside is in shade: lift it, glass is shiny */
		if (I > (255 << 7)) I = 255 << 7;
		I = idiv32(imul16(I, yend - ytop), REFL_ROWS);
		dI = -idiv32((long)I, yend - ytop);
		if (ybot > yend) ybot = yend;
	} else {
		dI = flat_shade ? I >> 7 : shade(nu, f[2 + bot]);
		dI = ybot > ytop ? idiv32((long)((dI << 7) - I), ybot - ytop) : 0;
	}
	if (ytop < 0) { I += dI * -ytop; ytop = 0; }
	if (ybot > VIEW_Y1) ybot = VIEW_Y1;
	if (mirror) {   /* the polygon stops where the fade ends */
		int k2;
		for (k2 = 0; k2 < n; k2++) if (xy[2 * k2 + 1] > ybot) xy[2 * k2 + 1] = ybot;
	}
	if (ybot >= ytop)
		rows_asm(FP_OFF(&drow[ndraw][ytop]), (unsigned)I << 1, (unsigned)dI << 1, ybot - ytop + 1,
		         FP_OFF(mirror ? refl[r] : ramp[r]), ytop & 1);
	dn[ndraw++] = n;
}

static int nrefl;   /* the first nrefl entries are the reflection */

static void prep_faces(int mirror)
{
	int pass;
	const signed char __far *f;
	int i;
	ndraw = 0; vis_f = 0;
	if (mirror)
		for (i = 0, refl_y0 = 32767; i < nv; i++) if (rp[i].sy < refl_y0) refl_y0 = rp[i].sy;
	for (pass = mirror ? 0 : 1; pass < 2; pass++) {
		for (i = 0, f = sh_f; i < sh_nf; i++, f += 2 + f[1]) {
			v3 nu;
			const v3 *p = &cv[f[2]];
			long vis;
			plane_normal(f[0], &nu);
			if (!pass) {   /* mirrored: y flips, for the normal and the point */
				vis = imul16(nu.x, p->x) + imul16(-nu.y, -p->y - CAMH) + imul16(nu.z, p->z);
				if (vis < 0) add_face(f, &nu, rp, plane_ramp[f[0]], 1);
			} else {
				vis = imul16(nu.x, p->x) + imul16(nu.y, p->y - CAMH) + imul16(nu.z, p->z);
				if (vis < 0) { vis_f |= 1u << i; add_face(f, &nu, cp, plane_ramp[f[0]], 0); }
			}
		}
		if (!pass) nrefl = ndraw;
	}
	if (!mirror) nrefl = 0;
}

static void draw_faces(int from, int to)
{
	int f;
	for (f = from; f < to; f++) fx_poly_rows(dn[f], dxy[f], drow[f]);
}

/* neon edges: white on the silhouette, light cyan inside */
static void draw_edges(void)
{
	const signed char __far *e = sh_e;
	int i;
	for (i = 0; i < sh_ne; i++, e += 4) {
		int a = (vis_f >> e[2]) & 1, b = (vis_f >> e[3]) & 1;
		if (a | b) {
			const p2 *p = &cp[e[0]], *q = &cp[e[1]];
			fx_line(p->sx, p->sy, q->sx, q->sy, a & b ? C_LCYAN : C_WHITE);
		}
	}
}

/* ---- Shadow ------------------------------------------------------------ */

typedef struct { int x, y; } s2;

static long cross(const s2 *o, const s2 *a, const s2 *b)
{
	return imul16(a->x - o->x, b->y - o->y) - imul16(a->y - o->y, b->x - o->x);
}

/* corner i projected from the light onto the floor, then to the screen */
static int floor_shadow(int i, s2 *q)
{
	int den = lt.y - cv[i].y, f, x, z;
	if (den < 8) den = 8;
	f = idiv32((long)lt.y << 8, den);        /* 8.8 stretch factor */
	x = lt.x + mul8(cv[i].x - lt.x, f);
	z = lt.z + mul8(cv[i].z - lt.z, f);
	if (z < 40) z = 40;
	return project(x, 0, z, &q->x, &q->y);
}

#define MAXSH 8
static int nshadow, shn[MAXSH], sxy[MAXSH][2 * (MAXV + 2)];

/* The shadow of the whole solid: the convex hull of its corners' shadows
 * (projection keeps convexity while every point is in front of the
 * camera, which the orbit ensures: floor points nearer than z = 40 are
 * pushed out to it). */
static void prep_shadow(void)
{
	s2 p[MAXV], h[2 * MAXV + 2];
	int i, j, n = 0, m = 0;
	nshadow = 0;
	for (i = 0; i < nv; i++) {
		s2 q;
		if (!floor_shadow(i, &q)) return;
		for (j = n; j > 0 && (p[j - 1].x > q.x || (p[j - 1].x == q.x && p[j - 1].y > q.y)); j--) p[j] = p[j - 1];
		p[j] = q; n++;
	}
	/* convex hull (monotone chain) */
	for (i = 0; i < n; i++) {
		while (m >= 2 && cross(&h[m - 2], &h[m - 1], &p[i]) <= 0) m--;
		h[m++] = p[i];
	}
	for (i = n - 2, j = m + 1; i >= 0; i--) {
		while (m >= j && cross(&h[m - 2], &h[m - 1], &p[i]) <= 0) m--;
		h[m++] = p[i];
	}
	m--;
	if (m < 3) return;
	for (i = 0; i < m; i++) { sxy[0][2 * i] = h[i].x; sxy[0][2 * i + 1] = h[i].y; }
	shn[0] = m;
	nshadow = 1;
}

/* ---- The pieces: the cube comes apart ----------------------------------
 * e (0-256) is how far apart; each face flies out along its normal,
 * tumbles about one of its own edges' directions, and sinks a little.
 * Its 4 corners go in cv[4f..4f+3]. Drawn far to near, both sides. */
static int npieces, porder[6], pdep[6];
static v3 pnorm[6];

static void piece_geometry(int e, int tumble)
{
	int f, k;
	nv = 24; npieces = 6;
	for (f = 0; f < 6; f++) {
		int a = f >> 1, s = f & 1 ? 1 : -1, b = (a + 1) % 3, c = (a + 2) % 3;
		int th = mul8(tumble * (f + 2) * (f & 1 ? 1 : -1), e), sn = SIN(th), cs = COS(th);
		int out = 256 + mul8(e, 420), lift = mul8(e, 40), miny = 32767;
		v3 N, U, W, C;
		N.x = s * ax3[a].x; N.y = s * ax3[a].y; N.z = s * ax3[a].z;
		U = ax3[b];
		/* W' = W cos + N sin, N' = N cos - W sin */
		W.x = mul14(ax3[c].x, cs) + mul14(N.x, sn); W.y = mul14(ax3[c].y, cs) + mul14(N.y, sn); W.z = mul14(ax3[c].z, cs) + mul14(N.z, sn);
		pnorm[f].x = mul14(N.x, cs) - mul14(ax3[c].x, sn);
		pnorm[f].y = mul14(N.y, cs) - mul14(ax3[c].y, sn);
		pnorm[f].z = mul14(N.z, cs) - mul14(ax3[c].z, sn);
		C.x = ctr.x + mul8(N.x, out); C.y = ctr.y + mul8(N.y, out) - lift; C.z = ctr.z + mul8(N.z, out);
		for (k = 0; k < 4; k++) {
			int su = k == 1 || k == 2 ? 1 : -1, sw = k >= 2 ? 1 : -1;
			v3 *p = &cv[4 * f + k];
			p->x = C.x + su * U.x + sw * W.x;
			p->y = C.y + su * U.y + sw * W.y;
			p->z = C.z + su * U.z + sw * W.z;
			if (p->y < miny) miny = p->y;
		}
		if (miny < 2)   /* not through the floor */
			for (k = 0; k < 4; k++) cv[4 * f + k].y += 2 - miny;
		for (k = 0; k < 4; k++) project(cv[4 * f + k].x, cv[4 * f + k].y, cv[4 * f + k].z, &cp[4 * f + k].sx, &cp[4 * f + k].sy);
		/* depth of the center, for the drawing order */
		pdep[f] = depth((cv[4 * f].y + cv[4 * f + 2].y) >> 1, (cv[4 * f].z + cv[4 * f + 2].z) >> 1);
		porder[f] = f;
	}
	for (f = 1; f < 6; f++)   /* far first */
		for (k = f; k > 0 && pdep[porder[k - 1]] < pdep[porder[k]]; k--) {
			int t = porder[k]; porder[k] = porder[k - 1]; porder[k - 1] = t;
		}
}

static void prep_pieces(void)
{
	static signed char face4[2 + 4] = { 0, 4, 0, 1, 2, 3 };
	int i, f, k;
	ndraw = 0; nrefl = 0; nshadow = 0;
	flat_shade = 1;
	for (i = 0; i < 6; i++) {
		v3 nu, *c = &cv[4 * (f = porder[i])];
		long vis;
		nu.x = mul8(pnorm[f].x, 65536L / CS); nu.y = mul8(pnorm[f].y, 65536L / CS); nu.z = mul8(pnorm[f].z, 65536L / CS);
		vis = imul16(nu.x, c->x) + imul16(nu.y, c->y - CAMH) + imul16(nu.z, c->z);
		if (vis > 0) { nu.x = -nu.x; nu.y = -nu.y; nu.z = -nu.z; }   /* we see its back */
		for (k = 0; k < 4; k++) face4[2 + k] = (signed char)(4 * f + k);
		add_face(face4, &nu, cp, plane_ramp[2 * (f >> 1)], 0);
		if (nshadow < MAXSH) {   /* each piece's own shadow */
			s2 q;
			for (k = 0; k < 4; k++) {
				if (!floor_shadow(4 * f + k, &q)) break;
				sxy[nshadow][2 * k] = q.x; sxy[nshadow][2 * k + 1] = q.y;
			}
			if (k == 4) shn[nshadow++] = 4;
		}
	}
	flat_shade = 0;
}

/* ---- The timeline: what the song position means ------------------------ */

static int edges_on = 1;

typedef struct {
	int morph, step;     /* the shape */
	int mirror;          /* glass floor */
	int burst;           /* 0-256: the pieces, 0 = whole */
	int tumble;
	int grid_period;     /* retraces per grid step */
	int spin;            /* rotation phase */
	int cz;              /* how far away the solid is */
} scene;

/* song tick st -> scene. p = pattern, x = 0-255 through it */
static void timeline(unsigned long st, scene *s)
{
	int p = (int)(st / PAT_TICKS), x = (int)((st % PAT_TICKS) * 256 / PAT_TICKS);
	long spin = (long)st * 3, extra = (long)st - 9L * PAT_TICKS;
	s->morph = MORPH_OCTA; s->step = 0;
	/* in the drop the solid glides away, so its reflection comes into view */
	s->cz = CZ + (p == 8 ? ease(x) * 240 >> 8 : p >= 9 && p < 12 ? 240 : p == 12 ? 240 - (ease(x) * 240 >> 8) : 0);
	s->mirror = (p >= 9 && p < 13) || p >= 15;
	s->burst = 0; s->tumble = 0;
	s->grid_period = p >= 9 && p < 13 ? 2 : p < 2 ? 6 : 4;
	if (extra > 0) spin += (extra > 4L * PAT_TICKS ? 4L * PAT_TICKS : extra) * 3;   /* twice as fast in the drop */
	s->spin = (int)(spin % 3072);   /* the three angles are spin, 2/3 and 1/3 of it */
	switch (p) {   /* a pattern each way: there is a lot to get through */
	case 4: s->step = ease(x) * SHAPE_STEPS >> 8; break;
	case 5: s->step = SHAPE_STEPS - (ease(x) * SHAPE_STEPS >> 8); break;
	case 6: s->morph = MORPH_TETRA; s->step = ease(x) * SHAPE_STEPS >> 8; break;
	case 7: s->morph = MORPH_TETRA; s->step = SHAPE_STEPS - (ease(x) * SHAPE_STEPS >> 8); break;
	case 10: s->step = ease(x) * (SHAPE_STEPS / 2) >> 8; break;   /* to the cuboctahedron in the drop */
	case 11: s->step = (SHAPE_STEPS / 2) - (ease(x) * (SHAPE_STEPS / 2) >> 8); break;
	case 13: s->burst = x < 128 ? 256 - ease(256 - 2 * x) : 256; break;   /* out fast */
	case 14: s->burst = 256 - ease(x); break;                              /* and back */
	}
	if (s->burst) s->tumble = (int)(((st - 13L * PAT_TICKS) * 2) & 0x3FF);
}

/* ---- Frame ------------------------------------------------------------- */

static int beat_flare;   /* frames left of the kick flare */

static void draw_orb(int t)
{
	int sx, sy, s, k;
	int z = depth(lt.y, lt.z);
	for (k = 2; k >= 1; k--) {      /* trail: where the orb was 1/15 and 2/15 s ago */
		v3 p;
		light_at(t - k * 4, &p);
		if (project(p.x, p.y, p.z, &sx, &sy))
			fx_sprite_draw(spark[k - 1], sx - sparkw[k - 1] / 2, sy - sparkh[k - 1] / 2);
	}
	if (!project(lt.x, lt.y, lt.z, &sx, &sy)) return;
	if (beat_flare) { fx_sprite_draw(flare, sx - FLAREW / 2, sy - FLAREH / 2); return; }
	s = z < 400 ? 0 : z < 520 ? 1 : 2;
	fx_sprite_draw(orb[s], sx - orbw[s] / 2, sy - orbh[s] / 2);
}

static void draw_pool(void)
{
	int sx, sy, s, z = depth(0, lt.z);
	if (!project(lt.x, 0, lt.z, &sx, &sy)) return;
	s = z < 420 ? 0 : z < 560 ? 1 : 2;
	fx_sprite_draw(pool[s], sx - poolw[s] / 2, sy - poolh[s] / 2);
}

static void frame(unsigned long st, int t)
{
	scene sc;
	int behind, i;
	mark(6);
	/* everything that doesn't touch the page runs while the last flip
	 * may still be waiting for the retrace */
	timeline(st, &sc);
	cur_cz = sc.cz;
	rotation(sc.spin, sc.spin / 3 * 2, sc.spin / 3, mul14(6, SIN(t * 4)));
	light_at(t, &lt);
	if (sc.burst) piece_geometry(sc.burst, sc.tumble);
	else { use_shape(sc.morph, sc.step); solid_geometry(sc.mirror); }
	view_light();
	mark(0);
	if (sc.burst) prep_pieces();
	else prep_faces(sc.mirror);
	mark(7);
	if (!sc.burst) prep_shadow();
	mark(8);
	fx_begin();
	mark(1);
	fx_restore(bg);
	mark(2);
	draw_faces(0, nrefl);   /* the reflection lies on the floor, under everything */
	draw_pool();
	for (i = 0; i < nshadow; i++) fx_poly_src(shn[i], sxy[i], shbuf);
	mark(3);
	behind = depth(lt.y, lt.z) > depth(ctr.y, ctr.z);
	if (behind) draw_orb(t);
	draw_faces(nrefl, ndraw);
	mark(4);
	if (edges_on && !sc.burst) draw_edges();   /* the pieces go without (24 lines) */
	mark(5);
	if (!behind) draw_orb(t);
}

/* ---- Main -------------------------------------------------------------- */

static int lock, grid_period;

static void start_grid(int period)
{
	grid_period = period;
	if (lock) return;
	fx_pal_anim(0, C_G0, C_G3, period, -1);
	fx_pal_anim(1, C_S0, C_S3, period, -1);
}

/* Fixed time steps (GIF, video): the palette is worked out here instead
 * of by the IRQ, from the song time, flashes included. */
static unsigned long snare_t, crash_t;
static void lock_palette(unsigned long t, int fade)
{
	unsigned char p[16];
	int ph = (int)((t / (unsigned long)grid_period) & 3), k;
	dim(p, pal, fade);
	if (!fade)
		for (k = 0; k < 4; k++) {   /* the grid steps toward us: entry k shows k + ph */
			p[C_G0 + k] = pal[C_G0 + ((k + ph) & 3)];
			p[C_S0 + k] = pal[C_S0 + ((k + ph) & 3)];
		}
	if (!fade && t - snare_t < 4) p[C_VLINE] = 15;
	if (!fade && t - crash_t < 8) p[C_BLUE] = 9;
	fx_pal_set(p);
	if (split_on) { dim(p, sky_pal, fade); fx_split(SPLIT_ROW, p); }
}

static int fade_at(unsigned long st)   /* in at the start, out at the end */
{
	int p = (int)(st / PAT_TICKS), x = (int)(st % PAT_TICKS);
	if (p == 0 && x < 48) return 3 - x / 16;
	if (p == SONG_PATS - 1 && x >= PAT_TICKS - 48) return (x - (PAT_TICKS - 48)) / 16 + 1;
	return 0;
}

int main(int argc, char **argv)
{
	int i, bench = 0, gif = 0, video = 0, showfps = 0, frames = 0, fps10 = 0, fade = 3, runfor = 0, fxflags = 0;
	int quiet = 0, at = 0;
	unsigned tf, nf = 0;
	unsigned long ms0, t0 = 0;
	for (i = 1; i < argc; i++) {
		if (!strnicmp(argv[i], "/BENCH:", 7)) bench = atoi(argv[i] + 7);
		else if (!strnicmp(argv[i], "/GIF:", 5)) gif = atoi(argv[i] + 5);
		else if (!strnicmp(argv[i], "/VIDEO:", 7)) gif = video = atoi(argv[i] + 7);
		else if (!strnicmp(argv[i], "/RUN:", 5)) runfor = atoi(argv[i] + 5);
		else if (!strnicmp(argv[i], "/AT:", 4)) at = atoi(argv[i] + 4);
		else if (!stricmp(argv[i], "/PROF")) prof = 1;
		else if (!stricmp(argv[i], "/NOEDGE")) edges_on = 0;
		else if (!stricmp(argv[i], "/QUIET")) quiet = 1;
		else if (!stricmp(argv[i], "/SPLIT")) split_on = 1;
		else if (!stricmp(argv[i], "/ONEPAGE")) fxflags = 8;   /* tests: as if no second video page */
	}
	lock = gif != 0;
	for (i = 0; i < 1024; i++) sin14[i] = (int)(fix_sin(i) >> 2);
	make_sqrt();
	mus_init();
	tmr_init();
	if (fx_init(fxflags)) { printf("JRCUBE needs a PCjr (mode 8 failed, %d)\n", gfx_err); return 1; }
	key_init();
	bg = fx_alloc_buffer(); shbuf = fx_alloc_buffer();
	if (!bg || !shbuf) { gfx_done(); printf("JRCUBE: not enough memory\n"); return 1; }
	if (split_on && !fx_irq) split_on = 0;
	pal_fade(3);
	make_ramps(); make_spec(); make_orbs(); make_strip();
	draw_sky(bg); draw_floor(bg, 0);
	fx_copy_rect(shbuf, bg, 0, 0, 159, HORIZON);
	draw_floor(shbuf, 1);
	for (i = 0; i < 2; i++) {     /* background on both pages */
		fx_copy_rect(fx_seg, bg, 0, 0, 159, 199);
		fx_dirty(0, 0, 159, 199);
		fx_flip(); fx_begin();
	}
	fx_clip(0, VIEW_Y1);
	fx_track(1);
	if (lock) mus_lockstep(1);
	if (video) mus_log_open();
	mus_start(quiet || bench || (gif && !video));
	t0 = (unsigned long)at * PAT_TICKS;   /* /AT: the pictures start later (the music doesn't) */
	start_grid(4);
	dbg_log("jrcube: pages %d irq %d split %d", fx_pages, fx_irq, split_on);
	tf = (unsigned)mus_T; ms0 = tmr_ms(); plast = tmr_ticks();
	for (;;) {
		unsigned long T, st;
		scene sc;
		int f;
		mus_poll();
		T = mus_T + t0;
		st = T % SONG_TICKS;
		if (bench && T < 48) st = 48;   /* benchmarks skip the fade */
		/* the beat */
		if (mus_kick) { mus_kick = 0; beat_flare = 2; }
		else if (beat_flare) beat_flare--;
		if (mus_snare) { mus_snare = 0; snare_t = T; if (!lock && !fade) fx_pal_flash(C_VLINE, 15, 4); }
		if (mus_crash) { mus_crash = 0; crash_t = T; if (!lock && !fade) fx_pal_flash(C_BLUE, 9, 8); }
		timeline(st, &sc);
		f = bench ? 0 : fade_at(st);
		if (lock) lock_palette(T, f);
		else if (f != fade) { pal_fade(f); fade = f; if (!f) start_grid(sc.grid_period); }
		if (!lock && !fade && sc.grid_period != grid_period) start_grid(sc.grid_period);
		else if (lock) grid_period = sc.grid_period;
		frame(st, (int)(T & 0x7FFF));
		draw_strip((unsigned)(T / 2));
		if (showfps) {
			char b[16];
			sprintf(b, "%2d.%d %s", fps10 / 10, fps10 % 10, fx_split_state > 0 ? "S" : fx_split_state < 0 ? "S?" : "");
			fx_text(2, 12, b, C_WHITE);
		}
		fx_flip();
		frames++; nf++;
		if (gif) {
			char name[16];
			fx_begin();
			sprintf(name, "F%03d.PPM", frames);
			fx_screenshot(name);
			if (frames >= gif) break;
			mus_step(4);
			continue;
		}
		if ((unsigned)mus_T - tf >= 60) {
			unsigned d = (unsigned)mus_T - tf;
			fps10 = (int)(nf * 600UL / d);
			tf += d; nf = 0;
		}
		if (bench) { if (frames >= bench) break; continue; }
		if (runfor && frames >= runfor) break;
		if (key_down[KEY_ESC] || key_down[KEY_SPACE] || key_down[KEY_ENTER]) break;
		if (key_down[0x21]) { showfps ^= 1; while (key_down[0x21]) ; }  /* F */
		if (key_down[0x12]) { edges_on ^= 1; while (key_down[0x12]) ; } /* E */
	}
	ms0 = tmr_ms() - ms0;
	if (video) mus_log_close();
	mus_stop();
	if (!gif && !bench) {   /* fade out */
		fx_pal_anim(0, 0, 0, 0, 0);
		fx_pal_anim(1, 0, 0, 0, 0);
		for (i = 1; i <= 3; i++) {
			unsigned w = fx_vbl;
			unsigned long m = tmr_ms();
			pal_fade(i);
			while (fx_irq ? fx_vbl - w < 6 : tmr_ms() - m < 100) ;
		}
	}
	fx_split(0, 0);
	dbg_log("jrcube: %d frames in %lu ms = %lu.%lu fps, split %d (%u)", frames, ms0,
	        frames * 1000UL / ms0, frames * 10000UL / ms0 % 10, fx_split_state, fx_split_guard);
	for (i = 0; prof && i < NPROF; i++)
		dbg_log("prof %-10s %5lu.%lu ms/frame", pname[i], TMR_TICKS_MS(pt[i]) / frames, TMR_TICKS_MS(pt[i] * 10) / frames % 10);
	key_done();
	gfx_done();
	return 0;
}
