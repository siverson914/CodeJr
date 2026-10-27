/* AIRBORNE: the 1985 Mac game Airborne! (Silicon Beach Software; game by
 * Jonathan Gay, sound by Eric Zocher) on the PCjr. 320x200, 4 greys.
 *
 * The game itself is game.c: the original's logic translated routine by
 * routine and checked against the original running in an emulator, tick
 * for tick, on a hidden 1-bit copy of the Mac screen. This file is the
 * PCjr around it: the screen (the Mac's sprites pre-shrunk to 5/8 by
 * tools/mkdat.py, ORed onto a white background, only changed areas
 * redrawn, page flipping), the keyboard, the 60 Hz clock, the menus and
 * the high scores.
 *
 * AIRBORNE [/LEVEL:n] [/U] [/Q] [/DEMO:ticks] [/SHOT:ticks] [/BENCH] [/TIME:secs]
 *   /LEVEL:n    start at level n (1 Beginner, 5 Intermediate, 10 Advanced)
 *   /U          unguided shells
 *   /Q          quiet: no sound
 *   /H          the song on interrupt timing (DOSBox has no 8259 poll)
 *   /KEYS:s     tests: type the keys in s, one every 1.5 s (E Enter, X Esc)
 *   /DEMO:t     play by itself for t ticks (aim sweeps, fires), then exit:
 *               for tests, nobody at the keys
 *   /SHOT:t     with /DEMO: save the screen at tick t as SHOTn.PPM
 *   /BENCH      with /DEMO: run as fast as possible and log the frame times
 *   /TIME:secs  the Mac clock (seconds since 1904): seeds the game's Random,
 *               so a /DEMO run can be checked against the original
 *
 * Sound: the effects are the SFX chunk (tools/mksfx.py: the original's
 * recordings turned into SN76496 settings per 60 Hz tick), played from the
 * retrace interrupt; the startup song is AIRSONG.SND (tools/mksong.py),
 * real samples through SAY's player (apps/say/player.asm).
 *
 * Keys: Left/Right aim, Up fires (hold), Space switches guns, Tab pauses,
 * G guided/unguided shells, Esc ends the game. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include <io.h>
#include <conio.h>
#include "jrgfx.h"
#include "game.h"

extern const unsigned char jr_font[96 * 8];

#define MAXS 128 /* live sprites */
#define MAXD 64  /* live shell dots */
#define MAXI 200 /* items drawn on a page */

/* ---- the data file ---- */
static unsigned dat_seg;
static unsigned long dat_len;

static unsigned char far *dat_at(unsigned long off)
{
	return (unsigned char far *)MK_FP(dat_seg + (unsigned)(off >> 4), (unsigned)(off & 15));
}

static unsigned long rd32(unsigned char far *p) { return p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }
static unsigned rd16(unsigned char far *p) { return p[0] | (p[1] << 8); }

static unsigned char far *chunk(const char *id, unsigned long *len)
{
	unsigned char far *h = dat_at(0);
	unsigned n = rd16(h + 6), i;
	for (i = 0; i < n; i++) {
		unsigned char far *e = h + 8 + 12 * i;
		if (e[0] == id[0] && e[1] == id[1] && e[2] == id[2] && e[3] == id[3]) {
			if (len) *len = rd32(e + 8);
			return dat_at(rd32(e + 4));
		}
	}
	return 0;
}

static unsigned char far *back;  /* BACK: the game screen without sprites */
static unsigned char far *imgs;  /* IMGS */
static unsigned char far *smap;  /* SMAP */
static unsigned char far *digt;  /* DIGT */
static unsigned over_img;
static unsigned nimg;


static u8 far fb_mem[MAC_FB_BYTES];

/* ---- screen ---- */
static unsigned char far *vid;
static int npg, pg;
static const unsigned char PAL4[4] = { 15, 7, 8, 0 }; /* white, light grey, dark grey, black */
static char err[80];

static void mode_320x4(void)
{
	unsigned char pal16[16];
	int i;
	_disable();
	inp(0x3DA);
	outp(0x3DA, 0); outp(0x3DA, 0x0A);
	outp(0x3DA, 1); outp(0x3DA, 0x03);
	outp(0x3DA, 3); outp(0x3DA, 0x00);
	inp(0x3DA);
	_enable();
	*(unsigned char far *)MK_FP(0x40, 0x49) = 4; /* screenshots read it as 4 colors */
	for (i = 0; i < 16; i++) pal16[i] = PAL4[i & 3];
	fx_pal_set(pal16);
}

static void begin(void) { fx_begin(); vid = (unsigned char far *)MK_FP(fx_seg, 0); }
static void flip(void) { fx_flip(); if (npg == 2) pg ^= 1; }

static void full_screen(const unsigned char far *src)
{
	int y;
	for (y = 0; y < 200; y++) _fmemcpy(vid + jr_rowoff[y], src + y * 80, 80);
	if (npg == 1) fx_dirty(0, 0, 159, 199);
}

static void show_full(const char *id)
{
	unsigned char far *s = chunk(id, 0);
	int i;
	if (!s) return;
	for (i = 0; i < npg; i++) { begin(); full_screen(s); flip(); }
}

/* text in the ROM font, 8x8, at a byte column (4 pixels) */
static void text(int bx, int y, const char *s, int fg, int bg)
{
	unsigned char fgb = (unsigned char)(fg * 0x55), bgb = (unsigned char)(bg * 0x55);
	int bx0 = bx;
	for (; *s; s++, bx += 2) {
		int r;
		unsigned ch = (unsigned char)*s;
		if (ch < 32 || ch > 127) ch = '?';
		if (bx < 0 || bx > 78) continue;
		for (r = 0; r < 8; r++) {
			unsigned bits = jr_font[(ch - 32) * 8 + r], m = 0, b;
			unsigned char far *p;
			if (y + r < 0 || y + r > 199) continue;
			for (b = 0; b < 8; b++) if (bits & (0x80 >> b)) m |= 0xC000u >> (2 * b);
			p = vid + jr_rowoff[y + r] + bx;
			p[0] = (unsigned char)(((m >> 8) & fgb) | (~(m >> 8) & bgb));
			p[1] = (unsigned char)((m & fgb) | (~m & bgb));
		}
	}
	if (npg == 1 && bx > bx0) fx_dirty(bx0 * 2, y, bx * 2 - 1, y + 7);
}

