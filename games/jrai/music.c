/* Music and the demo clock.
 *
 * A small tracker for the PCjr's SN76496: voice 0 lead, voice 1 bass,
 * voice 2 chord arpeggios (a new chord note every 60 Hz tick, the classic
 * chip sound), and the noise voice for drums. It runs inside the vertical
 * retrace interrupt, so it keeps time whatever the drawing code is doing.
 *
 * Spoken lines are in voice.c: the music pauses for them (mus_pause) and
 * picks up where it stopped (mus_resume).
 */
#include <dos.h>
#include <conio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "jrai.h"

#pragma off (check_stack) /* parts of this run inside IRQ5, on any stack */

/* ---- the song ------------------------------------------------------------
 * Patterns are 16 rows. Tokens: a note (C4, F#3...), "." hold, "-" off.
 * Chord tokens: root + "m" (minor) or "M" (major), e.g. A3m.
 * Drum rows are one character each: k kick, s snare, h hat, o open hat,
 * c crash, . nothing.
 */
static const char *const leadpat[] = {
	/* 0 */ "- . . . . . . . . . . . . . . .",
	/* 1 */ "A4 . C5 . E5 . . . F5 . E5 . C5 . A4 .",
	/* 2 */ "G4 . C5 . E5 . G5 . . . F5 . D5 . B4 .",
	/* 3 */ "A5 . . . G5 . E5 . F5 . . . A5 . C6 .",
	/* 4 */ "C6 . . . G5 . E5 . D5 . . . B4 . . .",
	/* 5 */ "E5 . . . . . . . C5 . . . . . . .",       /* slow (meeting) */
	/* 6 */ "G5 . . . . . . . D5 . . . B4 . . .",
	/* 7 */ "A5 . A5 C6 . A5 G5 . F5 . F5 A5 . F5 E5 .", /* finale */
	/* 8 */ "E5 . G5 . C6 . B5 . D6 . . . B5 . G5 .",
	/* 9 */ "A5 . . . . . . . . . . . - . . .",
};
static const char *const basspat[] = {
	/* 0 */ "- . . . . . . . . . . . . . . .",
	/* 1 */ "A2 . A3 . A2 . A3 . F3 . F4 . F3 . F4 .",
	/* 2 */ "C3 . C4 . C3 . C4 . G3 . G4 . G3 . G4 .",
	/* 3 */ "A2 . . . . . . . F3 . . . . . . .",
	/* 4 */ "C3 . . . . . . . G3 . . . . . . .",
	/* 5 */ "A2 A2 A3 A2 A2 A2 A3 A2 F3 F3 F4 F3 F3 F3 F4 F3",
	/* 6 */ "C3 C3 C4 C3 C3 C3 C4 C3 G3 G3 G4 G3 G3 G3 G4 G3",
	/* 7 */ "A2 . . . . . . . . . . . - . . .",
};
static const char *const arppat[] = {
	/* 0 */ "- . . . . . . . . . . . . . . .",
	/* 1 */ "A3m . . . . . . . F3M . . . . . . .",
	/* 2 */ "C4M . . . . . . . G3M . . . . . . .",
	/* 3 */ "A4m . . . . . . . F4M . . . . . . .",
	/* 4 */ "C5M . . . . . . . G4M . . . . . . .",
	/* 5 */ "A3m . . . . . . . . . . . - . . .",
};
static const char *const drumpat[] = {
	/* 0 */ "................",
	/* 1 */ "k...s...k.k.s...",
	/* 2 */ "k.h.s.h.k.hks.h.",
	/* 3 */ "k.h.s.h.k.hks.ss",
	/* 4 */ "h...h...h...h...",
	/* 5 */ "c.h.s.h.k.hks.h.",
	/* 6 */ "kkk.ssshkkhhssss",
	/* 7 */ "c...............",
};
/* order list: lead, bass, arp, drums per pattern. Parts start at the
 * positions in main.c (ORD_*). */
