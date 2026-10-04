/* SAY: plays .SND speech clips made by mksnd.py on the IBM PCjr.
 *
 *   SAY HELLO              plays HELLO.SND
 *   SAY HELLO.SNP BYE      several clips in a row
 *   SAY /I HELLO           show what is in a clip without playing it
 *   SAY /Q HELLO           don't print the clip's text
 *   SAY /V HELLO           afterwards, print samples played and how many were late
 *
 * A key stops playback. The clip's method (chip or pwm) is in its header;
 * the actual playing is done by player.asm, from a block above 128 KB.
 *
 * File format (little-endian):
 *   0  "JRSN"
 *   4  u8  version (1)
 *   5  u8  method: 1 = SN76496, three voices; 2 = PWM on PIT channel 2
 *   6  u16 PIT counts per sample (1193182 / rate)
 *   8  u32 samples
 *  12  u16 text length
 *  14  u16 table length (768 for method 1, else 0)
 *  16  text, then table, then one byte per sample
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>

extern char __far sp_begin[];
extern char __far sp_end[];
#pragma aux sp_begin "sp_begin"
#pragma aux sp_end "sp_end"

#define CODE_MAX 0x400u
#define TABLE_OFF 0x400u
#define DATA_PARA 0xA0u

static int quiet, info, verbose;

static unsigned short rd16(const unsigned char *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned long rd32(const unsigned char *p)
{
	return rd16(p) | ((unsigned long)rd16(p + 2) << 16);
}

/* DOS allocation, "last fit" so the block lands at the top of memory,
 * above the PCjr's slow low 128 KB when the machine has expansion RAM. */
static unsigned alloc_high(unsigned paras)
{
	union REGS r;
	unsigned old, seg;
	r.x.ax = 0x5800;
	intdos(&r, &r);
	old = r.x.ax;
	r.x.ax = 0x5801;
	r.x.bx = 2;
	intdos(&r, &r);
	r.h.ah = 0x48;
	r.x.bx = paras;
	intdos(&r, &r);
	seg = r.x.cflag ? 0 : r.x.ax;
	r.x.ax = 0x5801;
	r.x.bx = old;
	intdos(&r, &r);
	return seg;
}

/* The player stops the BIOS clock while it runs; add the ticks back. */
static void fix_clock(unsigned long samples, unsigned divisor)
{
	unsigned long __far *ticks = (unsigned long __far *)MK_FP(0x40, 0x6C);
	unsigned char __far *midnight = (unsigned char __far *)MK_FP(0x40, 0x70);
	unsigned long add = (unsigned long)((samples >> 8) * divisor) >> 8;
	_disable();
	*ticks += add;
	if (*ticks >= 0x1800B0UL) {
		*ticks -= 0x1800B0UL;
		*midnight = 1;
	}
	_enable();
}

static int read_far(int fd, unsigned seg, unsigned long n)
{
	while (n) {
		unsigned chunk = n > 0x8000UL ? 0x8000u : (unsigned)n;
		unsigned got;
		if (_dos_read(fd, MK_FP(seg, 0), chunk, &got) || got != chunk)
			return -1;
		n -= chunk;
		seg += 0x800;
	}
	return 0;
}

static int play(const char *arg)
{
	static char name[80], text[512];
	unsigned char h[16];
	unsigned long n, done;
	unsigned method, divisor, tlen, tablen, seg, codesz, got;
	unsigned long paras;
	unsigned __far *par;
	void (__far *fn)(void);
	int fd, rc = 0;

	strncpy(name, arg, sizeof name - 5);
	name[sizeof name - 5] = 0;
	if (!strchr(name, '.'))
		strcat(name, ".SND");
	if (_dos_open(name, 0, &fd)) {
		printf("SAY: can't open %s\n", name);
		return 1;
	}
	if (_dos_read(fd, h, 16, &got) || got != 16 || memcmp(h, "JRSN", 4) || h[4] != 1) {
		printf("SAY: %s is not a SAY clip\n", name);
		_dos_close(fd);
		return 1;
	}
	method = h[5];
	divisor = rd16(h + 6);
	n = rd32(h + 8);
	tlen = rd16(h + 12);
	tablen = rd16(h + 14);
	if ((method != 1 && method != 2) || divisor < 40 || n == 0
	    || tlen >= sizeof text || tablen > 768 || (method == 1 && tablen != 768)) {
		printf("SAY: %s has a header I don't understand\n", name);
		_dos_close(fd);
		return 1;
	}
	_dos_read(fd, text, tlen, &got);
	text[tlen] = 0;
	if (info) {
		printf("%s: %s, %lu samples at %lu Hz, %lu.%lu s\n  \"%s\"\n", name,
		       method == 1 ? "chip (3 voices)" : "pwm",
		       n, 1193182UL / divisor, n * divisor / 1193182UL,
		       n * divisor / 119318UL % 10, text);
		_dos_close(fd);
		return 0;
	}

	paras = DATA_PARA + (n + 15) / 16;
	if (paras > 0xFFFFUL || (seg = alloc_high((unsigned)paras)) == 0) {
		printf("SAY: not enough memory for %s (%lu KB)\n", name, paras / 64);
		_dos_close(fd);
		return 1;
	}
	if (tablen)
		_dos_read(fd, MK_FP(seg, TABLE_OFF), tablen, &got);
	if (read_far(fd, seg + DATA_PARA, n)) {
		printf("SAY: %s is shorter than its header says\n", name);
		_dos_close(fd);
		_dos_freemem(seg);
		return 1;
	}
	_dos_close(fd);

	codesz = FP_OFF(sp_end) - FP_OFF(sp_begin);
	if (codesz > CODE_MAX) {
		printf("SAY: player too big\n");
		_dos_freemem(seg);
		return 1;
	}
	_fmemcpy(MK_FP(seg, 0), sp_begin, codesz);
	par = (unsigned __far *)MK_FP(seg, 0);
	par[2] = method;
	par[3] = divisor;
	par[4] = (unsigned)n;
	par[5] = (unsigned)(n >> 16);
	par[6] = seg + DATA_PARA;

	if (!quiet && text[0])
		printf("%s\n", text);
	fn = (void (__far *)(void))MK_FP(seg, 0);
	fn();
	done = par[7] | ((unsigned long)par[8] << 16);
	fix_clock(done, divisor);
	if (par[9]) {
		union REGS r;
		r.h.ah = 0;
		int86(0x16, &r, &r); /* eat the key */
		rc = 2;
	}
	if (verbose)
		printf("SAY: %s method %u divisor %u block %04X played %lu of %lu, late %u%s\n",
		       name, method, divisor, seg, done, n, par[10], rc == 2 ? " (key)" : "");
	_dos_freemem(seg);
	return rc;
}

int main(int argc, char **argv)
{
	int i, any = 0, rc = 0;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '/' || argv[i][0] == '-') {
			char c = argv[i][1] & 0xDF;
			if (c == 'Q') quiet = 1;
			else if (c == 'I') info = 1;
			else if (c == 'V') verbose = 1;
			else {
				printf("SAY: unknown option %s\n", argv[i]);
				return 1;
			}
		}
	}
	for (i = 1; i < argc && rc != 2; i++) {
		if (argv[i][0] == '/' || argv[i][0] == '-')
			continue;
		any = 1;
		rc = play(argv[i]);
	}
	if (!any) {
		printf("SAY clip[.SND] ...   plays speech clips (a key stops)\n"
		       "  /I  show what is in a clip   /Q  don't print its text\n"
		       "  /V  report samples played and late (the player fell behind)\n");
		return 1;
	}
	return rc == 2 ? 0 : rc;
}