static void box(int bx0, int y0, int bx1, int y1, int c)
{
	int y;
	for (y = y0; y <= y1; y++) _fmemset(vid + jr_rowoff[y] + bx0, c * 0x55, bx1 - bx0 + 1);
	if (npg == 1) fx_dirty(bx0 * 2, y0, bx1 * 2 + 1, y1);
}

/* ---- the inner loops (rblit.asm) ---- */
struct rbp { unsigned src_off, src_seg, src_stride, dst_off, dst_seg, y, rows, bytes; };
extern void rb_copy(struct rbp far *p);
extern void rb_or(struct rbp far *p);
static struct rbp rbp;

/* rows y0..y0+rows-1, bytes x0.. of the background into the page */
static void copy_back(int x0, int y0, int rows, int bytes)
{
	unsigned char far *sp = back + y0 * 80 + x0;
	rbp.src_off = FP_OFF(sp); rbp.src_seg = FP_SEG(sp); rbp.src_stride = 80;
	rbp.dst_off = FP_OFF(vid) + jr_rowoff[y0] + x0; rbp.dst_seg = FP_SEG(vid);
	rbp.y = y0; rbp.rows = rows; rbp.bytes = bytes;
	rb_copy((struct rbp far *)&rbp);
}

/* ---- drawing an image (IMGS) at a PCjr pixel position, ORed ---- */
static unsigned char far *img_ptr[256]; /* cached at load */
static int blit_linear; /* 1: draw into a linear 80-byte-row buffer (blit_dst) */
static unsigned char far *blit_dst;

static void blit(unsigned i, int px, int py)
{
	unsigned char far *im = img_ptr[i];
	int wb = im[0], h = im[1], vers = im[3], top = (signed char)im[2];
	int bx = px >> 2, k0 = 0, k1 = wb, r0 = 0, r1 = h, r, k;
	unsigned char far *src;
	src = im + 4 + (unsigned)((((px & 3) * vers) >> 2) * h * wb);
	py += top;
	if (bx < 0) k0 = -bx;
	if (bx + k1 > 80) k1 = 80 - bx;
	if (py < 0) r0 = -py;
	if (py + r1 > 200) r1 = 200 - py;
	if (k0 >= k1 || r0 >= r1) return;
	src += r0 * wb;
	if (!blit_linear) {
		unsigned char far *sp = src + k0;
		rbp.src_off = FP_OFF(sp); rbp.src_seg = FP_SEG(sp); rbp.src_stride = wb;
		rbp.dst_off = FP_OFF(vid) + jr_rowoff[py + r0] + bx + k0; rbp.dst_seg = FP_SEG(vid);
		rbp.y = py + r0; rbp.rows = r1 - r0; rbp.bytes = k1 - k0;
		rb_or((struct rbp far *)&rbp);
		return;
	}
	for (r = r0; r < r1; r++, src += wb) {
		unsigned char far *d = blit_dst + (unsigned)(py + r) * 80 + bx;
		for (k = k0; k < k1; k++) d[k] |= src[k];
	}
}

/* ---- what the game has drawn (from its hooks) ---- */
typedef struct { u16 img; int px, py; u8 on; } pos_t;
typedef struct { u16 spr; s16 x, y; u8 on, dirty; } raw_t;
static u16 slot_rec[MAXS];
static raw_t raw[MAXS];        /* as the game drew it (Mac coordinates) */
static pos_t live[MAXS];       /* the same in PCjr terms, worked out per frame */
static pos_t shown[2][MAXS];   /* what each page shows */
static int nlive;
static u8 slot_of[0x1600 / 2]; /* erase record -> slot + 1 */
typedef struct { int px, py; } dot_t;
static dot_t dots[MAXD], dshown[2][MAXD];
static int ndots, ndshown[2];
static long cur_score;

/* Mac to PCjr: x * 5/8, y * 7/12 (rounded down). 16-bit math: x stays
 * within -1000..1000 here, and rows come from a table. */
static int px_of(s16 x) { return (x * 5) >> 3; }
#define PY_LO 128
static signed char py_lo[PY_LO];           /* y = -128..-1 */
static unsigned char py_tab[512];          /* y = 0..511 (rows over 255 never show) */
static void py_init(void)
{
	int y;
	for (y = 0; y < 512; y++) { int v = y * 7 / 12; py_tab[y] = (unsigned char)(v > 255 ? 255 : v); }
	for (y = 1; y <= PY_LO; y++) py_lo[PY_LO - y] = (signed char)(-((y * 7 + 11) / 12));
}
static int py_of(s16 y)
{
	if (y >= 0) return y < 512 ? py_tab[y] : 255;
	return y >= -PY_LO ? py_lo[y + PY_LO] : -100;
}

/* sprite record (CODE 4 offset) -> its 16 images: offsets are at least 32
 * bytes apart, so (offset - 27E0h) / 32 picks the entry */
#define SPR_BASE 0x27e0u
static u16 spr_img[700][16];
static unsigned char spr_ok[700];

static void spr_init(void)
{
	int i, k;
	memset(spr_ok, 0, sizeof spr_ok);
	for (i = 0; i < 28; i++) {
		unsigned char far *e = smap + 34 * i;
		unsigned off = rd16(e), j = (off - SPR_BASE) >> 5;
		if (off < SPR_BASE || j >= 700 || spr_ok[j]) continue;
		spr_ok[j] = 1;
		for (k = 0; k < 16; k++) spr_img[j][k] = (u16)rd16(e + 2 + 2 * k);
	}
}

static unsigned sprite_img(u16 spr, s16 x)
{
	unsigned j = (unsigned)(spr - SPR_BASE) >> 5;
	if (spr < SPR_BASE || j >= 700 || !spr_ok[j]) return 0xffff;
	return spr_img[j][x & 15];
}

