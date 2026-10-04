/* JRGFX fast path demo and benchmark (DEMO F). Linked into DEMO.EXE.
 * Benchmarks every fx_ primitive (LOG.TXT), then runs two scenes with an
 * on-screen fps counter: a raycaster-style view (fx_wall, compiled sprites,
 * a static HUD, palette cycling) and a flat-shaded polygon scene. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "jrgfx.h"

static unsigned rs = 1;
static int rnd(int n)
{
	rs ^= rs << 7; rs ^= rs >> 9; rs ^= rs << 8;
	return (int)(rs % (unsigned)n);
}

#define BENCH(name, count, body) do { \
	unsigned long t_ = tmr_ms(); int i_; \
	for (i_ = 0; i_ < (count); i_++) { body; } \
	t_ = tmr_ms() - t_; \
	dbg_log("fxbench %-26s %4d x: %6lu ms total, %6lu us each", name, count, t_, t_ * 1000UL / (count)); \
} while (0)

static unsigned char tex[4][256];      /* packed wall textures (64 texels used) */
static unsigned char rowpat[200];
static unsigned recip[512];            /* (64 << 8) / h: texture step per wall height */
static unsigned char spr[16 * 16];
static fx_sprite *ball;
static int shots;

static void shot(const char *n)
{
	char b[16];
	if (!shots) return;
	sprintf(b, "F%s.PPM", n);
	fx_flip(); /* show what was drawn, then capture the page on screen */
	fx_begin();
	fx_screenshot(b);
}

static void make_art(void)
{
	int i, t;
	for (t = 0; t < 4; t++)
		for (i = 0; i < 256; i++) {
			int v = i & 63, c;
			static const unsigned char base[4][2] = { {4, 12}, {8, 7}, {2, 10}, {1, 9} };
			if (t == 0) c = (v & 15) == 0 ? 7 : ((v >> 4) & 1) ? base[t][0] : base[t][1];  /* bricks */
			else if (t == 1) c = (v & 7) < 2 ? 0 : base[t][(v >> 3) & 1];               /* panels */
			else c = base[t][(v * 5 >> 3) & 1];
			tex[t][i] = (unsigned char)(t == 0 && (v & 15) && ((v >> 4) & 1) ? FX_PAIR(c, 6) : FX_SOLID(c));
		}
	for (i = 0; i < 200; i++)
		rowpat[i] = i < 30 ? FX_SOLID(9 + (i / 10)) :           /* 3 sky bands: colors 9-11 cycle */
		            i < 80 ? FX_PAIR(1, i & 1 ? 0 : 1) :
		            (unsigned char)(i < 120 ? FX_PAIR(6, 8) : FX_SOLID(8));
	recip[0] = 0xFFFF;
	for (i = 1; i < 512; i++) recip[i] = (unsigned)((64UL << 8) / i);
	for (i = 0; i < 256; i++) {
		int x = i & 15, y = i >> 4, dx = x - 8, dy = y - 8, d = dx * dx + dy * dy;
		spr[i] = (unsigned char)(d < 56 ? (d < 12 ? 15 : d < 30 ? 14 : 6) : 0);
	}
	ball = fx_compile(spr, 16, 16, 0);
}

static int qsink;
static unsigned long tsink;
static void quad_calcs(int a)
{
	int q[8], k, j;
	for (j = 0; j < 16; j++)
		for (k = 0; k < 4; k++) {
			q[2 * k] = 50 + FIX2INT(fix_cos(a + k * 256) * 12);
			q[2 * k + 1] = 50 + FIX2INT(fix_sin(a + k * 256) * 24);
		}
	qsink += q[3];
}

static void small_tri(void)
{
	int x0 = rnd(130), y0 = rnd(170);
	fx_tri(x0, y0, x0 + rnd(30), y0 + rnd(30), x0 + rnd(30), y0 + rnd(30), FX_SOLID2(rnd(16)));
}

