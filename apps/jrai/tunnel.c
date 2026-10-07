/* Part 3: the tunnel, "into the neural net".
 *
 * Every screen cell (one byte, 2 pixels wide, 2 rows tall) always shows the
 * same texel of a 256x256 texture, offset by where we are: the angle
 * around the tunnel in the low byte and the depth (1/distance) in the high
 * byte. So the whole effect is "texel = texture[cell's fixed offset + BX]".
 * prep() writes that as 8088 code, two cells per 14 bytes:
 *
 *     mov al,[bx+d1]        8A 87 d1        texel of the left cell
 *     mov ah,[bx+d2]        8A A7 d2        texel of the right cell
 *     stosw                 AB              even row (bank 0)
 *     mov es:[di+1FFEh],ax  26 89 85 FE 1F  odd row (bank 1)
 *
 * No loop, no index arithmetic, no address calculation: about 3040 of
 * these per frame, ~43 KB of code. The texture's 64K segment wraps the
 * offsets for free. Far cells read a darker copy of the texture (the code
 * switches DS between the two), and the middle is left black.
 *
 * The texture's circuit traces use palette entries that the retrace
 * interrupt rotates, so pulses of light run along them for free.
 */
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "jrai.h"

#define CR0 12            /* first cell row (pixel row 24)  */
#define CR1 88            /* past the last (pixel row 176) */

static unsigned tex[2], code;      /* segments: bright and dim textures, code */
static unsigned codelen;
static unsigned char far *cp;

static const unsigned char atan_t[65] = {
	0,1,1,2,3,3,4,4,5,6,6,7,8,8,9,9,10,11,11,12,12,13,13,14,15,15,16,16,17,17,18,18,
	19,19,20,20,21,21,22,22,23,23,24,24,25,25,25,26,26,27,27,27,28,28,29,29,29,30,30,30,31,31,31,32,32 };

static unsigned char iatan2(int dy, int dx)
{
	unsigned ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy; /* both < 200 */
	unsigned a;
	if (!ax && !ay) return 0;
	if (ax >= ay) a = atan_t[ay * 64 / ax];
	else a = 64 - atan_t[ax * 64 / ay];
	if (dx < 0) a = 128 - a;
	if (dy < 0) a = 256 - a;
	return (unsigned char)a;
}

static unsigned isqrt(unsigned v)
{
	unsigned r = 0, b = 1u << 14;
	while (b > v) b >>= 2;
	while (b) {
		if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
		else r >>= 1;
		b >>= 2;
	}
	return (unsigned)r;
}

/* code is assembled a row at a time in ebuf, then copied to cp */
static unsigned char ebuf[1024];
static unsigned en;
static void emit(unsigned char b) { ebuf[en++] = b; }
static void emitw(unsigned w) { ebuf[en++] = (unsigned char)w; ebuf[en++] = (unsigned char)(w >> 8); }
static void flush(void) { _fmemcpy(cp, ebuf, en); cp += en; en = 0; }

/* The geometry of one quadrant (px > 0, py > 0); the others are mirrors:
 * the angle turns into 128 - a, 256 - a or 128 + a, the depth is the same. */
#define QW 40
#define QH ((CR1 - CR0) / 2)
static unsigned char far *qdepth, far *qang, far *qshade;

static void quadrant(void)
{
	int qx, qy;
	for (qy = 0; qy < QH; qy++)
		for (qx = 0; qx < QW; qx++) {
			int px = qx * 2 + 1, py = qy * 2 + 1;
			int dx = px * 5 / 3; /* a pixel is 5/3 as wide as it is tall */
			unsigned r = isqrt((unsigned)(dx * dx + py * py)); /* at most 133^2 + 100^2 */
			int i = qy * QW + qx;
			qdepth[i] = (unsigned char)(r ? 3000u / r : 255);
			qang[i] = iatan2(py, dx);
			qshade[i] = (unsigned char)(r < 9 ? 2 : r < 34 ? 1 : 0);
		}
}

/* the cell's texture offset and shade: 0 bright, 1 dim, 2 black */
static void cell(int bx, int cr, unsigned *off, int *shade)
{
	int qx = bx >= 40 ? bx - 40 : 39 - bx, qy = cr >= 50 ? cr - 50 : 49 - cr;
	int i = qy * QW + qx;
	unsigned a = qang[i];
	if (bx < 40) a = 128 - a;
	if (cr < 50) a = 256 - a;
	*off = ((unsigned)qdepth[i] << 8) | (a & 255);
	*shade = qshade[i];
}

