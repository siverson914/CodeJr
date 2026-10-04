/* JRDOOM renderer: a fixed-point raycaster drawing straight into the hidden
 * video page (the library's fx_* page flipping), so there is no copy.
 *
 * Each ray is one video byte (2 pixels) wide, or two bytes in low detail.
 * A byte holds two pixels, so a "color" here is a word: low byte for even
 * rows, high byte for odd rows. Two different nibbles make a checkerboard
 * dither for free, which gives distance shading with only 16 colors.
 *
 * Rays are cast every 4th column (fastcol.asm: cast_col); a flat wall's
 * height is linear across the screen, so columns in between are
 * interpolated when both ends see the same wall face. Walls and sprites are
 * drawn as vertical runs of one color (textures are run-length encoded per
 * column and pre-shaded), jumping into an unrolled store sequence: about
 * 2 instructions per pair of rows.
 */
#include <dos.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include "jrdoom.h"

unsigned long rend_prof[5];
static unsigned char far *vbuf;   /* the view window in the hidden video page */
static void draw_sprite(const Sprite *s, int left, int wcols, int top16, int step16, int sh);
static void draw_things(int px, int py, int dirx, int diry);
static void draw_weapon(void);
void compile_guns(void);
void view_target(void);
static unsigned rowofs[VIEW_H / 2 + 1];

/* Everything the ray caster (cast_col in fastcol.asm) reads and writes.
 * The layout is fixed: fastcol.asm has the offsets (C_*). Per column:
 * wall height (also the z-buffer: a nearer thing is taller), map cell (only
 * valid on columns actually cast), texture column, material, side. */
typedef struct {
	int px, py, dirx, diry, plx, ply;   /* 0 */
	int start;                          /* 12 */
	unsigned char far *map;             /* 14 */
	unsigned wallk;                     /* 18 */
	int camx[VIEW_COLS];                /* 20 */
	int wallh[VIEW_COLS];               /* 180 */
	int hidx[VIEW_COLS];                /* 340 */
	unsigned char hu[VIEW_COLS];        /* 500 */
	unsigned char hmat[VIEW_COLS];      /* 580 */
	unsigned char hside[VIEW_COLS];     /* 660 */
	int dopen[32];                      /* 740 */
	unsigned char dmat[32];             /* 804 */
} CastCtx;
typedef char ctx_check[sizeof(CastCtx) == 836 ? 1 : -1];
static CastCtx cc;
#define camx  cc.camx
#define hidx  cc.hidx
#define hu    cc.hu
#define hmat  cc.hmat
#define hside cc.hside
int *wallh_p = cc.wallh;
#define wallh cc.wallh

void cast_col(CastCtx far *ctx, int c);
#pragma aux cast_col "cast_col_" parm [dx bx] [ax] modify [ax bx cx dx es];
#define cast_ray(c) cast_col(&cc, c)

/* View window: vcols x vh (at most 80 x 144), drawn at the top left of the
 * buffer and copied to (vx0, vy0) on screen (in bytes and rows). */
int vcols = VIEW_COLS, vh = VIEW_H, view_size, low_detail = 1;
static int ncols = VIEW_COLS;   /* rays: vcols, or vcols / 2 in low detail */
static int horizon = VIEW_H / 2, vx0, vy0;
static unsigned wallk = 51712u;   /* wall height in rows at distance 1 (x256) */
static unsigned sprk = 15514u;    /* sprite width in columns at distance 1 (x256) */
static int colk = 61;             /* columns per unit of lateral/depth */
static unsigned char bands[8][3]; /* ceiling and floor: start row, colors */
static unsigned char shade_h[256];   /* distance shade for wall heights < 256 */

/* ---- Inner loops (8088 assembly) --------------------------------------- */

/* BANK as text for the assembler; checked below. */
/* Inline asm below has BANK (0x2000) written out: each pragma string is one line. */
typedef char bank_check[BANK == 0x2000 ? 1 : -1];

/* n row pairs: even row gets AL, odd row (BANK bytes on) gets AH. */
void fill_pairs(unsigned char far *p, unsigned n, unsigned pat);
#pragma aux fill_pairs = \
	"L1: mov es:[di],al" \
	"    mov es:[di+2000h],ah" \
	"    add di,80" \
	"    loop L1" \
	parm [es di] [cx] [ax] modify exact [di cx];