static void benchmarks(void)
{
	unsigned buf = fx_alloc_buffer(), pg = fx_seg;
	int x;
	BENCH("fx_clear", 10, fx_clear(FX_SOLID2(i_)));
	BENCH("fx_flip (IRQ deferred)", 10, (fx_flip(), fx_begin()));
	BENCH("fx_fill_rect full", 10, fx_fill_rect(0, 0, 160, 200, FX_SOLID2(i_)));
	BENCH("fx_col x80 full height", 5, for (x = 0; x < 80; x++) fx_col(x, 0, 199, FX_SOLID(x & 15)));
	BENCH("fx_texcol x80 full height", 5, for (x = 0; x < 80; x++) fx_texcol(x, 0, 199, tex[0], 0, 82));
	BENCH("fx_texcol x80 ~150 rows", 5, for (x = 0; x < 80; x++) fx_texcol(x, 25, 174, tex[0], 0, 109));
	shot("TEX");
	fx_view(0, 159, rowpat);
	BENCH("fx_wall x80 changing", 10, for (x = 0; x < 80; x++) { int h = 40 + ((x * 7 + i_ * 13) & 63); fx_wall(x, 80 - h, 79 + h, tex[x & 3], 0, recip[2 * h]); });
	BENCH("fx_wall2 x40 changing", 10, for (x = 0; x < 40; x++) { int h = 40 + ((x * 14 + i_ * 13) & 63); fx_wall2(x, 80 - h, 79 + h, tex[x & 3], 0, recip[2 * h]); });
	BENCH("fx_wall x80 same height", 10, for (x = 0; x < 80; x++) fx_wall(x, 50, 109, tex[x & 3], 0, recip[60]));
	fx_view(0, 159, 0);
	BENCH("fx_hspan 0-100 px", 200, fx_hspan(rnd(200), rnd(160), rnd(160) % 100, FX_SOLID(rnd(16))));
	BENCH("fx_tri ~30 px", 100, small_tri());
	BENCH("fx_poly quad 24x48", 100, { int q[8]; q[0] = 10; q[1] = 10; q[2] = 34; q[3] = 12; q[4] = 32; q[5] = 58; q[6] = 8; q[7] = 56; fx_poly(4, q, FX_SOLID2(3)); });
	BENCH("fx_poly sky 160x80", 10, { int q[8]; q[0] = 0; q[1] = 0; q[2] = 159; q[3] = 0; q[4] = 159; q[5] = 70; q[6] = 0; q[7] = 90; fx_poly(4, q, FX_DITH(9, 11)); });
	BENCH("16 quad vertex calcs", 10, quad_calcs(i_));
	BENCH("fx_tri big", 10, fx_tri(rnd(160), 0, 0, 199, 159, rnd(200), FX_SOLID2(rnd(16))));
	shot("TRIS");
	BENCH("fx_sprite 16x16 compiled", 100, fx_sprite_draw(ball, rnd(144), rnd(184)));
	BENCH("gfx_blit 16x16 keyed (old)", 100, gfx_blit(rnd(144), rnd(184), 16, 16, spr, 0));
	BENCH("fx_text 10 chars", 50, fx_text(rnd(80) & ~1, rnd(190), "JRGFX FAST", rnd(16)));
	shot("SPRITES");
	BENCH("fx_line random", 200, fx_line(rnd(160), rnd(200), rnd(160), rnd(200), rnd(16)));
	BENCH("gfx_line random (old)", 200, gfx_line(rnd(160), rnd(200), rnd(160), rnd(200), (unsigned char)rnd(16)));
	BENCH("tmr_ms", 200, tsink += tmr_ms());
	BENCH("tmr_ticks", 200, tsink += tmr_ticks());
	BENCH("fx_pset", 1000, fx_pset(rnd(160), rnd(200), rnd(16)));
	if (buf) {
		BENCH("fx_copy_rect full (RAM->page)", 10, fx_copy_rect(pg, buf, 0, 0, 159, 199));
		BENCH("fx_copy_rect 160x40 HUD", 10, fx_copy_rect(pg, buf, 0, 160, 159, 199));
		_fmemcpy(MK_FP(pg, 0), MK_FP(buf, 0), 16384u);
		BENCH("fx_copy_delta identical", 10, fx_copy_delta(pg, buf));
		BENCH("fx_copy_delta 1/16 changed", 10, { int k; for (k = 0; k < 1000; k += 16) ((unsigned char far *)MK_FP(buf, 0))[k * 16] ^= 0xFF; fx_copy_delta(pg, buf); });
		BENCH("fx_copy_delta all changed", 10, { _fmemset(MK_FP(buf, 0), i_ * 17 + 1, 16384u); fx_copy_delta(pg, buf); });
	}
	BENCH("fx_pal_cycle", 10, fx_pal_cycle(9, 11, 1));
	BENCH("gfx_present (old, full)", 5, gfx_present());
}

static void hud(int fps10, const char *name)
{
	char b[24];
	fx_fill_rect(0, 160, 160, 40, FX_SOLID2(8));
	fx_fill_rect(0, 160, 160, 1, FX_SOLID2(7));
	sprintf(b, "%2d.%d FPS", fps10 / 10, fps10 % 10);
	fx_text(4, 170, b, 15);
	fx_text(4, 184, name, 14);
	fx_sync_rows(160, 199); /* the HUD is static: put it on both pages */
}

/* Raycaster-style: 80 wall columns (heights from a sine "corridor" that
 * scrolls), two compiled sprites, palette cycling sky, HUD redrawn only
 * when the fps number changes. */