static const unsigned char order[][4] = {
	/* 0 plasma: arps alone, then bass, then drums */
	{0,0,1,0},{0,0,2,0},{0,0,1,0},{0,0,2,0},
	{0,1,1,1},{0,2,2,1},{0,1,1,2},{0,2,2,3},
	/* 8 fire: the tune */
	{1,1,1,5},{2,2,2,2},{1,1,1,2},{2,2,2,3},
	{3,1,1,2},{4,2,2,2},{3,1,1,2},{4,2,2,6},
	/* 16 tunnel: high arps, double-time bass */
	{1,5,3,5},{2,6,4,2},{3,5,3,2},{4,6,4,3},
	{1,5,3,2},{2,6,4,2},{3,5,3,2},{4,6,4,6},
	/* 24 meeting: quiet */
	{0,3,1,4},{0,4,2,4},{5,3,1,4},{6,4,2,4},
	{5,3,1,4},{6,4,2,4},{5,3,1,4},{6,4,2,4},
	{1,3,1,4},{2,4,2,4},{3,3,1,1},{4,4,2,3},
	/* 36 finale */
	{7,5,3,5},{8,6,4,2},{7,5,3,2},{8,6,4,3},
	{1,1,1,2},{2,2,2,2},{3,1,1,2},{4,2,2,3},
	{7,5,3,5},{8,6,4,2},{7,5,3,2},{8,6,4,3},
	{3,5,3,2},{4,6,4,2},{7,5,3,2},{8,6,4,6},
	/* 52 end */
	{9,7,5,7},{0,0,0,0},
};
#define NORDER (sizeof order / sizeof order[0])

/* parsed patterns: note+1 (1-96), 0 hold, 255 off; arp: low 7 bits note+1, bit 7 major */
#define NLEAD (sizeof leadpat / sizeof leadpat[0])
#define NBASS (sizeof basspat / sizeof basspat[0])
#define NARP (sizeof arppat / sizeof arppat[0])
static unsigned char lead_r[NLEAD][16], bass_r[NBASS][16], arp_r[NARP][16][2];

static unsigned period[96]; /* SN76496 divider per note, C0 = 0 */

static int parse_note(const char **pp, unsigned char *major)
{
	static const signed char semi[7] = { 9, 11, 0, 2, 4, 5, 7 }; /* A..G */
	const char *p = *pp;
	int n;
	while (*p == ' ') p++;
	if (*p == '.') { *pp = p + 1; return 0; }
	if (*p == '-') { *pp = p + 1; return 255; }
	n = semi[*p++ - 'A'];
	if (*p == '#') { n++; p++; }
	n += (*p++ - '0') * 12;
	if (major) { *major = (unsigned char)(*p == 'M'); p++; }
	*pp = p;
	return n + 1;
}

void mus_init(void)
{
	static const unsigned hz100[12] = { 26163u, 27718u, 29366u, 31113u, 32963u, 34923u,
		36999u, 39200u, 41530u, 44000u, 46616u, 49388u }; /* C4..B4 x100 */
	int i, r;
	for (i = 0; i < 96; i++) {
		unsigned long d = (unsigned long)hz100[i % 12] << (i / 12);
		unsigned long p = (111861UL * 1600UL) / d; /* 3579545/32/hz, hz = hz4 * 2^(oct-4) */
		period[i] = p > 1023 ? 1023 : (unsigned)p;
	}
	for (i = 0; i < (int)NLEAD; i++) {
		const char *p = leadpat[i];
		for (r = 0; r < 16; r++) lead_r[i][r] = (unsigned char)parse_note(&p, 0);
	}
	for (i = 0; i < (int)NBASS; i++) {
		const char *p = basspat[i];
		for (r = 0; r < 16; r++) bass_r[i][r] = (unsigned char)parse_note(&p, 0);
	}
	for (i = 0; i < (int)NARP; i++) {
		const char *p = arppat[i];
		for (r = 0; r < 16; r++) {
			unsigned char maj = 0;
			int n = parse_note(&p, &maj);
			if (n == 0 || n == 255) maj = 0;
			arp_r[i][r][0] = (unsigned char)n;
			arp_r[i][r][1] = maj;
		}
	}
}

