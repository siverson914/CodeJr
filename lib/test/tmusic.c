/* TMUSIC: the music player (mus_*), headless. Plays a short song with the
 * retrace interrupt, with the game's own hook (MUS_NOHOOK), from the timer
 * (mus_poll) and tick by tick, and checks what was sent to the sound chip.
 * Logs PASS/FAIL lines to LOG.TXT, the last one "TMUSIC done: N failures",
 * written at exit after the library's own cleanup has run. Also writes
 * SND.BIN and SND2.BIN, the chip writes of two tick-by-tick runs
 * (apps/jrai/render.py turns one into a WAV).
 *
 * The emulator's sound chip can't be read back. So the test runs the
 * player with the processor's single-step trap on: the trap handler sees
 * every instruction before it runs, records the byte of every OUT to port
 * C0h, and counts the instructions of a tick while it is at it (exact,
 * and the same on any 8088).
 *
 * jrmusic.c is included, not linked, to look at the player's own state
 * (the linker then leaves the library's copy alone). MUS_TRACE only adds
 * a count of the notes started.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#define MUS_TRACE
#include "../jrmusic.c"

/* ---- the test song: 2 patterns of 16 rows, 6 ticks a row (3.2 s) -------- */
static const char *const lead[] = {
	"E5 . A5 . | . . G5 . | A5 . . . | C6 . B5 .",
	"C6:4 G5:4 D6:4 B5:2 G5:2",
	0
};
static const char *const bass[] = {
	"A2 . A3 . A2 . A3 . F3 . F4 . F3 . F4 .",
	"C3:2 C4:2 C3:2 C4:2 G3:2 G4:2 G3:2 G4:2",
	0
};
static const char *const chord[] = { "A3m:8 F3M:8", "C4M:8 G3M:8", 0 };
static const char *const drums[] = { "k.h.S.h.k.h.S.h.", "k.h.S.h.k.hkS.ss", 0 };
static const mus_inst inst[] = {
	{ 13, 10, 2, 0, 2, 0 },   /* 0 lead: pluck, vibrato */
	{ 15,  9, 1, 0, 0, 0 },   /* 1 bass */
	{ 11,  9, 2, 0, 0, 1 },   /* 2 fast arpeggio */
	{  8,  8, 0, 0, 0, 0 },   /* 3 a plain held tone */
};
static const mus_step order[] = {
	{ { 0, 0, 0, 0 }, { 0, 1, 2 }, { 0, 0, 0 }, 0 },
	{ { 1, 1, 1, 1 }, { 0, 1, 2 }, { 0, 0, 0 }, 0 },
};
static const mus_song song_once = { 6, 16, { lead, bass, chord, drums }, order, MUS_COUNT(order), MUS_ONCE, inst, MUS_COUNT(inst), 0 };
#define SONG_TICKS 192

/* The most instructions one tick of the test song may take (test_steps):
 * the worst measured is about 350; this catches a change that makes the
 * tick much dearer. */
#ifdef MUS_C_TICK
#define COST_MAX 600   /* the C version of the tick is only the reference */
#else
#define COST_MAX 400
#endif

static int fails;

static void check(int ok, const char *what)
{
	if (!ok) { fails++; dbg_log("FAIL %s", what); }
	else dbg_log("PASS %s", what);
}

static const mus_song song = { 6, 16, { lead, bass, chord, drums }, order, MUS_COUNT(order), 0, inst, MUS_COUNT(inst), 0 };
/* notes in the two patterns together, per voice */
static const unsigned want_notes[4] = { 11, 16, 4, 18 };

/* one long C4 on voice 2, for the effect to borrow */
static const char *const held[] = { "C4:16", 0 };
static const mus_step held_order[] = { { { 0, 0, 0, 0 }, { 0, 1, 3 }, { 0, 0, 0 }, 0 } };
static const mus_song song_held = { 6, 16, { 0, 0, held, 0 }, held_order, 1, 0, inst, MUS_COUNT(inst), 0 };

static const mus_fx blip[] = { { MUS_HZ(1760), 12, 6, 0 }, { 0, 0, 0, 0 } };
static const mus_fx thud[] = { { 6, 10, 3, 0 }, { 0, 0, 0, 0 } };
static const mus_fx zap[] = { { MUS_HZ(440), 14, 8, -20 }, { 0, 0, 0, 0 } };

/* songs that must not compile */
static const char *const bad_syn[] = { "E5 . H4", 0 };
static const char *const bad_long[] = { "E5:8 A5:8 C6", 0 };
static const char *const bad_low[] = { "A2 G#2", 0 };
static const char *const bad_len[] = { "E5:0", 0 };
static const mus_step bad_order[] = { { { 2, 0, 0, 0 }, { 0, 1, 2 }, { 0, 0, 0 }, 0 } };

