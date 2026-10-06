/* JRCUBE music: a small tracker for the PCjr's SN76496 (after JRAI's).
 *
 * Voice 0 lead (decay or swell, vibrato), voice 1 bass, voice 2 chord
 * arpeggios (fast: a new chord note every 60 Hz tick; slow: an up-down
 * broken chord for pads), the noise voice for drums. It runs inside the
 * vertical retrace interrupt, chained after the graphics library's own
 * IRQ5 handler (fx_irq_hook), so it keeps time whatever the drawing code is doing.
 *
 * Synthwave in A minor at 128.6 BPM (7 ticks a 16th). Sections (patterns):
 *   0-3   intro: slow arps, sparse bass, a soft pad from 2
 *   4-11  groove: four on the floor, octave bass, the hook (low) from 8
 *   12-19 morph: a second melody, crash at 12, 14, 16, 18
 *   20-21 build: snare roll 8ths then 16ths, rising chords, crash at 22
 *   22-29 drop: the hook up an octave, 16th bass, busiest drums
 *   30-31 explosion: crash + noise sweep, then bass and slow arps only
 *   32-33 rebuild: climbing arps over an E pedal, snare roll, crash at 34
 *   34-37 outro: the hook again, then A minor rings out and fades
 */
#include <dos.h>
#include <conio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "jrgfx.h"
#include "music.h"

#pragma off (check_stack) /* the player runs inside IRQ5, on the library's stack */

/* ---- the song ------------------------------------------------------------
 * Patterns are 16 rows. Tokens: a note (C4, F#3...), "." hold, "-" off.
 * Arp tokens: root + "m" (minor) or "M" (major), e.g. A3m.
 * Drum rows, one character each: k kick, S kick + snare (one noise voice,
 * so the kick sounds for two ticks and the snare takes over), s snare,
 * 1-9 snare at volume 6+n (rolls), h hat, o open hat, C kick + crash,
 * x crash + falling noise sweep (borrows voice 2), . nothing.
 */
