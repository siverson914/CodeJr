/* SHREDDY: a dirt jump rider doing random tricks, a one-minute loop that
 * repeats with no seam, the name of each trick at the top. 320x200, 4
 * colors (black, dark grey, light grey, white).
 *
 * The ride is simulated on the laptop by Knuckle (tools/pcjr/export.mjs in
 * the Knuckle repo) and saved as the lines of every frame (SHREDDY.DAT).
 * This program only draws them: polylines, ellipses (wheels, head) and dots
 * (eyes), straight into the hidden video page, then flips pages at the
 * vertical retrace. Playback follows the clock: if frames take too long to
 * draw, it shows every 2nd (or 3rd...) frame, so the ride keeps its real
 * speed and the motion stays even.
 *
 * Video: JRGFX's fast path sets up two 16 KB pages and the retrace flip in
 * 160x200x16 (mode 8); then the gate array is switched to 320x200x4. Both
 * modes have the same memory layout (80-byte rows, odd rows 2000h later),
 * only the pixels are packed differently: 4 to a byte, 2 bits each, high
 * bits = left pixel.
 *
 * SHREDDY [file]        play until a key
 *   /DEMO:n             quit after n frames drawn (tests; nobody at the keys)
 *   /SHOT:a,b,...       draw every frame in order (no skipping), and save the
 *                       page after frame a, b... as SHOT1.PPM, SHOT2.PPM...
 *   /BENCH              draw every frame once, as fast as possible, and log
 *                       the times to LOG.TXT
 *
 * File format (SHR1, little endian), written by export.mjs:
 *   16-byte header: "SHR1", u16 frames, u8 fps, u8 names, u8 palette[4], 4 x 0
 *   the trick names: per name a length byte, then the letters
 *   per frame: u16 n, then n bytes: a label byte (0: no name up, else 1 +
 *   the name's index), with a label its color (1-3, the fade) and its top
 *   row (signed: above the screen while it slides in); then the shapes.
 *   Each shape starts with a byte:
 *     bits 7-6 color (1-3), bit 5 thick (one more pixel across),
 *     bits 4-3 type, bit 0 the 9th bit of x for types 0, 2, 3
 *     type 0, polyline:  count, x, y, then count-1 steps
 *     type 1, polyline starting one step from the pen: count, step, steps
 *     type 2, ellipse:   x, y, ux, uy, vx, vy (signed, half pixels), sides;
 *                        point k at angle (k * (65536 / sides)) >> 8 (256 a
 *                        turn): x + (ux cos a + vx sin a + 128) >> 8, Q7 sine
 *     type 3, dot:       x, y
 *   step: one byte, dx in the high nibble and dy in the low (signed), or
 *   80h then signed bytes dx, dy. The pen is where the last shape ended.
 *   Every point is on the screen (the exporter clips), and a thick shape
 *   never touches x 319 or y 199.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include <io.h>
#include <conio.h>
#include "jrgfx.h"

#define MAXF 3000
#define MAXN 255

extern const unsigned char jr_font[96 * 8]; /* JRGFX: the ROM's 8x8 font, ASCII 32-127 */

static const signed char SIN[256] = {
	0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46,
	49, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88,
	90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
	117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
	127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
	117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100, 98, 96, 94, 92,
	90, 88, 85, 83, 81, 78, 76, 73, 71, 68, 65, 63, 60, 57, 54, 51,
	49, 46, 43, 40, 37, 34, 31, 28, 25, 22, 19, 16, 12, 9, 6, 3,
	0, -3, -6, -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
	-49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
	-90, -92, -94, -96, -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
	-117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
	-127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
	-117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100, -98, -96, -94, -92,
	-90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
	-49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12, -9, -6, -3
};