void fill_words(unsigned char far *p, unsigned n, unsigned pat);
#pragma aux fill_words = "rep stosw" parm [es di] [cx] [ax] modify exact [di cx];

/* fastcol.asm: one column of runs (len, keep mask, even byte, odd byte). */
void draw_runs(unsigned char far *col, const unsigned char far *runs, int top16, int step16);
#pragma aux draw_runs "draw_runs_" parm [es di] [dx si] [bx] [cx] modify [ax bx cx dx si di es];

void draw_wall_runs(unsigned char far *col, const unsigned char far *runs, unsigned rowtab);
#pragma aux draw_wall_runs "draw_wall_runs_" parm [es di] [dx si] [bx] modify [ax bx cx dx si di es];

void set_clip(int rows);
#pragma aux set_clip "set_clip_" parm [ax] modify exact [];

void draw_wall_runs2(unsigned char far *col, const unsigned char far *runs, unsigned rowtab);
#pragma aux draw_wall_runs2 "draw_wall_runs2_" parm [es di] [dx si] [bx] modify [ax bx cx dx si di es];

void draw_runs2(unsigned char far *col, const unsigned char far *runs, int top16, int step16);
#pragma aux draw_runs2 "draw_runs2_" parm [es di] [dx si] [bx] [cx] modify [ax bx cx dx si di es];

/* (a * b) >> 14, signed */
int muls14(int a, int b);
#pragma aux muls14 = "imul dx" "shl ax,1" "rcl dx,1" "shl ax,1" "rcl dx,1" \
	parm [ax] [dx] value [dx] modify exact [ax dx];

/* a * b / c, signed; the caller keeps the result in range */
int muldiv(int a, int b, int c);
#pragma aux muldiv = "imul dx" "idiv bx" parm [ax] [dx] [bx] value [ax] modify exact [ax dx];

/* Rows [r0, r1) of one column, dithered pattern `pat`. */
static void col_fill(unsigned char far *base, int r0, int r1, unsigned pat)
{
	unsigned char far *p = base + rowofs[r0 >> 1];
	int n = r1 - r0;
	if (r0 & 1) { p[BANK] = (unsigned char)(pat >> 8); p += VIEW_COLS; n--; }
	if (n >= 2) { fill_pairs(p, n >> 1, pat); p += rowofs[n >> 1]; }
	if (n & 1) *p = (unsigned char)pat;
}

/* ---- Colors --------------------------------------------------------------- */

#define PAT(a, b) ((unsigned)(((a) << 4) | (b)) | ((unsigned)(((b) << 4) | (a)) << 8))
#define RAMP 16
static unsigned ramp[NUM_MATS][RAMP];
static const unsigned char mat_tex[NUM_MATS] = {
	0, TEX_STONE, TEX_BRICK, TEX_TECH, TEX_MARBLE, TEX_WOOD, TEX_ROCK, TEX_EXIT, TEX_DOOR, TEX_DOOR
};
/* light to dark, pairs of colors; index = texture tone + distance + side */
static const unsigned char ramp_src[NUM_MATS][8][2] = {
	{{0,0}},
	{{15,7},{7,7},{7,8},{8,8},{8,0},{8,0},{0,0},{0,0}},   /* stone  */
	{{12,6},{6,6},{6,4},{4,8},{4,0},{8,0},{0,0},{0,0}},   /* brick  */
	{{15,11},{7,3},{3,8},{3,1},{1,0},{8,0},{0,0},{0,0}},  /* tech   */
	{{10,2},{2,2},{2,8},{2,0},{8,0},{8,0},{0,0},{0,0}},   /* marble */
	{{14,6},{6,6},{6,8},{6,0},{8,0},{8,0},{0,0},{0,0}},   /* wood   */
	{{12,4},{4,4},{4,8},{4,0},{8,0},{8,0},{0,0},{0,0}},   /* rock   */
	{{15,15},{12,12},{4,4},{8,8},{8,8},{0,0},{0,0},{0,0}},/* exit (fullbright) */
	{{15,7},{7,6},{6,8},{8,8},{8,0},{8,0},{0,0},{0,0}},   /* door   */
	{{15,12},{12,4},{4,4},{4,8},{4,0},{8,0},{0,0},{0,0}}, /* locked */
};

/* Pre-shaded wall runs: for each material, shade level and texture column,
 * the texture column's runs with the dithered colors already looked up:
 * (end texel, even-row byte, odd-row byte)..., 0.
 * After them, in the same segment, a table of the screen rows of the 17
 * texel boundaries for every wall height (heights over VIEW_H in steps of 8). */