static const char *const leadpat[] = {
	/* 0 */ "- . . . . . . . . . . . . . . .",
	/* hook (Am F | C G), played an octave down in the groove */
	/* 1 */ "E5 . A5 . . . G5 . A5 . . . C6 . B5 .",
	/* 2 */ "C6 . . . G5 . . . D6 . . . B5 . G5 .",
	/* 3 */ "E5 . A5 . . . G5 . A5 . . . C6 . E6 .",
	/* 4 */ "D6 . . . C6 . . . B5 . . . G5 . . .",
	/* 5 */ "E6 . D6 . C6 . . . A5 . . . C6 . B5 .",
	/* 6 */ "D6 . . . E6 . . . D6 . . . B5 . G5 .",
	/* morph melody: Am F | C G | Am F | C G | F G | Em Am | F G | Em Am */
	/* 7 */ "A5 . G5 . E5 . . . F5 . E5 . C5 . . .",
	/* 8 */ "E5 . . . C5 . E5 . D5 . . . B4 . D5 .",
	/* 9 */ "A5 . G5 . E5 . A5 . C6 . . . A5 . F5 .",
	/* 10 */ "G5 . . . E5 . . . D5 . . . . . . .",
	/* 11 */ "A5 . . . C6 . . . B5 . . . D6 . . .",
	/* 12 */ "E6 . . . B5 . . . C6 . . . A5 . . .",
	/* 13 */ "A5 . C6 . F6 . . . D6 . . . B5 . G5 .",
	/* 14 */ "G5 . . . B5 . . . A5 . . . . . . .",
	/* intro pad (swells) */
	/* 15 */ "C5 . . . . . . . A4 . . . . . . .",
	/* 16 */ "G4 . . . . . . . B4 . . . . . . .",
	/* 17 the end */ "A5 . . . . . . . . . . . . . . .",
};
static const char *const basspat[] = {
	/* 0 */ "- . . . . . . . . . . . . . . .",
	/* 1 */ "A2 . . . . . . . F3 . . . . . . .",
	/* 2 */ "C3 . . . . . . . G3 . . . . . . .",
	/* 3 */ "A2 . . . . . A2 . F3 . . . . . F3 .",
	/* 4 */ "C3 . . . . . C3 . G3 . . . G3 . G4 .",
	/* 5 */ "A2 . A3 . A2 . A3 . F3 . F4 . F3 . F4 .",
	/* 6 */ "C3 . C4 . C3 . C4 . G3 . G4 . G3 . G4 .",
	/* 7 */ "A2 A2 A3 A2 A2 A2 A3 A2 F3 F3 F4 F3 F3 F3 F4 F3",
	/* 8 */ "C3 C3 C4 C3 C3 C3 C4 C3 G3 G3 G4 G3 G3 G3 G4 G3",
	/* 9 */ "F3 . F4 . F3 . F4 . G3 . G4 . G3 . G4 .",
	/* 10 */ "E3 . E4 . E3 . E4 . A2 . A3 . A2 . A3 .",
	/* 11 */ "F3 . F3 . F3 . F3 . G3 . G3 . G3 . G3 .",
	/* 12 */ "G3 G3 G3 G3 G3 G3 G3 G3 G3 G3 G3 G3 G3 G3 G3 G3",
	/* 13 */ "A2 . . . . . . . . . . . . . . .",
	/* 14 */ "F3 . . . . . . . E3 . . . . . . .",
	/* 15 */ "E3 . E3 . E3 . E3 . E3 . E3 . E3 . E3 .",
	/* 16 */ "E3 E3 E3 E3 E3 E3 E3 E3 E3 E3 E3 E3 E3 E3 E3 E3",
};
static const char *const arppat[] = {
	/* 0 */ "- . . . . . . . . . . . . . . .",
	/* 1 */ "A3m . . . . . . . F3M . . . . . . .",
	/* 2 */ "C4M . . . . . . . G3M . . . . . . .",
	/* 3 */ "A4m . . . . . . . F4M . . . . . . .",
	/* 4 */ "C5M . . . . . . . G4M . . . . . . .",
	/* 5 */ "F4M . . . . . . . G4M . . . . . . .",
	/* 6 */ "E4m . . . . . . . A4m . . . . . . .",
	/* 7 */ "F3M . . . G3M . . . A3m . . . C4M . . .",
	/* 8 */ "D4m . . . E4m . . . F4M . . . G4M . . .",
	/* 9 */ "- . . . . . . . A3m . . . . . . .",
	/* 10 */ "F3M . . . . . . . E3M . . . . . . .",
	/* 11 */ "A3m . . . C4M . . . E4m . . . F4M . . .",
	/* 12 */ "G4M . . . A4m . . . C5M . . . E5M . . .",
	/* 13 */ "A3m . . . . . . . . . . . . . . .",
};
static const char *const drumpat[] = {
	/* 0 */ "................",
	/* 1 */ "k.h.S.h.k.h.S.h.",  /* groove */
	/* 2 */ "k.h.S.h.k.hkS.ss",  /* groove fill */
	/* 3 */ "C.hhS.hhk.hhS.hh",  /* morph, crash */
	/* 4 */ "k.hhS.hhk.hhS.hh",  /* morph */
	/* 5 */ "3.3.4.4.5.5.6.6.",  /* build: 8th roll */
	/* 6 */ "6677788889999999",  /* build: 16th roll */
	/* 7 */ "ChhhShhokhhkShho",  /* drop, crash */
	/* 8 */ "khhhShhokhhkShho",  /* drop */
	/* 9 */ "khhhShhhkhhkSsss",  /* drop fill */
	/* 10 */ "x...............", /* explosion */
	/* 11 */ "1.1.2.2.3.3.4.4.", /* rebuild: 8th roll */
	/* 12 */ "5555666677778899", /* rebuild: 16th roll */
	/* 13 */ "k...............", /* the end */
	/* 14 */ "k...S...k...S...", /* groove, first bar */
};

