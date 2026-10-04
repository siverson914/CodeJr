/* JRAI: "PCjr MEETS AI", a demo for the IBM PCjr (8088, 4.77 MHz, 16 colors).
 *
 * Everything is timed by T, a 60 Hz tick counted by the vertical retrace
 * interrupt (IRQ5), which also runs the music. A part draws one frame for
 * the current T, so the demo stays in time with the music on any machine;
 * a slow machine just shows fewer frames. Mode 8 (160x200), drawn with the
 * library's fast path (lib/jrgfx.h fx_*), page flipped at retrace.
 */
#ifndef JRAI_H
#define JRAI_H

#include "jrgfx.h"

/* ---- library internals we use (lib/jrgfx.c, lib/jrtab.c) ---------------- */
extern const unsigned char jr_font[96 * 8];
extern unsigned jr_rowoff[200];
unsigned jr_alloc_high(unsigned paras);
void jr_dos_free(unsigned seg);

/* ---- time ---------------------------------------------------------------- */
extern volatile unsigned long T;   /* 60 Hz ticks since the music started   */
extern int lockstep;               /* /LOCK: T advances a fixed step a frame */
void clock_start(void);            /* hook IRQ5 (after fx_init)              */
void clock_stop(void);
void clock_poll(void);             /* advance T without IRQ5, or in lockstep */

/* ---- music and speech (music.c, speech.c) -------------------------------- */
#define ROW_TICKS 7                /* 60/7 rows a second: 128.6 bpm, 4 rows a beat */
#define PAT_ROWS 16
#define PAT_TICKS (ROW_TICKS * PAT_ROWS)
void mus_init(void);
void mus_start(int order);         /* play from this position in the song   */
void mus_stop(void);
void mus_tick(void);               /* one 60 Hz step (called from IRQ5)     */
extern volatile int mus_row;       /* rows played since mus_start           */
extern volatile unsigned char mus_level[4]; /* recent volume per voice, 0-15 (for VU, mouths) */
extern volatile unsigned char mus_beat;     /* set on every kick drum; parts clear it */
extern int mus_mute;               /* /QUIET */

typedef struct { const unsigned char far *d; unsigned n; } speech;
extern const speech sp_hello, sp_wait, sp_friends;
/* Plays a phrase now, busy, with the music paused (palette animation and
 * T keep running in IRQ5). mouth, if set, is called about 30 times a
 * second with the current loudness 0-15 so a face can move its lips. */
void say(const speech *s, void (*mouth)(int level));

/* ---- drawing helpers (draw.c) -------------------------------------------- */
extern unsigned char pal[16];      /* the palette the current part set      */
void set_pal(const unsigned char *p16);
void both_pages(void (*draw)(void)); /* draw into each page in turn         */
/* 8x8 font scaled sx by sy; pat2 as for fx_fill_rect; shadow color or -1 */
void big_text(int x, int y, const char *s, int sx, int sy, unsigned pat2, int shadow);
int  big_width(const char *s, int sx);
void center_text(int y, const char *s, int c);   /* fx_text, centered        */
unsigned rnd(void);
void rnd_seed(unsigned long s);
extern signed char sin8[256];      /* sine, amplitude 127 (draw.c)        */
extern unsigned page_seg[2];       /* the two video pages (both_pages)    */
void pages_find(void);             /* fill page_seg (flips twice)         */

/* ---- parts --------------------------------------------------------------
 * enter() draws the static parts of the scene (both pages) and may speak;
 * frame(t) draws one frame for t = ticks since the part's music started.
 * prep() runs once at startup in text mode, while CodeJr "thinks". */
typedef struct {
	const char *name;
	void (*prep)(void);
	void (*enter)(void);
	void (*frame)(unsigned t);
	void (*leave)(void);
	int ord, npat;        /* where the part starts in the song, how long */
} part;
extern const part part_plasma, part_fire, part_tunnel, part_meet, part_scroll;

void boot_run(void (*work)(int step), int nsteps); /* text mode intro */
void boot_end(void);                               /* "Done." and the glitch */

extern int opt_bench;
int mus_at(void);         /* song position playing now, -1 if stopped */

#endif
