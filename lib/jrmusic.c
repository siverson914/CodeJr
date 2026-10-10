/* JRMUSIC: music tracker and sound effects for the PCjr's SN76496.
 * See jrmusic.h for the interface and MUSIC.md for how to write a song.
 *
 * Layout of this file: the divisor table, the song compiler (main program
 * only), the player (runs in the retrace interrupt: short, integer only,
 * no DOS, no C library), then the clock and the public calls.
 *
 * The rule that keeps the chip's two-byte writes whole: while the player
 * runs, everything that writes the chip runs either inside the interrupt
 * or with interrupts off.
 */
#include <dos.h>
#include <conio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "jrgfx.h"
#include "jrmusic.h"

#pragma off (check_stack) /* the player runs inside IRQ5, on the library's stack */

/* Interrupts off, returning the flags as they were; and back. Not
 * _disable/_enable: a call made with interrupts already off must leave
 * them off. */
extern unsigned mus_ioff(void);
#pragma aux mus_ioff = "pushf" "pop ax" "cli" value [ax];
extern void mus_ion(unsigned f);
#pragma aux mus_ion = "push ax" "popf" parm [ax];

/* SN76496 divisor per note, C0 = 0: 3579545 / 32 / hz, rounded, equal
 * temperament from A4 = 440 Hz. Ten bits, so the lowest note is A2 (110
 * Hz, 1017); lower ones are clamped to 1023. Worked out ahead of time:
 * no floating point runs on the PCjr. */
#define NNOTE 96    /* notes a pattern can name or reach by transposing */
#define NOTE_LO 33  /* A2 */
#define VIB_DELAY 12 /* ticks a note sounds plain before vibrato sets in */
/* ...and one more octave, so a chord's top note needs no range check */
const unsigned mus_div[NNOTE + 12] = {
	1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023,  /* C0 */
	1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023,  /* C1 */
	1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1023, 1017,  960,  906,  /* C2 */
	 855,  807,  762,  719,  679,  641,  605,  571,  539,  508,  480,  453,  /* C3 */
	 428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,  /* C4 */
	 214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,  /* C5 */
	 107,  101,   95,   90,   85,   80,   76,   71,   67,   64,   60,   57,  /* C6 */
	  53,   50,   48,   45,   42,   40,   38,   36,   34,   32,   30,   28,  /* C7 */
	  27,   25,   24,   22,   21,   20,   19,   18,   17,   16,   15,   14,  /* C8 */
};

/* The built-in sounds: 0 lead (pluck, vibrato), 1 bass, 2 fast arpeggio. */
static const mus_inst def_inst[3] = {
	{ 13, 10, 2, 0, 2, 0 },
	{ 15,  9, 1, 0, 0, 0 },
	{ 11,  9, 2, 0, 0, 1 },
};
/* The built-in kit (JRCUBE's). The kick is periodic noise dropping an
 * octave after a tick; S and C start as a kick and turn into a snare and
 * a crash, since there is only one noise voice. */
#define MAXKIT 14
static const mus_drum def_kit[] = {
	{ 'k', 1, 1, 2, 60,  0, 12 },
	{ 's', 5, 0, 0, 56,  0,  7 },
	{ 'h', 4, 0, 0, 36,  0, 16 },
	{ 'o', 4, 0, 0, 40,  0,  6 },
	{ 'c', 4, 0, 0, 60,  0,  2 },
	{ 'S', 1, 2, 5, 60, 52,  7 },
	{ 'C', 1, 2, 4, 60, 60,  2 },
	{ 'x', 6, 0, 0, 60,  0,  1 },
	{ 0 }
};
static const mus_step def_step = { { 0, 0, 0, 0 }, { 0, 1, 2 }, { 0, 0, 0 }, 0 };

/* ---- What the player works on ------------------------------------------ */

/* A sounding note: its envelope and, for arpeggio and vibrato alike, a
 * short list of divisors it steps through (`seq`). The first 12 bytes of
 * a vstate are copied from the instrument when the note is prepared.
 * vtick below is assembly and knows these offsets: keep the layout
 * checks under it true. */
#define B_ENV 1   /* busy: the envelope is moving             */
#define B_SEQ 2   /* busy: the divisor steps through seq      */
typedef struct {
	unsigned char vol;    /*  0 volume now                               */
	unsigned char sus;    /*  1 where the first stage ends               */
	unsigned char rate;   /*  2 ticks per step of the first stage        */
	unsigned char fade;   /*  3 ticks per step of the fade; 0: hold      */
	unsigned char stage;  /*  4 0 towards sus, 1 fading                  */
	unsigned char cnt;    /*  5 ticks to the next envelope step          */
	unsigned char busy;   /*  6 B_ flags; 0: nothing to do per tick      */
	unsigned char sspd;   /*  7 ticks per seq entry                      */
	unsigned char sn2;    /*  8 seq entries x 2                          */
	unsigned char sdel;   /*  9 ticks before seq starts (vibrato delay)  */
	unsigned char si2;    /* 10 seq entry now x 2                        */
	unsigned char sc;     /* 11 ticks on this entry                      */
} vhead;
typedef struct {
	vhead h;
	unsigned per;         /* 12 divisor of the note itself               */
	unsigned seq[12];     /* 14 arpeggio: root, third, fifth, octave,
	                            fifth, third; vibrato: one cycle         */
} vstate;
typedef struct {          /* an instrument, ready to copy */
	vhead h;
	unsigned char vsh;    /* vibrato: divisor >> vsh is one unit; 0: seq is a chord */
	unsigned char pad;
} pinst;
typedef struct {          /* a drum of the kit, 8 bytes so that indexing is a shift */
	unsigned char key, n0, at, n1, vol, vol1, dec, pad;
} kdrum;

/* A song as the player wants it: one block of memory (buf) holding
 * - the patterns, one byte per row: tone voices 0 hold, 1 off, else
 *   note + 2 (bit 7: minor chord); drums 0 nothing, else kit entry + 1
 *   (high nibble: a volume in place of the drum's own, 0 none);
 * - the instruments as pinst, the order as cstep.
 * Everything that can be worked out ahead (pattern addresses, how an
 * envelope starts) is, here, by the main program. The song's own strings
 * and tables are not used again after mus_start. */
