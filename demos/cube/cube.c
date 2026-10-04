/* JRCUBE: a spinning, light-shaded cube over a neon grid, with a glowing
 * orb circling it and casting a real-time shadow. IBM PCjr, mode 8
 * (160x200x16), built on the JRGFX fast path.
 *
 * Tricks, cheapest first:
 * - The whole scene except the cube, orb and shadow is a static picture
 *   (bg) in RAM. Each frame restores only the bytes the hidden page drew
 *   last time (fx_track / fx_restore), so there is no clear and no full
 *   copy.
 * - The grid scrolls by palette rotation alone: its horizontal lines use 4
 *   palette entries in turn and the retrace IRQ rotates which one is lit
 *   (fx_pal_anim), at a steady 60 Hz pace, whatever the frame rate.
 * - The shadow is the convex hull of the 8 corners projected from the orb
 *   onto the floor, filled by copying from a pre-darkened copy of the floor
 *   (fx_poly_src), so the grid lines stay visible, dimmer, inside it.
 * - Faces are lit per corner by the point light (diffuse + Blinn specular),
 *   and the shade is interpolated down the screen rows and drawn as a 2x2
 *   ordered dither between ramp colors (fx_poly_rows): ~17 shades per face
 *   from 5 colors, at the cost of a flat fill.
 * - The cube is three rotated axis vectors (9 multiplies); its 8 corners
 *   are sums of them.
 * - All motion runs from the retrace counter, so a slow machine shows the
 *   same speed at a lower frame rate.
 *
 *   JRCUBE            run until a key (F toggles the fps counter)
 *   JRCUBE /BENCH:n   n frames, then log the frame rate to LOG.TXT
 *   JRCUBE /GIF:n     n frames at fixed time steps, each saved as Fnnn.PPM
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <malloc.h>
#include "jrgfx.h"

extern const unsigned char jr_font[96 * 8];

/* ---- Scene constants (world units; y up, camera looks along +z) -------- */

#define CAMH 330          /* camera height                               */
#define SS   6085         /* camera pitch (21.8 deg down): sin, cos in 2.14 */
#define CC   15212
#define FY   210          /* focal lengths in pixels (pixels are 2:1 wide) */
#define FX   126
#define HY0  104          /* screen row of the view axis                 */
#define HORIZON 20        /* = HY0 - FY * tan(pitch)                     */
#define VIEW_Y1 189       /* last 3D row; the scroller is below          */
#define CZ   460          /* cube center                                 */
#define CY   100
#define CS   52           /* cube half size                              */
#define LR   170          /* light orbit radius                          */
#define LY   300          /* light orbit height, +-LYA                   */
#define LYA  40
#define GRID 64           /* floor grid spacing; lines every GRID/4      */

/* Logical palette. 2-5 and 6-9 are the scrolling grid lines (lit floor,
 * shadowed floor); the IRQ rotates them in step. */
enum { C_BLACK, C_BLUE, C_G0, C_G1, C_G2, C_G3, C_S0, C_S1, C_S2, C_S3,
       C_VLINE, C_SVLINE, C_CYAN, C_LCYAN, C_WHITE, C_YELLOW };
static const unsigned char pal[16] = { 0, 1, 13, 1, 1, 1, 5, 0, 0, 0, 13, 5, 3, 11, 15, 14 };

static int sin14[1024];   /* sin * 16384 */
#define SIN(a) sin14[(a) & 1023]
#define COS(a) sin14[((a) + 256) & 1023]

static unsigned bg, shbuf;      /* background and its shadowed copy    */
static unsigned char rowpat[200];

/* ---- Shading ramps: 2x2 ordered dither between neighbouring colors ----- */

#define NRAMP 3
static const unsigned char ramp_cols[NRAMP][6] = {
	{ C_BLACK, C_CYAN, C_LCYAN, C_WHITE, 255 },           /* chrome   */
	{ C_BLACK, C_SVLINE, C_VLINE, C_WHITE, 255 },         /* magenta  */
	{ C_BLACK, C_SVLINE, C_YELLOW, C_WHITE, 255 },        /* sunset   */
};
static unsigned char ramp_e[NRAMP][256], ramp_o[NRAMP][256]; /* even/odd row bytes */