/* Logical colors: 0 wall, 1 faint grid, 2-9 bright traces (rotating),
 * 10 nodes, 11-14 dim traces (rotating), 15 text.
 * Texel (a = angle, d = depth): every 32 in each direction a trace (a pulse
 * color that changes along it), between them a faint grid every 8, nodes
 * where traces cross, and a scatter of synapses. Built a row (one depth)
 * at a time: start from a template row, then draw the traces into it. */
static void tex_row(unsigned char far *p, int d)
{
	static unsigned char grid[256], plain[256];
	unsigned char row[256];
	int a, ld = d & 31;
	if (!plain[4]) for (a = 0; a < 256; a++) {
		plain[a] = (unsigned char)((a & 7) == 4);
		grid[a] = 1;
	}
	_fmemcpy(row, (d & 7) == 4 ? grid : plain, 256);
	if (ld == 0 || ld == 16) { /* a ring: pulses run around it */
		for (a = 0; a < 256; a++)
			if (ld == 0 || (a & 32)) row[a] = (unsigned char)(2 + ((a >> 2) & 7));
	}
	for (a = 0; a < 256; a += 32) {
		row[a] = (unsigned char)(2 + ((d >> 1) & 7));                 /* traces along */
		if (d & 64) row[a + 16] = row[a];
		if (ld < 2 || ld == 31) row[a] = row[(a + 1) & 255] = row[(a + 255) & 255] = 10; /* node */
	}
	row[(d * 13 * 55 + 5) & 127] = 10;                                  /* synapses */
	row[((d * 13 * 55 + 5) & 127) + 128] = 10;
	for (a = 0; a < 256; a++) row[a] = (unsigned char)(row[a] * 0x11);
	_fmemcpy(p, row, 256);
}

/* the dim copy: grid gone, traces in the dim colors */
static void tex_dim(unsigned char far *dst, const unsigned char far *src)
{
	static const unsigned char m[16] = { 0, 0, 11, 12, 13, 14, 11, 12, 13, 14, 12, 0, 0, 0, 0, 0 };
	unsigned char map[256], row[256];
	unsigned i, d;
	for (i = 0; i < 256; i++) map[i] = (unsigned char)(m[i & 15] * 0x11);
	for (d = 0; d < 256; d++, src += 256, dst += 256) {
		_fmemcpy(row, src, 256);
		for (i = 0; i < 256; i++) row[i] = map[row[i]];
		_fmemcpy(dst, row, 256);
	}
}

/* ---- /DIAG and /TUNC: finding out why the tunnel is gray on the PCjr ---- */

int opt_diag, opt_tunc;

static unsigned sum16(unsigned seg, unsigned n) /* 16-bit sum of n bytes */
{
	const unsigned char far *p = (const unsigned char far *)MK_FP(seg, 0);
	unsigned s = 0, i;
	for (i = 0; i < n; i++) s += p[i];
	return s;
}

static unsigned largest_free(void) /* paragraphs */
{
	union REGS r;
	r.h.ah = 0x48; r.x.bx = 0xFFFF;
	intdos(&r, &r);
	return r.x.bx;
}

static void diag(const char *when)
{
	if (!opt_diag) return;
	dbg_log("tunnel %s: tex %04X %04X code %04X len %u free %u paras", when, tex[0], tex[1], code, codelen, largest_free());
	if (tex[0]) dbg_log("  sums: tex0 %04X/%04X tex1 %04X/%04X code %04X",
		sum16(tex[0], 32768u), sum16(tex[0] + 0x800, 32768u),
		tex[1] ? sum16(tex[1], 32768u) : 0, tex[1] ? sum16(tex[1] + 0x800, 32768u) : 0,
		code ? sum16(code, codelen) : 0);
	if (tex[0]) {
		const unsigned char far *p = (const unsigned char far *)MK_FP(tex[0], 0x4000);
		dbg_log("  tex0 row 64: %02X %02X %02X %02X %02X %02X %02X %02X", p[0], p[4], p[8], p[16], p[32], p[33], p[64], p[100]);
	}
	if (code) {
		const unsigned char far *c = (const unsigned char far *)MK_FP(code, 0);
		dbg_log("  code: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X ... %02X",
			c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[10], c[13], c[14], c[15], c[16], c[codelen - 1]);
	}
}

