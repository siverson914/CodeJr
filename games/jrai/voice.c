/* Spoken lines: NAME.SND clips (made by mkvoice.sh), loaded into memory at
 * startup and played by SAY's player, built into JRAI.
 *
 * The player is speech/player.asm, the one proven on the real PCjr: the
 * three SN76496 tone voices at period 1 as one DAC, paced by the timer
 * with every other interrupt off. As in SAY.EXE, its code is copied into
 * its own DOS block at the top of memory and called there. While it plays,
 * the picture holds still (the retrace interrupt is off too) and T stops,
 * so the parts' clocks skip the line by themselves.
 *
 * Every clip is read from disk once, while "CodeJr is thinking", into its
 * own DOS block: 768 bytes of level tables, then the samples. So a line
 * starts at once, and nothing touches DOS or the disk after graphics
 * start. (An earlier version ran SAY.EXE per line: seconds of loading each
 * time, and SAY's progress text scrolled the picture.) If memory runs
 * short, the rest are loaded when the tunnel gives its 192 KB back
 * (voice_more); a clip that still doesn't fit is skipped, and its line
 * just shows as text.
 *
 * Clips are SAY's format at SAY's default quality (speech/README.md):
 * 8 kHz, 4-bit ADPCM, about 4 KB per second on disk and over the link.
 * They are decoded once, at load, into one byte per sample. The decoder,
 * the built-in level table and the format rules are SAY's own: say.c is
 * compiled in below (its main renamed), so JRAI plays whatever SAY plays.
 *
 * In the frame-locked /VIDEO mode there is no real time to play in, so
 * the writes a line stands for are logged, and T moves on as if it played.
 */
#include <conio.h>
#include <fcntl.h>
#include "jrai.h"

#pragma off (check_stack)

#define main say_main           /* SAY.EXE's source: decoder, tables, helpers */
#include "../../speech/say.c"
#undef main

#define TABLE_PARAS 0x30u       /* 768 bytes of tables before a clip's samples */
#define RESERVE 0x1800u         /* paragraphs kept free for graphics (96 KB) */

typedef struct {
	const char *name;
	unsigned seg;               /* its block: tables, then samples; 0 = not loaded */
	unsigned long n;            /* samples */
	unsigned div;               /* PIT counts per sample */
} clip;
static clip clips[] = { /* in the order they are spoken */
	{ "JHELLO" }, { "JTITLE" }, { "JNET" },
	{ "JM1" }, { "JM2" }, { "JM3" }, { "JM4" }, { "JM5" }, { "JM6" }, { "JM7" }, { "JM8" }, { "JM9" },
	{ "JEND" },
};
#define NCLIPS (sizeof clips / sizeof clips[0])

static unsigned player;         /* the player's block */
unsigned long Tpause;           /* ticks spent speaking, in all (only /VIDEO moves T) */
void (*say_hook)(void);
int say_h;                      /* /SAYH: time with the interrupt (DOSBox has no 8259 poll) */

static unsigned largest_free(void) /* paragraphs */
{
	union REGS r;
	r.h.ah = 0x48; r.x.bx = 0xFFFF;
	intdos(&r, &r);
	return r.x.bx;
}

/* 0 loaded, 1 no file or not a clip we play, 2 no memory (try again later).
 * The steps are SAY's (play() in say.c), into our own block. */
static unsigned reserve = RESERVE;