/* ---- single-step trap: chip bytes and instruction counts ---------------- */
#define TRN 6000
static unsigned char __far tr_b[TRN];   /* every byte sent to the chip, in order */
static unsigned __far tr_t[TRN];        /* and the tick it went out on           */
static volatile unsigned tr_n;
static volatile unsigned long steps;    /* instructions run with the trap on     */
static volatile unsigned long nbytes;   /* and their length in bytes, roughly    */
static unsigned last_ip;

/* With the trap flag set the processor calls INT 1 after every
 * instruction; r is the state before the next one. */
static void __interrupt __far trap(union INTPACK r)
{
	unsigned char far *ip = (unsigned char far *)MK_FP(r.w.cs, r.w.ip);
	steps++;
	/* instruction bytes: the distance to the next instruction, or 3 for a jump taken */
	nbytes += (unsigned)(r.w.ip - last_ip) <= 6 ? (unsigned)(r.w.ip - last_ip) : 3;
	last_ip = r.w.ip;
	if ((ip[0] == 0xEE && r.w.dx == 0xC0) || (ip[0] == 0xE6 && ip[1] == 0xC0))   /* out dx,al / out 0C0h,al */
		if (tr_n < TRN) { tr_b[tr_n] = r.h.al; tr_t[tr_n] = tick_n; tr_n++; }
}
extern void tf_on(void);
#pragma aux tf_on = "pushf" "pop ax" "or ax,0100h" "push ax" "popf" modify [ax];
extern void tf_off(void);
#pragma aux tf_off = "pushf" "pop ax" "and ax,0FEFFh" "push ax" "popf" modify [ax];
static void nothing(void) { }
static void (__interrupt __far *old1)(void);

static void trace_reset(void)
{
	unsigned f = mus_ioff();
	tr_n = 0;
	tr_notes[0] = tr_notes[1] = tr_notes[2] = tr_notes[3] = 0;
	mus_ion(f);
}

/* Every divisor is a latch byte and a data byte, back to back: a data
 * byte anywhere else, or a latch without one, means two writers
 * interleaved. Returns the number of such places; *most = the most
 * bytes written in one tick. */
static unsigned trace_bad(unsigned *most)
{
	unsigned i, bad = 0, run = 0, n = tr_n;
	*most = 0;
	for (i = 0; i < n; i++) {
		unsigned char b = tr_b[i];
		int latch = b >= 0x80 && !(b & 0x10) && (b & 0x60) != 0x60; /* a tone divisor's first byte */
		if (b < 0x80) {
			unsigned char a = i ? tr_b[i - 1] : 0x90;
			if (!(a >= 0x80 && !(a & 0x10) && (a & 0x60) != 0x60)) bad++;
		} else if (latch && (i + 1 >= n || tr_b[i + 1] >= 0x80)) bad++;
		run = i && tr_t[i] == tr_t[i - 1] ? run + 1 : 1;
		if (run > *most) *most = run;
	}
	return bad;
}

/* The first divisor written to a voice in the trace; 0 if none. */
static unsigned trace_first_div(int v)
{
	unsigned i;
	for (i = 0; i + 1 < tr_n; i++)
		if ((tr_b[i] & 0xF0) == (0x80 | (v << 5))) return (tr_b[i] & 15) | ((unsigned)tr_b[i + 1] << 4);
	return 0;
}

/* The last volume byte written to a channel: 15 = silent; -1 if none. */
static int trace_last_att(int c)
{
	unsigned i = tr_n;
	while (i--)
		if ((tr_b[i] & 0xF0) == (0x90 | (c << 5))) return tr_b[i] & 15;
	return -1;
}

/* Waits until n more ticks have been played (polling as a game would);
 * 0 if they don't come within 3 seconds. */
static int wait_ticks(unsigned n)
{
	unsigned long t0 = mus_ticks(), ms0 = tmr_ms();
	while (mus_ticks() - t0 < n) {
		mus_poll();
		if ((long)(tmr_ms() - ms0) > 3000) return 0;
	}
	return 1;
}

static void wait_ms(unsigned ms, int poll)
{
	unsigned long t = tmr_ms();
	while ((long)(tmr_ms() - t) < (long)ms) if (poll) mus_poll();
}

static volatile unsigned my_n;
static void my_hook(void)   /* a game's own retrace hook, chaining the player */
{
	my_n++;
	tf_on();                /* the test's trace, not something a game does */
	mus_irq();
	tf_off();
}