static void h_draw(u16 rec, u16 spr, s16 x, s16 y)
{
	int i = slot_of[rec >> 1];
	if (!i) {
		if (nlive >= MAXS) return;
		i = ++nlive;
		slot_of[rec >> 1] = (u8)i;
		slot_rec[i - 1] = rec;
	}
	i--;
	raw[i].spr = spr; raw[i].x = x; raw[i].y = y; raw[i].on = 1; raw[i].dirty = 1;
}

static void h_erase(u16 rec)
{
	int i = slot_of[rec >> 1];
	if (i) { raw[i - 1].on = 0; raw[i - 1].dirty = 1; }
}

/* per frame: Mac positions to PCjr images and pixels, for what changed */
static void convert(void)
{
	int i;
	for (i = 0; i < nlive; i++) {
		raw_t *r = &raw[i];
		if (!r->dirty) continue;
		r->dirty = 0;
		if (!r->on) { live[i].on = 0; continue; }
		live[i].img = (u16)sprite_img(r->spr, r->x);
		live[i].px = px_of((s16)(r->x & ~15));
		live[i].py = py_of(r->y);
		live[i].on = live[i].img != 0xffff;
	}
}

static void h_dot(s16 x, s16 y, u8 on)
{
	int i;
	if (on) { if (ndots < MAXD) { dots[ndots].px = x; dots[ndots].py = y; ndots++; } return; }
	for (i = 0; i < ndots; i++) if (dots[i].px == x && dots[i].py == y) { dots[i] = dots[--ndots]; return; }
}

static void h_score(s32 s) { cur_score = s; }

/* ---- per page: redraw only what changed ---- */
static long score_on[2] = { -1, -1 };
static int force_all[2];
/* What changed this frame: rectangles to restore from the background, and
 * per 8-row band a bitmask of 8-pixel columns (40 bits in 3 words) to find
 * the sprites that overlap them and must be drawn again. */
#define MAXR 160
typedef struct { int x0, y0, x1, y1; } rect_t; /* x in bytes */
static rect_t rects[MAXR];
static int nrects, all_dirty;
static unsigned band[25][3];

static void colmask(int x0, int x1, unsigned m[3])
{
	int c0 = x0 >> 1, c1 = x1 >> 1, c;
	m[0] = m[1] = m[2] = 0;
	for (c = c0; c <= c1; c++) m[c >> 4] |= 1u << (c & 15);
}

static void mark(int x0, int y0, int x1, int y1) /* x in bytes */
{
	unsigned m[3];
	int cy;
	if (x0 < 0) x0 = 0;
	if (x1 > 79) x1 = 79;
	if (y0 < 0) y0 = 0;
	if (y1 > 199) y1 = 199;
	if (x0 > x1 || y0 > y1) return;
	if (nrects >= MAXR) { all_dirty = 1; return; }
	rects[nrects].x0 = x0; rects[nrects].y0 = y0; rects[nrects].x1 = x1; rects[nrects].y1 = y1; nrects++;
	colmask(x0, x1, m);
	for (cy = y0 >> 3; cy <= (y1 >> 3); cy++) { band[cy][0] |= m[0]; band[cy][1] |= m[1]; band[cy][2] |= m[2]; }
}

static void pos_box(const pos_t *p, int *x0, int *y0, int *x1, int *y1)
{
	unsigned char far *im = img_ptr[p->img];
	*x0 = p->px >> 2; *x1 = *x0 + im[0] - 1;
	*y0 = p->py + (signed char)im[2]; *y1 = *y0 + im[1] - 1;
}

static void mark_pos(const pos_t *p) { int x0, y0, x1, y1; pos_box(p, &x0, &y0, &x1, &y1); mark(x0, y0, x1, y1); }

static int pos_dirty(const pos_t *p)
{
	int x0, y0, x1, y1, cy;
	unsigned m[3];
	if (all_dirty) return 1;
	pos_box(p, &x0, &y0, &x1, &y1);
	if (x0 < 0) x0 = 0;
	if (x1 > 79) x1 = 79;
	if (y0 < 0) y0 = 0;
	if (y1 > 199) y1 = 199;
	if (x0 > x1 || y0 > y1) return 0;
	colmask(x0, x1, m);
	for (cy = y0 >> 3; cy <= (y1 >> 3); cy++) if ((band[cy][0] & m[0]) | (band[cy][1] & m[1]) | (band[cy][2] & m[2])) return 1;
	return 0;
}

static void draw_digits_at(long s, int x)
{
	/* as DOSC: right-aligned to Mac x, baseline 325, Airborne Scores 12 */
	int n = 0;
	do {
		int d = (int)(s % 10);
		unsigned char far *e = digt + 4 * d;
		x -= e[0];
		blit(rd16(e + 2), px_of((s16)(x + (signed char)e[1])), py_of(325 - 13));
		s /= 10;
	} while (s && ++n < 5);
}

static void render(int over)
{
	int i;
	pos_t *sh = shown[pg];
	dot_t *ds = dshown[pg];
	static dot_t dpc[MAXD];
	convert();
	for (i = 0; i < ndots; i++) { dpc[i].px = px_of((s16)dots[i].px); dpc[i].py = py_of((s16)dots[i].py); }
	nrects = 0;
	memset(band, 0, sizeof band);
	all_dirty = force_all[pg];
	force_all[pg] = 0;
	if (!all_dirty) {
		for (i = 0; i < nlive; i++) {
			pos_t *a = &sh[i], *b = &live[i];
			if (a->on == b->on && (!b->on || (a->img == b->img && a->px == b->px && a->py == b->py))) continue;
			if (a->on) mark_pos(a);
			if (b->on) mark_pos(b);
		}
		for (i = 0; i < ndshown[pg]; i++) mark(ds[i].px >> 2, ds[i].py, ds[i].px >> 2, ds[i].py);
		for (i = 0; i < ndots; i++) mark(dpc[i].px >> 2, dpc[i].py, dpc[i].px >> 2, dpc[i].py);
		if (score_on[pg] != cur_score) mark(238 * 5 / 32, 175, 300 * 5 / 32 + 1, 199);
	}
	begin();
	if (all_dirty) {
		copy_back(0, 0, 200, 80);
		if (npg == 1) fx_dirty(0, 0, 159, 199);
	} else for (i = 0; i < nrects; i++) {
		rect_t *r = &rects[i];
		copy_back(r->x0, r->y0, r->y1 - r->y0 + 1, r->x1 - r->x0 + 1);
		if (npg == 1) fx_dirty(r->x0 * 2, r->y0, r->x1 * 2 + 1, r->y1);
	}
	if (all_dirty || score_on[pg] != cur_score) { draw_digits_at(cur_score, 290); score_on[pg] = cur_score; }
	for (i = 0; i < nlive; i++) {
		sh[i] = live[i];
		if (live[i].on && pos_dirty(&live[i])) blit(live[i].img, live[i].px, live[i].py);
	}
	for (i = 0; i < ndots; i++) {
		int px = dpc[i].px, py = dpc[i].py;
		if (py >= 0 && py < 200 && px >= 0 && px < 320) vid[jr_rowoff[py] + (px >> 2)] |= (unsigned char)(0xC0 >> ((px & 3) * 2));
		ds[i] = dpc[i];
	}
	ndshown[pg] = ndots;
	if (over) blit(over_img, px_of(186), py_of(130));
	flip();
}

