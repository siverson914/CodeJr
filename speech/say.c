/* SAY: plays .SND speech clips made by mksnd.py on the IBM PCjr.
 *
 *   SAY HELLO              plays HELLO.SND
 *   SAY HELLO.SNP BYE      several clips in a row
 *   SAY /I HELLO           show what is in a clip without playing it
 *   SAY /Q HELLO           don't print the clip's text
 *   SAY /V HELLO           afterwards, report the speed reached and late samples
 *   SAY /T HELLO           speed test: play a .SND clip as fast as the PCjr can,
 *                          and report that rate (it sounds like a chipmunk)
 *   SAY /H HELLO           time .SND clips with the timer interrupt (SAY 1.0's
 *                          way) instead of polling the 8259; DOSBox needs it
 *   SAY /X HELLO           write the decoded samples to SAYDUMP.BIN (tests)
 *   SAY /C HELLO           decode ADPCM with the C decoder (tests; 3-bit always)
 *
 * A key stops playback. The clip's method (chip or pwm) is in its header;
 * the actual playing is done by player.asm, from a block above 128 KB.
 *
 * File format (little-endian):
 *   0  "JRSN"
 *   4  u8  version: 1, or 2 with the 4 extra bytes at 16
 *   5  u8  method: 1 = SN76496, three voices; 2 = PWM on PIT channel 2
 *   6  u16 PIT counts per sample (1193182 / rate)
 *   8  u32 samples
 *  12  u16 text length
 *  14  u16 table length (768 for method 1, else 0)
 *  16  version 2 only: u8 ADPCM code bits (2, 3 or 4), u8 0, u16 0
 *  then text, then table, then the samples: one byte each (version 1), or
 *  ADPCM codes packed most significant bit first (version 2), which SAY
 *  decodes into the block before it plays. mksnd.py has the reference
 *  decoder (adpcm_step); this one must match it bit for bit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>

extern char __far sp_begin[];
extern char __far sp_end[];
extern char __far sp_dec[];
#pragma aux sp_begin "sp_begin"
#pragma aux sp_end "sp_end"
#pragma aux sp_dec "sp_dec"

unsigned get_ss(void);
#pragma aux get_ss = "mov ax,ss" value [ax];

#define CODE_MAX 0x400u
#define TABLE_OFF 0x400u
#define DATA_PARA 0xA0u

static int quiet, info, verbose, speedtest, irqtiming, dump, slowdec;

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

/* The player stops the BIOS clock while it runs; add the ticks back.
 * pit: PIT counts (1.193 MHz) that went by. */
static void fix_clock(unsigned long pit)
{
	unsigned long __far *ticks = (unsigned long __far *)MK_FP(0x40, 0x6C);
	unsigned char __far *midnight = (unsigned char __far *)MK_FP(0x40, 0x70);
	unsigned long add = pit >> 16;
	_disable();
	*ticks += add;
	if (*ticks >= 0x1800B0UL) {
		*ticks -= 0x1800B0UL;
		*midnight = 1;
	}
	_enable();
}

/* Far pointer to byte off of the area starting at seg:0, normalized. */
static unsigned char __far *at(unsigned seg, unsigned long off)
{
	return (unsigned char __far *)MK_FP(seg + (unsigned)(off >> 4), (unsigned)off & 15);
}

static int read_far(int fd, unsigned seg, unsigned long off, unsigned long n)
{
	while (n) {
		unsigned chunk = n > 0x8000UL ? 0x8000u : (unsigned)n;
		unsigned got;
		if (_dos_read(fd, at(seg, off), chunk, &got) || got != chunk)
			return -1;
		n -= chunk;
		off += chunk;
	}
	return 0;
}

/* ---- ADPCM (see adpcm_step in mksnd.py) -------------------------------- */