#define SHADES 8
#define NHEIGHTS (VIEW_H + 1 + 232)   /* for the largest window */
static unsigned char far *wallruns;
static unsigned wallofs[NUM_MATS][SHADES][16];
static unsigned htab;            /* offset of the row table in wallruns */

void render_init(void)
{
	int m, i, b, c;
	unsigned n = 0, pass;
	for (i = 0; i <= VIEW_H / 2; i++) rowofs[i] = i * VIEW_COLS;
	for (m = 0; m < NUM_MATS; m++)
		for (i = 0; i < RAMP; i++) {
			int k = i < 8 ? i : 7;
			ramp[m][i] = PAT(ramp_src[m][k][0], ramp_src[m][k][1]);
		}
	/* pass 0 counts, pass 1 fills */
	for (pass = 0; pass < 2; pass++) {
		n = 0;
		for (m = 1; m < NUM_MATS; m++)
			for (b = 0; b < SHADES; b++)
				for (c = 0; c < 16; c++) {
					const Texture *t = &textures[mat_tex[m]];
					const unsigned char *r = t->data + t->col[c];
					unsigned last = 0xFFFF, v = 0;
					if (pass) wallofs[m][b][c] = n;
					for (; r[0]; r += 2) {
						unsigned pat = ramp[m][r[1] + b];
						v += r[0];
						if (pat == last) {         /* same color: extend */
							if (pass) wallruns[n - 3] = (unsigned char)v;
							continue;
						}
						if (pass) {
							wallruns[n] = (unsigned char)v;
							wallruns[n + 1] = (unsigned char)pat;
							wallruns[n + 2] = (unsigned char)(pat >> 8);
						}
						last = pat;
						n += 3;
					}
					if (pass) wallruns[n] = 0;
					n++;
				}
		if (!pass) wallruns = (unsigned char far *)_fmalloc(n + NHEIGHTS * 17);
	}
	htab = n;
	set_view_size(0);
}

/* View sizes: 0 = full (80x144), 1 = 64x116, 2 = 48x88. */
static const unsigned char sizes[3][2] = { {80, 144}, {64, 116}, {48, 88} };

void set_view_size(int n)
{
	static const unsigned char bandf[7][3] = {   /* start (64ths of half view), colors */
		{0, 8, 8}, {7, 8, 0}, {23, 0, 0}, {64, 0, 0}, {76, 8, 0}, {91, 6, 8}, {108, 6, 6}
	};
	int i, v;
	view_size = n;
	vcols = sizes[n][0]; vh = sizes[n][1];
	horizon = vh / 2;
	vx0 = (VIEW_COLS - vcols) / 2;
	vy0 = (VIEW_H - vh) / 2;
	wallk = (unsigned)(51712UL * vcols / VIEW_COLS);
	sprk = (unsigned)(15514UL * vcols / VIEW_COLS);
	colk = (61 * vcols + 40) / VIEW_COLS;
	ncols = low_detail ? vcols / 2 : vcols;
	for (i = 0; i < ncols; i++)
		camx[i] = (int)(10813L * (2 * i + 1) / ncols - 10813);
	for (i = 0; i < 7; i++) {
		bands[i][0] = (unsigned char)(((horizon * bandf[i][0]) >> 6) & ~1);
		bands[i][1] = bandf[i][1]; bands[i][2] = bandf[i][2];
	}
	bands[7][0] = (unsigned char)vh;
	set_clip(vh);
	for (i = 1; i < 256; i++) {
		unsigned perp = (unsigned)(51712UL * VIEW_COLS / vcols / i);   /* in tiles * 256 */
		int b2 = (int)(perp / 480);      /* one shade step per ~2 tiles */
		shade_h[i] = (unsigned char)(b2 > 6 ? 6 : b2);
	}
	for (i = 0; i < NHEIGHTS; i++) {
		int h = i <= vh ? i : vh + (i - vh) * 8;
		for (v = 0; v <= 16; v++) {
			int r = (horizon * 16 - h * 8 + v * h) >> 4;
			if (r < 0) r = 0;
			if (r > vh) r = vh;
			wallruns[htab + i * 17 + v] = (unsigned char)r;
		}
	}
}

/* Ceiling and floor. Rows that every wall column covers (the middle
 * `keep` rows, from the shortest wall) are left alone: the walls paint them. */
