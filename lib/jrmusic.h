/* JRMUSIC: a small music tracker and sound-effect player for the PCjr's
 * SN76496 (3 tone voices + noise), part of JRGFX. See MUSIC.md.
 *
 * The player advances once per 60 Hz tick. With the fast path's retrace
 * interrupt (fx_irq == 1) the tick comes from fx_irq_hook, so a slow frame
 * never holds a note; without it, mus_poll() catches up from the timer.
 * Every write to the chip is made either inside that interrupt or with
 * interrupts off, so music and effects never interleave their bytes.
 * While the player is running, do not call snd_tone/snd_noise yourself:
 * use mus_sfx.
 *
 * Build: Open Watcom, large model, same flags as the library. No floating
 * point anywhere. Nothing here has run on a real PCjr yet.
 */
#ifndef JRMUSIC_H
#define JRMUSIC_H

/* ---- Song -------------------------------------------------------------- */

/* How a tone voice sounds. A note starts at `vol` and moves one step
 * towards `sus` every `rate` ticks (down: a pluck; up: a swell). After
 * that, with `fade` > 0, it drops one step every `fade` ticks to silence.
 * A voice plays an arpeggio or vibrato, not both: with arp set, vib is
 * ignored. */
typedef struct {
	unsigned char vol;   /* volume at the start of a note, 0-15            */
	unsigned char sus;   /* volume the note settles at                     */
	unsigned char rate;  /* ticks per step from vol to sus; 0: stay at vol */
	unsigned char fade;  /* then ticks per step down to 0; 0: hold         */
	unsigned char vib;   /* vibrato depth 0-4 (0 none, 2 = about 1.5% each
	                        way), 5 Hz, starting 13 ticks into the note    */
	unsigned char arp;   /* arpeggio: 0 none, else ticks per chord note
	                        (root, third, fifth); + MUS_UPDOWN walks root,
	                        third, fifth, octave and back                  */
} mus_inst;
#define MUS_UPDOWN 0x80

/* A drum: a shot of noise with a falling volume. Volumes are in quarter
 * steps (60 = full) so slow decays are possible. */
typedef struct {
	char key;            /* the character in drum patterns; 0 ends the kit
	                        (not '.', a digit, a space or '|')             */
	unsigned char n0;    /* SN76496 noise type at the start (0-7: 0-2
	                        periodic high/mid/low, 4-6 white high/mid/low,
	                        3 and 7 follow voice 2's pitch)                */
	unsigned char at;    /* tick at which it changes to n1; 0: never       */
	unsigned char n1;
	unsigned char vol;   /* volume at the start, 0-60                      */
	unsigned char vol1;  /* volume from tick `at`; 0: carry on             */
	unsigned char dec;   /* quarter steps lost per tick                    */
} mus_drum;

/* One step of the song: which pattern each voice plays (voices 0-2, then
 * drums), with which instrument, moved by how many semitones. */
typedef struct {
	unsigned char pat[4];   /* pattern numbers; MUS_NONE: silent          */
	unsigned char inst[3];  /* instrument numbers for voices 0-2          */
	signed char tr[3];      /* transpose, semitones                       */
	unsigned char flags;    /* MUS_FADE                                   */
} mus_step;
#define MUS_NONE 255
#define MUS_FADE 1          /* fade to silence over this step             */

/* A song. Fields left out at the end of the initializer are 0, which
 * means: one step playing pattern 0 of every voice, the built-in
 * instruments (0 lead, 1 bass, 2 fast arpeggio), the built-in drum kit,
 * loop from the start.
 *
 * Patterns are strings, one list per voice, each list ended by 0.
 * Tone voices: one token per row, spaces and '|' are ignored.
 *   C4 F#3 Bb2   a note (A2 to B7), sounding until the next note or '-'
 *   .            hold (nothing new on this row)
 *   -            note off
 *   A3m C4M      a chord for an arpeggio instrument (m minor, M major)
 *   E5:4 -:8     ":n" stretches a token to n rows
 * Drums: one character per row from the kit; the built-in kit has
 *   k kick, s snare, h hat, o open hat, c crash, S kick+snare,
 *   C kick+crash, x boom, 1-9 snare at rising volume (rolls), . nothing.
 * A pattern shorter than `rows` is filled up with holds. */