static unsigned long ray_scene(int frames, int lo)
{
	int f, x, last = -1;
	unsigned long t0 = tmr_ms(), tf = t0;
	int n = 0;
	fx_clear(0);
	fx_view(0, 159, rowpat);
	hud(0, lo ? "RAYCAST LO" : "RAYCASTER");
	for (f = 0; f < frames && !key_down[KEY_ESC]; f++) {
		fx_begin();
		for (x = 0; x < 80; x += 1 + lo) {
			int a = x * 6 + f * 12;
			int h = 50 + FIX2INT(fix_sin(a) * 36) + FIX2INT(fix_cos(x * 3 - f * 5) * 18);
			if (h < 4) h = 4;
			if (lo) fx_wall2(x >> 1, 80 - h, 79 + h, tex[((x + f / 4) >> 3) & 3], 0, recip[2 * h]);
			else    fx_wall(x, 80 - h, 79 + h, tex[((x + f / 4) >> 3) & 3], 0, recip[2 * h]);
		}
		fx_sprite_draw(ball, 40 + FIX2INT(fix_sin(f * 20) * 30), 100);
		fx_sprite_draw(ball, 100 + FIX2INT(fix_cos(f * 14) * 40), 70 + (f & 15));
		if ((f & 7) == 0) fx_pal_cycle(9, 11, 1);
		n++;
		if (tmr_ms() - tf >= 1000 || f == frames - 1) {
			unsigned long t = tmr_ms();
			int fps10 = (int)(n * 10000UL / (t - tf));
			if (fps10 != last) { hud(fps10, lo ? "RAYCAST LO" : "RAYCASTER"); last = fps10; }
			tf = t; n = 0;
		}
		if (f == 20) { shot(lo ? "RAYLO" : "RAY"); continue; }
		fx_flip();
	}
	return tmr_ms() - t0;
}

/* Flat-shaded polygons: sky/ground split by a tilting horizon, 16 spinning
 * quads with dither patterns, one big pentagon. */
static unsigned long poly_scene(int frames)
{
	int f, i, last = -1, n = 0;
	unsigned long t0 = tmr_ms(), tf = t0;
	fx_clear(0);
	fx_view(0, 159, 0);
	hud(0, "POLYGONS");
	fx_clip(0, 159);
	for (f = 0; f < frames && !key_down[KEY_ESC]; f++) {
		int xy[10], tilt = FIX2INT(fix_sin(f * 6) * 20);
		fx_begin();
		xy[0] = 0; xy[1] = 0; xy[2] = 159; xy[3] = 0; xy[4] = 159; xy[5] = 80 - tilt; xy[6] = 0; xy[7] = 80 + tilt;
		fx_poly(4, xy, FX_DITH(9, 11));
		xy[1] = 159; xy[3] = 159;
		fx_poly(4, xy, FX_DITH(2, 6));
		for (i = 0; i < 16; i++) {
			int cx = 10 + (i & 7) * 20, cy = 40 + (i >> 3) * 70, a = f * 16 + i * 64, k, r = 12;
			for (k = 0; k < 4; k++) {
				xy[2 * k] = cx + FIX2INT(fix_cos(a + k * 256) * r);
				xy[2 * k + 1] = cy + FIX2INT(fix_sin(a + k * 256) * r * 2);
			}
			fx_poly(4, xy, i & 1 ? FX_SOLID2(i) : FX_DITH(i, 15 - i));
		}
		for (i = 0; i < 5; i++) {
			int a = f * 8 + i * ANG_FULL / 5;
			xy[2 * i] = 80 + FIX2INT(fix_cos(a) * 40);
			xy[2 * i + 1] = 80 + FIX2INT(fix_sin(a) * 50);
		}
		fx_poly(5, xy, FX_SOLID2(14));
		n++;
		if (tmr_ms() - tf >= 1000 || f == frames - 1) {
			unsigned long t = tmr_ms();
			int fps10 = (int)(n * 10000UL / (t - tf));
			fx_clip(0, 199);
			if (fps10 != last) { hud(fps10, "POLYGONS"); last = fps10; }
			fx_clip(0, 159);
			tf = t; n = 0;
		}
		if (f == 20) { shot("POLY"); continue; }
		fx_flip();
	}
	fx_clip(0, 199);
	return tmr_ms() - t0;
}

int fast_main(int frames, int sh, int flags)
{
	unsigned long t;
	shots = sh;
	if (fx_init(flags)) { dbg_log("fx_init failed %d", gfx_err); return 1; }
	key_init();
	dbg_log("fast demo: pages %d irq %d buffer %d", fx_pages, fx_irq, (flags & FX_BUFFER) != 0);
	make_art();
	benchmarks();
	if (!frames) { key_done(); gfx_done(); return 0; }
	t = ray_scene(frames, 0);
	dbg_log("raycaster scene: %d frames in %lu ms = %lu.%lu fps", frames, t, frames * 1000UL / t, frames * 10000UL / t % 10);
	t = ray_scene(frames, 1);
	dbg_log("raycaster scene, low detail (fx_wall2): %d frames in %lu ms = %lu.%lu fps", frames, t, frames * 1000UL / t, frames * 10000UL / t % 10);
	t = poly_scene(frames);
	dbg_log("polygon scene: %d frames in %lu ms = %lu.%lu fps", frames, t, frames * 1000UL / t, frames * 10000UL / t % 10);
	key_done();
	gfx_done();
	return 0;
}
