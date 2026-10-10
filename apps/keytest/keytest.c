/* KEYTEST: what does the PCjr keyboard really send?
 *
 * Left and right "get stuck" in games. The theory (2026-10-09) is that a
 * key's release code is lost when two codes arrive close together. This
 * records every scan code INT 9 sees, with its time, while someone mashes
 * the arrow keys, and counts what can't happen on a keyboard that loses
 * nothing: a press of a key that is already down (a lost release, or the
 * keyboard repeating), a release of a key that is up (a lost press).
 * Everything goes to LOG.TXT. Esc ends it; it ends by itself after 45
 * seconds (KEYTEST n: after n seconds).
 *
 * KEYTEST G [n] does the same the way a game runs: graphics with page
 * flipping on the retrace interrupt, something moving, and music from
 * the library's player. The first run (text mode, 2026-10-09) lost
 * nothing in 400 codes, so if keys stick in games, something a game does
 * is part of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "jrgfx.h"
#include "jrmusic.h"

static const char *const lead[] = { "E5:4 B4:2 C5:2 D5:4 C5:2 B4:2 | A4:4 A4:2 C5:2 E5:4 D5:2 C5:2", 0 };
static const char *const bass[] = { "E3 E4 E3 E4 E3 E4 E3 E4 E3 E4 E3 E4 E3 E4 E3 E4 | A2 A3 A2 A3 A2 A3 A2 A3 A2 A3 A2 A3 A2 A3 A2 A3", 0 };
static const char *const drums[] = { "k.h.s.h.k.h.s.h.k.h.s.h.k.hks.ss", 0 };
static const mus_song song = { 6, 32, { lead, bass, 0, drums } };

#define NEV 1500

/* Frame timing in game mode: ms spent in each part, summed and the worst. */
static unsigned long ph_sum[6], ph_max[6];
static const char *const ph_name[6] = { "begin", "clear", "text", "rects", "flip", "music" };
#define PHASE(n) do { tr = tmr_ms(); if (tr - tq > ph_max[n]) ph_max[n] = tr - tq; \
                      ph_sum[n] += tr - tq; tq = tr; } while (0)

static volatile unsigned ev_n;
static unsigned char ev_sc[NEV];
static unsigned ev_tick[NEV];  /* BIOS ticks (18.2 a second), low word */
static unsigned ev_pit[NEV];   /* timer count inside the tick, falling */
static volatile unsigned char down[128];
static volatile unsigned n_make, n_break, n_remake, n_rebreak, n_lost;
static volatile unsigned char esc;
static void (__interrupt __far *old9)(void);
static int pcjr;

static void __interrupt __far kt_isr(union INTPACK r)
{
	unsigned char sc = pcjr ? r.h.al : (unsigned char)inp(0x60);
	unsigned char k = sc & 0x7F;
	unsigned lo, hi;

	if (sc != 0xE0 && sc != 0xE1) {
		if (ev_n < NEV) {
			outp(0x43, 0x00); /* latch channel 0 */
			lo = inp(0x40);
			hi = inp(0x40);
			ev_sc[ev_n] = sc;
			ev_pit[ev_n] = hi << 8 | lo;
			ev_tick[ev_n] = *(unsigned far *)MK_FP(0x40, 0x6C);
			ev_n++;
		} else {
			n_lost++;
		}
		if (sc & 0x80) {
			n_break++;
			if (!down[k]) n_rebreak++;
			down[k] = 0;
		} else {
			n_make++;
			if (down[k]) n_remake++;
			down[k] = 1;
			if (k == KEY_ESC) esc = 1;
		}
	}
	_chain_intr(old9);
}

static const char *name(unsigned char k)
{
	static char b[8];
	switch (k) {
	case KEY_LEFT: return "LEFT";
	case KEY_RIGHT: return "RIGHT";
	case KEY_UP: return "UP";
	case KEY_DOWN: return "DOWN";
	case KEY_SPACE: return "SPACE";
	case KEY_ESC: return "ESC";
	case KEY_ENTER: return "ENTER";
	}
	sprintf(b, "%02X", k); /* 2 digits */
	return b;
}

/* The library's own keyboard hook (key_init) sees the same codes as our
 * ISR, which chains to it. If its key_down[] ever disagrees with ours for
 * longer than a moment, the library's hook is wrong on this machine. */
#define NDIS 40
static struct { unsigned long t, len; unsigned char k, lib; } dis[NDIS];
static unsigned ndis, ndis_lost;
static unsigned long dis_t0[2];
static unsigned char dis_on[2], dis_lib[2];