static void clear_view(int keep)
{
	int b, s0 = (horizon - keep / 2 + 3) >> 1, s1 = (horizon + keep / 2 - 2) >> 1;
	unsigned words = vcols >> 1;
	if (s1 < s0) s1 = s0;
	for (b = 0; bands[b][0] < vh; b++) {
		int r0 = bands[b][0] >> 1, r1 = bands[b + 1][0] >> 1, k;
		unsigned pat = PAT(bands[b][1], bands[b][2]);
		unsigned ev = (pat & 0xFF) * 0x101, od = (pat >> 8) * 0x101;
		for (k = 0; k < 2; k++) {   /* the part above the gap, then below */
			int a0 = k ? (r0 > s1 ? r0 : s1) : r0;
			int a1 = k ? r1 : (r1 < s0 ? r1 : s0);
			if (a1 <= a0) continue;
			if (vcols == VIEW_COLS) {
				fill_words(vbuf + rowofs[a0], (rowofs[a1] - rowofs[a0]) >> 1, ev);
				fill_words(vbuf + BANK + rowofs[a0], (rowofs[a1] - rowofs[a0]) >> 1, od);
			} else {
				int r;
				for (r = a0; r < a1; r++) {
					fill_words(vbuf + rowofs[r], words, ev);
					fill_words(vbuf + BANK + rowofs[r], words, od);
				}
			}
		}
	}
}
/* ---- Walls -------------------------------------------------------------- */


/* Columns strictly between a and b (cast already). A flat wall's height is
 * linear across the screen, so if both ends see the same face, interpolate;
 * otherwise cast the middle column and split. */
static void span(int a, int b)
{
	int c, n = b - a;
	if (n < 2) return;
	if (hidx[a] == hidx[b] && hside[a] == hside[b]) {
		int h = wallh[a] << 2, u = hu[a] << 2;
		int dh = n == 4 ? wallh[b] - wallh[a] : ((wallh[b] - wallh[a]) << 2) / n;
		int du = n == 4 ? hu[b] - hu[a] : ((hu[b] - hu[a]) << 2) / n;
		unsigned char m = hmat[a], sd = hside[a];
		if (n == 2) { dh <<= 1; du <<= 1; }
		for (c = a + 1; c < b; c++) {
			h += dh; u += du;
			wallh[c] = h >> 2;
			hu[c] = (unsigned char)(u >> 2);
			hmat[c] = m;
			hside[c] = sd;
		}
		return;
	}
	c = (a + b) >> 1;
	cast_ray(c);
	span(a, c);
	span(c, b);
}

static int hmin;   /* shortest wall this frame */

static void cast_walls(int px, int py, int dirx, int diry, int plx, int ply)
{
	int c;
	cc.px = px; cc.py = py; cc.dirx = dirx; cc.diry = diry; cc.plx = plx; cc.ply = ply;
	cc.start = (py >> 8) * MAPW + (px >> 8);
	cc.map = map;
	cc.wallk = wallk;
	for (c = 0; c < ndoors; c++) {
		cc.dopen[c] = doors[c].open;
		cc.dmat[c] = (unsigned char)(doors[c].locked ? M_LOCKED : M_DOOR);
	}
	for (c = 0; c < ncols; c += 4) cast_ray(c);
	cast_ray(ncols - 1);
	for (c = 0; c < ncols - 4; c += 4) span(c, c + 4);
	span(ncols - 4, ncols - 1);
	hmin = 2000;
	for (c = 0; c < ncols; c++) if (wallh[c] < hmin) hmin = wallh[c];
}

static void draw_walls(void)
{
	int c, step = low_detail ? 2 : 1;
	unsigned char far *col = vbuf;
	unsigned tab = FP_OFF(wallruns) + htab;
	int light = muzzle_light >> 1;
	for (c = 0; c < ncols; c++, col += step) {
		int h = wallh[c], mat = hmat[c], bias;
		const unsigned char far *runs;
		if (mat == M_EXIT) bias = 0;
		else {
			bias = (h < 256 ? shade_h[h] : 0) + hside[c] - light;
			if (bias < 0) bias = 0;
		}
		if (h < 16) { /* too small for texture detail */
			int r0 = horizon - (h >> 1);
			col_fill(col, r0, r0 + h, ramp[mat][1 + bias]);
			if (step == 2) col_fill(col + 1, r0, r0 + h, ramp[mat][1 + bias]);
			continue;
		}
		if (h > vh) {
			h = vh + ((h - vh) >> 3);
			if (h >= NHEIGHTS) h = NHEIGHTS - 1;
		}
		runs = wallruns + wallofs[mat][bias][hu[c] >> 4];
		if (step == 2) draw_wall_runs2(col, runs, tab + h * 17);
		else draw_wall_runs(col, runs, tab + h * 17);
	}
	if (low_detail)   /* the z-buffer is per byte column */
		for (c = ncols - 1; c >= 0; c--) wallh[2 * c + 1] = wallh[2 * c] = wallh[c];
}

