/* The rest of Andrew Jenner's sopasm.asm, in C for Open Watcom.
 * New file, written 2026-10-07 for the CodeJr PCjr port. The interrupt
 * handlers are in sopsys.asm. */
#include <dos.h>
#include <string.h>
#include "owcompat.h"
#include "sopasm.h"

extern unsigned char writecharcol;
extern unsigned int scrseg, scroff, interlacediff;

/* Note the order: source first. Overlap is handled, as in the original. */
void farmemmove(void far *src, void far *dest, int length)
{
	_fmemmove(dest, src, (unsigned)length);
}

void farmemset(void far *p, int length, char fill)
{
	_fmemset(p, fill, (unsigned)length);
}

void writechar(char c)
{
	union REGS r;
	r.h.ah = 0x0E; r.h.al = (unsigned char)c;
	r.h.bh = 0; r.h.bl = (unsigned char)writecharcol;
	int86(0x10, &r, &r);
}

void clearline(void)
{
	union REGS r;
	r.x.ax = 0x0A20; r.x.bx = 0; r.x.cx = 40;
	int86(0x10, &r, &r);
}

void poscurs(int x, int y)
{
	union REGS r;
	r.h.ah = 2; r.h.bh = 0;
	r.h.dl = (unsigned char)x; r.h.dh = (unsigned char)y;
	int86(0x10, &r, &r);
}

void setgmode(int m)
{
	union REGS r;
	r.x.ax = (unsigned)m;
	int86(0x10, &r, &r);
}

unsigned int getwordfromport(int port)
{
	unsigned lo = inp(port);
	return lo | ((unsigned)inp(port) << 8);
}

/* A filled bar in the 4-colour screen layout (even rows in one half of the
 * buffer, odd rows interlacediff later): x0 and width in pixels, both
 * multiples of 4, rows counted from the top. */
int cgafbar(int x0, int y0, int width, int height, int c)
{
	unsigned off = (y0 & 1 ? (unsigned)interlacediff : 0) + (unsigned)(y0 >> 1) * 80 + (unsigned)scroff + ((unsigned)x0 >> 2);
	unsigned w = (unsigned)width >> 2;
	int d = interlacediff;
	unsigned char fill = (unsigned char)(c * 0x55);
	while (height--) {
		_fmemset(MK_FP(scrseg, off), fill, w);
		off += d;
		d = 80 - d;   /* down a bank, then up a bank and on a line */
	}
	return 0;
}