/* ---- the clock: Mac ticks (60.15 a second) ---- */
static unsigned long tick0;
static unsigned long now_ticks(void) { return (tmr_ticks() - tick0) / 19837UL; }

static unsigned long mac_time(void)
{
	struct dosdate_t d;
	struct dostime_t t;
	unsigned long days = 0;
	unsigned y, m;
	static const unsigned char md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	_dos_getdate(&d);
	_dos_gettime(&t);
	for (y = 1904; y < d.year; y++) days += (y % 4 == 0) ? 366 : 365;
	for (m = 1; m < d.month; m++) days += md[m - 1] + ((m == 2 && d.year % 4 == 0) ? 1 : 0);
	days += d.day - 1;
	return days * 86400UL + t.hour * 3600UL + t.minute * 60UL + t.second;
}

/* ---- sound effects: the SFX chunk (tools/mksfx.py) ---- */
/* The Mac's rule: the two lowest-numbered active effects are heard (the
 * first on the noise voice, its rate from tone 3, the second on tone 1),
 * the others run on silently; an effect that ends stops, a loop rewinds.
 * Run from the retrace interrupt, 60 times a second, as the Mac's mixer ran
 * from its tick: the game only starts and stops effects. */
static const unsigned char far *sfx_tab;
static struct { unsigned first, n; unsigned char loop; } sfx[12];
static volatile unsigned sfx_on, sfx_pos[12];
static volatile unsigned char sfx_paused;
static unsigned char sfx_sh[8];
static int sfx_fresh, quiet, song_irq;

static void sfx_load(void)
{
	unsigned char far *c = chunk("SFX ", 0);
	int i;
	if (!c || c[0] != 12) return;
	for (i = 0; i < 12; i++) {
		sfx[i].first = rd16(c + 1 + 6 * i);
		sfx[i].n = rd16(c + 3 + 6 * i);
		sfx[i].loop = c[5 + 6 * i];
	}
	sfx_tab = c + 1 + 6 * 12;
}

static void sfx_start(u8 n)
{
	if (n >= 12 || !sfx[n].n) return;
	_disable(); sfx_pos[n] = 0; sfx_on |= 1u << n; _enable();
}

static void sfx_stop(u8 n)
{
	if (n >= 12) return;
	_disable(); sfx_on &= ~(1u << n); _enable();
}

static void sfx_pause(u8 on) { sfx_paused = on; }

static void sn_out(unsigned char b) { outp(0xC0, b); }

static void sfx_irq(void)
{
	unsigned nd = 1, td = 1, n, p;
	unsigned char na = 15, ta = 15, b[8];
	int v = 0;
	if (!sfx_paused)
		for (n = 0; n < 12; n++) {
			if (!(sfx_on & (1u << n))) continue;
			p = sfx_pos[n];
			if (p >= sfx[n].n) {
				if (!sfx[n].loop) { sfx_on &= ~(1u << n); continue; }
				p = 0;
			}
			if (v < 2) {
				const unsigned char far *r = sfx_tab + (sfx[n].first + p) * 6u;
				if (v == 0) { nd = r[0] | ((unsigned)r[1] << 8); na = r[2]; }
				else { td = r[3] | ((unsigned)r[4] << 8); ta = r[5]; }
				v++;
			}
			sfx_pos[n] = p + 1;
		}
	b[0] = (unsigned char)(0xC0 | (nd & 15));          /* tone 3: the noise's rate */
	b[1] = (unsigned char)((nd >> 4) & 63);
	b[2] = (unsigned char)(nd & 0x8000u ? 0xE3 : 0xE7); /* periodic or white, rate from tone 3 */
	b[3] = (unsigned char)(0xF0 | na);
	b[4] = (unsigned char)(0x80 | (td & 15));          /* tone 1 */
	b[5] = (unsigned char)((td >> 4) & 63);
	b[6] = (unsigned char)(0x90 | ta);
	b[7] = 0xDF;                                       /* tone 3 itself silent */
	if (sfx_fresh || b[0] != sfx_sh[0] || b[1] != sfx_sh[1]) { sn_out(b[0]); sn_out(b[1]); }
	if (sfx_fresh || b[4] != sfx_sh[4] || b[5] != sfx_sh[5]) { sn_out(b[4]); sn_out(b[5]); }
	/* writing the noise control restarts the noise: only on a change */
	if (sfx_fresh || b[2] != sfx_sh[2]) sn_out(b[2]);
	if (sfx_fresh || b[3] != sfx_sh[3]) sn_out(b[3]);
	if (sfx_fresh || b[6] != sfx_sh[6]) sn_out(b[6]);
	if (sfx_fresh) sn_out(b[7]);
	memcpy(sfx_sh, b, sizeof sfx_sh);
	sfx_fresh = 0;
}

static void sfx_begin(void)
{
	sfx_on = 0; sfx_paused = 0;
	if (quiet || !sfx_tab || !fx_irq) return;
	snd_init();
	sfx_fresh = 1;
	fx_irq_hook(sfx_irq);
}