/* Runs last of all (registered first): after mus_stop, snd_done, gfx_done.
 * main returned with the trap on, so the bytes mus_stop wrote on the way
 * out are in the trace. */
static void final(void)
{
	int c, ok = 1;
	tf_off();
	_dos_setvect(1, old1);
	for (c = 0; c < 4; c++) if (mus_level(c) || trace_last_att(c) != 15) ok = 0;
	dbg_log("at exit: playing %d, volumes %d %d %d %d, last volume byte per channel %d %d %d %d (15 = silent), %u bytes traced",
	        mus_playing(), mus_level(0), mus_level(1), mus_level(2), mus_level(3),
	        trace_last_att(0), trace_last_att(1), trace_last_att(2), trace_last_att(3), tr_n);
	check(ok && !engine && !cs.buf, "exit without mus_stop leaves the chip silent");
	dbg_log("TMUSIC done: %d failures", fails);
	printf("TMUSIC done: %d failures\n", fails);
}

static void test_table(void)
{
	static const unsigned hz100[12] = { 26163u, 27718u, 29366u, 31113u, 32963u, 34923u,
		36999u, 39200u, 41530u, 44000u, 46616u, 49388u }; /* C4..B4 x100 */
	int i, bad = 0;
	for (i = NOTE_LO; i < NNOTE; i++) {
		/* 3579545/32/hz, hz = hz100 * 2^(oct-4) / 100: a second way to the same number */
		unsigned long d = (unsigned long)hz100[i % 12] << (i / 12);
		long p = (long)((111861UL * 1600UL + d / 2) / d) - (long)mus_div[i];
		if (p < -1 || p > 1) { bad++; dbg_log("divisor of note %d: %u, reference %ld off", i, mus_div[i], p); }
	}
	dbg_log("divisors C4..B4: %u %u %u %u %u %u %u %u %u %u %u %u", mus_div[48], mus_div[49], mus_div[50], mus_div[51],
	        mus_div[52], mus_div[53], mus_div[54], mus_div[55], mus_div[56], mus_div[57], mus_div[58], mus_div[59]);
	dbg_log("A2 %u (%lu Hz)  A4 %u (%lu Hz)  B7 %u (%lu Hz)", mus_div[33], 111861UL / mus_div[33],
	        mus_div[57], 111861UL / mus_div[57], mus_div[95], 111861UL / mus_div[95]);
	check(!bad && mus_div[57] == 254 && mus_div[33] == 1017 && mus_div[48] == 428 && MUS_HZ(440) == 254,
	      "divisor table matches the integer reference (A4 = 254)");
}

static void test_compile(void)
{
	mus_song s;
	int e;
	memset(&s, 0, sizeof s);
	s.tempo = 6; s.rows = 16;
	s.pat[0] = bad_syn; e = mus_start(&s, 0);
	dbg_log("bad letter: error %d at voice %d pattern %d char %d", e, mus_err.voice, mus_err.pat, mus_err.pos);
	check(e == MUS_E_SYNTAX && mus_err.voice == 0 && mus_err.pat == 0 && mus_err.pos == 5, "syntax error found and located");
	s.pat[0] = bad_long; check(mus_start(&s, 0) == MUS_E_LONG, "pattern longer than rows refused");
	s.pat[0] = bad_low; e = mus_start(&s, 0);
	check(e == MUS_E_RANGE && mus_err.pos == 3, "note below A2 refused");
	s.pat[0] = bad_len; check(mus_start(&s, 0) == MUS_E_SYNTAX, "length 0 refused");
	s.pat[0] = lead; s.order = bad_order; s.norder = 1;
	check(mus_start(&s, 0) == MUS_E_SONG, "order naming a pattern that isn't there refused");
	s.order = 0; s.tempo = 0; check(mus_start(&s, 0) == MUS_E_SONG, "tempo 0 refused");
	check(!engine && !mus_playing(), "a refused song starts nothing");
}

/* The retrace interrupt drives the player through the library's hook. No
 * trace here (the hook is the player itself): the player's own record of
 * what it sent is checked; test_chain traces the same thing. */