/* ---- player ------------------------------------------------------------- */

/* /VIDEO: every register write, with its time in 1/48000 s, goes to
 * SND.BIN (4-byte time, 1-byte value) for render.py. The time is T's,
 * unless voice.c sets one between ticks (snd_log_at; 0 = T's again). */
static FILE *slog;
static unsigned long slog_t;
void snd_log_open(void) { slog = fopen("SND.BIN", "wb"); }
void snd_log_close(void) { if (slog) fclose(slog); slog = 0; }
void snd_log_at(unsigned long t) { slog_t = t; }

static void sn(unsigned char b)
{
	outp(0xC0, b);
	if (slog) {
		unsigned long t = slog_t ? slog_t : T * 800;
		fwrite(&t, 4, 1, slog);
		fwrite(&b, 1, 1, slog);
	}
}

void sn_out(unsigned char b) { sn(b); }

static void sn_period(int v, unsigned p)
{
	sn((unsigned char)(0x80 | (v << 5) | (p & 15)));
	sn((unsigned char)(p >> 4));
}

static void sn_vol(int v, int vol) /* vol 0-15, 15 loudest */
{
	if (vol < 0) vol = 0;
	if (vol > 15) vol = 15;
	sn((unsigned char)(0x90 | (v << 5) | (15 - vol)));
}

int mus_mute;
volatile int mus_row;
volatile unsigned char mus_level[4], mus_beat;
static volatile int playing;
static int ord, row, tick;
static unsigned long ordT; /* T when the current pattern started */
static struct { int note, vol, age; } ch[3];
static int arp_note, arp_maj, arp_i;
static int drum, dvol, dage;

void mus_stop(void)
{
	int v;
	playing = 0;
	for (v = 0; v < 4; v++) { sn((unsigned char)(0x9F | (v << 5))); mus_level[v] = 0; }
}

void mus_start(int o)
{
	_disable();
	ord = o; row = 0; tick = 0; mus_row = 0; ordT = T;
	memset(ch, 0, sizeof ch);
	arp_note = 0; drum = 0; dvol = 0;
	playing = !mus_mute;
	_enable();
}

static void new_row(void)
{
	const unsigned char *o;
	int n;
	if (ord >= (int)NORDER) { mus_stop(); return; }
	o = order[ord];
	n = lead_r[o[0]][row];
	if (n == 255) ch[0].note = 0;
	else if (n) { ch[0].note = n; ch[0].vol = 14; ch[0].age = 0; }
	n = bass_r[o[1]][row];
	if (n == 255) ch[1].note = 0;
	else if (n) { ch[1].note = n; ch[1].vol = 15; ch[1].age = 0; }
	n = arp_r[o[2]][row][0];
	if (n == 255) arp_note = 0;
	else if (n) { arp_note = n; arp_maj = arp_r[o[2]][row][1]; ch[2].age = 0; }
	switch (drumpat[o[3]][row]) {
	case 'k': drum = 'k'; dvol = 15; dage = 0; sn(0xE2); mus_beat = 1; break; /* periodic, low */
	case 's': drum = 's'; dvol = 14; dage = 0; sn(0xE5); break;              /* white */
	case 'h': drum = 'h'; dvol = 9;  dage = 0; sn(0xE4); break;
	case 'o': drum = 'o'; dvol = 10; dage = 0; sn(0xE4); break;
	case 'c': drum = 'c'; dvol = 15; dage = 0; sn(0xE4); mus_beat = 1; break;
	}
}

