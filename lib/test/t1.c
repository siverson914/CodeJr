/* Draws every primitive once and writes SHOT8.PPM / SHOT9.PPM. */
#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <conio.h>
#include "../jrgfx.h"
extern unsigned jr_rowoff[200], gfx_vseg;
extern int gfx_force_claim, gfx_err;
static long vcheck(void)
{
	int x, y; long bad = 0;
	for (y = 0; y < gfx_h; y++) {
		unsigned char far *v = (unsigned char far *)MK_FP(gfx_vseg, jr_rowoff[y]);
		for (x = 0; x < gfx_w; x++) {
			unsigned char b = v[x >> 1], p = (x & 1) ? (b & 15) : (b >> 4);
			if (p != gfx_back[(unsigned)y * gfx_w + x]) bad++;
		}
	}
	return bad;
}
static void scene(void)
{
	int i, w = gfx_w;
	static const int poly[] = { 0,0, 30,-10, 50,10, 35,35, 5,30 };
	int p2[10];
	gfx_clear(1);
	for (i = 0; i < 16; i++) gfx_fill_rect(i * w / 16, 0, w / 16, 12, (unsigned char)i);
	gfx_rect(2, 14, w - 4, 186 - 2, 15);
	gfx_line(0, 0, w - 1, 199, 14);
	gfx_line(w - 1, 0, 0, 199, 13);
	gfx_line(-50, 100, w + 50, 120, 10);
	gfx_fill_tri(w / 8, 40, w / 3, 60, w / 6, 110, 12);
	gfx_line(w / 8, 40, w / 3, 60, 15); gfx_line(w / 3, 60, w / 6, 110, 15); gfx_line(w / 6, 110, w / 8, 40, 15);
	for (i = 0; i < 5; i++) { p2[2*i] = poly[2*i] + w / 2; p2[2*i+1] = poly[2*i+1] + 130; }
	gfx_fill_poly(5, p2, 11);
	for (i = 0; i < 1024; i += 8)
		gfx_pset(w / 2 + FIX2INT(fix_cos(i) * 30), 60 + FIX2INT(fix_sin(i) * 30), 14);
	for (i = 0; i < 20; i++) gfx_vline(w - 30 + i, 150 - i * 3, 150 + i, (unsigned char)(i & 15));
	gfx_text(4, 170, "HELLO JR!", 15);
	gfx_text(4, 180, "0123456789", 10);
	gfx_fill_tri(-20, 190, 40, 150, 30, 260, 6);
}
int main(int argc, char **argv)
{
	int m = argc > 1 ? atoi(argv[1]) : 8;
	unsigned long t;
	int i;
	tmr_init();
	gfx_force_claim = argc > 2;
	if (gfx_init(m)) { dbg_log("init failed %d", gfx_err); return 1; }
	dbg_log("vseg %04X", gfx_vseg);
	scene();
	t = tmr_ms(); gfx_present(); t = tmr_ms() - t;
	gfx_screenshot(m == 8 ? "SHOT8.PPM" : "SHOT9.PPM");
	dbg_log("vram mismatches %ld", vcheck());
	t = tmr_ms(); for (i = 0; i < 10; i++) gfx_present(); t = tmr_ms() - t;
	dbg_log("10 presents %lu ms", t);
	dbg_log("mode %d present %lu ms pget(1,1)=%d sqrt(2)=%ld mul=%ld div=%ld", m, t, gfx_pget(5, 5),
		fix_sqrt(INT2FIX(2)), fix_mul(INT2FIX(3) / 2, INT2FIX(-5)), fix_div(INT2FIX(1), INT2FIX(3)));
	{ /* wait ~1 s so the video can be seen in a capture */
		unsigned long e = tmr_ms() + 500; while (tmr_ms() < e) ;
	}
	gfx_done();
	{ /* keyboard: simulate the PCjr path (INT 48h does OUT 60h,AL; INT 9) */
		union REGS r; int a, b;
		key_init();
		r.h.al = KEY_A; outp(0x60, KEY_A); int86(9, &r, &r); a = key_down[KEY_A];
		r.h.al = KEY_A | 0x80; outp(0x60, KEY_A | 0x80); int86(9, &r, &r); b = key_down[KEY_A];
		key_done();
		dbg_log("key A down=%d after release=%d", a, b);
	}
	tmr_done();
	return 0;
}