static void test_irq(void)
{
	unsigned v0, dv;
	unsigned long t;
	int i, ok;

	trace_reset();
	v0 = fx_vbl;
	check(mus_start(&song, 0) == 0 && mode == M_IRQ && mus_playing(), "mus_start takes the retrace hook");
	wait_ms(1500, 0);            /* no mus_poll at all: the interrupt alone */
	t = mus_ticks(); dv = fx_vbl - v0;
	dbg_log("IRQ mode: 1500 ms, %lu ticks, %u retraces; notes started %u %u %u, drums %u; row %u of step %u",
	        t, dv, tr_notes[0], tr_notes[1], tr_notes[2], tr_notes[3], mus_row, mus_ord);
	check(t >= 84 && t <= 96 && (long)t - (long)dv >= -1 && (long)t - (long)dv <= 1, "one tick per retrace, about 90 in 1.5 s");
	check(tr_notes[0] >= 5 && tr_notes[1] >= 7 && tr_notes[2] >= 1 && tr_notes[3] >= 7, "notes started on every voice");
	dbg_log("row %u: lead divisor %u = %lu Hz (B5 is 988 Hz), bass %u = %lu Hz (F4 is 349 Hz), volumes %d %d %d %d",
	        mus_row, P.sh_per[0], 111861UL / P.sh_per[0], P.sh_per[1], 111861UL / P.sh_per[1],
	        mus_level(0), mus_level(1), mus_level(2), mus_level(3));
	check(mus_row == 14 ? P.sh_per[0] == mus_div[83] && P.sh_per[1] == mus_div[65] : mus_row == 15 || mus_row == 13,
	      "the notes of row 14 are on the chip");

	/* an effect borrows voice 2 and gives it back */
	mus_start(&song_held, 0);
	ok = wait_ticks(8);
	dbg_log("held note: voice 2 divisor %u volume %d", P.sh_per[2], mus_level(2));
	check(ok && P.sh_per[2] == mus_div[48] && mus_level(2) == 8 && !mus_sfx_busy(2), "song has voice 2");
	mus_sfx(2, blip);
	ok = wait_ticks(3);
	dbg_log("effect: busy %d, voice 2 divisor %u volume %d", mus_sfx_busy(2), P.sh_per[2], mus_level(2));
	check(ok && mus_sfx_busy(2) && P.sh_per[2] == MUS_HZ(1760) && mus_level(2) == 12, "effect borrowed voice 2");
	ok = wait_ticks(8);
	dbg_log("after: busy %d, voice 2 divisor %u volume %d", mus_sfx_busy(2), P.sh_per[2], mus_level(2));
	check(ok && !mus_sfx_busy(2) && P.sh_per[2] == mus_div[48] && mus_level(2) == 8, "song got voice 2 back");
	mus_sfx(MUS_NOISE, thud);
	ok = wait_ticks(2);
	check(ok && mus_sfx_busy(MUS_NOISE) && P.sh_noise == 6 && mus_level(3) == 10, "effect on the noise channel");
	ok = wait_ticks(4);
	check(ok && !mus_sfx_busy(MUS_NOISE) && mus_level(3) == 0, "noise channel given back, silent");
	mus_sfx(0, zap);
	ok = wait_ticks(5);
	dbg_log("slide: voice 0 divisor %u after 5 ticks (from %u, -20 a tick)", P.sh_per[0], MUS_HZ(440));
	check(ok && P.sh_per[0] <= MUS_HZ(440) - 60 && P.sh_per[0] >= MUS_HZ(440) - 100, "effect slides its pitch");
	wait_ticks(6);

	/* pause: silent at once, the song stands still, effects still play */
	mus_pause();
	i = mus_row;
	check(!mus_playing() && mus_level(2) == 0, "pause silences the song at once");
	mus_sfx(1, blip);
	ok = wait_ticks(3);
	check(ok && mus_level(1) == 12 && mus_level(2) == 0, "effects play while paused");
	wait_ticks(20);
	check(mus_row == (unsigned)i, "song stands still while paused");
	mus_resume();
	ok = wait_ticks(2);
	check(ok && mus_playing() && mus_level(2) == 8, "resume brings the note back");
	mus_volume(10);
	check(mus_level(2) == 3, "mus_volume(10) takes 5 steps off at once");
	mus_volume(15);

	/* the hook is lost (what fx_init does): mus_poll puts it back */
	fx_irq_hook(0);
	t = mus_ticks();
	wait_ms(100, 0);
	i = mus_ticks() == t;
	wait_ms(400, 1);
	dbg_log("hook removed: %lu ticks in 400 ms of mus_poll", mus_ticks() - t);
	check(i && mus_ticks() - t >= 10, "mus_poll re-installs a lost hook");

	/* mus_stop: silent, hook released */
	trace_reset();
	tf_on(); mus_stop(); tf_off();
	t = mus_ticks();
	wait_ms(100, 1);
	ok = 1;
	for (i = 0; i < 4; i++) if (mus_level(i) || trace_last_att(i) != 15) ok = 0;
	check(ok && !mus_playing() && mus_ticks() == t, "mus_stop silences the chip and stops ticking");
}