/* One pattern of the song: which part each voice plays, and how. */
typedef struct {
	unsigned char lead;  /* leadpat */
	signed char ltr;     /* lead transpose, semitones */
	unsigned char lvol;  /* lead sustain volume */
	unsigned char lenv;  /* 0 pluck (attack then decay), 1 swell (pad) */
	unsigned char bass;  /* basspat */
	unsigned char arp;   /* arppat */
	unsigned char aspd;  /* 1: a chord note a tick; n: an up-down step every n ticks */
	unsigned char avol;  /* arp volume */
	unsigned char drum;  /* drumpat */
	unsigned char fade;  /* 1: fade to silence over the pattern */
} ordent;

static const ordent order[SONG_PATS] = {
	/* intro */
	{  0,   0,  0, 0,  0,  1, 7,  7,  0, 0 },  /*  0 */
	{  0,   0,  0, 0,  1,  2, 7,  8,  0, 0 },  /*  1 */
	{ 15,   0,  7, 1,  3,  1, 4,  9,  0, 0 },  /*  2 */
	{ 16,   0,  8, 1,  4,  2, 2, 10,  0, 0 },  /*  3 */
	/* groove */
	{  0,   0,  0, 0,  5,  1, 1,  9, 14, 0 },  /*  4 */
	{  0,   0,  0, 0,  6,  2, 1,  9,  1, 0 },  /*  5 */
	{  0,   0,  0, 0,  5,  1, 1,  9,  1, 0 },  /*  6 */
	{  0,   0,  0, 0,  6,  2, 1,  9,  2, 0 },  /*  7 */
	{  1, -12, 11, 0,  5,  1, 1,  9,  1, 0 },  /*  8 */
	{  2, -12, 11, 0,  6,  2, 1,  9,  1, 0 },  /*  9 */
	{  3, -12, 11, 0,  5,  1, 1,  9,  1, 0 },  /* 10 */
	{  4, -12, 11, 0,  6,  2, 1,  9,  2, 0 },  /* 11 */
	/* morph */
	{  7,   0, 10, 0,  5,  1, 1,  9,  3, 0 },  /* 12 */
	{  8,   0, 10, 0,  6,  2, 1,  9,  4, 0 },  /* 13 */
	{  9,   0, 10, 0,  5,  3, 1,  9,  3, 0 },  /* 14 */
	{ 10,   0, 10, 0,  6,  4, 1,  9,  2, 0 },  /* 15 */
	{ 11,   0, 10, 0,  9,  5, 1,  9,  3, 0 },  /* 16 */
	{ 12,   0, 10, 0, 10,  6, 1,  9,  4, 0 },  /* 17 */
	{ 13,   0, 10, 0,  9,  5, 1,  9,  3, 0 },  /* 18 */
	{ 14,   0, 10, 0, 10,  6, 1,  9,  2, 0 },  /* 19 */
	/* build */
	{  0,   0,  0, 0, 11,  7, 1, 10,  5, 0 },  /* 20 */
	{  0,   0,  0, 0, 12,  8, 1, 11,  6, 0 },  /* 21 */
	/* drop */
	{  1,   0, 12, 0,  7,  3, 1, 10,  7, 0 },  /* 22 */
	{  2,   0, 12, 0,  8,  4, 1, 10,  8, 0 },  /* 23 */
	{  3,   0, 12, 0,  7,  3, 1, 10,  8, 0 },  /* 24 */
	{  4,   0, 12, 0,  8,  4, 1, 10,  8, 0 },  /* 25 */
	{  5,   0, 12, 0,  7,  3, 1, 10,  8, 0 },  /* 26 */
	{  2,   0, 12, 0,  8,  4, 1, 10,  8, 0 },  /* 27 */
	{  3,   0, 12, 0,  7,  3, 1, 10,  8, 0 },  /* 28 */
	{  6,   0, 12, 0,  8,  4, 1, 10,  9, 0 },  /* 29 */
	/* explosion */
	{  0,   0,  0, 0, 13,  9, 4,  9, 10, 0 },  /* 30 */
	{  0,   0,  0, 0, 14, 10, 4,  9,  0, 0 },  /* 31 */
	/* rebuild */
	{  0,   0,  0, 0, 15, 11, 2,  9, 11, 0 },  /* 32 */
	{  0,   0,  0, 0, 16, 12, 1, 11, 12, 0 },  /* 33 */
	/* outro */
	{  1,   0, 12, 0,  7,  3, 1, 10,  7, 0 },  /* 34 */
	{  2,   0, 12, 0,  8,  4, 1, 10,  8, 0 },  /* 35 */
	{  3,   0, 12, 0,  7,  3, 1, 10,  9, 0 },  /* 36 */
	{ 17,   0, 12, 0, 13, 13, 4, 10, 13, 1 },  /* 37 */
};