#define NOPAT 0xFFFFu
typedef struct {
	unsigned off[4];      /* this step's pattern per voice, from buf; NOPAT */
	unsigned ioff[3];     /* its instrument, from buf                       */
	signed char tr[3];
	unsigned char flags;
} cstep;
typedef struct {
	unsigned char far *buf;
	const cstep far *steps;    /* in buf */
	const cstep far *loop_p;   /* the step to go back to */
	unsigned rows;
	unsigned char tempo, norder, loop, nkit;
	kdrum kit[MAXKIT];
} songst;

static songst cs;              /* the song playing */
static songst ns;              /* the one mus_start is compiling */

struct mus_err_s mus_err;
volatile unsigned char mus_beat, mus_ord;
volatile unsigned mus_row;

/* The state the per-tick code reads and writes, in one near block that
 * the assembly reaches through BP. Per channel things are arrays of 4
 * words (voices 0-2, noise) even where a byte would do, so that one
 * index register serves them all; only the low byte is used. */
#define P_NOTE 1
#define P_OFF  2
#define P_DSTOP 0xFF
static struct pblk {
	vstate __near *cur[4];  /*  0 the note sounding per voice              */
	vstate __near *nxt[4];  /*  8 where the next one is prepared           */
	unsigned w_per[4];      /* 16 what the song wants: divisor             */
	unsigned sh_per[4];     /* 24 what the chip has: divisor               */
	unsigned w_vol[4];      /* 32 what the song wants: volume              */
	unsigned level[4];      /* 40 what the chip has: volume                */
	unsigned sfx_on[4];     /* 48 an effect has the channel                */
	unsigned pend[4];       /* 56 the prepared row: P_ per voice, the drum byte */
	unsigned regv[4];       /* 64 volume byte for volume 0; xor the volume in */
	unsigned reg[4];        /* 72 first byte of a divisor write            */
	unsigned char cut;      /* 80 volume taken off the song: master + fade */
	unsigned char dbusy;    /* 81 a drum is sounding                       */
	unsigned char dnew;     /* 82 ...and has just been hit                 */
	unsigned char dat;      /* 83 the tick at which it changes type; 0 no  */
	unsigned char dage;     /* 84 its age in ticks                         */
	unsigned char dn1;      /* 85 the type it changes to                   */
	unsigned char dvol1;    /* 86 ...and the volume, 0: carry on           */
	unsigned char ddec;     /* 87 quarter steps lost per tick              */
	unsigned char n_type;   /* 88 noise type the song wants                */
	unsigned char n_trig;   /* 89 it was hit while an effect had the noise */
	unsigned char sh_noise; /* 90 what the chip has: noise type            */
	unsigned char pad;
	int dvol;               /* 92 drum volume, quarter steps               */
} P = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
        { 0x9F, 0xBF, 0xDF, 0xFF }, { 0x80, 0xA0, 0xC0, 0xE0 } };

/* ---- Compiler (main program) ------------------------------------------- */

static int fail(int code, int voice, int pat, int pos)
{
	mus_err.voice = voice; mus_err.pat = pat; mus_err.pos = pos;
	return code;
}

/* ":n" after a token: n rows, 1-1024. Returns 0 if it is malformed. */
static unsigned parse_len(const char **pp)
{
	const char *p = *pp;
	unsigned n = 0;
	int k = 0;
	if (*p != ':') return 1;
	p++;
	while (*p >= '0' && *p <= '9' && k < 4) { n = n * 10 + (unsigned)(*p - '0'); p++; k++; }
	if (!k || n > 1024 || (*p >= '0' && *p <= '9')) return 0;
	*pp = p;
	return n;
}

/* One tone pattern into rows bytes at o. 0 or a MUS_E_ code, with the
 * character it stopped at in *pos. */
static int parse_tone(const char *s, unsigned char far *o, unsigned rows, int *pos)
{
	static const signed char semi[7] = { 9, 11, 0, 2, 4, 5, 7 }; /* A..G */
	const char *p = s;
	unsigned r = 0, len;
	unsigned char b;
	int n;
	for (;;) {
		char c = *p;
		*pos = (int)(p - s);
		if (c == ' ' || c == '|' || c == '\t' || c == '\n') { p++; continue; }
		if (!c) break;
		if (c == '.') { b = 0; p++; }
		else if (c == '-') { b = 1; p++; }
		else if (c >= 'A' && c <= 'G') {
			n = semi[c - 'A']; p++;
			if (*p == '#') { n++; p++; }
			else if (*p == 'b') { n--; p++; }
			if (*p < '0' || *p > '7') return MUS_E_SYNTAX;
			n += (*p++ - '0') * 12;
			if (n < NOTE_LO || n >= NNOTE) return MUS_E_RANGE;
			b = (unsigned char)(n + 2);
			if (*p == 'm') { b |= 0x80; p++; }
			else if (*p == 'M') p++;
		}
		else return MUS_E_SYNTAX;
		len = parse_len(&p);
		if (!len) return MUS_E_SYNTAX;
		if (len > rows - r) return MUS_E_LONG;   /* r <= rows always */
		o[r++] = b;
		while (--len) o[r++] = 0;
	}
	while (r < rows) o[r++] = 0;
	return 0;
}

static int kit_find(char c)
{
	int i;
	for (i = 0; i < ns.nkit; i++) if (ns.kit[i].key == (unsigned char)c) return i;
	return -1;
}

static int parse_drum(const char *s, unsigned char far *o, unsigned rows, int *pos)
{
	const char *p = s;
	unsigned r = 0;
	unsigned char b;
	int k;
	for (;;) {
		char c = *p;
		*pos = (int)(p - s);
		if (c == ' ' || c == '|' || c == '\t' || c == '\n') { p++; continue; }
		if (!c) break;
		if (c == '.') b = 0;
		else if ((k = kit_find(c)) >= 0) b = (unsigned char)(k + 1);
		else if (c >= '1' && c <= '9' && (k = kit_find('s')) >= 0)
			b = (unsigned char)((k + 1) | ((6 + c - '0') << 4)); /* a roll: snare at volume 7-15 */
		else return MUS_E_SYNTAX;
		if (r >= rows) return MUS_E_LONG;
		o[r++] = b;
		p++;
	}
	while (r < rows) o[r++] = 0;
	return 0;
}