static void sfx_end(void)
{
	int v;
	if (quiet || !sfx_tab || !fx_irq) return;
	fx_irq_hook(0);
	sfx_on = 0;
	for (v = 0; v < 4; v++) sn_out((unsigned char)(0x9F | (v << 5)));
}

/* ---- the startup song: AIRSONG.SND through SAY's player ---- */
extern char __far sp_begin[];
extern char __far sp_end[];
#pragma aux sp_begin "sp_begin"
#pragma aux sp_end "sp_end"
#define SP_CODE_MAX 0x800u   /* player.asm's block layout */
#define SP_TABLE_OFF 0x800u
#define SP_DATA_PARA 0xE0u

/* DOS allocation, "last fit": the block lands at the top of memory, above
 * the PCjr's slow low 128 KB (as SAY does). */
static unsigned alloc_high(unsigned paras)
{
	union REGS r;
	unsigned old, seg;
	r.x.ax = 0x5800; intdos(&r, &r); old = r.x.ax;
	r.x.ax = 0x5801; r.x.bx = 2; intdos(&r, &r);
	r.h.ah = 0x48; r.x.bx = paras; intdos(&r, &r);
	seg = r.x.cflag ? 0 : r.x.ax;
	r.x.ax = 0x5801; r.x.bx = old; intdos(&r, &r);
	return seg;
}

/* Plays the song over what is on the screen; returns when it ends or at a
 * key. If memory is short it plays as much as fits, as the original does
 * on a 128K Mac. */
static void play_song(void)
{
	unsigned char h[16];
	unsigned long n, fit, done, pit;
	unsigned divisor, tlen, tablen, seg, maxp, got;
	unsigned __far *par;
	void (__far *fn)(void);
	int fd;
	if (quiet || _dos_open("AIRSONG.SND", 0, &fd)) return;
	if (_dos_read(fd, h, 16, &got) || got != 16 || memcmp(h, "JRSN", 4) || h[4] != 1 || h[5] != 1) { _dos_close(fd); return; }
	divisor = rd16(h + 6); n = rd32(h + 8); tlen = rd16(h + 12); tablen = rd16(h + 14);
	if (divisor < 100 || tablen != 768 || tlen > 512 || n == 0) { _dos_close(fd); return; }
	if (_dos_allocmem(0xFFFF, &maxp) == 0) { _dos_freemem(maxp); maxp = 0xFFFF; }
	if (maxp < SP_DATA_PARA + 256) { dbg_log("AIRBORNE: no memory for the song"); _dos_close(fd); return; }
	fit = (unsigned long)(maxp - SP_DATA_PARA) * 16UL - 48UL;
	if (n > fit) n = fit;
	if (!(seg = alloc_high((unsigned)(SP_DATA_PARA + (n + 48 + 15) / 16)))) { _dos_close(fd); return; }
	lseek(fd, 16L + tlen, SEEK_SET);
	_dos_read(fd, MK_FP(seg, SP_TABLE_OFF), 768, &got);
	{
		unsigned long off = 0;
		while (off < n) {
			unsigned want = n - off > 0x8000UL ? 0x8000u : (unsigned)(n - off);
			unsigned dseg = seg + SP_DATA_PARA + (unsigned)(off >> 4);
			if (_dos_read(fd, MK_FP(dseg, 0), want, &got) || got != want) { n = off + got; break; }
			off += want;
		}
	}
	_dos_close(fd);
	if ((unsigned)(FP_OFF(sp_end) - FP_OFF(sp_begin)) > SP_CODE_MAX) { _dos_freemem(seg); return; }
	_fmemcpy(MK_FP(seg, 0), sp_begin, FP_OFF(sp_end) - FP_OFF(sp_begin));
	par = (unsigned __far *)MK_FP(seg, 0);
	par[2] = 1; par[3] = divisor;
	par[4] = (unsigned)n; par[5] = (unsigned)(n >> 16);
	par[6] = seg + SP_DATA_PARA;
	par[13] = (unsigned)song_irq; /* /H: interrupt timing, for DOSBox (no 8259 poll) */
	dbg_log("AIRBORNE: song, %lu samples (of %lu) at block %04X", n, rd32(h + 8), seg);
	fx_begin(); /* no page flip left for the retrace interrupt, which the player masks */
	*(unsigned short far *)MK_FP(0x40, 0x1C) = *(unsigned short far *)MK_FP(0x40, 0x1A); /* empty the BIOS key buffer: a key there stops it */
	fn = (void (__far *)(void))MK_FP(seg, 0);
	fn();
	done = par[7] | ((unsigned long)par[8] << 16);
	pit = par[11] | ((unsigned long)par[12] << 16);
	{ /* the player stopped the BIOS clock: add the time back */
		unsigned long __far *ticks = (unsigned long __far *)MK_FP(0x40, 0x6C);
		_disable();
		*ticks += (pit ? pit : done * divisor) >> 16;
		if (*ticks >= 0x1800B0UL) { *ticks -= 0x1800B0UL; *(unsigned char __far *)MK_FP(0x40, 0x70) = 1; }
		_enable();
	}
	/* it leaves PIT channel 0 in the BIOS's mode 3; tmr_ms wants mode 2 */
	_disable(); outp(0x43, 0x34); outp(0x40, 0); outp(0x40, 0); _enable();
	*(unsigned short far *)MK_FP(0x40, 0x1C) = *(unsigned short far *)MK_FP(0x40, 0x1A);
	dbg_log("AIRBORNE: song played %lu, late %u%s", done, par[10], par[9] ? ", stopped by a key" : "");
	_dos_freemem(seg);
}

/* ---- keys ---- */
static unsigned char prev_key[128];
static int pressed(int sc) { int p = key_down[sc] && !prev_key[sc]; return p; }
static void keys_seen(void) { int i; for (i = 0; i < 128; i++) prev_key[i] = key_down[i]; }
/* /KEYS:script (tests): one scripted key every 1.5 s, as if typed.
 * E Enter, X Esc, I, G, S, Space as ' ', digits 1-3; P saves the screen
 * as SHOTn.PPM. */