static void cmp_keys(void)
{
    static const unsigned char ks[2] = { KEY_LEFT, KEY_RIGHT };
    unsigned i;
    unsigned long now = tmr_ms();
    for (i = 0; i < 2; i++) {
        int bad = key_down[ks[i]] != down[ks[i]];
        if (bad && !dis_on[i]) { dis_on[i] = 1; dis_t0[i] = now; dis_lib[i] = key_down[ks[i]]; }
        else if (!bad && dis_on[i]) {
            dis_on[i] = 0;
            if (now - dis_t0[i] >= 30) {
                if (ndis < NDIS) {
                    dis[ndis].t = dis_t0[i]; dis[ndis].len = now - dis_t0[i];
                    dis[ndis].k = ks[i]; dis[ndis].lib = dis_lib[i];
                    ndis++;
                } else
                    ndis_lost++;
            }
        }
    }
}

/* Milliseconds from a tick count and the timer's place inside the tick. */
static unsigned long ms_of(unsigned tick, unsigned pit)
{
	return (unsigned long)tick * 55UL + (((unsigned long)(unsigned)(0u - pit)) * 55UL >> 16);
}

int main(int argc, char **argv)
{
	unsigned long t0, t, prev = 0, gap, mingap = 99999UL;
	unsigned secs = 45, i, shown = 0xFFFF, base;
	unsigned char st[128];
	unsigned long far *bios = (unsigned long far *)MK_FP(0x40, 0x6C);
	unsigned stuck = 0, frames = 0;
	unsigned n_rep = 0, n_lostrel = 0, n_silent = 0, n_lostpr = 0;
	unsigned char newest = 0, newest_rep = 0, newest_dn = 0;
	unsigned long lastmk = 0;
	int game = 0, a, x = 0, music = -1, fx_irq0 = -1;
	unsigned long vbls = 0;
	FILE *f;

	for (a = 1; a < argc; a++) {
		if (argv[a][0] == 'G' || argv[a][0] == 'g')
			game = 1;
		else if (atoi(argv[a]) > 0 && atoi(argv[a]) <= 300)
			secs = (unsigned)atoi(argv[a]);
	}
	pcjr = *(unsigned char far *)MK_FP(0xF000, 0xFFFE) == 0xFD;
	/* Graphics first, as the games do it (Junris: fx_init, then key_init,
	 * tmr_init). Run 2 (2026-10-10) had them the other way round and the
	 * retrace interrupt came up off. */
	if (game && fx_init(0) != 0) {
		printf("KEYTEST: no graphics (error %d); the plain test instead.\n", gfx_err);
		game = 0;
	}
	key_init();
	tmr_init(); /* the timer counts down evenly inside a tick from here on */
	old9 = _dos_getvect(9);
	_dos_setvect(9, kt_isr);

	if (game) {
		/* Run 2 managed 338 frames in 43 s (128 ms a frame) and the LEFT/
		 * RIGHT boxes lagged behind the fingers. Time each part of a frame. */
		unsigned long tq, tr, vbl0;
		memset(ph_sum, 0, sizeof ph_sum);
		memset(ph_max, 0, sizeof ph_max);
		music = mus_start(&song, 0);
		fx_irq0 = fx_irq;
		vbl0 = fx_vbl;
		t0 = *bios;
		while (!esc && *bios - t0 < (unsigned long)secs * 182UL / 10UL) {
			tq = tmr_ms();
			fx_begin();                       PHASE(0);
			fx_clear(0x1111);                 PHASE(1);
			fx_text(4, 8, "MASH LEFT AND RIGHT", 15);
			fx_text(4, 20, "ESC ENDS", 7);    PHASE(2);
			fx_fill_rect(x, 60, 24, 100, 0xCCCC);
			fx_fill_rect(159 - 24 - x, 100, 24, 60, 0xAAAA);
			if (down[KEY_LEFT]) fx_fill_rect(10, 170, 60, 20, 0xEEEE);
			if (down[KEY_RIGHT]) fx_fill_rect(90, 170, 60, 20, 0xEEEE);
			PHASE(3);
			fx_flip();                        PHASE(4);
			mus_poll();                       PHASE(5);
			cmp_keys();
			x = (x + 3) % 136;
			frames++;
		}
		vbls = fx_vbl - vbl0;
		mus_stop();
		_dos_setvect(9, old9);
		key_done();
		tmr_done();
		gfx_done();
		goto report;
	}
	printf("KEYTEST: what the keyboard sends.\n\n");
	printf("For %u seconds: mash LEFT and RIGHT as in a game. Roll from one to\n", secs);
	printf("the other, hold one and tap the other, let go of both at once.\n");
	printf("Esc ends it early.\n\n");
	t0 = *bios;
	while (!esc && *bios - t0 < (unsigned long)secs * 182UL / 10UL) {
		cmp_keys();
		if (ev_n != shown) {
			shown = ev_n;
			printf("\rcodes %4u  odd presses %3u  odd releases %3u  %s%s ",
			       shown, n_remake, n_rebreak,
			       down[KEY_LEFT] ? "LEFT " : "     ", down[KEY_RIGHT] ? "RIGHT" : "     ");
		}
	}
	_dos_setvect(9, old9);
	key_done();
	tmr_done();

report:
	f = fopen("LOG.TXT", "w");
	if (!f) {
		printf("\nCan't write LOG.TXT\n");
		return 1;
	}
	fprintf(f, "KEYTEST %s, %u codes (%u not recorded), %u presses, %u releases\n",
	        pcjr ? "on a PCjr" : "not on a PCjr", ev_n, n_lost, n_make, n_break);
	fprintf(f, "press while down: %u   release while up: %u\n", n_remake, n_rebreak);
	if (game) {
		unsigned long tot = 0;
		fprintf(f, "game conditions: %u frames, retrace interrupt %d (at start %d), %lu retraces counted, pages %d, music start %d\n",
		        frames, fx_irq, fx_irq0, vbls, fx_pages, music);
		for (i = 0; i < 6; i++) tot += ph_sum[i];
		fprintf(f, "ms per frame: %lu on average over %u frames; by part (average / worst):",
		        frames ? tot / frames : 0UL, frames);
		for (i = 0; i < 6; i++)
			fprintf(f, " %s %lu/%lu", ph_name[i], frames ? ph_sum[i] / frames : 0UL, ph_max[i]);
		fprintf(f, "\n");
	}
	/* Verdicts. The first run showed how the keyboard repeats: only the
	 * newest key pressed repeats (after 0.6 s, then every 96 ms); a new
	 * press moves the repeat to that key; letting the newest key go stops
	 * all repeats; letting any other key go changes nothing. So a press of
	 * a key we have down is a repeat only if it is the newest key; any
	 * other is a key the keyboard had as up: its release was lost. And if
	 * the newest key was repeating and falls silent with no release, that
	 * release was lost too. */
	memset(st, 0, sizeof st);
	base = ev_n ? ev_tick[0] : 0;
	for (i = 0; i < ev_n; i++) {
		unsigned char k = ev_sc[i] & 0x7F, up = ev_sc[i] & 0x80;
		const char *note = "";
		t = ms_of(ev_tick[i] - base, ev_pit[i]);
		gap = i ? t - prev : 0;
		if (i && (long)gap < 0) gap = 0; /* a timer read glitch */
		if (i && gap < mingap) mingap = gap;
		if (newest_dn && newest_rep && k != newest && t - lastmk > 300UL) {
			fprintf(f, "            %s stopped repeating with no release  <-- LOST RELEASE\n", name(newest));
			n_silent++;
			newest_dn = 0;
		}
		if (!up) {
			if (st[k]) {
				if (k == newest && newest_dn) {
					note = "  repeat";
					newest_rep = 1;
					n_rep++;
				} else {
					note = "  <-- LOST RELEASE (pressed, but we had it down)";
					n_lostrel++;
				}
			}
			if (k != newest || !st[k]) {
				newest = k;
				newest_rep = 0;
			}
			newest_dn = 1;
			lastmk = t;
		} else {
			if (!st[k]) {
				note = "  <-- LOST PRESS (released, but we had it up)";
				n_lostpr++;
			}
			if (k == newest) newest_dn = 0;
		}
		st[k] = (unsigned char)!up;
		fprintf(f, "%7lu ms  +%5lu  %-5s %s%s\n", t, gap, name(k), up ? "up" : "DOWN", note);
		prev = t;
	}
	for (i = 0; i < 128; i++)
		if (st[i]) {
			fprintf(f, "still down at the end: %s%s\n", name((unsigned char)i),
			        i == newest && newest_dn ? " (the newest key: may be really held)" : "");
			stuck++;
		}
	fprintf(f, "shortest time between two codes: %lu ms\n", ev_n > 1 ? mingap : 0UL);
	fprintf(f, "library key_down[] disagreed with the raw codes %u times (30 ms or longer)%s\n",
	        ndis + ndis_lost, ndis_lost ? ", not all listed" : "");
	for (i = 0; i < ndis; i++)
		fprintf(f, "   at %lu ms (timer clock): %s library %s, keyboard %s, for %lu ms\n",
		        dis[i].t, name(dis[i].k), dis[i].lib ? "DOWN" : "up",
		        dis[i].lib ? "up" : "DOWN", dis[i].len);
	fprintf(f, "KEYTEST done: %u repeats, %u lost releases (%u seen by a new press,"
	           " %u by silence), %u lost presses, %u keys left down\n",
	        n_rep, n_lostrel + n_silent, n_lostrel, n_silent, n_lostpr, stuck);
	fclose(f);
	printf("\n\nDone. %u codes, %u repeats, %u lost releases, %u lost presses, %u keys left down.\n",
	       ev_n, n_rep, n_lostrel + n_silent, n_lostpr, stuck);
	printf("The details are in LOG.TXT.\n");
	return 0;
}