/* Checks s and fills ns (ns.buf malloc'd). 0 or a MUS_E_ code. */
static int compile(const mus_song *s)
{
	unsigned long total = 0;
	unsigned off = 0, voff[4], ibase;
	unsigned char npat[4];
	const mus_drum *kit = s->kit ? s->kit : def_kit;
	const mus_inst *insts = s->inst ? s->inst : def_inst;
	const mus_step *steps = s->order ? s->order : &def_step;
	int ninst = s->inst ? s->ninst : 3;
	int norder = s->order ? s->norder : 1;
	pinst far *pi;
	cstep far *cst;
	int v, i, e, pos;

	if (!s->tempo || !s->rows || s->rows > 1024) return fail(MUS_E_SONG, -1, 0, 0);
	if (!norder || (s->loop != MUS_ONCE && s->loop >= norder)) return fail(MUS_E_SONG, -1, 0, 0);
	ns.tempo = s->tempo; ns.rows = s->rows; ns.loop = s->loop; ns.norder = (unsigned char)norder;
	for (i = 0; kit[i].key; i++) {
		if (i == MAXKIT || kit[i].n0 > 7 || kit[i].n1 > 7 || kit[i].vol > 60 || kit[i].vol1 > 60)
			return fail(MUS_E_SONG, 3, i, 0);
		ns.kit[i].key = (unsigned char)kit[i].key;
		ns.kit[i].n0 = kit[i].n0; ns.kit[i].at = kit[i].at; ns.kit[i].n1 = kit[i].n1;
		ns.kit[i].vol = kit[i].vol; ns.kit[i].vol1 = kit[i].vol1; ns.kit[i].dec = kit[i].dec;
	}
	ns.nkit = (unsigned char)i;
	for (v = 0; v < 4; v++) {
		i = 0;
		if (s->pat[v]) while (s->pat[v][i]) if (++i == 255) return fail(MUS_E_SONG, v, i, 0);
		npat[v] = (unsigned char)i;
		total += (unsigned long)i * s->rows;
	}
	for (i = 0; i < ninst; i++)
		if (insts[i].vol > 15 || insts[i].sus > 15 || insts[i].vib > 4 || insts[i].arp == MUS_UPDOWN)
			return fail(MUS_E_SONG, -1, i, 0);
	for (e = 0; e < norder; e++) {
		for (v = 0; v < 4; v++)
			if (npat[v] && steps[e].pat[v] != MUS_NONE && steps[e].pat[v] >= npat[v]) return fail(MUS_E_SONG, v, e, 0);
		for (v = 0; v < 3; v++)
			if (npat[v] && steps[e].inst[v] >= ninst) return fail(MUS_E_SONG, v, e, 0);
	}
	total += (unsigned long)ninst * sizeof(pinst) + (unsigned long)norder * sizeof(cstep);
	if (total > 0xFF00UL) return fail(MUS_E_MEM, -1, 0, 0);
	ns.buf = (unsigned char far *)malloc((size_t)total);
	if (!ns.buf) return fail(MUS_E_MEM, -1, 0, 0);
	for (v = 0; v < 4; v++) {
		voff[v] = off;
		for (i = 0; i < npat[v]; i++, off += ns.rows) {
			e = v < 3 ? parse_tone(s->pat[v][i], ns.buf + off, ns.rows, &pos)
			          : parse_drum(s->pat[v][i], ns.buf + off, ns.rows, &pos);
			if (e) { free(ns.buf); ns.buf = 0; return fail(e, v, i, pos); }
		}
	}
	ibase = off;
	pi = (pinst far *)(ns.buf + off);
	for (i = 0; i < ninst; i++, pi++, off += sizeof(pinst)) {
		const mus_inst *in = &insts[i];
		_fmemset(pi, 0, sizeof(pinst));
		pi->h.vol = in->vol; pi->h.sus = in->sus; pi->h.rate = in->rate; pi->h.fade = in->fade;
		if (in->rate && in->vol != in->sus) { pi->h.cnt = in->rate; pi->h.busy = B_ENV; }
		else if (in->fade && in->vol) { pi->h.stage = 1; pi->h.cnt = in->fade; pi->h.busy = B_ENV; }
		if (in->arp) {
			/* the tick a note starts on is the root's first: seq goes on from there */
			pi->h.sspd = (unsigned char)(in->arp & 0x7F);
			pi->h.sn2 = (unsigned char)(in->arp & MUS_UPDOWN ? 12 : 6);
			if (pi->h.sspd > 1) pi->h.sc = 1; else pi->h.si2 = 2;
			pi->h.busy |= B_SEQ;
		} else if (in->vib) {      /* an arpeggio has no vibrato */
			pi->h.sspd = 1; pi->h.sn2 = 24; pi->h.sdel = VIB_DELAY;
			pi->vsh = (unsigned char)(9 - in->vib);
			pi->h.busy |= B_SEQ;
		}
	}
	cst = (cstep far *)(ns.buf + off);
	ns.steps = cst;
	ns.loop_p = cst + (ns.loop == MUS_ONCE ? 0 : ns.loop);
	for (e = 0; e < norder; e++, cst++) {
		const mus_step *st = &steps[e];
		for (v = 0; v < 4; v++)
			cst->off[v] = st->pat[v] < npat[v] ? voff[v] + st->pat[v] * ns.rows : NOPAT;
		for (v = 0; v < 3; v++) {
			cst->ioff[v] = ibase + (st->inst[v] < ninst ? st->inst[v] : 0) * sizeof(pinst);
			cst->tr[v] = st->tr[v];
		}
		cst->flags = st->flags;
	}
	return 0;
}

/* ---- Player (interrupt) ------------------------------------------------ */

