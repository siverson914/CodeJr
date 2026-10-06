/* JRCUBE music: an SN76496 tracker run by the 60 Hz retrace interrupt.
 *
 * The song is SONG_PATS patterns of PAT_ROWS rows, ROW_TICKS ticks a row,
 * and loops to pattern 0. Every pattern is exactly PAT_TICKS long, so the
 * song position is (mus_T / PAT_TICKS) % SONG_PATS. Sections:
 *   0-1 intro, 2-3 groove, 4-7 morph (a crash on each), 8 build,
 *   9-12 drop, 13-14 explosion and back, 15-16 outro.
 */
#ifndef MUSIC_H
#define MUSIC_H

#define ROW_TICKS 7          /* 60 Hz ticks per row (about 128 BPM, 4 rows per beat) */
#define PAT_ROWS  16
#define PAT_TICKS (ROW_TICKS * PAT_ROWS)   /* 112 ticks = 1.87 s */
#define SONG_PATS 17         /* song length in patterns; it loops to 0 */

extern volatile unsigned long mus_T;     /* 60 Hz ticks since mus_start (counts in lockstep mode too) */
extern volatile unsigned char mus_kick;  /* set to 1 on every kick drum; the demo clears it */
extern volatile unsigned char mus_snare; /* set to 1 on every snare/clap; the demo clears it */
extern volatile unsigned char mus_crash; /* set to 1 on every crash */
extern volatile unsigned char mus_level[4]; /* recent volume per voice 0-15 */

void mus_init(void);        /* parse the song tables (before graphics is fine) */
void mus_start(int quiet);  /* after fx_init: hook IRQ5, start at pattern 0. quiet = no sound but mus_T
                               still runs. If fx_irq is 0 (no IRQ5), mus_T is advanced from tmr_ms() in
                               mus_poll(). */
void mus_poll(void);        /* call once per frame; only does work without IRQ5 */
void mus_lockstep(int on);  /* on: do not hook IRQ5; the demo calls mus_step(n) itself */
void mus_step(int n);       /* lockstep: advance n ticks, playing them */
void mus_log_open(void);    /* log every register write with its time to SND.BIN (render.py format).
                               Writes made inside the interrupt are not logged: use it with
                               lockstep (or without IRQ5). */
void mus_log_close(void);
void mus_stop(void);        /* silence and unhook; safe to call twice; runs at exit too */

#endif