typedef struct {
	unsigned char tempo;          /* 60 Hz ticks per row (7: 16th notes at 128 BPM) */
	unsigned rows;                /* rows per pattern, 1-1024                */
	const char *const *pat[4];    /* voices 0-2 and drums; 0: voice unused   */
	const mus_step *order;        /* the steps; 0: play pattern 0 of each    */
	unsigned char norder;         /* MUS_COUNT(order)                        */
	unsigned char loop;           /* step to go back to at the end, or MUS_ONCE */
	const mus_inst *inst;         /* 0: the built-in three                   */
	unsigned char ninst;
	const mus_drum *kit;          /* 0: the built-in kit; at most 14 drums   */
} mus_song;
#define MUS_ONCE 255
#define MUS_COUNT(a) (sizeof (a) / sizeof (a)[0])

/* ---- Player ------------------------------------------------------------ */

/* Starts `song` from the top (0: no song, effects only), replacing the one
 * playing. Call it after fx_init. Returns 0, or a MUS_E_ code with the
 * place in mus_err; on an error nothing changes. The song is compiled into
 * a block from malloc (a byte per row and voice of every pattern, plus a
 * little); its strings and tables are not needed afterwards.
 * flags 0: with fx_irq == 1 the player takes fx_irq_hook for itself;
 * otherwise mus_poll() drives it.
 * MUS_NOHOOK: the hook is left alone, for a game with a hook of its own:
 * call mus_irq() from your hook function. mus_poll() still drives the
 * player while there is no retrace interrupt. */
int  mus_start(const mus_song *song, int flags);
#define MUS_NOHOOK 1
/* One 60 Hz tick: no DOS, no C library, no multiply or divide. Call it
 * from your own fx_irq_hook function, nowhere else in an interrupt (it
 * needs the library's stack: see fx_irq_hook). The main program may call
 * it too, to step a MUS_NOHOOK player by hand (then leave mus_poll out). */
void mus_irq(void);
/* Call once per frame, in every mode. Without the retrace interrupt it
 * plays the ticks that came due since the last call (it catches up, at
 * most MUS_CATCHUP at a time; a note can hang for as long as you stay
 * away). With it, it costs nothing and re-installs the hook if fx_init
 * took it away. Not from an interrupt. */
void mus_poll(void);
#define MUS_CATCHUP 240
void mus_stop(void);             /* silence, unhook, free; also runs at exit */
void mus_pause(void);            /* the song falls silent at once; effects still play */
void mus_resume(void);
void mus_volume(int vol);        /* song volume 0-15 (15: as written); effects keep theirs */
void mus_tempo(int ticks);       /* ticks per row from the next row on, 1-255 */
int  mus_playing(void);          /* 1: a song is sounding (not paused, not ended) */
unsigned long mus_ticks(void);   /* ticks played since the player started    */

#define MUS_E_MEM    1  /* out of memory                                  */
#define MUS_E_SYNTAX 2  /* a character the pattern language doesn't have  */
#define MUS_E_LONG   3  /* a pattern has more rows than song.rows         */
#define MUS_E_RANGE  4  /* a note below A2 or above B7                    */
#define MUS_E_SONG   5  /* tempo, rows, a pattern or instrument number, loop, kit */
extern struct mus_err_s {
	int voice;   /* 0-2, 3 = drums; -1: the song header or the order  */
	int pat;     /* pattern number (or step number for the order)     */
	int pos;     /* character in the pattern string                   */
} mus_err;

/* For the game's display, written by the player (read only): */
int mus_level(int ch);                   /* volume sounding now on channel 0-3: 0-15 */
extern volatile unsigned char mus_beat;     /* set to the drum's key on every drum; clear it yourself */
extern volatile unsigned char mus_ord;      /* step of the song being played    */
extern volatile unsigned mus_row;           /* row in the pattern               */

/* ---- Sound effects ----------------------------------------------------- */

/* An effect is a list of steps ended by one with ticks 0. It borrows one
 * channel (0-2 a tone voice, MUS_NOISE the noise) for as long as it lasts;
 * the song keeps time underneath and gets the channel back afterwards. */
typedef struct {
	unsigned div;        /* tone: SN76496 divisor, MUS_HZ(880); noise: type 0-7 */
	unsigned char vol;   /* 0-15; 0 is a pause                              */
	unsigned char ticks; /* length; 0 ends the effect                       */
	signed char slide;   /* added to div every tick (negative: pitch rises) */
} mus_fx;
#define MUS_NOISE 3
#define MUS_HZ(hz) ((unsigned)(111861UL / (hz)))   /* 110 Hz and up */
extern const unsigned mus_div[108];  /* divisor of every note, C0 = 0 to B8 (below A2: 1023) */

/* Starts `fx` on channel `ch` now, replacing an effect already there.
 * `fx` must stay valid while it plays (make it static const). Starts the
 * player (as mus_start(0, 0)) if it isn't running. */
void mus_sfx(int ch, const mus_fx *fx);
int  mus_sfx_busy(int ch);       /* 1 while an effect has the channel       */

#endif