/* The game has its own hook and chains the player from it. Here the trap
 * is on inside the hook and around the main program's calls, so every
 * byte either of them sends is in the trace, in order. */
static void test_chain(void)
{
	unsigned n, most, bad, d;
	unsigned long t;
	int i;
	trace_reset();
	my_n = 0;
	fx_irq_hook(my_hook);
	check(mus_start(&song, MUS_NOHOOK) == 0 && mode == M_EXT, "MUS_NOHOOK leaves the hook alone");
	wait_ms(1000, 1);
	t = mus_ticks(); n = my_n;
	dbg_log("chained from the game's hook: 1000 ms, %lu ticks, hook ran %u times, notes %u %u %u, drums %u", t, n,
	        tr_notes[0], tr_notes[1], tr_notes[2], tr_notes[3]);
	check(t >= 55 && t <= 64 && (long)t - (long)n >= -1 && (long)t - (long)n <= 1 && tr_notes[0] >= 3, "one tick per call of the game's hook");
	d = trace_first_div(0);
	dbg_log("first lead divisor sent %u = %lu Hz (E5 is 659 Hz), bass %u = %lu Hz (A2 is 110 Hz)", d, d ? 111861UL / d : 0,
	        trace_first_div(1), trace_first_div(1) ? 111861UL / trace_first_div(1) : 0);
	check(d == 170 && trace_first_div(1) == 1017, "first notes have the right pitch");

	/* the main program pesters the player while the interrupt plays */
	t = tmr_ms(); i = 0;
	while ((long)(tmr_ms() - t) < 1500) {
		tf_on();
		mus_sfx(i & 1 ? 2 : MUS_NOISE, i & 1 ? blip : thud);
		mus_volume(i & 2 ? 15 : 11);
		if ((i & 7) == 7) { mus_pause(); mus_resume(); }
		tf_off();
		mus_poll();
		wait_ms(13, 0);
		i++;
	}
	mus_volume(15);
	bad = trace_bad(&most);
	dbg_log("with %d effect/volume/pause calls from the main program: %u bytes to the chip, %u split writes, at most %u bytes in a tick",
	        i * 2, tr_n, bad, most);
	check(tr_n > 300 && tr_n < TRN && bad == 0, "no divisor write split between interrupt and main program");
	mus_stop();
	n = my_n;
	wait_ms(100, 1);
	check(my_n != n, "mus_stop leaves the game's hook installed");
	fx_irq_hook(0);
}

/* The retrace interrupt goes away under a playing song. */
static void test_fallback(void)
{
	unsigned long t;
	mus_start(&song, 0);
	wait_ms(300, 1);
	fx_done();
	mus_poll();
	t = mus_ticks();
	wait_ms(1000, 1);
	dbg_log("after fx_done: mode %d, %lu ticks in 1000 ms", mode, mus_ticks() - t);
	check(mode == M_POLL && mus_ticks() - t >= 57 && mus_ticks() - t <= 61, "song carries on from the timer when the interrupt goes");
	mus_stop();
}

/* No retrace interrupt: mus_poll from the timer, with a game that stalls. */
static void test_poll(void)
{
	unsigned long t0, t;
	int i;
	trace_reset();
	check(mus_start(&song, 0) == 0 && mode == M_POLL, "without the interrupt mus_start chooses polling");
	mus_poll();                  /* starts the clock */
	t0 = tmr_ms();
	for (i = 0; i < 6; i++) {
		wait_ms(250, 1);         /* a smooth stretch */
		wait_ms(250, 0);         /* a slow frame: 15 ticks late */
		if (i == 2) mus_sfx(2, blip);
	}
	mus_poll();
	t = mus_ticks();
	dbg_log("poll mode: %lu ms, %lu ticks, notes started %u %u %u, drums %u; row %u of step %u",
	        tmr_ms() - t0, t, tr_notes[0], tr_notes[1], tr_notes[2], tr_notes[3], mus_row, mus_ord);
	check(t >= 177 && t <= 183, "mus_poll catches up: about 180 ticks in 3 s despite stalls");
	check(tr_notes[0] >= 10 && tr_notes[1] >= 14 && tr_notes[2] >= 3 && tr_notes[3] >= 16 && mus_ord == 1, "notes started on every voice");
	mus_stop();
}

static void snd_file(const char *name)
{
	FILE *f = fopen(name, "wb");  /* 4-byte time in 1/48000 s, then the byte */
	unsigned i;
	if (!f) return;
	for (i = 0; i < tr_n; i++) {
		unsigned long t = (unsigned long)tr_t[i] * 800UL;
		unsigned char c = tr_b[i];
		fwrite(&t, 4, 1, f);
		fwrite(&c, 1, 1, f);
	}
	fclose(f);
}