/* ---- Sprites ------------------------------------------------------------ */

/* Draws sprite `s` with its left edge at column `left`, `wcols` columns
 * wide, top at `top16` (rows * 16), `step16` (rows * 16) per texel row.
 * Columns where the wall is at least `sh` tall (nearer) are skipped. */
static void draw_sprite(const Sprite *s, int left, int wcols, int top16, int step16, int sh)
{
	int c0 = left < 0 ? 0 : left, c1 = left + wcols, c, step = low_detail ? 2 : 1;
	unsigned tstep = (unsigned)(((unsigned long)s->w << 8) / (unsigned)wcols);
	unsigned tu;
	if (c1 > vcols) c1 = vcols;
	if (step == 2) c0 &= ~1;     /* 4-pixel columns, like the walls */
	tu = (unsigned)(c0 - left) * tstep + (tstep >> 1);
	if (c0 < left) tu = tstep >> 1;
	tstep *= step;
	for (c = c0; c < c1; c += step, tu += tstep) {
		const unsigned char far *run;
		if (wallh[c] >= sh) continue;
		if ((tu >> 8) >= s->w) break;
		run = s->data + s->col[tu >> 8];
		if (step == 2) draw_runs2(vbuf + c, run, top16, step16);
		else draw_runs(vbuf + c, run, top16, step16);
	}
}

static int obj_sprite(const Obj *o)
{
	int walk = (int)((gametic >> 3) & 1);
	switch (o->type) {
	case OT_ZOMBIE:
		switch (o->state) {
		case S_CHASE: return walk ? SPR_ZOMBIE_B : SPR_ZOMBIE_A;
		case S_ATTACK: return o->timer <= 3 ? SPR_ZOMBIE_FIRE : SPR_ZOMBIE_A;
		case S_PAIN: return SPR_ZOMBIE_B;
		case S_DIE: return SPR_ZOMBIE_DIE;
		case S_DEAD: return SPR_ZOMBIE_DEAD;
		}
		return SPR_ZOMBIE_A;
	case OT_IMP:
		switch (o->state) {
		case S_CHASE: return walk ? SPR_IMP_B : SPR_IMP_A;
		case S_ATTACK: return o->timer <= 5 ? SPR_IMP_ATTACK : SPR_IMP_A;
		case S_PAIN: return SPR_IMP_B;
		case S_DIE: return SPR_IMP_DIE;
		case S_DEAD: return SPR_IMP_DEAD;
		}
		return SPR_IMP_A;
	case OT_FIREBALL: return o->state == S_BOOM ? SPR_BOOM : SPR_FIREBALL;
	case OT_MEDIKIT: return SPR_MEDIKIT;
	case OT_STIM: return SPR_STIMPACK;
	case OT_CLIP: return SPR_CLIP;
	case OT_SHELLS: return SPR_SHELLS;
	case OT_SHOTGUN: return SPR_SHOTGUN;
	case OT_KEY: return SPR_REDKEY;
	case OT_LAMP: return SPR_LAMP;
	case OT_BARREL: return SPR_BARREL;
	case OT_COLUMN: return SPR_COLUMN;
	}
	return -1;
}