static const char *key_script;
static unsigned long key_script_t;
static void save_ppm(void);
static int script_key(void)
{
	static const char ch[] = "EXIGS 123";
	static const unsigned char sc[] = { 0x1c, 0x01, 0x17, 0x22, 0x1f, 0x39, 0x02, 0x03, 0x04 };
	const char *f;
	if (!key_script || !*key_script || tmr_ms() - key_script_t < 1500) return 0;
	key_script_t = tmr_ms();
	if (*key_script == 'P') { key_script++; save_ppm(); dbg_log("AIRBORNE: screen saved"); return 0; }
	f = strchr(ch, *key_script++);
	dbg_log("AIRBORNE: scripted key %c", f ? *f : '?');
	return f ? sc[f - ch] : 0;
}
static int any_new_key(void) { int i = script_key(); if (i) return i; for (i = 1; i < 128; i++) if (key_down[i] && !prev_key[i]) return i; return 0; }
static void wait_release(void) { int i, any; do { any = 0; for (i = 1; i < 128; i++) if (key_down[i]) any = 1; } while (any); keys_seen(); }

static const char SCAN_CH[] = /* scan code -> character, 0 = none */
	"\0\0" "1234567890-=" "\0\0" "QWERTYUIOP[]" "\0\0" "ASDFGHJKL;'`" "\0\\" "ZXCVBNM,./";

/* ---- high scores (AIRBORNE.HI) ---- */
typedef struct { char name[16]; long score; } hs_t;
static hs_t hs[10];

static void hs_load(void)
{
	FILE *f = fopen("AIRBORNE.HI", "rb");
	memset(hs, 0, sizeof hs);
	if (!f) return;
	if (fread(hs, sizeof hs, 1, f) != 1) memset(hs, 0, sizeof hs);
	fclose(f);
	{ int i; for (i = 0; i < 10; i++) hs[i].name[15] = 0; }
}

static void hs_save(void)
{
	FILE *f = fopen("AIRBORNE.HI", "wb");
	if (!f) return;
	fwrite(hs, sizeof hs, 1, f);
	fclose(f);
}

/* ---- screens ---- */
static int opt_level = 1, opt_guided = 1;

static void title_draw(void)
{
	static const char *const lv[3] = { "1 Beginner", "2 Intermediate", "3 Advanced" };
	static const int lvn[3] = { 1, 5, 10 };
	unsigned char far *s = chunk("TITL", 0);
	char line[40];
	int i, p;
	for (p = 0; p < npg; p++) {
		begin();
		full_screen(s);
		box(1, 141, 52, 197, 0);
		text(2, 144, "ENTER Start  I Help", 3, 0);
		for (i = 0; i < 3; i++) {
			sprintf(line, "%c%s", opt_level == lvn[i] ? '>' : ' ', lv[i]);
			text(2, 156 + 11 * i, line, opt_level == lvn[i] ? 3 : 2, 0);
		}
		text(36, 156, opt_guided ? "G Guided" : "G Unguid", 3, 0);
		text(36, 167, "S Song", 2, 0);
		text(36, 178, "ESC Quit", 2, 0);
		box(54, 99, 79, 197, 0);
		for (i = 0; i < 10; i++) {
			sprintf(line, "%2d %-6.6s%4ld", i + 1, hs[i].name[0] ? hs[i].name : "", hs[i].score % 100000L);
			text(54, 101 + 9 * i, line, 3, 0);
		}
		flip();
	}
}

static void help_page(void)
{
	static const char *const lines[] = {
		"AIRBORNE! ON THE PCjr", "",
		"Left/Right  aim the gun", "Up          fire (hold for a stream)",
		"Space       switch guns", "Tab         pause (any key goes on)",
		"G           guided/unguided shells", "Esc         end the game", "",
		"The game is the 1985 Mac original's", "logic, checked tick for tick against", "the original's own code.", "",
		"Press a key", 0 };
	int p, i;
	for (p = 0; p < npg; p++) {
		begin();
		box(0, 0, 79, 199, 0);
		for (i = 0; lines[i]; i++) text(2, 20 + 10 * i, lines[i], i == 0 ? 3 : 2, 0);
		flip();
	}
}

static void wait_any_key(void)
{
	wait_release();
	while (!any_new_key()) ;
	wait_release();
}

static void instructions(void)
{
	char id[5] = "INS1";
	int i;
	for (i = 1; i <= 5; i++) {
		id[3] = (char)('0' + i);
		show_full(id);
		wait_release();
		for (;;) { int k = any_new_key(); if (k == KEY_ESC) { wait_release(); return; } if (k) break; }
	}
	help_page();
	wait_any_key();
}

static void name_entry(long score)
{
	char name[16];
	int n = 0, p, i;
	name[0] = 0;
	wait_release();
	for (;;) {
		char line[48];
		for (p = 0; p < npg; p++) {
			begin();
			box(6, 70, 73, 129, 0);
			box(6, 70, 73, 70, 3); box(6, 129, 73, 129, 3);
			sprintf(line, "Top ten with %ld points!", score);
			text(10, 78, line, 3, 0);
			text(10, 94, "Your name:", 2, 0);
			sprintf(line, "%s_ ", name);
			text(32, 94, line, 3, 0);
			text(10, 114, "Enter when done", 2, 0);
			flip();
		}
		for (;;) {
			int k = any_new_key();
			if (!k) continue;
			keys_seen();
			if (k == KEY_ENTER) goto done;
			if (k == 0x0E) { if (n) name[--n] = 0; break; }
			if (k == KEY_SPACE && n < 15) { name[n++] = ' '; name[n] = 0; break; }
			if (k < (int)sizeof SCAN_CH && SCAN_CH[k] && n < 15) { name[n++] = SCAN_CH[k]; name[n] = 0; break; }
		}
	}
done:
	for (i = 9; i > 0 && score > hs[i - 1].score; i--) hs[i] = hs[i - 1];
	memset(&hs[i], 0, sizeof hs[i]);
	strncpy(hs[i].name, n ? name : "ANONYMOUS", 15);
	hs[i].score = score;
	hs_save();
}

/* ---- a game ---- */
static unsigned long demo_ticks, shot_tick, tk_game, tk_draw, fixed_time;
static int bench, prof;