/* Tick by tick with the trap on (nothing depends on the emulator's
 * clock): exact note counts, the end of a song played once, the cost of
 * a tick, SND.BIN. */
static void test_steps(void)
{
	static unsigned cnt[SONG_TICKS];
	unsigned long c0, b0, c, sum = 0, rsum = 0, psum = 0, mx = 0, bsum = 0, bmx = 0;
	unsigned most, bad, i, at = 0, hz, sumb = 0;

	steps = 0; nbytes = 0; tf_on(); nothing(); tf_off(); c0 = steps; b0 = nbytes;   /* the call and tf_off themselves */
	trace_reset();
	mus_start(&song_once, MUS_NOHOOK);
	check(mode == M_EXT, "MUS_NOHOOK: stepping by hand");
	for (i = 0; i < SONG_TICKS; i++) {
		steps = 0; nbytes = 0; tf_on(); mus_irq(); tf_off();
		c = steps - c0;
		cnt[i] = (unsigned)c;
		sum += c; bsum += nbytes - b0;
		if (nbytes - b0 > bmx) bmx = nbytes - b0;
		if (i % 6 == 0) rsum += c;
		else if (i % 6 >= 3) psum += c;
		if (c > mx) { mx = c; at = i; }
	}
	bad = trace_bad(&most);
	for (i = 0; i < tr_n; i++) sumb = (unsigned)(sumb * 31u + tr_b[i] + tr_t[i]);
	dbg_log("%d ticks by hand: notes %u %u %u, drums %u (want %u %u %u %u); %u bytes to the chip (checksum %04X), at most %u in a tick",
	        SONG_TICKS, tr_notes[0], tr_notes[1], tr_notes[2], tr_notes[3],
	        want_notes[0], want_notes[1], want_notes[2], want_notes[3], tr_n, sumb, most);
	check(tr_notes[0] == want_notes[0] && tr_notes[1] == want_notes[1] && tr_notes[2] == want_notes[2] &&
	      tr_notes[3] == want_notes[3] && bad == 0 && tr_n > 300, "every note of the song started, once");
	hz = trace_first_div(0);
	check(hz == 170 && trace_first_div(1) == 1017 && trace_first_div(2) == mus_div[45], "first notes have the right pitch");
	check(mus_playing(), "still on its last row after the last tick but one");
	tf_on(); mus_irq(); tf_off();
	check(!mus_playing() && !mus_level(0) && !mus_level(1) && !mus_level(2) && !mus_level(3) && mus_ticks() == SONG_TICKS + 1 &&
	      trace_last_att(0) == 15 && trace_last_att(1) == 15 && trace_last_att(2) == 15 && trace_last_att(3) == 15,
	      "a MUS_ONCE song ends silent");
	dbg_log("instructions per tick, 3 voices and drums busy: average %lu; first tick of a row %lu, the three before it (preparing the row) %lu, the others %lu; worst %lu (tick %u)",
	        sum / SONG_TICKS, rsum / (SONG_TICKS / 6), psum / (SONG_TICKS / 2),
	        (sum - rsum - psum) / (SONG_TICKS / 3), mx, at);
	dbg_log("the same in bytes of code run: average %lu, worst %lu (at about 6 clock cycles a byte on an 8088: %lu and %lu microseconds on the PCjr)",
	        bsum / SONG_TICKS, bmx, bsum / SONG_TICKS * 6 * 100 / 477, bmx * 6 * 100 / 477);
	dbg_log("ticks 0-23: %u %u %u %u %u %u | %u %u %u %u %u %u | %u %u %u %u %u %u | %u %u %u %u %u %u",
	        cnt[0], cnt[1], cnt[2], cnt[3], cnt[4], cnt[5], cnt[6], cnt[7], cnt[8], cnt[9], cnt[10], cnt[11],
	        cnt[12], cnt[13], cnt[14], cnt[15], cnt[16], cnt[17], cnt[18], cnt[19], cnt[20], cnt[21], cnt[22], cnt[23]);
	check(mx <= COST_MAX, "no tick over the instruction budget");
	snd_file("SND.BIN");

	/* tempo: at 3 ticks a row the same song takes half as long */
	mus_stop();
	trace_reset();
	mus_start(&song_once, MUS_NOHOOK);
	mus_tempo(3);
	for (i = 0; i < SONG_TICKS / 2 + 1; i++) mus_irq();
	check(tr_notes[0] == want_notes[0] && tr_notes[3] == want_notes[3] && !mus_playing(), "mus_tempo(3) plays it in half the ticks");
	/* and at 1 tick a row every tick prepares and starts a row */
	trace_reset();
	mus_start(&song_once, MUS_NOHOOK);
	mus_tempo(1);
	for (i = 0; i < 32 + 1; i++) { tf_on(); mus_irq(); tf_off(); }
	check(tr_notes[0] == want_notes[0] && tr_notes[1] == want_notes[1] && tr_notes[3] == want_notes[3] && !mus_playing(),
	      "mus_tempo(1) plays a row a tick");

	/* effects only: mus_sfx starts the player by itself */
	mus_stop();
	trace_reset();
	mus_sfx(0, blip);
	for (i = 0; i < 12; i++) { tf_on(); mus_irq(); tf_off(); }
	hz = trace_first_div(0);
	dbg_log("effect with no song: mode %d, divisor %u, %lu ticks, %u bytes", mode, hz, mus_ticks(), tr_n);
	check(mode == M_POLL && hz == MUS_HZ(1760) && !mus_sfx_busy(0) && mus_level(0) == 0 && tr_n == 4,
	      "mus_sfx alone starts the player and ends silent");
	steps = 0; tf_on(); mus_irq(); tf_off();
	dbg_log("a tick with no song and no effect: %lu instructions", steps - c0);
	mus_stop();
}