static int load(clip *c)
{
	char fn[16];
	unsigned char h[24];
	int fd;
	unsigned got, tl, tablen, paras, ver, bits = 0, flags = 0, rest = 0, dseg;
	unsigned long n, clen = 0;
	if (c->seg) return 0;
	sprintf(fn, "%s.SND", c->name);
	if (_dos_open(fn, O_RDONLY, &fd)) return 1;
	ver = 0;
	if (_dos_read(fd, h, 16, &got) || got != 16 || memcmp(h, "JRSN", 4) || (ver = h[4]) < 1
	    || ver > 3 || (ver == 2 && (_dos_read(fd, h + 16, 4, &got) || got != 4))
	    || (ver == 3 && (_dos_read(fd, h + 16, 8, &got) || got != 8))) {
		_dos_close(fd);
		return 1;
	}
	n = rd32(h + 8);
	tl = rd16(h + 12);
	tablen = rd16(h + 14);
	if (ver >= 2) { bits = h[16]; clen = (n * bits + 7) / 8; }
	if (ver == 3) { flags = h[17]; rest = h[18]; clen = rd32(h + 20); }
	if (h[5] != 1 || rd16(h + 6) < 40 || n == 0 || n > 0xF0000UL || (ver >= 2 && (bits < 2 || bits > 4))
	    || flags > 3 || clen > n + 32 || tablen != (flags & 1 ? 0 : 768)) {
		_dos_close(fd);
		return 1;
	}
	/* 48 bytes of slack: ADPCM codes sit at the end and are decoded in place */
	paras = TABLE_PARAS + (unsigned)((n + 48 + 15) >> 4);
	/* leave room for the graphics library's buffers and second page */
	if (largest_free() < paras + reserve || !(c->seg = jr_alloc_high(paras))) {
		c->seg = 0;
		_dos_close(fd);
		return 2;
	}
	dseg = c->seg + TABLE_PARAS;
	lseek(fd, (ver == 3 ? 24L : ver == 2 ? 20L : 16L) + tl, SEEK_SET);
	if (tablen) _dos_read(fd, MK_FP(c->seg, 0), 768, &got);
	else _fmemcpy(MK_FP(c->seg, 0), builtin_table, sizeof builtin_table);
	if (bits ? read_far(fd, dseg, n + 32 - clen, clen) : read_far(fd, dseg, 0, n)) {
		_dos_close(fd);
		jr_dos_free(c->seg);
		c->seg = 0;
		return 1;
	}
	_dos_close(fd);
	boot_spin();
	if (bits) {
		ptotal = n;
		dec_init(player, bits);
		if (flags & 2) { /* records: pauses and runs of codes */
			unsigned long in = n + 32 - clen, end = n + 32, out = 0, k;
			while (out < n && in + 2 <= end) {
				unsigned char __far *hp = at(dseg, in);
				unsigned rec = hp[0] | (hp[1] << 8);
				in += 2;
				k = rec & 0x7FFFu;
				if (k == 0) break;
				if (k > n - out) k = n - out;
				if (rec & 0x8000u) {
					far_fill(dseg, out, k, (unsigned char)rest);
					dec_rest(player, rest);
				} else {
					dec_run(player, dseg, in, out, k, bits);
					in += k * bits / 8;
				}
				out += k;
				boot_spin();
			}
		} else
			dec_run(player, dseg, n + 32 - clen, 0, n, bits);
	}
	c->n = n;
	c->div = rd16(h + 6);
	return 0;
}

static int load_all(void)
{
	int i, loaded = 0;
	for (i = 0; i < (int)NCLIPS; i++) {
		int r = load(&clips[i]);
		if (r == 2) break;  /* memory: the rest wait for voice_more */
		if (r == 0) loaded++;
	}
	return loaded;
}

/* At startup, in text mode, before anything else takes the top of memory. */
void voice_preload(void)
{
	unsigned codesz = FP_OFF(sp_end) - FP_OFF(sp_begin);
	int n;
	if (mus_mute) return;
	/* the player's block: its code (the decoder too) runs from up there */
	if (codesz > CODE_MAX || !(player = jr_alloc_high(DATA_PARA))) { player = 0; return; }
	_fmemcpy(MK_FP(player, 0), sp_begin, codesz);
	n = load_all();
	if (opt_bench) dbg_log("voice: player %04X, %d of %d clips loaded, %u paras free", player, n, (int)NCLIPS, largest_free());
}

/* Once graphics have started (nothing left to keep room for), and again
 * after the tunnel freed its memory: the clips that didn't fit before.
 * Returns how many are still missing. */
int voice_missing(void)
{
	int i, n = 0;
	if (mus_mute) return 0;
	for (i = 0; i < (int)NCLIPS; i++) if (!clips[i].seg) n++;
	return n;
}