static int shot_n;
static void save_ppm(void)
{
	static const unsigned char rgb[4] = { 255, 170, 85, 0 };
	char name[16]; /* "SHOT99.PPM" at most */
	FILE *fp;
	int x, y;
	sprintf(name, "SHOT%d.PPM", ++shot_n % 100);
	fp = fopen(name, "wb");
	if (!fp) return;
	fprintf(fp, "P6 320 200 255\n");
	for (y = 0; y < 200; y++)
		for (x = 0; x < 320; x++) {
			unsigned char v = (unsigned char)((vid[jr_rowoff[y] + (x >> 2)] >> (6 - 2 * (x & 3))) & 3);
			fputc(rgb[v], fp); fputc(rgb[v], fp); fputc(rgb[v], fp);
		}
	fclose(fp);
}

static long play_game(void);

/* a game, with its sound effects */
static long play(void)
{
	long r;
	sfx_begin();
	r = play_game();
	sfx_end();
	return r;
}

static long play_game(void)
{
	unsigned long last = 0, t, frames = 0, tfr0;
	int r = 0, held = 0;
	int clear_mods = 0;
	nlive = 0; ndots = 0; ndshown[0] = ndshown[1] = 0; force_all[0] = force_all[1] = 1;
	memset(slot_of, 0, sizeof slot_of); memset(shown, 0, sizeof shown); memset(raw, 0, sizeof raw); memset(live, 0, sizeof live);
	score_on[0] = score_on[1] = -1; cur_score = 0;
	g_mouse_h = 256; g_mouse_v = 150; g_button = 0; g_mods = 0;
	game_start((u16)opt_level, (u8)opt_guided, hs[0].score, fixed_time ? fixed_time : mac_time());
	/* the Hi Score box: into the background (cleared there by mkdat.py) */
	{
		int y;
		for (y = 180; y < 195; y++) _fmemset(back + y * 80 + 61, 0, 12);
		blit_linear = 1; blit_dst = back;
		draw_digits_at(hs[0].score, 456);
		blit_linear = 0;
	}
	{ extern u32 (*g_prof_clock)(void); if (demo_ticks && prof) g_prof_clock = tmr_ticks; }
	tick0 = tmr_ticks();
	game_tick(0);
	wait_release();
	tfr0 = tmr_ms();
	for (;;) {
		unsigned long now = demo_ticks && bench ? last + 3 : now_ticks();
		int stepped = 0;
		if (now > last + 12) { tick0 += (now - last - 12) * 19837UL; now = last + 12; }
		/* keys: once per loop */
		if (!demo_ticks) {
			if (pressed(KEY_ESC)) { keys_seen(); return game_score(); }
			if (pressed(KEY_SPACE)) game_key(49, ' ');
			if (pressed(0x0F)) game_key(48, 9);
			if (pressed(0x22)) { g_mods |= 0x100; game_key(1, 's'); clear_mods = 2; }
			if (r == 2) { int k = any_new_key(); if (k && k != 0x0F) game_key(0x7f, 0); }
			keys_seen();
		}
		for (t = last + 1; t <= now; t++) {
			int dir = 0;
			if (demo_ticks) {
				g_mouse_h = (s16)(56 + (t * 7) % 400);
				g_button = (t % 40) < 30;
				if (t % 600 == 300) game_key(49, ' ');
			} else {
				if (key_down[KEY_LEFT]) dir = -1; else if (key_down[KEY_RIGHT]) dir = 1;
				held = dir ? held + 1 : 0;
				g_mouse_h = (s16)(g_mouse_h + dir * (held > 20 ? 6 : 3));
				if (g_mouse_h < 0) g_mouse_h = 0;
				if (g_mouse_h > 511) g_mouse_h = 511;
				g_button = key_down[KEY_UP] ? 1 : 0;
			}
			{ unsigned long a = tmr_ticks(); r = game_tick(t); tk_game += tmr_ticks() - a; }
			stepped = 1;
			if (clear_mods && !--clear_mods) g_mods = 0;
			if (r == 1 || r == 3) break;
			if (demo_ticks && t >= demo_ticks) { last = t; goto end_demo; }
		}
		last = now;
		if (stepped) {
			{ unsigned long a = tmr_ticks(); render(r == 1); tk_draw += tmr_ticks() - a; }
			frames++;
			if (shot_tick && last >= shot_tick) { save_ppm(); shot_tick = 0; }
		}
		if (r == 3) return game_score();
		if (r == 1) {
			if (demo_ticks) break;
			wait_release();
			for (;;) { int k = any_new_key(); if (k == KEY_ENTER || k == KEY_SPACE || k == KEY_UP || k == KEY_ESC) break; keys_seen(); }
			wait_release();
			return game_score();
		}
	}
end_demo:
	{
		unsigned long ms = tmr_ms() - tfr0;
		if (!ms) ms = 1;
		dbg_log("AIRBORNE: %lu ticks, %lu frames in %lu ms: %lu.%lu fps, score %ld, level %u, sprites %d",
			last, frames, ms, frames * 1000UL / ms, (frames * 10000UL / ms) % 10, (long)game_score(), game_level(), nlive);
		{
			unsigned long h = 0x811c9dc5UL;
			unsigned k;
			for (k = 0; k < 300 * 64; k++) { h ^= fb_mem[k]; h *= 0x01000193UL; }
			dbg_log("AIRBORNE: hidden screen hash %08lx at tick %lu", h, last);
		}
		{
			extern u32 g_prof[16];
			static const char *const nm[16] = { "djet", "diml", "mfir", "diff", "gren+tksh", "airf", "heli", "diam", "diht", "gact", "dosc", "tank", "digm", "trpt", "expl", "catc" };
			int k;
			for (k = 0; k < 16; k++) if (g_prof[k]) dbg_log("  %-10s %lu ms", nm[k], TMR_TICKS_MS(g_prof[k]));
		}
		dbg_log("AIRBORNE: game %lu ms (%lu ms a step), drawing %lu ms (%lu ms a frame)",
			TMR_TICKS_MS(tk_game), TMR_TICKS_MS(tk_game) * 3 / (last ? last : 1), TMR_TICKS_MS(tk_draw), TMR_TICKS_MS(tk_draw) / (frames ? frames : 1));
	}
	return game_score();
}