/* parsed patterns: note+1 (1-96), 0 hold, 255 off; arp: note, major flag */
#define NLEAD (sizeof leadpat / sizeof leadpat[0])
#define NBASS (sizeof basspat / sizeof basspat[0])
#define NARP (sizeof arppat / sizeof arppat[0])
static unsigned char lead_r[NLEAD][PAT_ROWS], bass_r[NBASS][PAT_ROWS];
static unsigned char arp_r[NARP][PAT_ROWS][2];

/* SN76496 divider per note, C0 = 0. 10 bits, so the lowest note is A2
 * (110 Hz, 1017); anything lower is clamped to 1023 and the song has none. */
static unsigned period[96];

/* vibrato: sine, 32 steps, amplitude 127 */
static const signed char vib[32] = {
	0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
	0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25
};

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
		/* 3579545/32/hz with hz = hz4 * 2^(oct-4): 111861 * 1600 / (hz100 << oct) */
		unsigned long d = (unsigned long)hz100[i % 12] << (i / 12);
		unsigned long p = (111861UL * 1600UL + d / 2) / d;
		period[i] = p > 1023 ? 1023 : (unsigned)p;
	}
	for (i = 0; i < (int)NLEAD; i++) {
		const char *p = leadpat[i];
		for (r = 0; r < PAT_ROWS; r++) lead_r[i][r] = (unsigned char)parse_note(&p, 0);
	}
	for (i = 0; i < (int)NBASS; i++) {
		const char *p = basspat[i];
		for (r = 0; r < PAT_ROWS; r++) bass_r[i][r] = (unsigned char)parse_note(&p, 0);
	}
	for (i = 0; i < (int)NARP; i++) {
		const char *p = arppat[i];
		for (r = 0; r < PAT_ROWS; r++) {
			unsigned char maj = 0;
			int n = parse_note(&p, &maj);
			if (n == 0 || n == 255) maj = 0;
			arp_r[i][r][0] = (unsigned char)n;
			arp_r[i][r][1] = maj;
		}
	}
}

/* ---- register writes (and the log) -------------------------------------- */

volatile unsigned long mus_T;
volatile unsigned char mus_kick, mus_snare, mus_crash, mus_level[4];

static int quiet, lockstep, hooked, playing;
static FILE *slog;

void mus_log_open(void)
{
	slog = fopen("SND.BIN", "wb");
	if (slog) setvbuf(slog, 0, _IOFBF, 4096);
}

void mus_log_close(void)
{
	if (slog) fclose(slog);
	slog = 0;
}

/* SND.BIN: 4-byte time in 1/48000 s, then the byte (render.py). Not from
 * inside the interrupt: DOS isn't reentrant. */
static void sn(unsigned char b)
{
	if (!quiet) outp(0xC0, b);
	if (slog && !hooked) {
		unsigned long t = mus_T * 800UL;
		fwrite(&t, 4, 1, slog);
		fwrite(&b, 1, 1, slog);
	}
}

static void sn_period(int v, unsigned p)
{
	sn((unsigned char)(0x80 | (v << 5) | (p & 15)));
	sn((unsigned char)(p >> 4));
}

/* fade: attenuation the end of the song adds to every voice */
static unsigned char fade;

static unsigned char sn_vol(int v, int vol) /* vol 0-15, 15 loudest */
{
	vol -= fade;
	if (vol < 0) vol = 0;
	if (vol > 15) vol = 15;
	sn((unsigned char)(0x90 | (v << 5) | (15 - vol)));
	return (unsigned char)vol;
}

/* ---- player ------------------------------------------------------------- */

