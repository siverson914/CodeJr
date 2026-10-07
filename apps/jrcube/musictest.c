/* MUSTEST: plays the JRCUBE song on its own.
 *   MUSTEST /LOG:n   n ticks in lockstep, every register write to SND.BIN,
 *                    the drum flags per pattern to stdout
 *   MUSTEST [n]      live from the retrace interrupt for n seconds (30),
 *                    any key stops it
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "jrgfx.h"
#include "music.h"

static unsigned char nk[SONG_PATS], ns[SONG_PATS], nc[SONG_PATS];

static void show(const char *what)
{
	char s[40];
	unsigned long p = mus_T / PAT_TICKS;
	fx_clear(FX_SOLID2(0));
	sprintf(s, "%s PAT %2lu T %5lu", what, p % SONG_PATS, mus_T);
	fx_text(4, 92, s, 15);
	fx_flip();
}

int main(int argc, char **argv)
{
	long logn = -1, secs = 30;
	int i;
	for (i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "/LOG:", 5) || !strncmp(argv[i], "/log:", 5)) logn = atol(argv[i] + 5);
		else secs = atol(argv[i]);
	}
	mus_init();
	if (fx_init(0)) { printf("fx_init failed\n"); return 1; }
	if (logn >= 0) {
		long t;
		mus_lockstep(1);
		mus_start(0);
		mus_log_open();
		for (t = 0; t < logn; t++) {
			unsigned p = (unsigned)((mus_T / PAT_TICKS) % SONG_PATS);
			if (mus_T % PAT_TICKS == 0) show("LOG");
			mus_step(1);
			if (mus_kick) { nk[p]++; mus_kick = 0; }
			if (mus_snare) { ns[p]++; mus_snare = 0; }
			if (mus_crash) { nc[p]++; mus_crash = 0; }
		}
		mus_log_close();
		mus_stop();
		gfx_done();
		printf("fx_irq %d, %ld ticks logged\npat kick snare crash\n", fx_irq, logn);
		for (i = 0; i < SONG_PATS; i++) printf("%2d %3u %3u %3u\n", i, nk[i], ns[i], nc[i]);
	} else {
		unsigned long end = (unsigned long)secs * 60UL, last = ~0UL;
		mus_start(0);
		while (mus_T < end && !kbhit()) {
			mus_poll();
			if (mus_T / 15 != last) { last = mus_T / 15; show(fx_irq ? "IRQ" : "PIT"); }
		}
		if (kbhit()) getch();
		mus_stop();
		gfx_done();
		printf("fx_irq %d, played %lu ticks\n", fx_irq, mus_T);
	}
	return 0;
}