void mus_tick(void)
{
	if (!playing) return;
	if (tick == 0) new_row();
	if (!playing) return;
	/* lead: decay to a sustain level, vibrato after a while */
	if (ch[0].note) {
		unsigned p = period[ch[0].note - 1];
		if (ch[0].age > 14) p += (unsigned)((sin8[(ch[0].age * 24) & 255] * (int)(p >> 6)) >> 7);
		if ((ch[0].age & 3) == 3 && ch[0].vol > 10) ch[0].vol--;
		sn_period(0, p); sn_vol(0, ch[0].vol);
		ch[0].age++;
		mus_level[0] = (unsigned char)ch[0].vol;
	} else { sn_vol(0, 0); mus_level[0] = 0; }
	/* bass: plucky */
	if (ch[1].note) {
		if (ch[1].vol > 10) ch[1].vol--;
		sn_period(1, period[ch[1].note - 1]); sn_vol(1, ch[1].vol);
		mus_level[1] = (unsigned char)ch[1].vol;
	} else { sn_vol(1, 0); mus_level[1] = 0; }
	/* arpeggio: root, third, fifth, a new one every tick */
	if (arp_note) {
		static const unsigned char third[2] = { 3, 4 };
		int n = arp_note - 1 + (arp_i == 0 ? 0 : arp_i == 1 ? third[arp_maj] : 7);
		int vol = ch[2].age < 4 ? 12 : 10;
		if (++arp_i == 3) arp_i = 0;
		sn_period(2, period[n > 95 ? 95 : n]); sn_vol(2, vol);
		ch[2].age++;
		mus_level[2] = (unsigned char)vol;
	} else { sn_vol(2, 0); mus_level[2] = 0; }
	/* drums: noise envelopes (the kick also drops its rate) */
	if (dvol > 0) {
		if (drum == 'k' && dage == 2) sn(0xE3);
		sn((unsigned char)(0xF0 | (15 - dvol)));
		mus_level[3] = (unsigned char)dvol;
		dvol -= drum == 'h' ? 4 : drum == 'c' ? 1 : drum == 'o' ? 2 : 3;
		dage++;
	} else { sn(0xFF); mus_level[3] = 0; }
	if (++tick == ROW_TICKS) {
		tick = 0;
		mus_row++;
		if (++row == PAT_ROWS) { row = 0; ord++; ordT = T; }
	}
}

/* ---- clock: IRQ5 chain -------------------------------------------------- */

volatile unsigned long T;
int lockstep;
static void (__interrupt __far *old_irq5)(void);
static int hooked;
static unsigned long poll_ms0, poll_T0;

static void __interrupt __far clock_irq(void)
{
	old_irq5(); /* the library's handler: page flip, palette, EOI */
	T++;
	mus_tick();
}

void clock_start(void)
{
	snd_init();
	if (fx_irq && !lockstep) {
		old_irq5 = _dos_getvect(0x0D);
		_dos_setvect(0x0D, clock_irq);
		hooked = 1;
	}
	poll_ms0 = tmr_ms();
	poll_T0 = T;
}

void clock_stop(void)
{
	mus_stop();
	if (hooked) { _dos_setvect(0x0D, old_irq5); hooked = 0; }
}

/* Without IRQ5: T from the PIT, and the music caught up tick by tick. */
void clock_poll(void)
{
	unsigned long want;
	if (hooked || lockstep) return;
	want = poll_T0 + (tmr_ms() - poll_ms0) * 3 / 50;
	while (T < want) { T++; mus_tick(); }
}

/* Stops the music where it is; returns whether it was playing. */
int mus_pause(void)
{
	int was = playing;
	mus_stop();
	return was;
}

/* Picks the music up where mus_pause left it, `ticks` later. The pattern's
 * start time moves by as much, so the parts' clocks skip the pause too. */
void mus_resume(int was, unsigned long ticks)
{
	if (!was || mus_mute) return;
	_disable();
	ordT += ticks;
	memset(ch, 0, sizeof ch);
	arp_note = 0; dvol = 0;
	playing = 1;
	_enable();
}

int mus_at(void)
{
	return playing ? ord : -1;
}

unsigned long mus_ord_T(void)
{
	return ordT;
}