/* A second song that uses everything else (its own kit, swell, fade-out
 * instrument, slow arpeggio, deep vibrato, transposes, a step with voices
 * missing, a fading last step, effects over it), tick by tick, for
 * SND2.BIN: tmusic.sh compares the assembly's bytes with the C code's. */
static const char *const t_lead[] = {
	"A4:4 C5:4 E5:4 G5:2 A5:2 | B5 - Bb5 . C6:3 - | F#5:4 . .",
	"E5:16 | D5:8 - . . . C5:4",
	0
};
static const char *const t_bass[] = {
	"A2 A2 A3 A2 | A2 A2 A3 A2 | F3 F3 F4 F3 | F3 F3 F4 F3 | C3:4 - . G3:2 | G3:4 G4 G3 G4 G3",
	"A2:8 . . . . | E3:4 - . . . | A2:8 . .",
	0
};
static const char *const t_chord[] = {
	"A3m:8 F3M:8 | C4M:4 G3M:4 E4m:8",
	"A4m:16 | - . . . D4m:6 G4M:6",
	0
};
static const char *const t_drums[] = {
	"k.h.S.h.k.hkS.ssC...o...12345678",
	"x...............k.b.b.k.1.3.5.7.",
	0
};
static const mus_inst t_inst[] = {
	{ 14, 10, 3, 0, 3, 0 },              /* 0 lead, deep vibrato        */
	{ 15,  8, 1, 0, 0, 0 },              /* 1 bass                      */
	{ 12, 10, 1, 0, 0, 1 },              /* 2 fast arpeggio             */
	{  3, 11, 2, 0, 1, 0 },              /* 3 swell with slight vibrato */
	{ 13, 13, 0, 3, 0, 0 },              /* 4 pluck that fades out      */
	{ 10,  7, 4, 2, 0, 4 | MUS_UPDOWN }, /* 5 slow broken chord, fading */
	{ 12, 12, 0, 0, 4, 0 },              /* 6 plain, widest vibrato     */
};
static const mus_drum t_kit[] = {
	{ 'k', 1, 1, 2, 60,  0, 12 }, { 's', 5, 0, 0, 56, 0, 7 }, { 'h', 4, 0, 0, 36, 0, 16 }, { 'o', 4, 0, 0, 40, 0, 6 },
	{ 'S', 1, 2, 5, 60, 52,  7 }, { 'C', 1, 2, 4, 60, 60, 2 }, { 'x', 7, 0, 0, 60, 0, 1 }, { 'b', 2, 3, 3, 48, 30, 5 },
	{ 0 }
};
static const mus_step t_order[] = {
	{ { 0, 0, 0, 0 }, { 0, 1, 2 }, {   0,  0,  0 }, 0 },
	{ { 1, 1, 1, 1 }, { 3, 4, 5 }, {   0, 12,  0 }, 0 },
	{ { 0, MUS_NONE, 0, MUS_NONE }, { 6, 1, 5 }, { -12,  0,  7 }, 0 },
	{ { 0, 0, 1, 0 }, { 4, 1, 2 }, {  12, -5, 24 }, 0 },
	{ { 1, 1, 1, 1 }, { 0, 4, 2 }, {   5,  0, -3 }, MUS_FADE },
};
static const mus_song song2 = { 5, 32, { t_lead, t_bass, t_chord, t_drums }, t_order, MUS_COUNT(t_order), 1, t_inst, MUS_COUNT(t_inst), t_kit };
static const mus_fx t_sweep[] = { { 8, 0, 40, 16 }, { 0, 0, 0, 0 } };       /* silent voice 2 steering noise type 7 */
static const mus_fx t_boom[] = { { 7, 15, 10, 0 }, { 7, 12, 10, 0 }, { 7, 9, 10, 0 }, { 7, 5, 10, 0 }, { 0, 0, 0, 0 } };
static const mus_fx t_jingle[] = { { MUS_HZ(784), 12, 4, 0 }, { 0, 0, 2, 0 }, { MUS_HZ(988), 12, 4, 0 }, { MUS_HZ(1175), 13, 9, -1 }, { 0, 0, 0, 0 } };

