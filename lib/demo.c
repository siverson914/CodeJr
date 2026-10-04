/* JRGFX demo and benchmark.
 *   DEMO [8|9|F] [/F:frames] [/S] [/B] [/N]
 * Runs a benchmark of each primitive (logged to LOG.TXT), then an animated
 * scene for `frames` frames (default 60) logging frames per second, with a
 * few sound effects. ESC quits early. /S writes screenshots DEMOx_*.PPM.
 * F runs the fast path (fx_*) instead: its benchmarks, then a raycaster
 * scene and a polygon scene with an fps counter (see fastdemo.c).
 * /B and /N pass FX_BUFFER and FX_NOIRQ to fx_init.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jrgfx.h"

static int mode = 8, frames = 60, shots, fxflags;
int fast_main(int frames, int shots, int flags);
static unsigned char tex[256], spr[16 * 16];

static void shot(const char *name)
{
	char buf[16];
	if (!shots) return;
	sprintf(buf, "D%d%s.PPM", mode, name);
	gfx_screenshot(buf);
}

/* Cheap random numbers so the benchmarks time the library, not rand(). */
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
	dbg_log("bench %-22s %3d x: %6lu ms total, %5lu us each", name, count, t_, t_ * 1000UL / (count)); \
} while (0)

static void small_tri(void)
{
	int x0 = rnd(gfx_w - 30), y0 = rnd(gfx_h - 30);
	gfx_fill_tri(x0, y0, x0 + rnd(30), y0 + rnd(30), x0 + rnd(30), y0 + rnd(30), (unsigned char)rnd(16));
}

static void benchmarks(void)
{
	int w = gfx_w, h = gfx_h, x;
	BENCH("gfx_clear", 10, gfx_clear((unsigned char)i_));
	BENCH("gfx_present (full)", 10, gfx_present());
	BENCH("gfx_present_rows 0-99", 10, gfx_present_rows(0, 99));
	BENCH("gfx_fill_rect (full)", 10, gfx_fill_rect(0, 0, w, h, (unsigned char)i_));
	BENCH("gfx_vline x all cols", 2, for (x = 0; x < w; x++) gfx_vline(x, 0, h - 1, (unsigned char)(x & 15)));
	shot("VLINE");
	BENCH("gfx_vtex x all cols", 2, for (x = 0; x < w; x++) gfx_vtex(x, 20 + (x & 31), h - 21 - (x & 31), tex, 0, (fix)(256L * 65536L / (h - 40 - 2 * (x & 31)))));
	shot("VTEX");
	BENCH("gfx_hline 100px", 200, gfx_hline(rnd(w), rnd(w) % 100 + 0, rnd(h), (unsigned char)rnd(16)));
	BENCH("gfx_line random", 200, gfx_line(rnd(w), rnd(h), rnd(w), rnd(h), (unsigned char)rnd(16)));
	shot("LINES");
	gfx_clear(0);
	BENCH("gfx_fill_tri ~30px", 100, small_tri());
	BENCH("gfx_fill_tri big", 10, gfx_fill_tri(rnd(w), 0, 0, h - 1, w - 1, rnd(h), (unsigned char)rnd(16)));
	shot("TRIS");
	gfx_clear(1);
	BENCH("gfx_pset", 1000, gfx_pset(rnd(w), rnd(h), (unsigned char)rnd(16)));
	BENCH("gfx_blit 16x16 keyed", 100, gfx_blit(rnd(w) - 8, rnd(h) - 8, 16, 16, spr, 0));
	BENCH("gfx_blit 16x16 opaque", 100, gfx_blit(rnd(w) - 8, rnd(h) - 8, 16, 16, spr, -1));
	BENCH("gfx_text 10 chars", 50, gfx_text(rnd(w) - 40, rnd(h), "JRGFX DEMO", (unsigned char)rnd(16)));
	shot("SPRITES");
	BENCH("fix_mul", 1000, fix_mul((fix)rs << 4, 123456L));
	BENCH("fix_div", 1000, fix_div((fix)rs << 4, 123456L));
	BENCH("fix_div (|b| < 1.0)", 1000, fix_div((fix)rs << 4, 12345L));
	BENCH("fix_sin", 1000, fix_sin(i_));
	BENCH("fix_sqrt", 200, fix_sqrt((fix)i_ << 12));
	BENCH("gfx_vsync", 10, gfx_vsync());
}