#define M_OFF  0
#define M_POLL 1   /* mus_poll counts the ticks              */
#define M_IRQ  2   /* our function is the library's hook     */
#define M_EXT  3   /* the game calls mus_irq (MUS_NOHOOK); mus_poll
                      counts them while there is no retrace interrupt */
static unsigned char engine, mode;
static unsigned char song_on, paused;
static unsigned long ticks;
static unsigned tick_n;               /* low word of ticks, readable in one go */

/* The player's own data is all near (in DGROUP): in the large model a
 * plain pointer is far, and loading a segment register for every field
 * would double the cost of a tick.
 *
 * The work of a row is spread over ticks so that none is long. The last
 * four ticks of a row prepare the next one, a stage each, with no sound:
 * the step's patterns and instruments if a new step begins, then one
 * voice per tick, each new note built completely in the voice's spare
 * vstate. The first tick of the row only swaps the states and writes the
 * chip. With fewer than four ticks a row the stages share ticks. */
static vstate vs[3][2];
static pinst vin[3];                  /* the prepared step's instrument per voice */
static signed char vtr[3];
static unsigned char pstage;          /* stages of the next row done, 0-4 */
static unsigned char pend_end;        /* the prepared row is the end of the song */
static unsigned prow;                 /* its row and step, for mus_row and mus_ord */
static unsigned char pord;
static const cstep far *fstp;         /* where the stages are: step, row */
static unsigned char ford;
static unsigned frow;
static unsigned char far *pp[4];      /* that step's pattern per voice */
static unsigned char phas[4];         /* 0: the voice has none         */
static unsigned char left;            /* ticks to the first tick of the next row */
static unsigned char tempo;           /* ticks per row from the next row on */
static unsigned char fade_on, fade_att, mcut; /* fade and master: volume taken off */
static unsigned char fade_new;        /* fade_att of the prepared row */
static unsigned fade_acc;
static unsigned char dirty;           /* channels for flush(), a bit each */

typedef struct {
	const mus_fx *p;      /* next step */
	unsigned div;
	unsigned char vol, left, trig;
	signed char slide;
} sfx_t;
static sfx_t sx[4];
static unsigned char sfx_any;

static const unsigned char bit[4] = { 1, 2, 4, 8 };

#ifdef MUS_TRACE
static unsigned tr_notes[4];   /* for lib/test/tmusic.c: notes prepared per voice */
#endif

static void sn(unsigned char b)
{
	outp(0xC0, b);
}

/* One tick of the three voices and the drum: starts the prepared notes
 * (cm: first tick of a row), steps arpeggio, vibrato and envelopes, and
 * writes what changed to the chip unless an effect has the channel.
 *
 * This runs 60 times a second inside an interrupt, so it is assembly
 * (about half the instructions of the compiler's version, and shorter
 * ones). vtick_c is the same thing in C: it is the specification, it is
 * used in place of the assembly with -dMUS_C_TICK, and lib/test/tmusic.sh
 * checks that both send the same bytes to the chip.
 *
 * In the assembly BP points at P (so P is reached through SS, which is
 * DGROUP wherever the player runs: see mus_irq) and DS is set to SS for
 * the voice in BX. SI is the voice x 2, CH the changes (1 divisor, 2
 * volume), DX the chip's port. Numbers are offsets in P and in vstate. */
typedef char mus_chk_v[offsetof(vstate, h.sus) == 1 && offsetof(vstate, h.rate) == 2 && offsetof(vstate, h.fade) == 3 &&
	offsetof(vstate, h.stage) == 4 && offsetof(vstate, h.cnt) == 5 && offsetof(vstate, h.busy) == 6 &&
	offsetof(vstate, h.sspd) == 7 && offsetof(vstate, h.sn2) == 8 && offsetof(vstate, h.sdel) == 9 &&
	offsetof(vstate, h.si2) == 10 && offsetof(vstate, h.sc) == 11 && offsetof(vstate, per) == 12 &&
	offsetof(vstate, seq) == 14 ? 1 : -1];
typedef char mus_chk_p[offsetof(struct pblk, nxt) == 8 && offsetof(struct pblk, w_per) == 16 &&
	offsetof(struct pblk, sh_per) == 24 && offsetof(struct pblk, w_vol) == 32 && offsetof(struct pblk, level) == 40 &&
	offsetof(struct pblk, sfx_on) == 48 && offsetof(struct pblk, pend) == 56 && offsetof(struct pblk, regv) == 64 &&
	offsetof(struct pblk, reg) == 72 && offsetof(struct pblk, cut) == 80 && offsetof(struct pblk, dbusy) == 81 &&
	offsetof(struct pblk, dnew) == 82 && offsetof(struct pblk, dat) == 83 && offsetof(struct pblk, dage) == 84 &&
	offsetof(struct pblk, dn1) == 85 && offsetof(struct pblk, dvol1) == 86 && offsetof(struct pblk, ddec) == 87 &&
	offsetof(struct pblk, n_type) == 88 && offsetof(struct pblk, n_trig) == 89 && offsetof(struct pblk, sh_noise) == 90 &&
	offsetof(struct pblk, dvol) == 92 ? 1 : -1];