/* ---- start ---- */
static unsigned largest_kb(void)
{
	unsigned seg;
	if (_dos_allocmem(0xFFFF, &seg) == 0) { _dos_freemem(seg); return 1023; }
	return seg >> 6;
}

static void load_bar(unsigned long done, unsigned long size)
{
	int w = size ? (int)(done * 60UL / size) : 0;
	begin();
	box(0, 0, 79, 199, 0);
	text(26, 84, "LOADING AIRBORNE!", 3, 0);
	box(10, 100, 69, 107, 1);
	if (w) box(10, 100, 9 + w, 107, 3);
	flip();
}

static int load(const char *path)
{
	int fd;
	unsigned paras, got;
	unsigned long done = 0;
	if (_dos_open(path, 0, &fd)) { sprintf(err, "can't open %.40s", path); return -1; }
	dat_len = (unsigned long)lseek(fd, 0L, SEEK_END);
	lseek(fd, 0L, SEEK_SET);
	if (dat_len < 64 || dat_len > 600000UL) { sprintf(err, "%.40s is %lu bytes", path, dat_len); _dos_close(fd); return -1; }
	paras = (unsigned)((dat_len + 15) >> 4);
	if (_dos_allocmem(paras, &dat_seg)) { sprintf(err, "not enough memory: %lu KB needed, %u KB free", (dat_len + 1023) >> 10, dat_seg >> 6); _dos_close(fd); return -1; }
	while (done < dat_len) {
		unsigned want = dat_len - done > 8192UL ? 8192u : (unsigned)(dat_len - done);
		if (_dos_read(fd, dat_at(done), want, &got) || got != want) { strcpy(err, "read error"); _dos_close(fd); return -1; }
		done += want;
		load_bar(done, dat_len);
	}
	_dos_close(fd);
	if (_fmemcmp(dat_at(0), "AIRB", 4)) { sprintf(err, "%.40s is not Airborne! data", path); return -1; }
	back = chunk("BACK", 0); imgs = chunk("IMGS", 0); smap = chunk("SMAP", 0); digt = chunk("DIGT", 0);
	if (!back || !imgs || !smap || !digt || !chunk("CODE", 0) || !chunk("OVER", 0)) { strcpy(err, "data file is missing parts"); return -1; }
	nimg = rd16(imgs);
	if (nimg > 256) { strcpy(err, "too many images"); return -1; }
	{ unsigned k; for (k = 0; k < nimg; k++) img_ptr[k] = imgs + rd32(imgs + 2 + 4 * k); }
	over_img = rd16(chunk("OVER", 0));
	g_code = chunk("CODE", 0);
	spr_init();
	py_init();
	sfx_load();
	return 0;
}

int main(int argc, char **argv)
{
	int i, test = 0;
	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (a[0] != '/' && a[0] != '-') continue;
		if (!strnicmp(a + 1, "LEVEL:", 6)) opt_level = atoi(a + 7);
		else if (!stricmp(a + 1, "U")) opt_guided = 0;
		else if (!stricmp(a + 1, "Q")) quiet = 1;
		else if (!stricmp(a + 1, "H")) song_irq = 1;
		else if (!strnicmp(a + 1, "DEMO:", 5)) demo_ticks = strtoul(a + 6, 0, 10);
		else if (!strnicmp(a + 1, "SHOT:", 5)) shot_tick = strtoul(a + 6, 0, 10);
		else if (!stricmp(a + 1, "BENCH")) bench = 1;
		else if (!stricmp(a + 1, "PROF")) prof = 1;
		else if (!strnicmp(a + 1, "TIME:", 5)) fixed_time = strtoul(a + 6, 0, 10);
		else if (!strnicmp(a + 1, "KEYS:", 5)) key_script = a + 6;
		else { printf("AIRBORNE [/LEVEL:n] [/U] [/DEMO:ticks] [/SHOT:ticks] [/BENCH]\n"); return 1; }
	}
	if (opt_level < 1 || opt_level > 28) opt_level = 1;
	test = demo_ticks != 0;
	if (fx_init(0)) { printf("AIRBORNE: no graphics (error %d), %u KB free\n", gfx_err, largest_kb()); return 1; }
	gfx_free_back();
	npg = fx_pages == 2 ? 2 : 1;
	mode_320x4();
	dbg_log("AIRBORNE: graphics up, %d pages, irq %d, %u KB free in one piece", fx_pages, fx_irq, largest_kb());
	if (load("AIRBORNE.DAT")) { gfx_done(); printf("AIRBORNE: %s\n", err); return 1; }
	dbg_log("AIRBORNE: data %lu bytes, %u images, %u KB left", dat_len, nimg, largest_kb());
	g_fb = fb_mem;
	g_hooks.draw = h_draw; g_hooks.erase = h_erase; g_hooks.dot = h_dot; g_hooks.score = h_score;
	g_hooks.sound_start = sfx_start; g_hooks.sound_stop = sfx_stop; g_hooks.sound_pause = sfx_pause;
	key_init();
	tmr_init();
	hs_load();
	if (test) {
		play();
	} else {
		show_full("PRES");
		{ unsigned long t = tmr_ms(); wait_release(); while (tmr_ms() - t < 2500 && !any_new_key()) ; }
		show_full("SPIC");
		if (quiet) wait_any_key();
		else { play_song(); wait_release(); }
		for (;;) {
			int k;
			title_draw();
			wait_release();
			do k = any_new_key(); while (!k);
			keys_seen();
			if (k == KEY_ESC) break;
			if (k == 0x02) opt_level = 1;
			else if (k == 0x03) opt_level = 5;
			else if (k == 0x04) opt_level = 10;
			else if (k == 0x22) opt_guided = !opt_guided;
			else if (k == 0x17) instructions();
			else if (k == 0x1F) { show_full("SPIC"); play_song(); wait_release(); }
			else if (k == KEY_ENTER) {
				long s = play();
				if (s > 0 && s > hs[9].score) name_entry(s);
			}
		}
	}
	dbg_log("AIRBORNE: quitting");
	gfx_done();
	_dos_freemem(dat_seg);
	dbg_log("AIRBORNE: back to DOS");
	return 0;
}