static int ord, row, tick;
static const ordent *op;
static struct { int note, vol, age; } ch[2]; /* lead, bass */
static int arp_note, arp_maj, arp_i, arp_c, arp_age;
static int drum, dvol, dage; /* dvol in quarter steps (0-60) */
static int sweep;            /* ticks left of the explosion sweep */

static void new_row(void)
{
	int n;
	if (row == 0) {
		op = &order[ord];
		if (!op->fade) fade = 0;
	}
	if (op->fade) fade = (unsigned char)row;
	n = lead_r[op->lead][row];
	if (n == 255) ch[0].note = 0;
	else if (n) {
		n += op->ltr;
		if (n < 1) n = 1;
		if (n > 96) n = 96;
		ch[0].note = n; ch[0].age = 0;
		ch[0].vol = op->lenv ? 3 : (op->lvol + 3 > 15 ? 15 : op->lvol + 3);
	}
	n = bass_r[op->bass][row];
	if (n == 255) ch[1].note = 0;
	else if (n) { ch[1].note = n; ch[1].vol = 15; ch[1].age = 0; }
	n = arp_r[op->arp][row][0];
	if (n == 255) arp_note = 0;
	else if (n) {
		arp_note = n; arp_maj = arp_r[op->arp][row][1];
		arp_i = 0; arp_c = 0; arp_age = 0;
	}
	n = drumpat[op->drum][row];
	switch (n) {
	case 'k': drum = 'k'; dvol = 60; dage = 0; sn(0xE1); mus_kick = 1; break;  /* periodic, 233 Hz */
	case 'S': drum = 'S'; dvol = 60; dage = 0; sn(0xE1); mus_kick = 1; mus_snare = 1; break;
	case 's': drum = 's'; dvol = 56; dage = 0; sn(0xE5); mus_snare = 1; break; /* white */
	case 'h': drum = 'h'; dvol = 36; dage = 0; sn(0xE4); break;
	case 'o': drum = 'o'; dvol = 40; dage = 0; sn(0xE4); break;
	case 'C': drum = 'C'; dvol = 60; dage = 0; sn(0xE1); mus_kick = 1; mus_crash = 1; break;
	case 'x': drum = 'x'; dvol = 60; dage = 0; sn(0xE7); mus_crash = 1; sweep = 60; break;
	default:
		if (n >= '1' && n <= '9') {
			drum = 's'; dvol = (6 + n - '0') * 4; dage = 0; sn(0xE5); mus_snare = 1;
		}
	}
}