/* One animated frame: sky/floor, raycaster-like textured columns, a
 * rotating filled polygon, sprites and a status line. */
static void frame(int f)
{
	int w = gfx_w, h = gfx_h, x, i, xy[10];
	char buf[40];
	gfx_fill_rect(0, 0, w, h / 2, 1);
	gfx_fill_rect(0, h / 2, w, h / 2, 6);
	for (x = 0; x < w; x += 1) {
		int ht = 40 + FIX2INT(fix_mul(fix_sin(x * 8 + f * 16), INT2FIX(30)));
		gfx_vtex(x, h / 2 - ht, h / 2 + ht, tex, 0, fix_div(INT2FIX(64), INT2FIX(2 * ht + 1)));
	}
	for (i = 0; i < 5; i++) {
		int a = f * 8 + i * ANG_FULL / 5;
		xy[2 * i] = w / 2 + FIX2INT(fix_cos(a) * (w / 6));
		xy[2 * i + 1] = 60 + FIX2INT(fix_sin(a) * 30);
	}
	gfx_fill_poly(5, xy, 14);
	for (i = 0; i < 5; i++)
		gfx_line(xy[2 * i], xy[2 * i + 1], xy[(2 * i + 2) % 10], xy[(2 * i + 3) % 10], 15);
	gfx_blit((f * 3) % (w + 16) - 16, h - 40, 16, 16, spr, 0);
	sprintf(buf, "F%d", f);
	gfx_text(2, 2, buf, 15);
}

int main(int argc, char **argv)
{
	int i, f;
	unsigned long t0, t;
	for (i = 1; i < argc; i++) {
		if ((argv[i][0] | 32) == 'f') mode = 'F';
		else if ((argv[i][0] == '/' || argv[i][0] == '-') && (argv[i][1] | 32) == 'b') fxflags |= FX_BUFFER;
		else if ((argv[i][0] == '/' || argv[i][0] == '-') && (argv[i][1] | 32) == 'n') fxflags |= FX_NOIRQ;
		else if (argv[i][0] == '9') mode = 9;
		else if (argv[i][0] == '8') mode = 8;
		else if ((argv[i][0] == '/' || argv[i][0] == '-') && (argv[i][1] | 32) == 'f') frames = atoi(argv[i] + 3);
		else if ((argv[i][0] == '/' || argv[i][0] == '-') && (argv[i][1] | 32) == 's') shots = 1;
	}
	for (i = 0; i < 256; i++) tex[i] = (unsigned char)(((i >> 4) & 1) ? 4 : (i & 8) ? 12 : 7);
	for (i = 0; i < 256; i++) { int x = i & 15, y = i >> 4, dx = x - 8, dy = y - 8;
		spr[i] = (unsigned char)(dx * dx + dy * dy < 49 ? (dx * dx + dy * dy < 16 ? 14 : 10) : 0); }
	tmr_init();
	if (mode == 'F') return fast_main(frames, shots, fxflags);
	if (gfx_init(mode)) { dbg_log("gfx_init(%d) failed", mode); tmr_done(); return 1; }
	key_init();
	snd_init();
	dbg_log("demo mode %d (%dx%d), %d frames", mode, gfx_w, gfx_h, frames);
	snd_tone(0, 440, 12);
	benchmarks();
	snd_tone(0, 0, 0);
	t0 = tmr_ms();
	for (f = 0; f < frames && !key_down[KEY_ESC]; f++) {
		frame(f);
		if (f == 10) { gfx_palette(6, 2); shot("PAL"); gfx_palette(6, 6); }
		gfx_present();
		snd_tone(1, (unsigned)(200 + (f & 31) * 20), (f & 1) ? 8 : 0);
		if ((f & 15) == 0) snd_noise(5, 10); else snd_noise(0, 0);
	}
	t = tmr_ms() - t0;
	shot("SCENE");
	snd_done();
	key_done();
	gfx_done();
	dbg_log("scene: %d frames in %lu ms = %lu.%lu fps", f, t, f * 1000UL / (t ? t : 1), f * 10000UL / (t ? t : 1) % 10);
	tmr_done();
	return 0;
}
