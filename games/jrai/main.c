/* JRAI main: options, the timeline, and the frame loop.
 *
 * Options (for testing; plain JRAI just runs the demo):
 *   /PART:n    start at part n (0 plasma, 1 fire, 2 tunnel, 3 meeting, 4 finale)
 *   /ONLY      play just that part
 *   /NOBOOT    skip the text-mode intro (the precompute still runs)
 *   /QUIET     no sound
 *   /LOCK[:n]  frame-locked: T advances n ticks (default 4) a frame, no music
 *              or speech, so screenshots don't depend on emulator speed
 *   /SHOT:a,b  screenshots (SHOTnn.PPM) at these T values (60 Hz ticks)
 *   /BENCH     log frames per second per part to LOG.TXT
 * Keys: Esc quits, Space skips to the next part.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "jrai.h"

int opt_bench;
static int opt_part, opt_only, opt_noboot, lockn = 4;
static unsigned long shots[32];
static int nshots, shot_i;

static const part *const parts[] = { &part_plasma, &part_fire, &part_tunnel, &part_meet, &part_scroll };
#define NPARTS 5

static void prep_step(int i)
{
	if (parts[i]->prep) parts[i]->prep();
}

static void args(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		strupr(a);
		if (!strncmp(a, "/PART:", 6)) opt_part = atoi(a + 6);
		else if (!strcmp(a, "/ONLY")) opt_only = 1;
		else if (!strcmp(a, "/NOBOOT")) opt_noboot = 1;
		else if (!strcmp(a, "/QUIET")) mus_mute = 1;
		else if (!strcmp(a, "/BENCH")) opt_bench = 1;
		else if (!strncmp(a, "/LOCK", 5)) { lockstep = 1; if (a[5] == ':') lockn = atoi(a + 6); }
		else if (!strncmp(a, "/SHOT:", 6)) {
			char *p = a + 6;
			while (*p && nshots < 32) {
				shots[nshots++] = strtoul(p, &p, 10);
				if (*p == ',') p++;
			}
		}
	}
	if (opt_part < 0 || opt_part >= NPARTS) opt_part = 0;
}

/* After each frame: flip, advance lockstep time, screenshots. */
static void frame_end(void)
{
	fx_flip();
	if (lockstep) T += lockn;
	else clock_poll();
	if (shot_i < nshots && T >= shots[shot_i]) {
		char name[16];
		fx_begin();
		sprintf(name, "SHOT%02d.PPM", shot_i + 1);
		fx_screenshot(name);
		shot_i++;
	}
}

static int quit;

static int run_part(const part *p)
{
	unsigned long t0, tend;
	unsigned frames = 0;
	unsigned long ms0;
	int skip = 0;
	if (p->enter) p->enter();
	if (mus_at() != p->ord) mus_start(p->ord);
	t0 = T;
	tend = t0 + (unsigned long)p->npat * PAT_TICKS;
	ms0 = tmr_ms();
	while (T < tend) {
		p->frame((unsigned)(T - t0));
		frame_end();
		frames++;
		if (key_down[KEY_ESC]) { quit = 1; break; }
		if (key_down[KEY_SPACE]) { while (key_down[KEY_SPACE]) ; skip = 1; break; }
		if (lockstep && shot_i >= nshots && nshots) { quit = 1; break; }
	}
	if (opt_bench) {
		unsigned long ms = tmr_ms() - ms0;
		dbg_log("%s: %u frames in %lu ms = %lu.%lu fps", p->name, frames, ms,
			ms ? frames * 1000UL / ms : 0, ms ? frames * 10000UL / ms % 10 : 0);
	}
	if (p->leave) p->leave();
	return skip;
}

int main(int argc, char **argv)
{
	int i;
	args(argc, argv);
	tmr_init();
	mus_init();
	if (!opt_noboot) boot_run(prep_step, NPARTS);
	else for (i = 0; i < NPARTS; i++) prep_step(i);
	if (fx_init(0)) {
		printf("JRAI: can't start 160x200 graphics (error %d)\n", gfx_err);
		return 1;
	}
	key_init();
	pages_find();
	clock_start();
	for (i = opt_part; i < NPARTS && !quit; i++) {
		run_part(parts[i]);
		if (opt_only) break;
	}
	clock_stop();
	key_done();
	gfx_done();
	snd_done();
	if (!quit) {
		printf("\nPCjr MEETS AI\n\n");
		printf("Made by CodeJr: Claude, talking to a 1984 IBM PCjr\n");
		printf("over a WiFi modem at 9600 baud.\n\n");
		printf("8088 at 4.77 MHz. 16 colors. 3 voices and a noise channel.\n");
		printf("Still got it.\n\n");
	}
	return 0;
}