static void make_ramps(void)
{
	int r, i;
	for (r = 0; r < NRAMP; r++) {
		int n = 0, levels;
		while (ramp_cols[r][n + 1] != 255) n++;   /* n intervals */
		levels = n * 4 + 1;
		for (i = 0; i < 256; i++) {
			int l = (int)((long)i * levels / 256), seg = l >> 2, k = l & 3;
			unsigned char a, b;
			if (seg >= n) { seg = n - 1; k = 4; }
			a = ramp_cols[r][seg]; b = ramp_cols[r][seg + 1];
			switch (k) {
			case 0: ramp_e[r][i] = FX_PAIR(a, a); ramp_o[r][i] = FX_PAIR(a, a); break;
			case 1: ramp_e[r][i] = FX_PAIR(b, a); ramp_o[r][i] = FX_PAIR(a, a); break;
			case 2: ramp_e[r][i] = FX_PAIR(b, a); ramp_o[r][i] = FX_PAIR(a, b); break;
			case 3: ramp_e[r][i] = FX_PAIR(b, b); ramp_o[r][i] = FX_PAIR(a, b); break;
			default: ramp_e[r][i] = FX_PAIR(b, b); ramp_o[r][i] = FX_PAIR(b, b); break;
			}
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

/* square root from a table: shift into 4096-16383, look up (about 1%) */
static unsigned sqt[256];   /* sqrt(64 i + 32) * 16 */
static void make_sqrt(void)
{
	unsigned i, r = 0;
	for (i = 0; i < 256; i++) {
		unsigned long v = (64UL * i + 32) * 256;
		while ((unsigned long)(r + 1) * (r + 1) <= v) r++;
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

/* n / d clamped to +-16000 (d > 0), so the IDIV can't overflow */
static int cdiv(long n, int d)
{
	long lim = (long)d << 14;
	if (n >= lim) return 16000;
	if (n <= -lim) return -16000;
	return idiv32(n, d);
}

/* world point to screen; returns 0 if too close to the camera */
static int project(int X, int Y, int Z, int *sx, int *sy)
{
	int dy = Y - CAMH, zp;
	long zl = (imul16(Z, CC) - imul16(dy, SS)) >> 14, yp;
	if (zl < 16) return 0;
	zp = zl > 32000 ? 32000 : (int)zl;
	yp = (imul16(dy, CC) + imul16(Z, SS)) >> 14;
	*sx = cdiv(imul16(80, zp) + imul16(X, FX), zp);
	*sy = cdiv(imul16(HY0, zp) - yp * FY, zp);
	return 1;
}

static long depth(int Y, int Z) { return ((long)Z * CC - (long)(Y - CAMH) * SS) >> 14; }

/* floor z seen at screen row y (y > HORIZON) */
static long floor_z(int y)
{
	long k = HY0 - y;
	return (long)CAMH * ((long)CC * FY + k * SS) / ((long)SS * FY - k * CC);
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

static void draw_floor(unsigned seg, int shadow)
{
	int y, j, prev = 999, last = VIEW_Y1, k;
	int base = shadow ? C_BLACK : C_BLUE, g0 = shadow ? C_S0 : C_G0, vl = shadow ? C_SVLINE : C_VLINE;
	long z0 = floor_z(VIEW_Y1), z;
	fx_target(seg);
	/* base, hazing toward the horizon: base -> magenta -> light magenta */
	for (y = HORIZON + 1; y <= VIEW_Y1; y++) {
		int d = y - HORIZON, p;
		if (d < 4) p = dith(C_SVLINE, C_VLINE, 4 - d, y);
		else if (d < 9) p = dith(base, C_SVLINE, (9 - d) * 4 / 5, y);
		else p = FX_SOLID(base);
		fx_hspan(y, 0, 159, (unsigned char)p);
	}
	/* horizontal lines every GRID/4, entry g0 + (k & 3), near to far,
	 * until they are less than 3 rows apart */
	for (k = 0; ; k++) {
		int sx, sy;
		z = (z0 / (GRID / 4) + 1) * (GRID / 4) + (long)k * (GRID / 4);
		if (!project(0, 0, (int)(z > 32000 ? 32000 : z), &sx, &sy)) continue;
		if (sy <= HORIZON + 4 || z > 12000) break;
		if (sy == prev) continue;
		fx_hspan(sy, 0, 159, FX_SOLID(g0 + (int)((z / (GRID / 4)) & 3)));
		prev = sy;
		last = sy;
	}
	/* converging lines, stopped where the horizontal lines stop */
	for (j = -24; j <= 24; j++) {
		int ax, ay, bx, by;
		long X = (long)j * GRID;
		project((int)X, 0, (int)floor_z(VIEW_Y1 + 40), &ax, &ay);
		project((int)X, 0, 2600, &bx, &by);
		if ((ax < 0 && bx < 0) || (ax > 159 && bx > 159)) continue;
		fx_line(ax, ay, bx, by, vl);
	}
	fx_target(0);
}

static void draw_sky(unsigned seg)
{
	int y, i, x;
	fx_target(seg);
	for (y = 0; y <= HORIZON; y++) {
		unsigned p;
		if (y < 4) p = FX_SOLID(C_BLACK);
		else if (y < 8) p = dith(C_BLACK, C_BLUE, y - 4, y);
		else if (y < 11) p = FX_SOLID(C_BLUE);
		else if (y < 15) p = dith(C_BLUE, C_SVLINE, y - 11, y);
		else p = dith(C_SVLINE, C_VLINE, (y - 15) * 4 / 5, y);
		fx_hspan(y, 0, 159, (unsigned char)p);
	}
	for (i = 0; i < 40; i++) {
		x = rnd(160); y = rnd(12);
		bpset(seg, x, y, i & 3 ? C_WHITE : C_G0 + (i & 12) / 4);
	}
	/* mountains: a jagged line, black below it, neon edge on top */
	{
		int h = 3, xx;
		for (xx = 0; xx < 160; xx += 4) {
			int nh = 1 + rnd(5) + (xx > 40 && xx < 70 ? 3 : 0) + (xx > 110 && xx < 130 ? 3 : 0), t;
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
	"     *** JRCUBE ***   REAL-TIME SHADOWS AND LIGHTING ON AN IBM PCjr ... "
	"8088 AT 4.77 MHZ, 16 COLORS, PAGE FLIPPED ON THE RETRACE INTERRUPT ... "
	"THE GRID SCROLLS BY PALETTE ROTATION, THE SHADOW IS COPIED FROM A DARKENED FLOOR, "
	"EVERY FACE IS DITHER-SHADED PER CORNER ... MADE WITH CODEJR ... "
	"GREETINGS TO EVERY PCjr STILL RUNNING ...      ";
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
			unsigned char far *p = strip + (unsigned)r * sw + i * 4;
			for (k = 0; k < 4; k++, bits <<= 2)
				p[k] = (unsigned char)(((bits & 0x80) ? c << 4 : 0) | ((bits & 0x40) ? c : 0));
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
}

/* ---- The orb ----------------------------------------------------------- */

#define NORB 3
static fx_sprite *orb[NORB];
static int orbw[NORB] = { 18, 14, 10 }, orbh[NORB] = { 34, 26, 19 };
static unsigned char orbpix[NORB][18 * 34];

static void make_orbs(void)
{
	int s, x, y;
	for (s = 0; s < NORB; s++) {
		int w = orbw[s], h = orbh[s];
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++) {
				/* distance in "round" units: x pixels are twice as wide */
				long dx = (2L * x + 1 - w) * 2 * 64 / w, dy = (2L * y + 1 - h) * 64 / h;
				long d = dx * dx + dy * dy;   /* 4096 = edge */
				int c;
				if (d < 500) c = C_WHITE;
				else if (d < 1300) c = (x + y) & 1 ? C_WHITE : C_YELLOW;
				else if (d < 2100) c = C_YELLOW;
				else if (d < 2900) c = (x + y) & 1 ? C_YELLOW : C_VLINE;
				else if (d < 4096) c = (x + y) & 1 ? 0 : C_VLINE;  /* halo: half see-through */
				else c = 0;
				orbpix[s][y * w + x] = (unsigned char)c;
			}
		orb[s] = fx_compile(orbpix[s], w, h, 0);
	}
}

/* ---- Cube -------------------------------------------------------------- */

typedef struct { int sx, sy; } p2;

static v3 cv[8];          /* corners, world             */
static p2 cp[8];          /* corners, screen            */
static v3 ax3[3];         /* rotated half-axes (length CS) */
static v3 ctr, lt;        /* cube center, light         */
static v3 ldir[8];        /* toward the light, unit * 256 */
static v3 hv[8];          /* half vector per corner, length 256 */
static v3 vdir;           /* toward the camera, length 256 */

/* face f: axis f >> 1, sign f & 1 (1 = +). Corner index bits: 1 x, 2 y, 4 z. */
static const unsigned char face_v[6][4] = {
	{0, 2, 6, 4}, {1, 3, 7, 5},   /* -x, +x */
	{0, 1, 5, 4}, {2, 3, 7, 6},   /* -y, +y */
	{0, 1, 3, 2}, {4, 5, 7, 6},   /* -z, +z */
};
static const unsigned char face_ramp[3] = { 0, 1, 2 };
static unsigned char spec_tab[129];

static void make_spec(void)
{
	int i, k;
	for (i = 0; i <= 128; i++) {
		long v = 255;          /* 255 * (i / 128) ^ 24 */
		for (k = 0; k < 24; k++) v = v * i / 128;
		spec_tab[i] = (unsigned char)v;
	}
}

static void cube_geometry(int t)
{
	int a1 = t * 3, a2 = t * 2, a3 = t;
	long sa = SIN(a1), ca = COS(a1), sb = SIN(a2), cb = COS(a2), sc = SIN(a3), cc = COS(a3);
	long m[3][3];
	int i;
	/* R = Rz(a3) * Ry(a2) * Rx(a1), entries 2.14 */
	m[0][0] = (cc * cb) >> 14;
	m[0][1] = ((((cc * sb) >> 14) * sa) >> 14) - ((sc * ca) >> 14);
	m[0][2] = ((((cc * sb) >> 14) * ca) >> 14) + ((sc * sa) >> 14);
	m[1][0] = (sc * cb) >> 14;
	m[1][1] = ((((sc * sb) >> 14) * sa) >> 14) + ((cc * ca) >> 14);
	m[1][2] = ((((sc * sb) >> 14) * ca) >> 14) - ((cc * sa) >> 14);
	m[2][0] = -sb;
	m[2][1] = (cb * sa) >> 14;
	m[2][2] = (cb * ca) >> 14;
	for (i = 0; i < 3; i++) {   /* column i is axis i */
		ax3[i].x = (int)((m[0][i] * CS) >> 14);
		ax3[i].y = (int)((m[1][i] * CS) >> 14);
		ax3[i].z = (int)((m[2][i] * CS) >> 14);
	}
	ctr.x = 0; ctr.y = CY + (int)((6L * SIN(t * 4)) >> 14); ctr.z = CZ;
	for (i = 0; i < 8; i++) {
		int sx = i & 1 ? 1 : -1, sy = i & 2 ? 1 : -1, sz = i & 4 ? 1 : -1;
		cv[i].x = ctr.x + sx * ax3[0].x + sy * ax3[1].x + sz * ax3[2].x;
		cv[i].y = ctr.y + sx * ax3[0].y + sy * ax3[1].y + sz * ax3[2].y;
		cv[i].z = ctr.z + sx * ax3[0].z + sy * ax3[1].z + sz * ax3[2].z;
		project(cv[i].x, cv[i].y, cv[i].z, &cp[i].sx, &cp[i].sy);
	}
}

static void light_geometry(int t)
{
	int a = t * 5;
	lt.x = (int)(((long)LR * COS(a)) >> 14);
	lt.z = CZ + (int)(((long)LR * SIN(a)) >> 14);
	lt.y = LY + (int)(((long)LYA * SIN(a)) >> 14);
}

/* unit vector * 256 (len = its length, > 64) */
static void unit(v3 *o, int x, int y, int z, unsigned len)
{
	int inv = idiv32(1L << 21, (int)len);
	o->x = (int)(imul16(x, inv) >> 13);
	o->y = (int)(imul16(y, inv) >> 13);
	o->z = (int)(imul16(z, inv) >> 13);
}

static long sq3(int x, int y, int z) { return imul16(x, x) + imul16(y, y) + imul16(z, z); }

/* per corner: direction to the light and the Blinn half vector (both
 * unit * 256); one table square root and one divide each */
static void corner_light(void)
{
	int i;
	int vx = -ctr.x, vy = CAMH - ctr.y, vz = -ctr.z;     /* view dir, from the center */
	unit(&vdir, vx, vy, vz, fsqrt(sq3(vx, vy, vz)));
	for (i = 0; i < 8; i++) {
		int dx = lt.x - cv[i].x, dy = lt.y - cv[i].y, dz = lt.z - cv[i].z, hx, hy, hz;
		unsigned hl;
		unit(&ldir[i], dx, dy, dz, fsqrt(sq3(dx, dy, dz)));
		hx = ldir[i].x + vdir.x; hy = ldir[i].y + vdir.y; hz = ldir[i].z + vdir.z;
		hl = fsqrt(sq3(hx, hy, hz));
		if (hl > 64) unit(&hv[i], hx, hy, hz, hl);
		else hv[i].x = hv[i].y = hv[i].z = 0;
	}
}

static int dot8(const v3 *a, const v3 *b) /* (a . b) >> 8 */
{
	return (int)((imul16(a->x, b->x) + imul16(a->y, b->y) + imul16(a->z, b->z)) >> 8);
}

/* light at corner v of a face with unit normal n (* 256): 0-255.
 * Ambient, a dim fill light from the camera, the orb (diffuse) and its
 * highlight (Blinn, exponent 24). */
static int shade(const v3 *n, int v)
{
	int d = dot8(n, &ldir[v]), s = dot8(n, &hv[v]) >> 1, f = dot8(n, &vdir);
	int i = 24 + (f > 0 ? f / 5 : 0);
	if (d > 0) i += (d * 3) >> 2;
	if (s > 0) i += spec_tab[s > 128 ? 128 : s];
	return i > 255 ? 255 : i;
}

/* edges: corner pairs and the two faces that share them */
static const unsigned char edge[12][4] = {
	{0, 1, 2, 4}, {2, 3, 3, 4}, {4, 5, 2, 5}, {6, 7, 3, 5},   /* along x */
	{0, 2, 0, 4}, {1, 3, 1, 4}, {4, 6, 0, 5}, {5, 7, 1, 5},   /* along y */
	{0, 4, 0, 2}, {1, 5, 1, 2}, {2, 6, 0, 3}, {3, 7, 1, 3},   /* along z */
};

static int vis_f;

static void draw_faces(void)
{
	int f;
	vis_f = 0;
	for (f = 0; f < 6; f++) {
		v3 n, nu;
		const v3 *a = &ax3[f >> 1];
		long vis;
		int xy[8], k, top = 0, bot = 0, ytop, ybot, r = face_ramp[f >> 1], y, I, dI;
		const unsigned char *pe = ramp_e[r], *po = ramp_o[r];
		if (f & 1) n = *a; else { n.x = -a->x; n.y = -a->y; n.z = -a->z; }
		vis = imul16(n.x, ctr.x + n.x) + imul16(n.y, ctr.y + n.y - CAMH) + imul16(n.z, ctr.z + n.z);
		if (vis >= 0) continue;
		vis_f |= 1 << f;
		for (k = 0; k < 4; k++) {
			const p2 *p = &cp[face_v[f][k]];
			xy[2 * k] = p->sx; xy[2 * k + 1] = p->sy;
			if (p->sy < cp[face_v[f][top]].sy) top = k;
			if (p->sy > cp[face_v[f][bot]].sy) bot = k;
		}
		ytop = xy[2 * top + 1]; ybot = xy[2 * bot + 1];
		/* shade down the rows from the top corner's light to the bottom one's */
		nu.x = (int)(imul16(n.x, 65536L / CS / 4) >> 6);   /* unit * 256 */
		nu.y = (int)(imul16(n.y, 65536L / CS / 4) >> 6);
		nu.z = (int)(imul16(n.z, 65536L / CS / 4) >> 6);
		I = shade(&nu, face_v[f][top]) << 7;
		dI = ybot > ytop ? idiv32((long)((shade(&nu, face_v[f][bot]) << 7) - I), ybot - ytop) : 0;
		if (ytop < 0) { I += dI * -ytop; ytop = 0; }
		if (ybot > VIEW_Y1) ybot = VIEW_Y1;
		for (y = ytop; y <= ybot; y++, I += dI) {
			int i = I >> 7;
			if (i < 0) i = 0;
			if (i > 255) i = 255;
			rowpat[y] = y & 1 ? po[i] : pe[i];
		}
		fx_poly_rows(4, xy, rowpat);
	}
}

/* neon edges: white on the silhouette, light cyan inside */
static void draw_edges(void)
{
	int f;
	for (f = 0; f < 12; f++) {
		int a = (vis_f >> edge[f][2]) & 1, b = (vis_f >> edge[f][3]) & 1;
		if (a | b) {
			const p2 *p = &cp[edge[f][0]], *q = &cp[edge[f][1]];
			fx_line(p->sx, p->sy, q->sx, q->sy, a & b ? C_LCYAN : C_WHITE);
		}
	}
}

/* ---- Shadow ------------------------------------------------------------ */

typedef struct { int x, z; } f2;

static long cross(const f2 *o, const f2 *a, const f2 *b)
{
	return imul16(a->x - o->x, b->z - o->z) - imul16(a->z - o->z, b->x - o->x);
}

static int clamp16k(long v) { return v > 16000 ? 16000 : v < -16000 ? -16000 : (int)v; }

static void draw_shadow(void)
{
	f2 p[8], h[18], c[20];
	int i, j, n = 0, k, m = 0, xy[40];
	/* corners projected from the light onto the floor (y = 0) */
	for (i = 0; i < 8; i++) {
		int den = lt.y - cv[i].y, f;
		f2 q;
		if (den < 8) den = 8;
		f = idiv32((long)lt.y << 8, den);        /* 8.8 stretch factor */
		q.x = clamp16k(lt.x + (imul16(cv[i].x - lt.x, f) >> 8));
		q.z = clamp16k(lt.z + (imul16(cv[i].z - lt.z, f) >> 8));
		for (j = n; j > 0 && (p[j - 1].x > q.x || (p[j - 1].x == q.x && p[j - 1].z > q.z)); j--) p[j] = p[j - 1];
		p[j] = q; n++;
	}
	/* convex hull (monotone chain) */
	for (i = 0; i < n; i++) {
		while (k = m, k >= 2 && cross(&h[k - 2], &h[k - 1], &p[i]) <= 0) m--;
		h[m++] = p[i];
	}
	for (i = n - 2, j = m + 1; i >= 0; i--) {
		while (m >= j && cross(&h[m - 2], &h[m - 1], &p[i]) <= 0) m--;
		h[m++] = p[i];
	}
	m--;
	/* clip to z >= 40 (in front of the camera) */
	for (i = 0, k = 0; i < m; i++) {
		const f2 *a = &h[i], *b = &h[(i + 1) % m];
		int ain = a->z >= 40, bin = b->z >= 40;
		if (ain) c[k++] = *a;
		if (ain != bin) {
			c[k].z = 40;
			c[k].x = a->x + (int)((long)(b->x - a->x) * (40 - a->z) / (b->z - a->z));
			k++;
		}
	}
	if (k < 3) return;
	for (i = 0; i < k; i++) project(c[i].x, 0, c[i].z, &xy[2 * i], &xy[2 * i + 1]);
	fx_poly_src(k, xy, shbuf);
}

/* ---- Main -------------------------------------------------------------- */

static void draw_orb(void)
{
	int sx, sy, s;
	long z = depth(lt.y, lt.z);
	if (!project(lt.x, lt.y, lt.z, &sx, &sy)) return;
	s = z < 400 ? 0 : z < 520 ? 1 : 2;
	fx_sprite_draw(orb[s], sx - orbw[s] / 2, sy - orbh[s] / 2);
}

/* /PROF: time per stage, summed over the run (tmr_ticks) */
#define NPROF 7
static int prof;
static unsigned long pt[NPROF], plast;
static const char *pname[NPROF] = { "math", "wait", "restore", "shadow", "faces", "edges", "orb+strip" };
static void mark(int i)
{
	unsigned long t;
	if (!prof) return;
	t = tmr_ticks();
	pt[i] += t - plast;
	plast = t;
}

static int edges_on = 1;

static void frame(int t)
{
	int behind;
	mark(NPROF - 1);
	cube_geometry(t);
	light_geometry(t);
	corner_light();
	mark(0);
	fx_begin();
	mark(1);
	fx_restore(bg);
	mark(2);
	draw_shadow();
	mark(3);
	behind = depth(lt.y, lt.z) > depth(ctr.y, ctr.z);
	if (behind) draw_orb();
	draw_faces();
	mark(4);
	if (edges_on) draw_edges();
	mark(5);
	if (!behind) draw_orb();
}

static unsigned now60(void)
{
	return fx_irq ? fx_vbl : (unsigned)(tmr_ms() * 6 / 100);
}

int main(int argc, char **argv)
{
	int i, bench = 0, gif = 0, showfps = 0, frames = 0, fps10 = 0;
	unsigned t0, tf, nf = 0;
	unsigned long ms0;
	for (i = 1; i < argc; i++) {
		if (!strnicmp(argv[i], "/BENCH:", 7)) bench = atoi(argv[i] + 7);
		else if (!strnicmp(argv[i], "/GIF:", 5)) gif = atoi(argv[i] + 5);
		else if (!stricmp(argv[i], "/PROF")) prof = 1;
		else if (!stricmp(argv[i], "/NOEDGE")) edges_on = 0;
	}
	for (i = 0; i < 1024; i++) sin14[i] = (int)(fix_sin(i) >> 2);
	make_sqrt();
	tmr_init();
	if (fx_init(0)) { printf("JRCUBE needs a PCjr (mode 8 failed, %d)\n", gfx_err); return 1; }
	key_init();
	bg = fx_alloc_buffer(); shbuf = fx_alloc_buffer();
	if (!bg || !shbuf) { gfx_done(); printf("JRCUBE: not enough memory\n"); return 1; }
	fx_pal_set(pal);
	make_ramps(); make_spec(); make_orbs(); make_strip();
	draw_sky(bg); draw_floor(bg, 0);
	fx_copy_rect(shbuf, bg, 0, 0, 159, HORIZON);
	draw_floor(shbuf, 1);
	for (i = 0; i < 2; i++) {     /* background on both pages */
		fx_copy_rect(fx_seg, bg, 0, 0, 159, 199);
		fx_flip(); fx_begin();
	}
	if (!gif) {
		fx_pal_anim(0, C_G0, C_G3, 4, -1);
		fx_pal_anim(1, C_S0, C_S3, 4, -1);
	}
	fx_clip(0, VIEW_Y1);
	fx_track(1);
	dbg_log("jrcube: pages %d irq %d", fx_pages, fx_irq);
	t0 = tf = now60(); ms0 = tmr_ms(); plast = tmr_ticks();
	for (;;) {
		unsigned t = gif ? (unsigned)frames * 4 : now60() - t0;
		if (gif) {   /* palette phase by hand, for fixed time steps */
			unsigned char p[16];
			int ph = (int)((t / 4) & 3), k;
			memcpy(p, pal, 16);
			for (k = 0; k < 4; k++) { p[C_G0 + k] = pal[C_G0 + ((k + ph) & 3)]; p[C_S0 + k] = pal[C_S0 + ((k + ph) & 3)]; }
			fx_pal_set(p);
		}
		frame(t);
		draw_strip(t / 2);
		if (showfps) {
			char b[12];
			sprintf(b, "%2d.%d", fps10 / 10, fps10 % 10);
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
			continue;
		}
		if (now60() - tf >= 60) {
			unsigned d = now60() - tf;
			fps10 = (int)(nf * 600UL / d);
			tf += d; nf = 0;
		}
		if (bench) { if (frames >= bench) break; continue; }
		if (key_down[KEY_ESC] || key_down[KEY_SPACE] || key_down[KEY_ENTER]) break;
		if (key_down[0x21]) { showfps ^= 1; while (key_down[0x21]) ; }  /* F */
	}
	ms0 = tmr_ms() - ms0;
	dbg_log("jrcube: %d frames in %lu ms = %lu.%lu fps", frames, ms0,
	        frames * 1000UL / ms0, frames * 10000UL / ms0 % 10);
	for (i = 0; prof && i < NPROF; i++)
		dbg_log("prof %-10s %5lu.%lu ms/frame", pname[i], TMR_TICKS_MS(pt[i]) / frames, TMR_TICKS_MS(pt[i] * 10) / frames % 10);
	key_done();
	gfx_done();
	return 0;
}
