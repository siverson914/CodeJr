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
	unsigned ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
	unsigned a;
	if (!ax && !ay) return 0;
	if (ax >= ay) a = atan_t[(unsigned)((unsigned long)ay * 64 / ax)];
	else a = 64 - atan_t[(unsigned)((unsigned long)ax * 64 / ay)];
	if (dx < 0) a = 128 - a;
	if (dy < 0) a = 256 - a;
	return (unsigned char)a;
}

static unsigned isqrt(unsigned long v)
{
	unsigned long r = 0, b = 1UL << 30;
	while (b > v) b >>= 2;
	while (b) {
		if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
		else r >>= 1;
		b >>= 2;
	}
	return (unsigned)r;
}

static void emit(unsigned char b) { *cp++ = b; }
static void emitw(unsigned w) { *cp++ = (unsigned char)w; *cp++ = (unsigned char)(w >> 8); }

/* the cell's texture offset and shade: 0 bright, 1 dim, 2 black */
static void cell(int bx, int cr, unsigned *off, int *shade)
{
	int px = bx * 2 + 1 - 80, py = cr * 2 + 1 - 100;
	int dx = px * 5 / 3; /* a pixel is 5/3 as wide as it is tall */
	unsigned r = isqrt((long)dx * dx + (long)py * py);
	unsigned depth = r ? 3000u / r : 255;
	*off = ((depth & 255) << 8) | iatan2(py, dx);
	*shade = r < 9 ? 2 : r < 34 ? 1 : 0;
}

/* Logical colors: 1 wall, 2-9 bright traces (rotating), 10 nodes,
 * 11-14 dim traces (rotating), 15 text. */
static unsigned char texel(int a, int d, int dim)
{
	int c;
	int la = a & 31, ld = d & 31;
	if ((la < 2 || la == 31) && (ld < 2 || ld == 31)) c = 10;     /* node */
	else if (la == 0 || (la == 16 && (d & 64)))
		c = 2 + ((d >> 1) & 7);                                   /* trace along */
	else if (ld == 0 || (ld == 16 && (a & 32)))
		c = 2 + ((a >> 2) & 7);                                   /* ring */
	else if (((a * 7 + d * 13) & 127) == 5) c = 10;               /* synapse */
	else c = 1;
	if (dim) c = c == 1 ? 0 : c == 10 ? 12 : 11 + (c & 3);
	return FX_SOLID(c);
}

static void tunnel_prep(void)
{
	int a, d, i, cr, bx;
	unsigned off0 = 0;
	tex[0] = jr_alloc_high(0x1000);
	tex[1] = jr_alloc_high(0x1000);
	code = jr_alloc_high(0x1000);
	if (!tex[0] || !code) { code = 0; return; }
	if (!tex[1]) tex[1] = tex[0];
	for (i = 0; i < 2; i++) {
		if (i && tex[1] == tex[0]) break;
		for (d = 0; d < 256; d++) {
			unsigned char far *p = (unsigned char far *)MK_FP(tex[i], d << 8);
			for (a = 0; a < 256; a++) p[a] = texel(a, d, i);
		}
	}
	/* prologue: save registers, load BX, ES, DX, CX (patched each frame) */
	cp = (unsigned char far *)MK_FP(code, 0);
	emit(0x1E); emit(0x06); emit(0x53); emit(0x51); emit(0x52); emit(0x57); emit(0x50); /* push ds es bx cx dx di ax */
	emit(0xBB); emitw(0);                    /* 7: mov bx,t      */
	emit(0xB8); emitw(0); emit(0x8E); emit(0xC0); /* 10: mov ax,es_seg / mov es,ax */
	emit(0xBA); emitw(tex[0]);               /* mov dx,bright    */
	emit(0xB9); emitw(tex[1]);               /* mov cx,dim       */
	emit(0xFC);                              /* cld              */
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
	}
	emit(0x58); emit(0x5F); emit(0x5A); emit(0x59); emit(0x5B); emit(0x07); emit(0x1F); /* pop ax di dx cx bx es ds */
	emit(0xCB);                                                                         /* retf */
	codelen = FP_OFF(cp) - off0;
}

static const unsigned char pal_net[16] = {
	0, 1,            /* wall: dark blue */
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

static void tunnel_enter(void)
{
	set_pal(pal_net);
	both_pages(statics);
	fx_pal_anim(0, 2, 9, 2, 1);
	fx_pal_anim(1, 11, 14, 3, 1);
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
	run();
	/* the readout at the bottom */
	li = (int)(t / 112) % 5;
	fx_fill_rect(0, 184, 160, 8, FX_SOLID2(0));
	switch (li) {
	case 0: sprintf(s, lines[0], 1 + t / 12 % 96); break;
	case 1: sprintf(s, lines[1], 50 + sin8[t & 255] * 49 / 127); break;
	case 2: sprintf(s, lines[2], t * 3 % 10000); break;
	default: strcpy(s, lines[li]); break;
	}
	center_text(184, s, 15);
}

static void tunnel_leave(void)
{
	fx_pal_anim(0, 0, 0, 0, 0);
	fx_pal_anim(1, 0, 0, 0, 0);
}

const part part_tunnel = { "tunnel", tunnel_prep, tunnel_enter, tunnel_frame, tunnel_leave, 16, 8 };