void voice_more(void)
{
	int n;
	if (!voice_missing() || !player) return;
	reserve = 0x100;
	dos_begin(); /* DOS and the disk, with the retrace interrupt live (see jrai.h) */
	n = load_all();
	dos_end();
	if (opt_bench) dbg_log("voice: %d clips loaded later, %d missing, %u paras free", n, voice_missing(), largest_free());
}

static void chip_voices(int on)
{
	sn_out(0x9F); sn_out(0xBF); sn_out(0xDF); sn_out(0xFF);
	if (on) { /* tone periods = 1 */
		sn_out(0x81); sn_out(0x00);
		sn_out(0xA1); sn_out(0x00);
		sn_out(0xC1); sn_out(0x00);
	}
}

/* The real thing: SAY's player on a clip in memory. */
static void speak(const clip *c)
{
	unsigned __far *par = (unsigned __far *)MK_FP(player, 0);
	void (__far *fn)(void) = (void (__far *)(void))MK_FP(player, 0);
	unsigned long pit;
	unsigned long __far *ticks = (unsigned long __far *)MK_FP(0x40, 0x6C);
	_fmemcpy(MK_FP(player, TABLE_OFF), MK_FP(c->seg, 0), 768);
	par[2] = 1;                      /* method: chip */
	par[3] = c->div;
	par[4] = (unsigned)c->n;
	par[5] = (unsigned)(c->n >> 16);
	par[6] = c->seg + TABLE_PARAS;   /* the first sample */
	par[13] = say_h ? 1 : 0;
	/* the player stops when a key is waiting in the BIOS buffer: empty it */
	_disable();
	*(unsigned short far *)MK_FP(0x40, 0x1A) = *(unsigned short far *)MK_FP(0x40, 0x1C);
	_enable();
	fn();
	/* as SAY does: the BIOS clock missed these ticks */
	pit = par[11] | ((unsigned long)par[12] << 16);
	if (!pit) pit = (par[7] | ((unsigned long)par[8] << 16)) * par[3];
	_disable();
	*ticks += pit >> 16;
	/* the player leaves the timer in the BIOS's mode 3; the library wants mode 2 */
	outp(0x43, 0x34); outp(0x40, 0); outp(0x40, 0);
	_enable();
}

/* /VIDEO: log the writes the clip stands for, move T, show the mouth. */
static void play_log(const clip *c, void (*mouth)(int level))
{
	unsigned long i, pc = 0, t_begin = T;
	const unsigned char far *tb = (const unsigned char far *)MK_FP(c->seg, 0);
	int lo = 255, hi = 0;
	dbg_log("say %s T %lu n %lu div %u", c->name, T, c->n, c->div); /* for checking the soundtrack */
	chip_voices(1);
	for (i = 0; i < c->n; i++) {
		unsigned char v = *(const unsigned char far *)MK_FP(c->seg + TABLE_PARAS + (unsigned)(i >> 4), (unsigned)i & 15);
		snd_log_at(t_begin * 800 + pc * 10 / 249); /* PIT counts -> 1/48000 s */
		sn_out(tb[v]); sn_out(tb[256 + v]); sn_out(tb[512 + v]);
		if (v < lo) lo = v;
		if (v > hi) hi = v;
		pc += c->div;
		while (T - t_begin < pc / 19886) { /* 19886 PIT counts a tick */
			T++;
			if (T % 4 == 0) {
				if (mouth) { mouth(hi > lo ? (hi - lo) >> 3 : 0); lo = 255; hi = 0; }
				if (say_hook) say_hook();
			}
		}
	}
	snd_log_at(0);
	chip_voices(0);
	Tpause += T - t_begin;
}

int voice_say(const char *name, void (*mouth)(int level))
{
	clip *c = 0;
	int i, was;
	unsigned long t_begin = T;
	for (i = 0; i < (int)NCLIPS; i++) if (!strcmp(clips[i].name, name)) c = &clips[i];
	if (mus_mute || !c || !c->seg || !player) return 0;
	was = mus_pause();
	if (lockstep) {
		if (say_hook) play_log(c, mouth);
	} else {
		if (mouth) mouth(10); /* a talking pose: nothing moves while it plays */
		speak(c);
	}
	if (mouth) mouth(0);
	mus_resume(was, T - t_begin);
	return 1;
}