static const unsigned steps[82] = { /* mksnd.py's STEPS: IMA's, cut at 16818 */
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
	12635, 13899, 15289, 16818
};
static const signed char adj4[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static const signed char adj3[4] = { -1, -1, 2, 4 };
static const signed char adj2[2] = { -1, 2 };

/* Decode n samples whose codes start at byte inoff of seg:0, writing one
 * byte per sample from byte 0 up. The codes sit at the end of the same
 * area, and output never catches up with input, so it works in place. */
static void adpcm_decode(unsigned seg, unsigned long inoff, unsigned long n, unsigned bits)
{
	const signed char *adj = bits == 4 ? adj4 : bits == 3 ? adj3 : adj2;
	unsigned half = 1u << (bits - 1), mask = (1u << bits) - 1;
	unsigned char __far *in = at(seg, inoff);
	unsigned char __far *out = at(seg, 0);
	unsigned acc = 0, nacc = 0, step, diff, m, code, k;
	long p = 0;
	int si = 0;
	while (n--) {
		if (nacc < bits) {
			acc = (acc << 8) | *in++;
			nacc += 8;
			if (FP_OFF(in) >= 0x8000u)
				in = (unsigned char __far *)MK_FP(FP_SEG(in) + 0x800, FP_OFF(in) - 0x8000u);
		}
		nacc -= bits;
		code = (acc >> nacc) & mask;
		step = steps[si];
		m = code & (half - 1);
		diff = step >> (bits - 1);
		for (k = 0; k + 1 < bits; k++)
			if (m & (half >> (k + 1)))
				diff += step >> k;
		p = code & half ? p - diff : p + diff;
		if (p < 0)
			p = 0;
		else if (p > 65535L)
			p = 65535L;
		si += adj[m];
		if (si < 0)
			si = 0;
		else if (si > 81)
			si = 81;
		*out++ = (unsigned char)(p >> 8);
		if (FP_OFF(out) >= 0x8000u)
			out = (unsigned char __far *)MK_FP(FP_SEG(out) + 0x800, FP_OFF(out) - 0x8000u);
	}
}

/* Tables for sp_dec (player.asm), in DGROUP, which is SS: DELTA[82][codes]
 * signed predictor changes, then NEXTOFF (2848) bytes on, NEXT[82][codes]
 * the SS offset of the next step index's DELTA row. Rows are 32-byte
 * aligned so the decoder can add a code to the low byte only. */
static unsigned atab[89 * 16 * 2 + 16];

static unsigned build_tables(unsigned bits)
{
	unsigned ncode = 1u << bits, half = ncode >> 1, base, si, code, k, diff, step, m;
	unsigned *delta, *next;
	int s2;
	base = (FP_OFF(atab) + 31) & ~31u;
	delta = (unsigned *)((char *)atab + (base - FP_OFF(atab)));
	next = delta + 89 * 16;
	for (si = 0; si < 82; si++) {
		for (code = 0; code < ncode; code++) {
			step = steps[si];
			m = code & (half - 1);
			diff = step >> (bits - 1);
			for (k = 0; k + 1 < bits; k++)
				if (m & (half >> (k + 1)))
					diff += step >> k;
			delta[si * ncode + code] = code & half ? (unsigned)(-(int)diff) : diff;
			s2 = (int)si + (bits == 4 ? adj4[m] : adj2[m]);
			if (s2 < 0) s2 = 0;
			if (s2 > 81) s2 = 81;
			next[si * ncode + code] = base + (unsigned)s2 * ncode * 2;
		}
	}
	return base;
}

/* Decode with sp_dec, already copied to block:0, in chunks of 32 KB of
 * output so its 16-bit offsets never wrap. */
static void adpcm_fast(unsigned block, unsigned seg, unsigned long inoff, unsigned long n,
                       unsigned bits)
{
	unsigned __far *par = (unsigned __far *)MK_FP(block, 0);
	void (__far *dec)(void) = (void (__far *)(void))MK_FP(block, FP_OFF(sp_dec) - FP_OFF(sp_begin));
	unsigned per = 8 / bits;
	unsigned long outoff = 0, nbytes = (n + per - 1) / per;
	par[0x28 / 2] = build_tables(bits);
	par[0x2A / 2] = 0x8000u;
	par[0x26 / 2] = bits;
	while (nbytes) {
		unsigned long chunk = nbytes > 0x8000UL / per ? 0x8000UL / per : nbytes;
		unsigned char __far *in = at(seg, inoff), __far *out = at(seg, outoff);
		par[0x1C / 2] = FP_SEG(in);
		par[0x1E / 2] = FP_OFF(in);
		par[0x20 / 2] = FP_SEG(out);
		par[0x22 / 2] = FP_OFF(out);
		par[0x24 / 2] = (unsigned)chunk;
		dec();
		inoff += chunk;
		outoff += chunk * per;
		nbytes -= chunk;
	}
}

static unsigned long bios_ticks(void)
{
	unsigned long t;
	_disable();
	t = *(unsigned long __far *)MK_FP(0x40, 0x6C);
	_enable();
	return t;
}

static int play(const char *arg)
{
	static char name[80], text[512];
	unsigned char h[20];
	unsigned long n, done, pit;
	unsigned method, divisor, tlen, tablen, seg, codesz, got, bits = 0;
	unsigned long clen = 0, t0 = 0, dticks = 0;
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
	if (_dos_read(fd, h, 16, &got) || got != 16 || memcmp(h, "JRSN", 4) || h[4] < 1 || h[4] > 2
	    || (h[4] == 2 && (_dos_read(fd, h + 16, 4, &got) || got != 4))) {
		printf("SAY: %s is not a SAY clip\n", name);
		_dos_close(fd);
		return 1;
	}
	method = h[5];
	divisor = rd16(h + 6);
	n = rd32(h + 8);
	tlen = rd16(h + 12);
	tablen = rd16(h + 14);
	if (h[4] == 2) {
		bits = h[16];
		clen = (n * bits + 7) / 8;
	}
	if ((method != 1 && method != 2) || divisor < 40 || n == 0
	    || (h[4] == 2 && (method != 1 || bits < 2 || bits > 4))
	    || tlen >= sizeof text || tablen > 768 || (method == 1 && tablen != 768)) {
		printf("SAY: %s has a header I don't understand\n", name);
		_dos_close(fd);
		return 1;
	}
	_dos_read(fd, text, tlen, &got);
	text[tlen] = 0;
	if (info) {
		printf("%s: %s%s, %lu samples at %lu Hz, %lu.%lu s\n  \"%s\"\n", name,
		       method == 1 ? "chip (3 voices)" : "pwm",
		       bits == 4 ? ", ADPCM 4-bit" : bits == 3 ? ", ADPCM 3-bit" : bits == 2 ? ", ADPCM 2-bit" : "",
		       n, 1193182UL / divisor, n * divisor / 1193182UL,
		       n * divisor / 119318UL % 10, text);
		_dos_close(fd);
		return 0;
	}

	paras = DATA_PARA + (n + 16 + 15) / 16; /* 16: slack for in-place ADPCM */
	if (paras > 0xFFFFUL || (seg = alloc_high((unsigned)paras)) == 0) {
		printf("SAY: not enough memory for %s (%lu KB)\n", name, paras / 64);
		_dos_close(fd);
		return 1;
	}
	if (tablen)
		_dos_read(fd, MK_FP(seg, TABLE_OFF), tablen, &got);
	if (bits ? read_far(fd, seg + DATA_PARA, n + 16 - clen, clen) : read_far(fd, seg + DATA_PARA, 0, n)) {
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
	if (bits) {
		t0 = bios_ticks();
		if (bits != 3 && FP_SEG(atab) == get_ss() && !slowdec)
			adpcm_fast(seg, seg + DATA_PARA, n + 16 - clen, n, bits);
		else
			adpcm_decode(seg + DATA_PARA, n + 16 - clen, n, bits);
		dticks = bios_ticks() - t0;
	}
	if (dump) {
		int dfd;
		unsigned long off;
		if (!_dos_creat("SAYDUMP.BIN", 0, &dfd)) {
			for (off = 0; off < n; off += 0x8000UL) {
				unsigned chunk = n - off > 0x8000UL ? 0x8000u : (unsigned)(n - off);
				_dos_write(dfd, at(seg + DATA_PARA, off), chunk, &got);
			}
			_dos_close(dfd);
		}
	}

	par = (unsigned __far *)MK_FP(seg, 0);
	par[2] = method;
	par[3] = speedtest && method == 1 ? 24 : divisor; /* 50 kHz: every tick is late */
	par[4] = (unsigned)n;
	par[5] = (unsigned)(n >> 16);
	par[6] = seg + DATA_PARA;
	par[13] = irqtiming;

	if (!quiet && text[0])
		printf("%s\n", text);
	fn = (void (__far *)(void))MK_FP(seg, 0);
	fn();
	done = par[7] | ((unsigned long)par[8] << 16);
	pit = par[11] | ((unsigned long)par[12] << 16);
	fix_clock(pit ? pit : done * par[3]);
	if (par[9]) {
		union REGS r;
		r.h.ah = 0;
		int86(0x16, &r, &r); /* eat the key */
		rc = 2;
	}
	if (verbose || speedtest)
		printf("SAY: %s method %u divisor %u block %04X played %lu of %lu, late %u%s\n",
		       name, method, par[3], seg, done, n, par[10], rc == 2 ? " (key)" : "");
	if ((verbose || speedtest) && bits)
		printf("SAY: decoded ADPCM %u-bit in %lu ms\n", bits, dticks * 549 / 10);
	if ((verbose || speedtest) && pit && done) {
		/* tenths of a PIT count per sample; the 8088 runs at 4.77 MHz,
		 * exactly 4 CPU clocks per PIT count */
		unsigned long t10 = pit * 10 / done;
		printf("SAY: speed %lu Hz (wanted %lu), %lu.%lu PIT counts = %lu CPU clocks per sample\n",
		       11931820UL / t10, 1193182UL / par[3], t10 / 10, t10 % 10, t10 * 4 / 10);
		if (speedtest)
			printf("SAY: so this machine can play chip clips at %s %lu Hz\n",
			       par[10] < done / 10 * 9 ? "at least" : "up to about", 11931820UL / t10);
	}
	if (speedtest && method != 1)
		printf("SAY: /T only measures chip (.SND) clips\n");
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
			else if (c == 'T') speedtest = 1;
			else if (c == 'H') irqtiming = 1;
			else if (c == 'X') dump = 1;
			else if (c == 'C') slowdec = 1;
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
		       "  /V  report the speed reached and late samples (the player fell behind)\n"
		       "  /T  speed test: play a .SND as fast as possible and report the rate\n"
		       "  /H  time .SND clips with the timer interrupt (slower; DOSBox needs it)\n");
		return 1;
	}
	return rc == 2 ? 0 : rc;
}
