/* T3: fast path (fx_*) correctness. Every fx primitive is checked pixel by
 * pixel against a reference: the old byte-per-pixel API (gfx_*) drawing the
 * same thing into gfx_back, or a plain C model. Writes FX*.PPM (the page
 * on screen, read from video RAM) and logs PASS/FAIL lines to LOG.TXT.
 *   T3 [flags]   flags = FX_* value (0 default, 1 buffer, 2 no irq) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "../jrgfx.h"
extern unsigned jr_rowoff[200];

static int fails;

/* Waits n vertical retraces with SS:SP in another segment and interrupts
 * on: what the retrace interrupt meets when it lands inside a DOS call on
 * real PC DOS (DOSBox's DOS never leaves the program's stack). */
extern void on_foreign_stack(unsigned seg, unsigned n);
#pragma aux on_foreign_stack = \
	"mov bx,ss" "mov cx,sp" "cli" "mov ss,ax" "mov sp,0FFF0h" "sti" \
	"push bx" "push cx" "mov bx,dx" "mov dx,3DAh" \
	"FS1:" "in al,dx" "test al,8" "jnz FS1" \
	"FS2:" "in al,dx" "test al,8" "jz FS2" \
	"dec bx" "jnz FS1" \
	"pop cx" "pop bx" "cli" "mov ss,bx" "mov sp,cx" "sti" \
	parm [ax] [dx] modify [ax bx cx dx];
static int px(unsigned seg, int x, int y)
{
	unsigned char b = *(unsigned char far *)MK_FP(seg, jr_rowoff[y] + (x >> 1));
	return x & 1 ? b & 15 : b >> 4;
}
/* compare fx_seg with gfx_back */
static long cmp_back(const char *what)
{
	long bad = 0; int x, y, fx = -1, fy = -1;
	for (y = 0; y < 200; y++)
		for (x = 0; x < 160; x++)
			if (px(fx_seg, x, y) != gfx_back[y * 160u + x]) { if (fx < 0) { fx = x; fy = y; } bad++; }
	dbg_log("%s %s: %ld mismatches (first %d,%d)", bad ? "FAIL" : "PASS", what, bad, fx, fy);
	if (bad) fails++;
	return bad;
}
static unsigned rs = 7;
static int rnd(int n) { rs ^= rs << 7; rs ^= rs >> 9; rs ^= rs << 8; return (int)(rs % (unsigned)n); }

static unsigned char rowpat[200];
static unsigned char tex[256];
static unsigned char spr[13 * 11];