static void draw_things(int px, int py, int dirx, int diry)
{
	static unsigned char order[MAX_OBJS];
	int i, j, n = 0;
	for (i = 0; i < nobjs; i++) {
		Obj *o = &objs[i];
		int dx, dy, depth, lat, col, half;
		o->seen = 0;
		if (o->type == OT_NONE) continue;
		dx = o->x - px; dy = o->y - py;
		if (abs(dx) > 24 * TILE || abs(dy) > 24 * TILE) continue;
		depth = muls14(dx, dirx) + muls14(dy, diry);
		if (depth < 40) continue;
		lat = muls14(dy, dirx) - muls14(dx, diry);
		if (abs(lat) > depth + 128) continue;   /* well outside the view */
		col = vcols / 2 + muldiv(lat, colk, depth);
		half = (int)((sprk >> 1) / (unsigned)depth);  /* half the columns of a tile */
		if (col + half < 0 || col - half >= vcols) continue;
		o->scol = col; o->sdepth = depth; o->shw = half / 2 + 1;
		o->sh = (int)(wallk / (unsigned)depth);
		if (o->sh > 2000) o->sh = 2000;
		o->seen = 1;
		/* insertion sort, far to near */
		for (j = n; j > 0 && objs[order[j - 1]].sdepth < depth; j--) order[j] = order[j - 1];
		order[j] = (unsigned char)i;
		n++;
	}
	for (i = 0; i < n; i++) {
		Obj *o = &objs[order[i]];
		int sp = obj_sprite(o), h, w;
		if (sp < 0) continue;
		h = o->sh;
		w = (int)(sprk / (unsigned)o->sdepth);
		if (w < 1) w = 1;
		draw_sprite(&sprites[sp], o->scol - w / 2, w, horizon * 16 - h * 8, h / 2, h);
	}
}

/* The weapon is drawn with the library's compiled sprites: the art becomes
 * 8088 code that stores the pixels with immediate moves. */
static fx_sprite *gun_spr[4];
static const unsigned char far *gun_pix[4] = { gunpix_pistol, gunpix_pistol_fire, gunpix_sgun, gunpix_sgun_fire };
static const unsigned char gun_ids[4] = { SPR_PISTOL, SPR_PISTOL_FIRE, SPR_SGUN, SPR_SGUN_FIRE };

static void draw_weapon(void)
{
	int k, bx = 0, by = 0, w, h, x, y;
	if (pl.dead) return;
	k = pl.weapon * 2 + (pl.fire_t ? 1 : 0);
	w = sprites[gun_ids[k]].w * 2;
	h = sprites[gun_ids[k]].h;
	if (pl.moving && !pl.fire_t) {
		bx = (int)(((long)sin_tab(pl.bob) * 3) >> 14);
		by = (int)(((long)abs(cos_tab(pl.bob)) * 5) >> 14);
	}
	if (pl.fire_t > 2) by += 3;
	x = (vx0 + vcols / 2 + bx) * 2 - w / 2;
	y = vy0 + vh - h - by;          /* bobbing only lifts it: it never leaves the view */
	if (gun_spr[k]) fx_sprite_draw(gun_spr[k], x, y);
}

/* Compiling a sprite takes a moment (seconds at 4.77 MHz): do all the
 * weapons up front, while the title screen is up. */
void compile_guns(void)
{
	int k;
	for (k = 0; k < 4; k++)
		gun_spr[k] = fx_compile(gun_pix[k], sprites[gun_ids[k]].w * 2, sprites[gun_ids[k]].h, 255);
}


void render_view(void)
{
	int dirx = cos_tab(pl.ang), diry = sin_tab(pl.ang);
	view_target();
	if (profiling) {   /* the same, timing each stage (JRDOOM /PROF) */
		unsigned long t0 = tmr_ticks(), t1;
		cast_walls(pl.x, pl.y, dirx, diry, -diry, dirx);
		t1 = tmr_ticks(); rend_prof[0] += t1 - t0; t0 = t1;
		clear_view(hmin);
		t1 = tmr_ticks(); rend_prof[1] += t1 - t0; t0 = t1;
		draw_walls();
		t1 = tmr_ticks(); rend_prof[2] += t1 - t0; t0 = t1;
		draw_things(pl.x, pl.y, dirx, diry);
		t1 = tmr_ticks(); rend_prof[3] += t1 - t0; t0 = t1;
		draw_weapon();
		rend_prof[4] += tmr_ticks() - t0;
		return;
	}
	cast_walls(pl.x, pl.y, dirx, diry, -diry, dirx);
	clear_view(hmin);
	draw_walls();
	draw_things(pl.x, pl.y, dirx, diry);
	draw_weapon();
}

/* Points the renderer at the hidden page; call after fx_begin. */
void view_target(void)
{
	vbuf = (unsigned char far *)MK_FP(fx_seg, (vy0 >> 1) * VIEW_COLS + vx0);
}