/* what a frame put on the page: a sum, and how many different bytes */
static void diag_page(const char *when)
{
	const unsigned char far *p = (const unsigned char far *)MK_FP(fx_seg, 0);
	static unsigned char seen[256];
	unsigned i, kinds = 0;
	if (!opt_diag) return;
	memset(seen, 0, sizeof seen);
	for (i = 0; i < 16384u; i++) if (!seen[p[i]]) { seen[p[i]] = 1; kinds++; }
	dbg_log("  page %s: seg %04X pages %d sum %04X, %u kinds of byte, row 100: %02X %02X %02X %02X %02X %02X",
		when, fx_seg, fx_pages, sum16(fx_seg, 16384u), kinds,
		p[50 * 80 + 10], p[50 * 80 + 20], p[50 * 80 + 30], p[50 * 80 + 50], p[50 * 80 + 60], p[50 * 80 + 70]);
}

/* the same tunnel in plain C (slow): what the generated code should draw */
static void tunnel_c(unsigned v)
{
	unsigned char far *pg = (unsigned char far *)MK_FP(fx_seg, 0);
	int cr, bx;
	for (cr = CR0; cr < CR1; cr++)
		for (bx = 0; bx < 80; bx += 2) { /* pairs, the way the code is made */
			unsigned o1, o2, d = cr * 80 + bx;
			int s1, s2, sh;
			cell(bx, cr, &o1, &s1);
			cell(bx + 1, cr, &o2, &s2);
			sh = s1 < s2 ? s1 : s2;
			if (sh == 2) continue;
			pg[d] = pg[0x2000 + d] = *((unsigned char far *)MK_FP(tex[sh], (o1 + v) & 0xFFFF));
			pg[d + 1] = pg[0x2001 + d] = *((unsigned char far *)MK_FP(tex[sh], (o2 + v) & 0xFFFF));
		}
}

static void tunnel_prep(void)
{
	int d, cr, bx;
	unsigned off0 = 0;
	tex[0] = jr_alloc_high(0x1000);
	tex[1] = jr_alloc_high(0x1000);
	code = jr_alloc_high(0x1000);
	if (!tex[0] || !code) { code = 0; return; }
	/* the quadrant tables live in the code segment's last 6K until the code is written */
	qdepth = (unsigned char far *)MK_FP(code + 0xE80, 0);
	qang = qdepth + QW * QH;
	qshade = qang + QW * QH;
	quadrant();
	if (!tex[1]) tex[1] = tex[0];
	for (d = 0; d < 256; d++) tex_row((unsigned char far *)MK_FP(tex[0], d << 8), d);
	if (tex[1] != tex[0]) tex_dim((unsigned char far *)MK_FP(tex[1], 0), (unsigned char far *)MK_FP(tex[0], 0));
	/* prologue: save registers, load BX, ES, DX, CX (patched each frame) */
	cp = (unsigned char far *)MK_FP(code, 0);
	emit(0x1E); emit(0x06); emit(0x53); emit(0x51); emit(0x52); emit(0x57); emit(0x50); /* push ds es bx cx dx di ax */
	emit(0xBB); emitw(0);                    /* 7: mov bx,t      */
	emit(0xB8); emitw(0); emit(0x8E); emit(0xC0); /* 10: mov ax,es_seg / mov es,ax */
	emit(0xBA); emitw(tex[0]);               /* mov dx,bright    */
	emit(0xB9); emitw(tex[1]);               /* mov cx,dim       */
	emit(0xFC);                              /* cld              */
	flush();
	for (cr = CR0; cr < CR1; cr++) {
		int ds = -1;
		unsigned di = 0xFFFF;
		for (bx = 0; bx < 80; bx += 2) {
			unsigned o1, o2, dst = cr * 80 + bx;
			int s1, s2, s;
			cell(bx, cr, &o1, &s1);
			cell(bx + 1, cr, &o2, &s2);
			s = s1 < s2 ? s1 : s2;
			if (s == 2) continue;
			if (ds != s) { emit(0x8E); emit(s ? 0xD9 : 0xDA); ds = s; } /* mov ds,cx / dx */
			if (di != dst) { emit(0xBF); emitw(dst); di = dst; }      /* mov di,dst */
			emit(0x8A); emit(0x87); emitw(o1);                         /* mov al,[bx+o1] */
			emit(0x8A); emit(0xA7); emitw(o2);                         /* mov ah,[bx+o2] */
			emit(0xAB);                                                /* stosw */
			emit(0x26); emit(0x89); emit(0x85); emitw(0x1FFE);         /* mov es:[di+1FFEh],ax */
			di += 2;
		}
		flush();
	}
	emit(0x58); emit(0x5F); emit(0x5A); emit(0x59); emit(0x5B); emit(0x07); emit(0x1F); /* pop ax di dx cx bx es ds */
	emit(0xCB);                                                                         /* retf */
	flush();
	codelen = FP_OFF(cp) - off0;
	diag("after prep");
}