int main(int argc, char **argv)
{
	int fl = argc > 1 ? atoi(argv[1]) : 0, i, f, x, y;
	unsigned char sh;
	tmr_init();
	if (fx_init(fl)) { dbg_log("fx_init failed %d", gfx_err); return 1; }
	sh = *(unsigned char far *)MK_FP(0x40, 0x8A);
	dbg_log("fx_init flags %d: pages %d irq %d fx_seg %04X 3DF=%02X vseg %04X", fl, fx_pages, fx_irq, fx_seg, sh, gfx_vseg);

	/* 0. B800 and gfx_vseg follow the hidden page (compatibility with games
	 * that copy their own packed buffer to B800 or gfx_vseg) */
	for (i = 0; i < 3; i++) {
		unsigned shown = (unsigned)(*(unsigned char far *)MK_FP(0x40, 0x8A) & 7) << 10;
		unsigned char far *b8 = (unsigned char far *)MK_FP(0xB800, 0x123);
		*(unsigned char far *)MK_FP(shown, 0x123) = 0x11;
		*(unsigned char far *)MK_FP(fx_seg, 0x123) = 0x22;
		*b8 = (unsigned char)(0x5A + i);
		if (fx_pages == 2 && !(fl & 1)) {
			int ok = *(unsigned char far *)MK_FP(fx_seg, 0x123) == 0x5A + i &&
			         *(unsigned char far *)MK_FP(shown, 0x123) == 0x11 && gfx_vseg == fx_seg;
			dbg_log("%s B800 -> hidden page (round %d): hidden %02X shown %02X vseg %04X", ok ? "PASS" : "FAIL", i,
			        *(unsigned char far *)MK_FP(fx_seg, 0x123), *(unsigned char far *)MK_FP(shown, 0x123), gfx_vseg);
			if (!ok) fails++;
		}
		fx_flip(); fx_begin();
	}

	/* 1. clear, rect, spans, polys, pset, text vs gfx_* */
	fx_clear(FX_SOLID2(1)); gfx_clear(1);
	for (i = 0; i < 16; i++) { fx_fill_rect(i * 10, 0, 10, 12, FX_SOLID2(i)); gfx_fill_rect(i * 10, 0, 10, 12, (unsigned char)i); }
	for (i = 0; i < 300; i++) {
		int x0 = rnd(200) - 20, x1 = rnd(200) - 20, yy = rnd(220) - 10, c = rnd(16);
		fx_hspan(yy, x0, x1, FX_SOLID(c)); gfx_hline(x0, x1, yy, (unsigned char)c);
	}
	cmp_back("spans+rects");
	for (i = 0; i < 200; i++) {
		int w = rnd(60) + 1, h = rnd(60) + 1, xx = rnd(190) - 15, yy = rnd(230) - 15, c = rnd(16);
		fx_fill_rect(xx, yy, w, h, FX_SOLID2(c)); gfx_fill_rect(xx, yy, w, h, (unsigned char)c);
	}
	cmp_back("fill_rect");
	for (i = 0; i < 300; i++) {
		int xy[6], c = rnd(16), k;
		for (k = 0; k < 3; k++) { xy[2 * k] = rnd(240) - 40; xy[2 * k + 1] = rnd(280) - 40; }
		fx_poly(3, xy, FX_SOLID2(c)); gfx_fill_poly(3, xy, (unsigned char)c);
	}
	cmp_back("triangles");
	for (i = 0; i < 100; i++) {
		int xy[10], k, cx = rnd(200) - 20, cy = rnd(240) - 20, r = rnd(60) + 1, c = rnd(16);
		for (k = 0; k < 5; k++) {
			xy[2 * k] = cx + FIX2INT(fix_cos(k * 205 + i * 3) * r);
			xy[2 * k + 1] = cy + FIX2INT(fix_sin(k * 205 + i * 3) * r);
		}
		fx_poly(5, xy, FX_SOLID2(c)); gfx_fill_poly(5, xy, (unsigned char)c);
	}
	cmp_back("pentagons");
	for (i = 0; i < 500; i++) {
		int xx = rnd(170) - 5, yy = rnd(210) - 5, c = rnd(16);
		fx_pset(xx, yy, c); gfx_pset(xx, yy, (unsigned char)c);
	}
	fx_text(4, 170, "HELLO FAST JR!", 15); gfx_text(4, 170, "HELLO FAST JR!", 15);
	fx_text(3, 182, "ODD 0123", 14); gfx_text(3, 182, "ODD 0123", 14);
	fx_text(-4, 192, "CLIP", 13); gfx_text(-4, 192, "CLIP", 13);
	for (i = 0; i < 200; i++) {
		int a = rnd(160), b = rnd(200), c = rnd(160), d = rnd(200), k = rnd(16);
		if (a == c || b == d) continue; /* gfx_line hands those to hline/vline */
		fx_line(a, b, c, d, k); gfx_line(a, b, c, d, (unsigned char)k);
	}
		cmp_back("pset+text");
	/* dither: checkerboard */
	fx_fill_rect(100, 120, 41, 31, FX_DITH(4, 14));
	for (y = 120; y < 151; y++) for (x = 100; x < 141; x++) gfx_back[y * 160u + x] = (unsigned char)(((x + y) & 1) ? 14 : 4);
	cmp_back("dither rect");

	/* 1b. fx_poly_rows (pattern per row) and fx_poly_src (copy from a
	 * buffer): coverage from gfx_fill_poly with a marker byte */
	{
		static unsigned char rp[200];
		unsigned src = fx_alloc_buffer();
		unsigned char far *ps = MK_FP(src, 0);
		for (i = 0; i < 16384; i++) ps[i] = (unsigned char)rnd(256);
		for (i = 0; i < 160; i++) {
			int xy[10], k, nv = 3 + (i & 1) * 2, cx = rnd(200) - 20, cy = rnd(240) - 20, r = rnd(70) + 1;
			for (k = 0; k < nv; k++) {
				xy[2 * k] = cx + FIX2INT(fix_cos(k * (1024 / nv) + i * 7) * r);
				xy[2 * k + 1] = cy + FIX2INT(fix_sin(k * (1024 / nv) + i * 7) * r);
			}
			for (k = 0; k < 200; k++) rp[k] = (unsigned char)rnd(256);
			gfx_fill_poly(nv, xy, 0x55);
			for (y = 0; y < 200; y++) for (x = 0; x < 160; x++) {
				unsigned char far *g = gfx_back + y * 160u + x;
				if (*g != 0x55) continue;
				if (i & 2) *g = (unsigned char)px(src, x, y);
				else *g = (unsigned char)(x & 1 ? rp[y] & 15 : rp[y] >> 4);
			}
			if (i & 2) fx_poly_src(nv, xy, src);
			else fx_poly_rows(nv, xy, rp);
		}
		cmp_back("fx_poly_rows + fx_poly_src");

		/* 1c. fx_track / fx_restore: whatever was drawn comes back out,
		 * across page flips (each page restores its own frame) */
		for (f = 0; f < 2; f++) { fx_copy_rect(fx_seg, src, 0, 0, 159, 199); fx_flip(); fx_begin(); }
		fx_track(1);
		for (f = 0; f < 5; f++) {
			fx_restore(src);
			for (i = 0; i < 6; i++) {
				int xy[8], k, cx = rnd(200) - 20, cy = rnd(240) - 20;
				for (k = 0; k < 4; k++) { xy[2 * k] = cx + rnd(60) - 30; xy[2 * k + 1] = cy + rnd(60) - 30; }
				fx_poly(3, xy, FX_SOLID2(rnd(16)));
				fx_poly_rows(3, xy + 2, rp);
				fx_poly_src(3, xy, src ^ 0x100);
			}
			fx_fill_rect(rnd(170) - 5, rnd(210) - 5, rnd(40), rnd(40), FX_DITH(1, 2));
			fx_text(rnd(150) & ~1, rnd(190), "TRK", 14);
			fx_line(rnd(160), rnd(200), rnd(160), rnd(200), 15);
			fx_pset(rnd(160), rnd(200), 3);
			fx_flip(); fx_begin();
		}
		for (f = 0; f < 2; f++) {
			long bad = 0;
			fx_restore(src);
			for (i = 0; i < 16384; i++) if ((i & 0x1FFF) < 8000 && ((unsigned char far *)MK_FP(fx_seg, 0))[i] != ps[i]) bad++;
			dbg_log("%s fx_restore page %d: %ld bytes differ from the background", bad ? "FAIL" : "PASS", f, bad);
			if (bad) fails++;
			fx_flip(); fx_begin();
		}
		fx_track(0);
		gfx_clear(0); fx_clear(0);
	}

	/* 2. columns */
	for (x = 0; x < 256; x++) tex[x] = (unsigned char)FX_PAIR(x & 15, (x >> 4) & 15);
	for (i = 0; i < 80; i++) {
		int y0 = rnd(240) - 20, y1 = rnd(240) - 20, c = rnd(16);
		unsigned v = (unsigned)rnd(30000), dv = (unsigned)rnd(1024);
		if (i & 1) {
			fx_col(i, y0, y1, FX_SOLID(c)); gfx_vline(2 * i, y0, y1, (unsigned char)c); gfx_vline(2 * i + 1, y0, y1, (unsigned char)c);
		} else {
			int a = y0 < y1 ? y0 : y1, b = y0 < y1 ? y1 : y0, yy;
			unsigned vv = v;
			fx_texcol(i, a, b, tex, v, dv);
			for (yy = a; yy <= b; yy++, vv += dv)
				if (yy >= 0 && yy < 200) { gfx_back[yy * 160u + 2 * i] = tex[vv >> 8] >> 4; gfx_back[yy * 160u + 2 * i + 1] = tex[vv >> 8] & 15; }
		}
	}
	cmp_back("fx_col/fx_texcol");
	fx_flip();
	fx_begin();
	sh = *(unsigned char far *)MK_FP(0x40, 0x8A);
	dbg_log("after flip: 3DF=%02X fx_seg %04X", sh, fx_seg);
	fx_screenshot("FXPRIM.PPM");

	/* 3. walls: 12 frames of random walls with sprites over them; the
	 * shown page must equal the C model of the last frame exactly. */
	for (y = 0; y < 200; y++) rowpat[y] = (unsigned char)(y < 100 ? FX_PAIR(1 + (y / 25 & 1) * 8, 1) : FX_SOLID(y < 150 ? 6 : 8));
	fx_view(10, 169, rowpat);
	for (i = 0; i < 13 * 11; i++) { int sx = i % 13 - 6, sy = i / 13 - 5; spr[i] = (unsigned char)(sx * sx + sy * sy < 30 ? (sx * sx + sy * sy < 9 ? 15 : 12 + (i & 1)) : 0); }
	{
		fx_sprite *sp = fx_compile(spr, 13, 11, 0);
		static int top[80], bot[80], sxs[4], sys[4];
		static unsigned v0[80], dv[80];
		unsigned shown;
		for (f = 0; f < 12; f++) {
			fx_begin();
			for (i = 0; i < 80; i++) {
				int h = rnd(4) == 0 ? -1 : rnd(250);
				top[i] = 90 - h / 2 + rnd(9) - 4; bot[i] = top[i] + h;
				if (h < 0) { top[i] = rnd(200); bot[i] = top[i] - 1; }
				v0[i] = (unsigned)rnd(64) << 8; dv[i] = (unsigned)(h > 0 ? 16384u / (unsigned)h : 0);
				if (f >= 4 && (f < 8 || i >= 40) && (i & 1)) { /* low detail pair: copy the left byte */
					top[i] = top[i - 1]; bot[i] = bot[i - 1]; v0[i] = v0[i - 1]; dv[i] = dv[i - 1];
					fx_wall2(i >> 1, top[i], bot[i], tex, v0[i], dv[i]);
				} else if (!(f >= 4 && (f < 8 || i >= 40)))
					fx_wall(i, top[i], bot[i], tex, v0[i], dv[i]);
			}
			for (i = 0; i < 4; i++) { sxs[i] = rnd(170) - 8; sys[i] = rnd(200) - 5; fx_sprite_draw(sp, sxs[i], sys[i]); }
			fx_flip();
		}
		fx_begin();
		shown = fx_pages == 2 ? (unsigned)(*(unsigned char far *)MK_FP(0x40, 0x8A) & 7) << 10 : fx_seg;
		/* model of the last frame into gfx_back */
		memset(gfx_back, 0, 64000u);
		for (y = 0; y < 200; y++) for (x = 0; x < 160; x++) {
			int c, b = x >> 1;
			if (y < 10 || y > 169) { c = px(shown, x, y); } /* outside the view: from frame setup, ignore */
			else {
				int t = top[b], bt = bot[b];
				unsigned char pb;
				if (t <= bt && y >= t && y <= bt) pb = tex[(unsigned char)((v0[b] + dv[b] * (unsigned)(y - t)) >> 8)];
				else if (t <= bt ? y < t : y < t) pb = rowpat[y];
				else pb = rowpat[y];
				c = x & 1 ? pb & 15 : pb >> 4;
			}
			gfx_back[y * 160u + x] = (unsigned char)c;
		}
		for (i = 0; i < 4; i++) for (y = 0; y < 11; y++) for (x = 0; x < 13; x++) {
			int X = sxs[i] + x, Y = sys[i] + y, c = spr[y * 13 + x];
			if (c && X >= 0 && X < 160 && Y >= 0 && Y < 200) gfx_back[Y * 160u + X] = (unsigned char)c;
		}
		/* compare the SHOWN page (the one fx_flip just made visible) */
		{
			unsigned save = fx_seg, shown = (unsigned)(*(unsigned char far *)MK_FP(0x40, 0x8A) & 7) << 10;
			if (fx_pages == 2) fx_seg = shown;
			cmp_back("walls+sprites (shown page)");
			fx_seg = save;
		}
		fx_screenshot("FXWALL.PPM");

		/* 4. sprites alone, all parities, vs gfx_blit */
		fx_view(0, 199, 0);
		fx_clear(FX_SOLID2(2)); gfx_clear(2);
		for (i = 0; i < 40; i++) {
			int xx = rnd(180) - 10, yy = rnd(210) - 5;
			fx_sprite_draw(sp, xx, yy); gfx_blit(xx, yy, 13, 11, spr, 0);
		}
		cmp_back("compiled sprites");
		fx_sprite_free(sp);
	}

	/* 5. copy_rect and delta copy between buffers */
	{
		unsigned a = fx_alloc_buffer(), b = fx_alloc_buffer(), n;
		long bad = 0;
		if (!a || !b) { dbg_log("FAIL alloc"); fails++; }
		else {
			unsigned char far *pa = MK_FP(a, 0), far *pb = MK_FP(b, 0);
			for (i = 0; i < 16384; i++) pa[i] = (unsigned char)rnd(256);
			_fmemcpy(pb, pa, 16384);
			for (i = 0; i < 300; i++) pb[rnd(16384)] ^= (unsigned char)(1 + rnd(255));
			for (i = 0; i < 60; i++) pb[8000 + i] = 0x55;   /* a run, and the bank edges */
			pb[0] ^= 1; pb[7999] ^= 1; pb[8192] ^= 1; pb[8192 + 7999] ^= 1;
			n = fx_copy_delta(a, b);
			for (i = 0; i < 8000; i++) if (pa[i] != pb[i] || pa[8192 + i] != pb[8192 + i]) bad++;
			dbg_log("%s fx_copy_delta: %ld mismatches, %u words written", bad ? "FAIL" : "PASS", bad, n);
			if (bad) fails++;
			n = fx_copy_delta(a, b);
			dbg_log("%s fx_copy_delta identical: %u words", n ? "FAIL" : "PASS", n);
			if (n) fails++;
			for (i = 0; i < 16384; i++) pb[i] = (unsigned char)rnd(256);
			fx_copy_rect(a, b, 13, 7, 100, 150);
			bad = 0;
			for (y = 0; y < 200; y++) for (x = 0; x < 160; x++) {
				int in = x >= 12 && x <= 101 && y >= 7 && y <= 150;
				if (in != (px(a, x, y) == px(b, x, y)) && in) bad++;
			}
			dbg_log("%s fx_copy_rect: %ld", bad ? "FAIL" : "PASS", bad);
			if (bad) fails++;
		}
	}
	if (fx_irq) { /* the retrace interrupt must not touch the interrupted stack's segment */
		unsigned seg = 0;
		long bad = 0;
		unsigned k, v0 = fx_vbl;
		if (_dos_allocmem(0x1000, &seg)) { dbg_log("FAIL foreign stack: no 64 KB"); fails++; }
		else {
			unsigned char far *q = (unsigned char far *)MK_FP(seg, 0);
			_fmemset(q, 0xA5, 0x8000u); _fmemset(q + 0x8000u, 0xA5, 0x8000u);
			fx_pal_anim(0, 2, 5, 1, 1);          /* palette rotation every retrace */
			fx_pal_flash(7, 15, 3);
			on_foreign_stack(seg, 8);
			fx_pal_anim(0, 0, 0, 0, 0);
			for (k = 0; k < 0xFE00u; k++) if (q[k] != 0xA5) bad++;   /* below the stack's own top */
			for (k = 0xFE00u; k < 0xFFB0u; k++) if (q[k] != 0xA5) bad++;  /* and it used under 64 bytes */
			dbg_log("%s IRQ5 on a foreign stack: %ld bytes of its segment changed, %u retraces", bad ? "FAIL" : "PASS", bad, fx_vbl - v0);
			if (bad) fails++;
			_dos_freemem(seg);
		}
	}
	if (fx_irq) { /* fx_split finds the display timing (DOSBox), and turns off */
		static const unsigned char top[16] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 };
		unsigned v0;
		int st;
		fx_split(100, top);
		v0 = fx_vbl;
		while (fx_vbl - v0 < 10) ;
		st = fx_split_state;
		dbg_log("%s fx_split: state %d, %u polls to spare", st == 1 && fx_split_guard ? "PASS" : "FAIL", st, fx_split_guard);
		if (st != 1 || !fx_split_guard) fails++;
		fx_split(0, 0);
		fx_pal_flash(3, 15, 2);
		v0 = fx_vbl;
		while (fx_vbl - v0 < 4) ;
	}
	{ /* tmr_ticks agrees with tmr_ms */
		unsigned long m0 = tmr_ms(), k0 = tmr_ticks(), m1, k1;
		while (tmr_ms() - m0 < 300) ;
		m1 = tmr_ms() - m0; k1 = tmr_ticks() - k0;
		i = (int)(TMR_TICKS_MS(k1) - m1);
		dbg_log("%s tmr_ticks: %lu ticks = %lu ms (TMR_TICKS_MS), tmr_ms %lu", i < -2 || i > 2 ? "FAIL" : "PASS", k1, TMR_TICKS_MS(k1), m1);
		if (i < -2 || i > 2) fails++;
	}
	fx_screenshot("FXEND.PPM");
	gfx_done();
	dbg_log("T3 done: %d failures", fails);
	return 0;
}