#ifndef MUS_C_TICK
extern void mus_vtick(struct pblk __near *p, unsigned char cm);
#pragma aux mus_vtick = \
	"push ds" "push bp" "push ss" "pop ds" "mov bp,ax" "mov dx,0C0h" "xor si,si" \
	/* a voice: start or stop it if the row says so */ \
	"VL:" "mov bx,[bp+si]" "xor ch,ch" "test cl,cl" "jz VT" \
	"mov al,[bp+si+56]" "test al,al" "jz VT" "dec al" "jz VN" \
	"mov byte ptr [bx+6],0" "mov byte ptr [bx],0" "mov ch,2" "jmp VW" \
	"VN:" "mov ax,[bp+si+8]" "mov [bp+si+8],bx" "mov bx,ax" "mov [bp+si],bx" \
	"mov ax,[bx+12]" "mov [bp+si+16],ax" "mov ch,3" "jmp VW" \
	"VT:" "mov al,[bx+6]" "test al,al" "jnz VT1" "jmp VW" \
	/* arpeggio or vibrato: the next divisor of seq */ \
	"VT1:" "test al,2" "jz VE" \
	"cmp byte ptr [bx+9],0" "je VS1" "dec byte ptr [bx+9]" "jmp VE" \
	"VS1:" "mov al,[bx+10]" "xor ah,ah" "mov di,ax" \
	"mov ax,[bx+di+14]" "mov [bp+si+16],ax" "or ch,1" \
	"mov al,[bx+11]" "inc al" "cmp al,[bx+7]" "jb VS3" \
	"mov ax,di" "add al,2" "cmp al,[bx+8]" "jb VS2" "xor al,al" \
	"VS2:" "mov [bx+10],al" "xor al,al" \
	"VS3:" "mov [bx+11],al" \
	/* envelope */ \
	"VE:" "test byte ptr [bx+6],1" "jz VW" "dec byte ptr [bx+5]" "jnz VW" \
	"or ch,2" "mov al,[bx]" "cmp byte ptr [bx+4],0" "jne VE5" \
	"cmp al,[bx+1]" "jb VE1" "dec al" "jmp VE2" \
	"VE1:" "inc al" \
	"VE2:" "mov [bx],al" "cmp al,[bx+1]" "je VE3" \
	"mov ah,[bx+2]" "mov [bx+5],ah" "jmp VW" \
	"VE3:" "mov ah,[bx+3]" "test ah,ah" "jz VE4" "test al,al" "jz VE4" \
	"mov byte ptr [bx+4],1" "mov [bx+5],ah" "jmp VW" \
	"VE4:" "and byte ptr [bx+6],0FEh" "jmp VW" \
	"VE5:" "dec al" "mov [bx],al" "jz VE6" \
	"mov ah,[bx+3]" "mov [bx+5],ah" "jmp VW" \
	"VE6:" "mov byte ptr [bx+6],0" \
	/* what changed goes to the chip, unless an effect has the voice */ \
	"VW:" "test ch,ch" "jz VX" \
	"test ch,2" "jz VW1" "mov al,[bx]" "mov [bp+si+32],al" \
	"VW1:" "cmp byte ptr [bp+si+48],0" "jne VX" \
	"test ch,1" "jz VW2" "mov ax,[bp+si+16]" "cmp ax,[bp+si+24]" "je VW2" \
	"mov [bp+si+24],ax" "mov di,ax" "and al,0Fh" "or al,[bp+si+72]" "out dx,al" \
	"mov ax,di" "shr ax,1" "shr ax,1" "shr ax,1" "shr ax,1" "out dx,al" \
	"VW2:" "test ch,2" "jz VX" \
	"mov al,[bx]" "sub al,[bp+80]" "jnc VW3" "xor al,al" \
	"VW3:" "cmp al,[bp+si+40]" "je VX" \
	"mov [bp+si+40],al" "xor al,[bp+si+64]" "out dx,al" \
	"VX:" "add si,2" "cmp si,6" "jae VD" "jmp VL" \
	/* the drum: a new hit or its change of noise type, then its volume */ \
	"VD:" "cmp byte ptr [bp+81],0" "jne DG" "jmp DZ" \
	"DG:" "xor ch,ch" "cmp byte ptr [bp+82],0" "je D1" "mov byte ptr [bp+82],0" "mov ch,1" \
	"D1:" "mov al,[bp+83]" "test al,al" "jz D2" "cmp al,[bp+84]" "jne D2" \
	"mov al,[bp+85]" "mov [bp+88],al" "mov ch,1" \
	"mov al,[bp+86]" "test al,al" "jz D2" "xor ah,ah" "mov [bp+92],ax" \
	"D2:" "test ch,ch" "jz D4" \
	"cmp byte ptr [bp+54],0" "je D3" "mov byte ptr [bp+89],1" "jmp D4" \
	"D3:" "mov al,[bp+88]" "mov [bp+90],al" "or al,0E0h" "out dx,al" \
	"D4:" "mov ax,[bp+92]" "test ax,ax" "jg D5" \
	"mov byte ptr [bp+81],0" "xor al,al" "jmp D6" \
	"D5:" "mov di,ax" "shr al,1" "shr al,1" "mov ch,al" \
	"mov al,[bp+87]" "xor ah,ah" "sub di,ax" "mov [bp+92],di" \
	"cmp byte ptr [bp+84],255" "je D5A" "inc byte ptr [bp+84]" \
	"D5A:" "mov al,ch" \
	"D6:" "mov [bp+38],al" "cmp byte ptr [bp+54],0" "jne DZ" \
	"sub al,[bp+80]" "jnc D7" "xor al,al" \
	"D7:" "cmp al,[bp+46]" "je DZ" \
	"mov [bp+46],al" "xor al,[bp+70]" "out dx,al" \
	"DZ:" "pop bp" "pop ds" \
	parm [ax] [cl] modify [ax bx cx dx si di];
