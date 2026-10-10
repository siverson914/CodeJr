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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "jrgfx.h"

#define NEV 1500

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
	unsigned stuck = 0;
	FILE *f;

	if (argc > 1 && atoi(argv[1]) > 0 && atoi(argv[1]) <= 300)
		secs = (unsigned)atoi(argv[1]);
	pcjr = *(unsigned char far *)MK_FP(0xF000, 0xFFFE) == 0xFD;
	key_heal = 0;
	key_init();
	tmr_init(); /* the timer counts down evenly inside a tick from here on */
	old9 = _dos_getvect(9);
	_dos_setvect(9, kt_isr);

	printf("KEYTEST: what the keyboard sends.\n\n");
	printf("For %u seconds: mash LEFT and RIGHT as in a game. Roll from one to\n", secs);
	printf("the other, hold one and tap the other, let go of both at once.\n");
	printf("Esc ends it early.\n\n");
	t0 = *bios;
	while (!esc && *bios - t0 < (unsigned long)secs * 182UL / 10UL) {
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

	f = fopen("LOG.TXT", "w");
	if (!f) {
		printf("\nCan't write LOG.TXT\n");
		return 1;
	}
	fprintf(f, "KEYTEST %s, %u codes (%u not recorded), %u presses, %u releases\n",
	        pcjr ? "on a PCjr" : "not on a PCjr", ev_n, n_lost, n_make, n_break);
	fprintf(f, "press while down: %u   release while up: %u\n", n_remake, n_rebreak);
	memset(st, 0, sizeof st);
	base = ev_n ? ev_tick[0] : 0;
	for (i = 0; i < ev_n; i++) {
		unsigned char k = ev_sc[i] & 0x7F, up = ev_sc[i] & 0x80;
		const char *note = "";
		t = ms_of(ev_tick[i] - base, ev_pit[i]);
		gap = i ? t - prev : 0;
		if (i && gap < mingap) mingap = gap;
		if (!up && st[k]) note = "  <-- press while already down";
		if (up && !st[k]) note = "  <-- release while up";
		st[k] = (unsigned char)!up;
		fprintf(f, "%7lu ms  +%5lu  %-5s %s%s\n", t, gap, name(k), up ? "up" : "DOWN", note);
		prev = t;
	}
	for (i = 0; i < 128; i++)
		if (st[i]) {
			fprintf(f, "still down at the end: %s\n", name((unsigned char)i));
			stuck++;
		}
	fprintf(f, "shortest time between two codes: %lu ms\n", ev_n > 1 ? mingap : 0UL);
	fprintf(f, "KEYTEST done: %u odd presses, %u odd releases, %u keys left down\n", n_remake, n_rebreak, stuck);
	fclose(f);
	printf("\n\nDone. %u codes, %u odd presses, %u odd releases, %u keys left down.\n", ev_n, n_remake, n_rebreak, stuck);
	printf("The details are in LOG.TXT.\n");
	return 0;
}