/* One 60 Hz step at time mus_T (the caller counts mus_T after it). */
static void mus_tick(void)
{
	if (!playing) return;
	if (tick == 0) new_row();
	/* lead: pluck decays to the sustain level or a pad swells up to it;
	 * vibrato once the note has sounded a while */
	if (ch[0].note) {
		unsigned p = period[ch[0].note - 1];
		int age = ch[0].age;
		if (age > 12) p += (vib[(age * 3) & 31] * (int)(p >> 6)) >> 7;
		if (op->lenv) { if ((age & 1) && ch[0].vol < op->lvol) ch[0].vol++; }
		else if ((age & 1) && ch[0].vol > op->lvol) ch[0].vol--;
		sn_period(0, p);
		mus_level[0] = sn_vol(0, ch[0].vol);
		if (age < 1000) ch[0].age++;
	} else { mus_level[0] = sn_vol(0, 0); }
	/* bass: plucky, every new note from the top */
	if (ch[1].note) {
		if (ch[1].vol > 8) ch[1].vol--;
		sn_period(1, period[ch[1].note - 1]);
		mus_level[1] = sn_vol(1, ch[1].vol);
	} else { mus_level[1] = sn_vol(1, 0); }
	/* the explosion sweep owns voice 2: silent, its period clocks the noise */
	if (sweep) {
		sn_period(2, 8 + (60 - sweep) * 16);
		mus_level[2] = sn_vol(2, 0);
		sweep--;
	} else if (arp_note) {
		/* arpeggio: fast cycles root, third, fifth a tick each; slow walks
		 * root, third, fifth, octave and back down */
		static const unsigned char upd[6] = { 0, 1, 2, 3, 2, 1 };
		static const unsigned char iv[2][4] = { { 0, 3, 7, 12 }, { 0, 4, 7, 12 } };
		int spd = op->aspd, n, vol;
		if (spd == 1) {
			n = iv[arp_maj][arp_i];
			if (++arp_i == 3) arp_i = 0;
			vol = arp_age < 4 ? op->avol + 2 : op->avol;
		} else {
			n = iv[arp_maj][upd[arp_i]];
			vol = op->avol - (arp_c >> 1);
			if (++arp_c >= spd) { arp_c = 0; if (++arp_i == 6) arp_i = 0; }
		}
		n += arp_note - 1;
		sn_period(2, period[n > 95 ? 95 : n]);
		mus_level[2] = sn_vol(2, vol);
		arp_age++;
	} else { mus_level[2] = sn_vol(2, 0); }
	/* drums: noise envelopes. The kick drops its rate: periodic noise at
	 * 233 Hz, 116 Hz from the next tick, then voice 2's rate (a sub thump).
	 * S and C start as a kick and turn into a snare and a crash. */
	if (dvol > 0) {
		if (dage == 1 && (drum == 'k' || drum == 'S' || drum == 'C')) sn(0xE2);
		if (dage == 3 && drum == 'k') sn(0xE3);
		if (dage == 2) {
			if (drum == 'S') { drum = 's'; dvol = 52; sn(0xE5); }
			else if (drum == 'C') { drum = 'c'; dvol = 60; sn(0xE4); }
		}
		mus_level[3] = sn_vol(3, dvol >> 2);
		switch (drum) {
		case 'k': dvol -= 12; break;
		case 's': dvol -= 7; break;
		case 'h': dvol -= 16; break;
		case 'o': dvol -= 6; break;
		case 'c': dvol -= 2; break;
		case 'x': dvol -= 1; break;
		default: dvol -= 8;
		}
		dage++;
	} else { mus_level[3] = sn_vol(3, 0); }
	if (++tick == ROW_TICKS) {
		tick = 0;
		if (++row == PAT_ROWS) { row = 0; if (++ord == SONG_PATS) ord = 0; }
	}
}

/* ---- clock: the library's retrace hook, lockstep, or the PIT ------------- */

static unsigned long poll_ms0;

/* Called by the library's IRQ5 handler on its own stack (fx_irq_hook).
 * Never chain INT 0Dh to get here: this code reaches its statics through
 * SS, and an interrupt inside a DOS call arrives on DOS's stack. */
static void clock_tick(void)
{
	mus_tick();
	mus_T++;
}

void mus_stop(void)
{
	int v;
	if (hooked) { fx_irq_hook(0); hooked = 0; }
	if (playing) {
		playing = 0;
		fade = 0;
		for (v = 0; v < 4; v++) { sn((unsigned char)(0x9F | (v << 5))); mus_level[v] = 0; }
	}
}

void mus_lockstep(int on)
{
	if (on && hooked) { fx_irq_hook(0); hooked = 0; }
	lockstep = on;
}

void mus_start(int q)
{
	static int reg;
	if (!reg) { reg = 1; atexit(mus_stop); }
	mus_stop();
	quiet = q;
	if (!quiet) snd_init();
	ord = 0; row = 0; tick = 0; op = &order[0];
	memset(ch, 0, sizeof ch);
	arp_note = 0; arp_i = 0; arp_c = 0; drum = 0; dvol = 0; sweep = 0; fade = 0;
	mus_T = 0;
	mus_kick = mus_snare = mus_crash = 0;
	playing = 1;
	if (!lockstep) {
		if (fx_irq) { fx_irq_hook(clock_tick); hooked = 1; }
		else poll_ms0 = tmr_ms();
	}
}

void mus_step(int n)
{
	if (hooked) return;
	while (n-- > 0) { mus_tick(); mus_T++; }
}

/* Without IRQ5: mus_T from the PIT, the music caught up tick by tick. */
void mus_poll(void)
{
	unsigned long want;
	if (hooked || lockstep || !playing) return;
	want = (tmr_ms() - poll_ms0) * 3 / 50;
	while (mus_T < want) { mus_tick(); mus_T++; }
}