#define vtick(cm) mus_vtick((struct pblk __near *)&P, cm)
#else
static void vtick_c(unsigned char cm)
{
	vstate __near *s;
	unsigned char f, ch, a;
	unsigned p;
	int v;
	for (v = 0; v < 3; v++) {
		s = P.cur[v]; ch = 0;        /* ch: 1 divisor changed, 2 volume changed */
		if (cm && P.pend[v]) {
			if ((unsigned char)P.pend[v] != P_NOTE) {
				s->h.busy = 0; s->h.vol = 0; ch = 2;
				goto write;
			}
			P.cur[v] = P.nxt[v]; P.nxt[v] = s; s = P.cur[v];
			P.w_per[v] = s->per;
			ch = 3;
			goto write;          /* arpeggio, vibrato and envelope run from the next tick */
		}
		f = s->h.busy;
		if (!f) goto write;
		if (f & B_SEQ) {
			if (s->h.sdel) s->h.sdel--;
			else {
				P.w_per[v] = s->seq[s->h.si2 >> 1]; ch |= 1;
				a = (unsigned char)(s->h.sc + 1);
				if (a >= s->h.sspd) {
					a = (unsigned char)(s->h.si2 + 2);
					if (a >= s->h.sn2) a = 0;
					s->h.si2 = a;
					a = 0;
				}
				s->h.sc = a;
			}
		}
		if ((f & B_ENV) && --s->h.cnt == 0) {
			ch |= 2;
			a = s->h.vol;
			if (s->h.stage == 0) {
				if (a < s->h.sus) a++; else a--;
				s->h.vol = a;
				if (a != s->h.sus) s->h.cnt = s->h.rate;
				else if (s->h.fade && a) { s->h.stage = 1; s->h.cnt = s->h.fade; }
				else s->h.busy &= ~B_ENV;
			} else {
				s->h.vol = --a;
				if (a) s->h.cnt = s->h.fade;
				else s->h.busy = 0;            /* faded out: nothing left to do */
			}
		}
	write:
		if (!ch) continue;
		if (ch & 2) P.w_vol[v] = s->h.vol;
		if (P.sfx_on[v]) continue;
		if (ch & 1) {
			p = P.w_per[v];
			if (p != P.sh_per[v]) {
				P.sh_per[v] = p;
				sn((unsigned char)(P.reg[v] | (p & 15)));
				sn((unsigned char)(p >> 4));
			}
		}
		if (ch & 2) {
			a = s->h.vol;
			a = (unsigned char)(a > P.cut ? a - P.cut : 0);
			if (a != (unsigned char)P.level[v]) {
				P.level[v] = a;
				sn((unsigned char)(P.regv[v] ^ a));
			}
		}
	}
	if (!P.dbusy) return;
	ch = 0;                          /* 1: the noise type changes */
	if (P.dnew) { P.dnew = 0; ch = 1; }
	if (P.dat && P.dat == P.dage) {
		P.n_type = P.dn1; ch = 1;
		if (P.dvol1) P.dvol = P.dvol1;
	}
	if (ch) {
		if (P.sfx_on[3]) P.n_trig = 1;       /* flush starts it when the effect is over */
		else { P.sh_noise = P.n_type; sn((unsigned char)(0xE0 | P.n_type)); }
	}
	if (P.dvol > 0) {
		a = (unsigned char)(P.dvol >> 2);
		P.dvol -= P.ddec;
		if (P.dage != 255) P.dage++;
	} else { a = 0; P.dbusy = 0; }
	P.w_vol[3] = a;
	if (P.sfx_on[3]) return;
	a = (unsigned char)(a > P.cut ? a - P.cut : 0);
	if (a != (unsigned char)P.level[3]) {
		P.level[3] = a;
		sn((unsigned char)(P.regv[3] ^ a));
	}
}
#define vtick(cm) vtick_c(cm)
#endif

/* Makes the chip match what the effects and the song want, for the
 * channels in `d`, writing only what changed: for a channel an effect
 * has or gives back, after a pause, when the volume changes. Interrupts
 * are off here and in vtick, always (mus_poll turns them off around each
 * tick too), so a two-byte divisor write is never split. */
static void flush(unsigned char d)
{
	unsigned char live = (unsigned char)(song_on && !paused);
	unsigned p;
	int c, v;
	for (c = 0; c < 3; c++) {
		if (!(d & bit[c])) continue;
		if (P.sfx_on[c]) {
			/* an effect has the voice: its divisor goes out even when
			 * silent (noise types 3 and 7 follow voice 2's divisor) */
			p = sx[c].div; v = sx[c].vol;
		} else {
			v = live ? (int)P.w_vol[c] - P.cut : 0;
			if (v <= 0) { v = 0; goto vol; }
			p = P.w_per[c];
		}
		if (p != P.sh_per[c]) {
			P.sh_per[c] = p;
			sn((unsigned char)(P.reg[c] | (p & 15)));
			sn((unsigned char)(p >> 4));
		}
	vol:
		if ((unsigned)v != P.level[c]) {
			P.level[c] = v;
			sn((unsigned char)(P.regv[c] ^ v));
		}
	}
	if (!(d & 8)) return;
	if (P.sfx_on[3]) {
		p = sx[3].div; v = sx[3].vol;
		c = sx[3].trig; sx[3].trig = 0;
	} else {
		p = P.n_type;
		v = live ? (int)P.w_vol[3] - P.cut : 0;
		if (v < 0) v = 0;
		c = P.n_trig; P.n_trig = 0;
	}
	/* writing the noise register restarts the noise, so only for a new
	 * hit or a new type */
	if (v && (c || (unsigned char)p != P.sh_noise)) {
		P.sh_noise = (unsigned char)p;
		sn((unsigned char)(0xE0 | p));
	}
	if ((unsigned)v != P.level[3]) {
		P.level[3] = v;
		sn((unsigned char)(0xFF ^ v));
	}
}

/* Stage 0 of preparing a row, when it is the first row of a step (or the
 * song loops or ends here): the step's patterns, instruments and
 * transposes. Nothing else shares this stage: a new step is the most
 * work of all. */
static void stage_step(void)
{
	const cstep far *st;
	int v;
	if (ford >= cs.norder) {
		if (cs.loop == MUS_ONCE) { pend_end = 1; return; }
		ford = cs.loop; fstp = cs.loop_p;
	}
	st = fstp;
	for (v = 0; v < 4; v++) {
		unsigned o = st->off[v];
		phas[v] = (unsigned char)(o != NOPAT);
		pp[v] = cs.buf + o;
	}
	for (v = 0; v < 3; v++) {
		if (phas[v]) vin[v] = *(const pinst far *)(cs.buf + st->ioff[v]);
		vtr[v] = st->tr[v];
	}
	fade_on = (unsigned char)(st->flags & MUS_FADE);
	fade_acc = 0; fade_new = 0;
}

/* Stages 1-3: one voice's token of the row (voice 2 first, then 1, then 0
 * and the drums). A new note is built in the voice's spare state; nothing
 * sounds yet. */