/* The view is drawn in place; only the one-page fallback needs telling. */
void view_to_screen(void)
{
	if (fx_pages == 1) fx_dirty(vx0 * 2, vy0, (vx0 + vcols) * 2 - 1, vy0 + vh - 1);
}

void view_to_back(void)
{
	int y, x;
	for (y = 0; y < vh; y++) {
		const unsigned char far *s = vbuf + (y & 1 ? BANK : 0) + rowofs[y >> 1];
		unsigned char far *d = gfx_back + (unsigned)(y + vy0) * gfx_w + 2 * vx0;
		/* the view starts on an even row, so row parity matches the page */
		for (x = 0; x < vcols; x++) {
			d[2 * x] = (unsigned char)(s[x] >> 4);
			d[2 * x + 1] = (unsigned char)(s[x] & 15);
		}
	}
}

/* The frame around a smaller view: a dark checker with a bevel, like
 * Doom's border, drawn into the back buffer and shown. */
void draw_border(void)
{
	int x, y;
	for (y = 0; y < VIEW_H; y++)
		for (x = 0; x < 160; x++)
			gfx_back[(unsigned)y * 160 + x] = (unsigned char)(((x >> 2) + (y >> 3)) & 1 ? 6 : 8);
	if (vcols < VIEW_COLS) {
		int x0 = vx0 * 2 - 1, x1 = (vx0 + vcols) * 2, y0 = vy0 - 1, y1 = vy0 + vh;
		gfx_hline(x0, x1, y0, 0);
		gfx_vline(x0, y0, y1, 0);
		gfx_hline(x0, x1, y1, 7);
		gfx_vline(x1, y0, y1, 7);
	}
	gfx_present_rows(0, VIEW_H - 1);
	fx_sync_rows(0, VIEW_H - 1);
}

/* ---- Testing aids ------------------------------------------------------ */

/* Draws sprites side by side at 2 rows per texel (art check). */
void sprite_gallery(int first)
{
	int i;
	view_target();
	clear_view(0);
	for (i = 0; i < 5 && first + i < NUM_SPRITES; i++) {
		const Sprite *s = &sprites[first + i];
		draw_sprite(s, i * 16, s->w, 0, s->h > 32 ? 16 : 64, 30000);
	}
	if (first == 0) {
		clear_view(0);
		draw_sprite(&sprites[0], 5, 30, 0, 64, 30000);
	}
}

/* Times each stage of the renderer, n times over, from the current view. */
void bench_render(int n)
{
	int dirx = cos_tab(pl.ang), diry = sin_tab(pl.ang), i;
	int plx = -diry, ply = dirx;
	unsigned long t0, t1, t2, t3, t4, t5, t6;
	view_target();
	t0 = tmr_ms();
	for (i = 0; i < n; i++) tmr_ms();
	t1 = tmr_ms();
	cast_walls(pl.x, pl.y, dirx, diry, plx, ply);
	dbg_log("tmr_ms x%d: %lu ms", n, t1 - t0);
	t0 = tmr_ms();
	for (i = 0; i < n; i++) { int c; for (c = 0; c < vcols; c++) cast_ray(c); }
	t1 = tmr_ms();
	for (i = 0; i < n; i++) { int c; for (c = 0; c < vcols; c++) cast_ray(c); }
	t2 = tmr_ms();
	dbg_log("80 rays: %lu ms*10, 80 more rays %lu", (t1 - t0) * 10 / n, (t2 - t1) * 10 / n);
	t0 = tmr_ms();
	cast_walls(pl.x, pl.y, dirx, diry, plx, ply);
	for (i = 0; i < n; i++) clear_view(hmin);
	t1 = tmr_ms();
	for (i = 0; i < n; i++) cast_walls(pl.x, pl.y, dirx, diry, plx, ply);
	t2 = tmr_ms();
	for (i = 0; i < n; i++) draw_walls();
	t3 = tmr_ms();
	for (i = 0; i < n; i++) draw_things(pl.x, pl.y, dirx, diry);
	t4 = tmr_ms();
	for (i = 0; i < n; i++) draw_weapon();
	t5 = tmr_ms();
	for (i = 0; i < n; i++) view_to_screen();
	t6 = tmr_ms();
	t0 = tmr_ms();
	for (i = 0; i < n; i++) { fx_begin(); render_view(); view_to_screen(); fx_flip(); }
	t1 = tmr_ms();
	dbg_log("full frames: %lu ms*10 each", (t1 - t0) * 10 / n);
	view_target();

}