/* PCjr RGBI colors as RGB, for screenshots */
static const unsigned char RGB16[16][3] = {
	{0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
	{85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
};

static unsigned nframes, fps;
static unsigned char pal4[4];
static unsigned fseg[MAXF], foff[MAXF], flen[MAXF]; /* each frame's bytes */
static unsigned data_seg;
static const unsigned char far *names[MAXN]; /* each: length, then the letters */
static unsigned wide[256];                    /* a font byte as 8 pixel masks (2 bytes) */

/* drawing state for the current frame */
static unsigned char far *vid;       /* hidden page (offset 0) */
static unsigned char cpat;           /* the color in all 4 pixel slots */
/* per page: the first byte drawn on each row (200), then the last (200);
 * first > last means nothing on that row. The next erase of the page
 * clears just these spans. */
static unsigned char ext[2][400];
static int pg;                       /* which extents slot is the hidden page */
static int npg;                      /* 2 with page flipping, 1 with one buffer */

static void plot(int x, int y)
{
	unsigned b = (unsigned)x >> 2;
	unsigned char m = (unsigned char)(0xC0 >> ((x & 3) << 1));
	unsigned char far *p = vid + jr_rowoff[y] + b;
	*p = (unsigned char)((*p & ~m) | (cpat & m));
	if (b < ext[pg][y]) ext[pg][y] = (unsigned char)b;
	if (b > ext[pg][200 + y]) ext[pg][200 + y] = (unsigned char)b;
}

/* lines.asm draws polylines: the steps straight from the frame data */
struct kp {
	const unsigned char far *steps;
	unsigned n, x, y, vseg;
	unsigned char far *ext;
	unsigned char cpat, thick;
};
extern void kn_poly(struct kp far *p);
static struct kp K;

static int snib(unsigned v) { return (int)(v & 7) - (int)(v & 8); }

/* A trick name, centered, the ROM font with every row doubled (8x16), only
 * the glyphs' set pixels drawn; rows off the screen are skipped. The name
 * is not in the page's extents: show() keeps track of it, since it mostly
 * stays put for a whole trick and needs drawing only when it changes. */
static void text(const unsigned char far *nm, int y0, unsigned c)
{
	unsigned len = nm[0], k, r, b0 = 40 - len;   /* 8 pixels = 2 bytes a letter */
	unsigned char cp = (unsigned char)(c * 0x55);
	for (r = 0; r < 16; r++) {
		int y = y0 + (int)r;
		unsigned char far *row;
		if (y < 0 || y > 199) continue;
		row = vid + jr_rowoff[y] + b0;
		for (k = 0; k < len; k++) {
			unsigned ch = nm[1 + k], m;
			if (ch < 32 || ch > 127) continue;
			m = wide[jr_font[(ch - 32) * 8 + (r >> 1)]];
			row[2 * k] = (unsigned char)((row[2 * k] & ~(m >> 8)) | (cp & (m >> 8)));
			row[2 * k + 1] = (unsigned char)((row[2 * k + 1] & ~m) | (cp & m));
		}
	}
}

static void draw_frame(unsigned f)
{
	const unsigned char far *p = (const unsigned char far *)MK_FP(fseg[f], foff[f]);
	const unsigned char far *end = p + flen[f];
	static unsigned char est[32 * 3]; /* an ellipse's points as steps */
	int px = 0, py = 0;
	K.vseg = FP_SEG(vid);
	K.ext = (unsigned char far *)ext[pg];
	p += *p ? 3 : 1;                           /* the label: show() reads it */
	while (p < end) {
		unsigned h = *p++, type = (h >> 3) & 3;
		int x, y, k, n;
		cpat = (unsigned char)((h >> 6) * 0x55);
		K.cpat = cpat;
		K.thick = (unsigned char)((h >> 5) & 1);
		if (type == 3) {
			x = (int)(((h & 1) << 8) | p[0]); y = p[1]; p += 2;
			plot(x, y); px = x; py = y;
		} else if (type == 2) {
			int ux, uy, vx, vy, lx = 0, ly = 0;
			unsigned step, acc;
			unsigned char *q = est;
			x = (int)(((h & 1) << 8) | p[0]); y = p[1];
			ux = (signed char)p[2]; uy = (signed char)p[3];
			vx = (signed char)p[4]; vy = (signed char)p[5];
			n = p[6]; p += 7;
			if (n > 31) n = 31;
			step = (unsigned)(65536UL / (unsigned)n); /* 8.8 angle step: a = k * 256 / n */
			for (k = 0, acc = 0; k <= n; k++, acc += step) {
				unsigned a = (acc >> 8) & 255;
				int co = SIN[(a + 64) & 255], si = SIN[a];
				int ex = x + ((ux * co + vx * si + 128) >> 8);
				int ey = y + ((uy * co + vy * si + 128) >> 8);
				if (k == 0) { K.x = ex; K.y = ey; }
				else {
					int dx = ex - lx, dy = ey - ly;
					if (dx >= -8 && dx <= 7 && dy >= -8 && dy <= 7 && !(dx == -8 && dy == 0))
						*q++ = (unsigned char)(((dx & 15) << 4) | (dy & 15));
					else { *q++ = 0x80; *q++ = (unsigned char)dx; *q++ = (unsigned char)dy; }
				}
				lx = ex; ly = ey;
			}
			K.steps = (const unsigned char far *)est;
			K.n = n;
			kn_poly(&K);
			px = x; py = y;
		} else {
			n = *p++;
			if (type == 1) {
				int dx, dy;
				if (*p == 0x80) { dx = (signed char)p[1]; dy = (signed char)p[2]; p += 3; }
				else { dx = snib(*p >> 4); dy = snib(*p); p++; }
				K.x = px + dx; K.y = py + dy;
			} else {
				K.x = ((h & 1) << 8) | p[0]; K.y = p[1]; p += 2;
			}
			K.steps = p;
			K.n = n - 1;
			kn_poly(&K);
			p = K.steps;
			px = K.x; py = K.y;
		}
	}
}

/* lines.asm clears what this page drew last time (its extents) */
struct ke { unsigned vseg; unsigned char far *ext; unsigned y0, y1; };
extern void kn_erase(struct ke far *p);

/* The rows and bytes this page has drawn (for the one-buffer fallback,
 * which copies only what it is told about). */
static void ext_box(int *y0, int *y1, unsigned *b0, unsigned *b1)
{
	int y;
	unsigned char *lo = ext[pg], *hi = ext[pg] + 200;
	for (y = 0; y < 200; y++) {
		if (lo[y] > hi[y]) continue;
		if (y < *y0) *y0 = y;
		if (y > *y1) *y1 = y;
		if (lo[y] < *b0) *b0 = lo[y];
		if (hi[y] > *b1) *b1 = hi[y];
	}
}

static void erase_page(int *y0, int *y1, unsigned *b0, unsigned *b1)
{
	static struct ke E;
	*y0 = 200; *y1 = -1; *b0 = 80; *b1 = 0;
	if (npg == 1) ext_box(y0, y1, b0, b1);
	E.vseg = FP_SEG(vid);
	E.ext = (unsigned char far *)ext[pg];
	E.y0 = 0; E.y1 = 199;
	kn_erase(&E);
}

/* time spent, in timer ticks (the /BENCH line) */
static unsigned long tk_wait, tk_erase, tk_draw;

/* One frame: erase this page's last frame, draw frame f, flip. */
/* the name on each page: label (0 none), color, top row */
static struct { unsigned lab, c; int y; } shown_name[2];

/* any lines on the rows y..y+15 of this page? */
static int lines_on(int y)
{
	int r;
	for (r = y < 0 ? 0 : y; r <= y + 15 && r < 200; r++)
		if (ext[pg][r] <= ext[pg][200 + r]) return 1;
	return 0;
}

static void clear_name(unsigned lab, int y)
{
	unsigned len = names[lab - 1][0], r;
	for (r = 0; r < 16; r++)
		if (y + (int)r >= 0 && y + (int)r < 200)
			_fmemset(vid + jr_rowoff[y + r] + 40 - len, 0, 2 * len);
	if (npg == 1) fx_dirty((40 - len) * 2, y < 0 ? 0 : y, (40 + len) * 2 - 1, y + 15 > 199 ? 199 : y + 15);
}

static void show(unsigned f, int shot)
{
	int y0, y1, ny = 0, redraw;
	unsigned b0, b1, nl, nc = 0;
	const unsigned char far *lp = (const unsigned char far *)MK_FP(fseg[f], foff[f]);
	unsigned long t0 = tmr_ticks(), t1, t2;
	nl = lp[0];
	if (nl > MAXN || (nl && !names[nl - 1])) nl = 0;
	if (nl) { nc = lp[1]; ny = (signed char)lp[2]; }
	fx_begin();
	t1 = tmr_ticks();
	vid = (unsigned char far *)MK_FP(fx_seg, 0);
	/* the name: unchanged and nothing crossing it, it stays as it is */
	redraw = shown_name[pg].lab != nl || shown_name[pg].c != nc || shown_name[pg].y != ny;
	if (shown_name[pg].lab) {
		if (redraw) clear_name(shown_name[pg].lab, shown_name[pg].y);
		else if (lines_on(shown_name[pg].y)) redraw = 1; /* the erase will cut through it */
	}
	erase_page(&y0, &y1, &b0, &b1);
	t2 = tmr_ticks();
	draw_frame(f);
	if (nl && (redraw || lines_on(ny))) {
		text(names[nl - 1], ny, nc);
		if (npg == 1) fx_dirty((40 - names[nl - 1][0]) * 2, ny < 0 ? 0 : ny, (40 + names[nl - 1][0]) * 2 - 1, ny + 15 > 199 ? 199 : ny + 15);
	}
	shown_name[pg].lab = nl; shown_name[pg].c = nc; shown_name[pg].y = ny;
	tk_wait += t1 - t0; tk_erase += t2 - t1; tk_draw += tmr_ticks() - t2;
	if (npg == 1) ext_box(&y0, &y1, &b0, &b1);
	if (npg == 1 && y1 >= y0) fx_dirty(b0 * 2, y0, b1 * 2 + 1, y1); /* mode-8 x: 2 per byte */
	if (shot) {
		/* the page just drawn, before it is shown: SHOTn.PPM */
		static int nshot = 0;
		char name[16];
		FILE *fp;
		sprintf(name, "SHOT%d.PPM", ++nshot);
		fp = fopen(name, "wb");
		if (fp) {
			int x, y;
			fprintf(fp, "P6 320 200 255\n");
			for (y = 0; y < 200; y++)
				for (x = 0; x < 320; x++) {
					unsigned v = (vid[jr_rowoff[y] + (x >> 2)] >> (6 - ((x & 3) << 1))) & 3;
					fwrite(RGB16[pal4[v] & 15], 1, 3, fp);
				}
			fclose(fp);
			dbg_log("shot %s: frame %u", name, f);
		}
	}
	fx_flip();
	if (npg == 2) pg ^= 1;
}

static int load(const char *path)
{
	int fd;
	unsigned paras, seg, got, i;
	unsigned long size, done = 0;
	unsigned char far *b;
	unsigned long at;
	if (_dos_open(path, 0, &fd)) { printf("SHREDDY: can't open %s\n", path); return -1; }
	size = (unsigned long)lseek(fd, 0L, SEEK_END);
	lseek(fd, 0L, SEEK_SET);
	if (size < 16 || size > 600000UL) { printf("SHREDDY: %s is %lu bytes\n", path, size); _dos_close(fd); return -1; }
	paras = (unsigned)((size + 15) >> 4);
	if (_dos_allocmem(paras, &seg)) { printf("SHREDDY: not enough memory (%lu KB needed)\n", (size + 1023) >> 10); _dos_close(fd); return -1; }
	data_seg = seg;
	printf("Loading %s, %lu KB ", path, (size + 1023) >> 10);
	while (done < size) {
		unsigned want = size - done > 32768UL ? 32768u : (unsigned)(size - done);
		b = (unsigned char far *)MK_FP(seg + (unsigned)(done >> 4), 0);
		if (_dos_read(fd, b, want, &got) || got != want) { printf("\nSHREDDY: read error\n"); _dos_close(fd); return -1; }
		done += want;
		putchar('.');
	}
	_dos_close(fd);
	putchar('\n');
	b = (unsigned char far *)MK_FP(seg, 0);
	if (b[0] != 'S' || b[1] != 'H' || b[2] != 'R' || b[3] != '1') { printf("SHREDDY: %s is not a Shreddy ride (SHR1)\n", path); return -1; }
	nframes = b[4] | (b[5] << 8);
	fps = b[6];
	for (i = 0; i < 4; i++) pal4[i] = b[8 + i];
	if (!nframes || nframes > MAXF || !fps || fps > 60) { printf("SHREDDY: bad header (%u frames, %u fps)\n", nframes, fps); return -1; }
	at = 16;
	for (i = 0; i < b[7]; i++) { /* the trick names */
		unsigned char far *q = (unsigned char far *)MK_FP(seg + (unsigned)(at >> 4), (unsigned)(at & 15));
		if (at + 1 + q[0] > size) { printf("SHREDDY: file ends in the names\n"); return -1; }
		names[i] = q;
		at += 1 + q[0];
	}
	for (i = 0; i < nframes; i++) {
		unsigned char far *q;
		unsigned len;
		if (at + 2 > size) { printf("SHREDDY: file ends at frame %u\n", i); return -1; }
		q = (unsigned char far *)MK_FP(seg + (unsigned)(at >> 4), (unsigned)(at & 15));
		len = q[0] | (q[1] << 8);
		at += 2;
		if (at + len > size) { printf("SHREDDY: file ends in frame %u\n", i); return -1; }
		fseg[i] = seg + (unsigned)(at >> 4);
		foff[i] = (unsigned)(at & 15);
		flen[i] = len;
		at += len;
	}
	return 0;
}

/* 320x200, 4 colors, on top of fx_init's pages: the gate array's mode
 * control 1 = graphics + video on (0Ah; mode 8 is 1Ah, the 16-color bit
 * added), palette mask = 3, mode control 2 = 0. */
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
	*(unsigned char far *)MK_FP(0x40, 0x49) = 4; /* BIOS mode byte: screenshots decode 4 colors */
	for (i = 0; i < 16; i++) pal16[i] = pal4[i & 3];
	fx_pal_set(pal16);
}

static unsigned shots[16], nshots;

int main(int argc, char **argv)
{
	const char *path = "SHREDDY.DAT";
	unsigned long demo = 0, drawn = 0, t0, t;
	int bench = 0, i;
	unsigned f;
	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (a[0] == '/' || a[0] == '-') {
			if (!strnicmp(a + 1, "DEMO", 4)) demo = a[5] == ':' ? strtoul(a + 6, 0, 10) : 300;
			else if (!strnicmp(a + 1, "BENCH", 5)) bench = 1;
			else if (!strnicmp(a + 1, "SHOT:", 5)) {
				char *s = a + 6;
				while (*s && nshots < 16) { shots[nshots++] = (unsigned)strtoul(s, &s, 10); if (*s == ',') s++; else break; }
			} else { printf("SHREDDY [file] [/DEMO:n] [/SHOT:a,b,...] [/BENCH]\n"); return 1; }
		} else path = a;
	}
	if (load(path)) return 1;
	for (i = 0; i < 256; i++) { /* font byte -> pixel masks, left 4 pixels in the high byte */
		unsigned m = 0, b;
		for (b = 0; b < 8; b++) if (i & (0x80 >> b)) m |= 0xC000u >> (2 * b);
		wide[i] = m;
	}
	if (fx_init(0)) { printf("SHREDDY: no graphics (error %d)\n", gfx_err); return 1; }
	npg = fx_pages == 2 ? 2 : 1;
	for (i = 0; i < 2; i++) { memset(ext[i], 255, 200); memset(ext[i] + 200, 0, 200); }
	mode_320x4();
	for (i = 0; i < 2; i++) { fx_begin(); fx_clear(0); fx_flip(); }
	key_init();
	tmr_init();
	dbg_log("SHREDDY: %u frames at %u fps, %d pages, irq %d", nframes, fps, fx_pages, fx_irq);

	if (bench || nshots) {
		/* every frame in order, no waiting */
		unsigned long worst = 0, prev;
		unsigned s = 0;
		t0 = prev = tmr_ms();
		for (f = 0; f < nframes && !key_down[KEY_ESC]; f++) {
			int take = 0;
			while (s < nshots && shots[s] == f) { take = 1; s++; }
			if (nshots && s >= nshots && !take && !bench) break;
			show(f, take);
			t = tmr_ms();
			if (t - prev > worst) worst = t - prev;
			prev = t;
			drawn++;
			if (demo && drawn >= demo) break;
		}
		t = tmr_ms() - t0;
		if (t == 0) t = 1;
		dbg_log("drew %lu frames in %lu ms: %lu.%lu fps, slowest frame %lu ms", drawn, t,
			drawn * 1000UL / t, (drawn * 10000UL / t) % 10, worst);
		if (drawn) dbg_log("per frame: waiting for the flip %lu ms, erasing %lu ms, drawing %lu ms",
			TMR_TICKS_MS(tk_wait) / drawn, TMR_TICKS_MS(tk_erase) / drawn, TMR_TICKS_MS(tk_draw) / drawn);
	} else {
		/* Play by the clock, looping. A frame that can't be drawn in time
		 * is skipped, but evenly: every frame, or every 2nd, 3rd...
		 * (stride), whichever this machine keeps up with, so the motion
		 * stays steady. The stride follows the average draw time: up after
		 * 8 frames in a row that don't fit, down after 30 that would fit
		 * the smaller one. A single late frame just shows the frame that is
		 * due (a skip), which beats dropping to a slower stride for good. */
		unsigned long tick, g, lastg = 0xFFFFFFFFUL, tb;
		unsigned stride = 1, want, over = 0, under = 0, avg = 0, period = 1000 / fps;
		t0 = tmr_ms();
		for (;;) {
			int k;
			for (k = 1; k < 128; k++) if (key_down[k]) break;
			if (k < 128) break;
			t = tmr_ms() - t0;
			tick = t * fps / 1000UL;
			g = tick - tick % stride;
			if (g == lastg) continue;
			lastg = g;
			f = (unsigned)(g % nframes);
			tb = tmr_ms();
			show(f, 0);
			tb = tmr_ms() - tb;
			if (tb > 1000) tb = 1000;
			avg = drawn ? (unsigned)((avg * 7UL + tb * 16UL) / 8) : (unsigned)(tb * 16); /* ms x 16 */
			want = (unsigned)(((unsigned long)avg / 16 + period - 1) / period);
			if (want < 1) want = 1;
			if (want > 6) want = 6;
			if (want > stride) { under = 0; if (++over >= 8) { stride = want; over = 0; dbg_log("stride %u (%u ms a frame)", stride, avg / 16); } }
			else if (want < stride) { over = 0; if (++under >= 30) { stride = want; under = 0; dbg_log("stride %u (%u ms a frame)", stride, avg / 16); } }
			else over = under = 0;
			drawn++;
			if (demo && drawn >= demo) break;
		}
		t = tmr_ms() - t0;
		if (t == 0) t = 1;
		dbg_log("played %lu frames in %lu ms: %lu.%lu fps drawn", drawn, t,
			drawn * 1000UL / t, (drawn * 10000UL / t) % 10);
	}
	gfx_done();
	_dos_freemem(data_seg);
	return 0;
}