static void stage_voice(int v)
{
	vstate __near *n;
	unsigned char b;
	unsigned p, u, a, c;
	int k;
	if (v == 2) {
		prow = frow; pord = ford;
		if (fade_on) {
			/* 15 volume steps spread over the pattern, without dividing */
			fade_acc += 15;
			while (fade_acc >= cs.rows) { fade_acc -= cs.rows; fade_new++; }
		}
	}
	if (!phas[v]) P.pend[v] = frow ? 0 : P_OFF;
	else if ((b = pp[v][frow]) < 2) P.pend[v] = b ? P_OFF : 0;
	else {
		P.pend[v] = P_NOTE;
#ifdef MUS_TRACE
		tr_notes[v]++;
#endif
		n = P.nxt[v];
		n->h = vin[v].h;
		k = (b & 0x7F) - 2 + vtr[v];
		if (k < 0) k = 0;
		if (k > NNOTE - 1) k = NNOTE - 1;
		n->per = p = mus_div[k];
		if (!(n->h.busy & B_SEQ)) ;
		else if (!vin[v].vsh) {
			a = mus_div[k + (b & 0x80 ? 3 : 4)];
			u = mus_div[k + 7];
			n->seq[0] = p; n->seq[1] = a; n->seq[2] = u;
			n->seq[3] = mus_div[k + 12]; n->seq[4] = u; n->seq[5] = a;
		} else {
			/* one cycle of vibrato: up one unit, two for three ticks, back, and the same below */
			u = p >> vin[v].vsh;
			a = p + u; c = a + u;
			if (c > 1023) { c = 1023; if (a > 1023) a = 1023; }
			n->seq[0] = n->seq[6] = p;
			n->seq[1] = n->seq[5] = a;
			n->seq[2] = n->seq[3] = n->seq[4] = c;
			a = p - u; c = a - u;
			n->seq[7] = n->seq[11] = a;
			n->seq[8] = n->seq[9] = n->seq[10] = c;
		}
	}
	if (v) return;
	P.pend[3] = phas[3] ? pp[3][frow] : frow ? 0 : P_DSTOP;
#ifdef MUS_TRACE
	if (P.pend[3] && P.pend[3] != P_DSTOP) tr_notes[3]++;
#endif
	if (++frow >= cs.rows) { frow = 0; ford++; fstp++; }
}

static void stage(void)
{
	unsigned char st = pstage++;
	if (st == 0) { if (frow == 0 || ford >= cs.norder) stage_step(); }
	else if (!pend_end) stage_voice(3 - st);
}

/* One tick of the song. */
static void song_tick(void)
{
	unsigned char cm = 0, b;
	if (left == 0) {
		/* first tick of a row: the prepared notes start */
		while (pstage < 4) stage();
		if (pend_end) { song_on = 0; dirty = 15; return; }
		pstage = 0;
		left = tempo;
		cm = 1;
		mus_row = prow; mus_ord = pord;
		if (fade_new != fade_att) {
			fade_att = fade_new;
			P.cut = (unsigned char)(mcut + fade_att);
			dirty = 15;
		}
		b = (unsigned char)P.pend[3];
		if (b == P_DSTOP) P.dvol = 0;
		else if (b) {
			const kdrum __near *d = (const kdrum __near *)&cs.kit[(b & 15) - 1];
			P.n_type = d->n0; P.dat = d->at; P.dn1 = d->n1; P.dvol1 = d->vol1; P.ddec = d->dec;
			P.dvol = b >> 4 ? (b >> 4) << 2 : d->vol;
			P.dage = 0; P.dbusy = 1; P.dnew = 1;
			mus_beat = d->key;
		}
	}
	vtick(cm);
	/* prepare the next row in the last four ticks of this one, a stage a tick */
	if (--left < 4)
		do stage(); while ((unsigned char)(4 - pstage) > left);
}

/* One tick of the effects: the next step of each, or its slide. */
static void sfx_tick(void)
{
	sfx_t __near *s = (sfx_t __near *)sx;
	unsigned char any = 0;
	int c, d;
	for (c = 0; c < 4; c++, s++) {
		if (!P.sfx_on[c]) continue;
		dirty |= bit[c];
		if (!s->left) {
			const mus_fx *st = s->p;
			if (!st->ticks) { P.sfx_on[c] = 0; continue; }  /* the song gets the channel back in this tick's flush */
			d = st->div;
			if (c == 3) d &= 7;
			else if (d < 1) d = 1;
			else if ((unsigned)d > 1023) d = 1023;
			s->div = d;
			s->vol = st->vol > 15 ? 15 : st->vol;
			s->slide = st->slide;
			s->left = st->ticks;
			s->p = st + 1;
		} else if (s->slide && c != 3) {
			d = (int)s->div + s->slide;
			if (d < 1) d = 1;
			if (d > 1023) d = 1023;
			s->div = d;
		}
		s->left--;
		any = 1;
	}
	sfx_any = any;
}

static void tick(void)
{
	ticks++; tick_n++;
	if (song_on && !paused) song_tick();
	if (sfx_any) sfx_tick();
	if (dirty) { unsigned char d = dirty; dirty = 0; flush(d); }
}

/* ---- Clock and public calls -------------------------------------------- */

/* The library's IRQ5 handler calls its hook on its own stack in DGROUP
 * with interrupts off (see fx_irq_hook). That is what this needs and all
 * it needs: in the large model Watcom reaches statics through SS, so
 * there is no DS to load (__loadds changes nothing here), and an
 * interrupt handler of your own that calls this on DOS's stack would
 * write into DOS. */
void mus_irq(void)
{
	if (engine) tick();
}

/* 60 Hz in timer counts: 1193182 / 60 (0.002% fast, a second in 15 hours) */
#define TICK_PIT 19886UL
static unsigned long due;          /* tmr_ticks() of the next tick (M_POLL) */
static unsigned char polling;      /* due is valid                         */
static unsigned wd_vbl, wd_n;      /* M_IRQ: is the hook still ours?       */