static void test_song2(void)
{
	unsigned most, bad, i, sumb = 0, at = 0;
	unsigned long mx = 0, c;
	trace_reset();
	i = (unsigned)mus_start(&song2, MUS_NOHOOK);
	if (i) dbg_log("second song: error %u at voice %d pattern %d char %d", i, mus_err.voice, mus_err.pat, mus_err.pos);
	check(i == 0, "the second song compiles");
	for (i = 0; i < 5 * 32 * 5 + 5 * 32 * 2; i++) {      /* all of it and two steps of the loop */
		if (i == 100) mus_sfx(0, t_jingle);
		if (i == 300) { mus_sfx(2, t_sweep); mus_sfx(MUS_NOISE, t_boom); }
		if (i == 420) mus_volume(9);
		if (i == 500) mus_pause();
		if (i == 520) mus_resume();
		if (i == 560) mus_volume(15);
		if (i == 700) mus_tempo(3);
		if (i == 760) mus_tempo(5);
		steps = 0; tf_on(); mus_irq(); tf_off();
		c = steps;
		if (c > mx) { mx = c; at = i; }
		if (tr_n >= TRN - 16) break;
	}
	bad = trace_bad(&most);
	for (i = 0; i < tr_n; i++) sumb = (unsigned)(sumb * 31u + tr_b[i] + tr_t[i]);
	dbg_log("second song: %lu ticks, notes %u %u %u, drums %u, %u bytes to the chip (checksum %04X), %u split, at most %u in a tick, worst tick %lu instructions (tick %u); step %u row %u",
	        mus_ticks(), tr_notes[0], tr_notes[1], tr_notes[2], tr_notes[3], tr_n, sumb, bad, most, mx, at, mus_ord, mus_row);
	check(bad == 0 && tr_n < TRN - 16 && mus_playing() && mus_ord >= 1 && tr_notes[0] > 40 && tr_notes[3] > 60, "second song plays through and loops");
	snd_file("SND2.BIN");
	mus_stop();
}

int main(void)
{
	int i;
	atexit(final);               /* first in, last out */
	tmr_init();
	old1 = _dos_getvect(1);
	_dos_setvect(1, trap);
	dbg_log("TMUSIC: mus_song %u bytes, mus_step %u, mus_inst %u, mus_fx %u; the player's near data %u bytes", (unsigned)sizeof(mus_song),
	        (unsigned)sizeof(mus_step), (unsigned)sizeof(mus_inst), (unsigned)sizeof(mus_fx),
	        (unsigned)(sizeof P + sizeof vs + sizeof vin + sizeof cs + sizeof ns + sizeof sx));
	test_table();
	test_compile();
	/* fx_init finds the retrace interrupt by waiting 100 ms on tmr_ms. In
	 * DOSBox a timer read can step back 55 ms just as the PIT wraps, the
	 * wait ends at once and fx_irq comes out 0: try again then. */
	for (i = 0; i < 3; i++) {
		if (fx_init(0)) { dbg_log("FAIL fx_init %d", gfx_err); fails++; return 1; }
		if (fx_irq) break;
		dbg_log("fx_init found no retrace interrupt (try %d)", i + 1);
		gfx_done();
	}
	dbg_log("fx_init: irq %d", fx_irq);
	if (fx_irq) {
		test_irq();
		test_chain();
		test_fallback();
	} else check(0, "no retrace interrupt here: IRQ mode not tested");
	gfx_done();
	test_poll();
	test_steps();
	test_song2();
	/* leave a song and an effect sounding: exit must silence them. The
	 * trap stays on through exit, until final(). */
	mus_start(&song, 0);
	mus_sfx(1, blip);
	wait_ms(200, 1);
	check(mus_playing() && (mus_level(0) || mus_level(1) || mus_level(2)), "a song is sounding when main returns");
	trace_reset();
	tf_on();
	return 0;
}
