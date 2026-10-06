/* Spoken lines: NAME.SND clips (made by mkvoice.sh), played by SAY.EXE.
 *
 * SAY (speech/) is the player proven on the real PCjr: the three SN76496
 * tone voices at period 1 as one DAC, paced by the timer with every other
 * interrupt off. So JRAI runs it for each line (SAY /Q NAME) with the music
 * paused. While SAY plays, the picture holds still: the retrace interrupt
 * is off too, so nothing flips and no palette turns. Then the music picks
 * up where it stopped.
 *
 * In the frame-locked /VIDEO mode there is no real time to play in, so
 * voice.c reads the clip itself (SAY's format: header, text, three
 * 256-byte tables of volume writes, one byte per sample), logs the writes
 * it stands for, and moves T on as if it had played.
 */
#include <dos.h>
#include <conio.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include "jrai.h"

#pragma off (check_stack)

static unsigned vbuf;               /* 64 KB for one clip */
unsigned long Tpause;               /* ticks spent speaking, in all */
void (*say_hook)(void);

int say_h; /* /SAYH: SAY times with the interrupt (DOSBox lacks the 8259 poll) */

void voice_init(void)
{
	if (lockstep && !vbuf) vbuf = jr_alloc_high(0x1000);
}

/* The real thing: SAY.EXE /Q NAME. 0 if SAY or the clip isn't there. */
static int say_exe(const char *name, const char *fn, void (*mouth)(int level))
{
	const char *argv[5];
	int n = 0, rc, was;
	dos_begin(); /* no retrace interrupt while DOS works (see jrai.h) */
	if (access(fn, 0) || access("SAY.EXE", 0)) {
		if (opt_diag) dbg_log("say %s: no clip or no SAY.EXE here", name);
		dos_end();
		return 0;
	}
	argv[n++] = "SAY.EXE";
	if (say_h) argv[n++] = "/H";
	argv[n++] = "/Q";
	argv[n++] = name;
	argv[n] = 0;
	dos_end();
	if (mouth) mouth(10); /* a talking pose: nothing moves while SAY plays */
	was = mus_pause();
	dos_begin();
	if (opt_diag) dbg_log("say %s: starting SAY", name);
	rc = spawnv(P_WAIT, "SAY.EXE", argv);
	if (opt_diag) dbg_log("say %s: SAY returned %d", name, rc);
	/* SAY leaves the timer in the BIOS's mode 3; the library wants mode 2 */
	_disable();
	outp(0x43, 0x34); outp(0x40, 0); outp(0x40, 0);
	_enable();
	dos_end();
	if (mouth) mouth(0);
	mus_resume(was, 0);
	return rc == 0;
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

int voice_say(const char *name, void (*mouth)(int level))
{
	char fn[16];
	unsigned char h[16];
	int fd;
	unsigned long n, size;
	unsigned div, tl, tbl, o, got, tab, smp;
	unsigned long t_begin = T;
	int was;
	if (mus_mute) return 0;
	sprintf(fn, "%s.SND", name);
	if (!lockstep) return say_exe(name, fn, mouth);
	if (!vbuf) return 0;
	if (_dos_open(fn, O_RDONLY, &fd)) return 0; /* (/VIDEO only: DOSBox, no need for dos_begin) */
	if (_dos_read(fd, h, 16, &got) || got != 16 || memcmp(h, "JRSN", 4) || h[4] != 1 || h[5] != 1) {
		_dos_close(fd);
		return 0;
	}
	div = h[6] | (h[7] << 8);
	n = h[8] | ((unsigned long)h[9] << 8) | ((unsigned long)h[10] << 16) | ((unsigned long)h[11] << 24);
	tl = h[12] | (h[13] << 8);
	tbl = h[14] | (h[15] << 8);
	/* load the file where its tables start on a 256-byte boundary */
	o = (256 - ((16 + tl) & 255)) & 255;
	size = 16 + tl + tbl + n;
	if (tbl != 768 || size + o > 65535UL) { _dos_close(fd); return 0; }
	lseek(fd, 0, SEEK_SET);
	if (_dos_read(fd, MK_FP(vbuf, o), (unsigned)size, &got) || got != size) { _dos_close(fd); return 0; }
	_dos_close(fd);
	tab = o + 16 + tl;
	smp = tab + 768;

	if (say_hook) dbg_log("say %s T %lu n %lu div %u", name, T, n, div); /* /VIDEO: for checking the soundtrack */
	was = mus_pause();
	{
		/* no real time: log the writes for the video, move T, show the mouth */
		unsigned long i, pc = 0;
		const unsigned char far *s = (const unsigned char far *)MK_FP(vbuf, smp);
		const unsigned char far *tb = (const unsigned char far *)MK_FP(vbuf, tab);
		int lo = 255, hi = 0;
		chip_voices(1);
		for (i = 0; i < n; i++) {
			unsigned char v = s[i];
			snd_log_at(t_begin * 800 + pc * 10 / 249); /* PIT counts -> 1/48000 s */
			sn_out(tb[v]); sn_out(tb[256 + v]); sn_out(tb[512 + v]);
			if (v < lo) lo = v;
			if (v > hi) hi = v;
			pc += div;
			while (T - t_begin < pc / 19886) { /* 19886 PIT counts a tick */
				T++;
				if (T % 4 == 0) {
					if (mouth) { mouth(hi > lo ? (hi - lo) >> 3 : 0); lo = 255; hi = 0; }
					if (say_hook) say_hook();
				}
			}
		}
		snd_log_at(0);
	}
	chip_voices(0);
	if (mouth) mouth(0);
	Tpause += T - t_begin;
	mus_resume(was, T - t_begin);
	return 1;
}