static const unsigned char pal_net[16] = {
	0, 1,            /* wall, grid: dark blue */
	2, 2, 2, 10, 15, 10, 2, 2, /* traces: a pulse of light */
	15,              /* nodes */
	8, 8, 2, 10,     /* dim traces */
	15 };

static void caption(void)
{
	fx_fill_rect(0, 0, 160, 24, FX_SOLID2(0));
	fx_fill_rect(0, 176, 160, 24, FX_SOLID2(0));
	center_text(8, "INTO THE NEURAL NET", 15);
}

/* the black middle of the tunnel and the caption, drawn once per page */
static void statics(void)
{
	fx_clear(0);
	caption();
}

static void tunnel_frame(unsigned t);

static void tunnel_enter(void)
{
	diag("at the start of the part");
	set_pal(pal_net);
	both_pages(statics);
	fx_pal_anim(0, 2, 9, 2, 1);
	fx_pal_anim(1, 11, 14, 3, 1);
	if (code) { /* the first frame on both pages, then the announcer */
		tunnel_frame(0);
		diag_page("after the first frame");
		fx_flip(); fx_begin();
		tunnel_frame(0); fx_flip(); fx_begin();
	}
	voice_say("JNET", 0);
}

static void tunnel_frame(unsigned t)
{
	static const char *const lines[] = {
		"LAYER %02u OF 96", "ATTENTION %u%%", "TOKENS %04u", "PARAMS: LOTS", "8088 SAYS HI",
	};
	char s[24];
	void (__far *run)(void);
	unsigned char far *c = (unsigned char far *)MK_FP(code, 0);
	unsigned tz = t * 2, ta = (unsigned)(sin8[(t / 2) & 255] / 2 + t / 3);
	unsigned v;
	int li;
	if (!code) { center_text(96, "not enough memory", 15); return; }
	v = ((tz & 255) << 8) | (ta & 255);
	c[8] = (unsigned char)v; c[9] = (unsigned char)(v >> 8);
	c[11] = (unsigned char)fx_seg; c[12] = (unsigned char)(fx_seg >> 8);
	run = (void (__far *)(void))MK_FP(code, 0);
	if (opt_tunc) tunnel_c(v);
	else run();
	/* the readout at the bottom */
	li = (int)(t / 112) % 5;
	fx_fill_rect(0, 184, 160, 8, FX_SOLID2(0));
	switch (li) {
	case 0: sprintf(s, lines[0], 1 + t / 12 % 96); break;
	case 1: sprintf(s, lines[1], 95 + sin8[(t * 3) & 255] * 4 / 127); break;
	case 2: sprintf(s, lines[2], t * 3 % 10000); break;
	default: strcpy(s, lines[li]); break;
	}
	center_text(184, s, 15);
}

static void tunnel_leave(void)
{
	/* 192 KB back to DOS, for any voice clips that didn't fit at startup */
	if (code) jr_dos_free(code);
	if (tex[1] && tex[1] != tex[0]) jr_dos_free(tex[1]);
	if (tex[0]) jr_dos_free(tex[0]);
	code = tex[0] = tex[1] = 0;
	voice_more();
	fx_pal_anim(0, 0, 0, 0, 0);
	fx_pal_anim(1, 0, 0, 0, 0);
}

const part part_tunnel = { "tunnel", tunnel_prep, tunnel_enter, tunnel_frame, tunnel_leave, 16, 8, 1 };