void mus_poll(void)
{
	unsigned long now;
	unsigned f;
	long late;
	int n = 0;
	if (!engine) return;
	if (mode == M_IRQ) {
		if (fx_irq) {
			unsigned v = fx_vbl;
			if (v - wd_vbl >= 8) {
				/* 8 retraces and no tick: fx_init cleared the hook */
				if (tick_n == wd_n) fx_irq_hook(mus_irq);
				wd_vbl = v; wd_n = tick_n;
			}
			return;
		}
		mode = M_POLL;   /* the retrace interrupt is gone (fx_done): carry on from the timer */
	}
	if (mode == M_EXT && fx_irq) { polling = 0; return; } /* the game's hook ticks us */
	now = tmr_ticks();
	if (!polling) { polling = 1; due = now + TICK_PIT; return; }
	late = (long)(now - due);
	if (late < -(long)(8 * TICK_PIT)) {
		/* the clock went back a long way: the BIOS tick count restarts at
		 * midnight. (A small step back is a timer read that caught the PIT
		 * between wrapping and its interrupt: just wait.) */
		due = now;
		late = 0;
	}
	while (late >= 0) {
		if (n++ == MUS_CATCHUP) { due = now + TICK_PIT; break; } /* too far behind: skip the rest */
		f = mus_ioff();
		tick();
		mus_ion(f);
		due += TICK_PIT;
		late -= (long)TICK_PIT;
	}
}

int mus_start(const mus_song *song, int flags)
{
	static char reg_exit;
	unsigned char far *old;
	unsigned char want;
	unsigned f;
	int c;
	ns.buf = 0;
	if (song && (c = compile(song)) != 0) return c;
	snd_init();   /* first, so that our atexit runs before snd_done */
	if (!reg_exit) { reg_exit = 1; atexit(mus_stop); }
	f = mus_ioff();
	song_on = 0;                 /* the old song, if any, stops here */
	old = cs.buf;
	if (song) cs = ns; else cs.buf = 0;
	for (c = 0; c < 3; c++) {
		P.cur[c] = (vstate __near *)&vs[c][0]; P.nxt[c] = (vstate __near *)&vs[c][1];
		P.cur[c]->h.busy = 0; P.w_vol[c] = 0; P.pend[c] = 0;
	}
	P.dbusy = 0; P.dnew = 0; P.dvol = 0; P.w_vol[3] = 0; P.n_trig = 0; P.pend[3] = 0;
	pend_end = 0; pstage = 0;
	ford = 0; frow = 0; fstp = cs.steps; left = 0; mus_ord = 0; mus_row = 0;
	tempo = cs.tempo;
	fade_on = 0; fade_att = 0; fade_new = 0; P.cut = mcut;
	paused = 0;
	if (!engine) {
		/* whatever the chip was doing: silent, and nothing assumed about it */
		for (c = 0; c < 4; c++) { sn((unsigned char)P.regv[c]); P.level[c] = 0; P.sfx_on[c] = 0; }
		P.sh_per[0] = P.sh_per[1] = P.sh_per[2] = 0;   /* 0 is never asked for */
		P.sh_noise = 0xFF;
		sfx_any = 0;
		ticks = 0; tick_n = 0;
		engine = 1;
	} else flush(15);            /* the old song's notes off now */
	mus_ion(f);
	if (old) free(old);
	if (song) {
		/* the first row, prepared here so the first tick is no longer than
		 * any other. Ticks meanwhile leave the song alone: song_on is 0. */
		while (pstage < 4) stage();
		song_on = 1;
	}
	want = (unsigned char)(flags & MUS_NOHOOK ? M_EXT : fx_irq ? M_IRQ : M_POLL);
	if (mode == M_IRQ && want != M_IRQ) fx_irq_hook(0);
	if (want != mode) polling = 0;   /* mus_poll starts its clock afresh */
	mode = want;
	if (want == M_IRQ) { wd_vbl = fx_vbl; wd_n = tick_n; fx_irq_hook(mus_irq); }
	return 0;
}

void mus_stop(void)
{
	unsigned char far *old;
	unsigned f;
	int c;
	if (!engine) return;
	if (mode == M_IRQ) fx_irq_hook(0);
	f = mus_ioff();
	engine = 0; mode = M_OFF; polling = 0; song_on = 0; paused = 0; sfx_any = 0;
	for (c = 0; c < 4; c++) { sn((unsigned char)P.regv[c]); P.level[c] = 0; P.sfx_on[c] = 0; }
	old = cs.buf; cs.buf = 0;
	mus_ion(f);
	if (old) free(old);
}

void mus_pause(void)
{
	unsigned f = mus_ioff();
	if (engine && !paused) { paused = 1; flush(15); } /* silent now, not at the next tick */
	mus_ion(f);
}

void mus_resume(void)
{
	if (!paused) return;
	/* ticks missed while paused are not owed to the song */
	if (polling) due = tmr_ticks() + TICK_PIT;
	paused = 0;
	dirty = 15;      /* one byte: no need to hold interrupts off */
}

void mus_volume(int vol)
{
	unsigned f;
	if (vol < 0) vol = 0;
	if (vol > 15) vol = 15;
	f = mus_ioff();
	mcut = (unsigned char)(15 - vol);
	P.cut = (unsigned char)(mcut + fade_att);
	if (engine) flush(15);
	mus_ion(f);
}

void mus_tempo(int t)
{
	if (t < 1) t = 1;
	if (t > 255) t = 255;
	tempo = (unsigned char)t;   /* one byte: no need to hold interrupts off */
}

int mus_level(int ch)
{
	return ch >= 0 && ch <= 3 ? (int)P.level[ch] : 0;
}

int mus_playing(void)
{
	return engine && song_on && !paused;
}

unsigned long mus_ticks(void)
{
	unsigned long t;
	unsigned f = mus_ioff();
	t = ticks;
	mus_ion(f);
	return t;
}

void mus_sfx(int ch, const mus_fx *fx)
{
	unsigned f;
	if (ch < 0 || ch > 3 || !fx) return;
	if (!engine && mus_start(0, 0)) return;
	f = mus_ioff();
	sx[ch].p = fx;
	sx[ch].left = 0;
	sx[ch].vol = 0;   /* silent until the next tick loads the first step */
	sx[ch].div = ch == 3 ? (P.sh_noise & 7) : P.sh_per[ch] ? P.sh_per[ch] : 1;
	sx[ch].trig = 1;
	P.sfx_on[ch] = 1;
	sfx_any = 1;
	mus_ion(f);
}

int mus_sfx_busy(int ch)
{
	return ch >= 0 && ch <= 3 && P.sfx_on[ch];
}
