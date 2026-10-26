/* JUNRIS.C - Junris: Tetris for the IBM PCjr, with Moscow behind it.
 *
 * 160x200, 16 colors, JRGFX fast path. A 10x20 well of 5x8 blocks sits
 * between St. Basil's Cathedral and the Spasskaya Tower under a sunset
 * sky, all drawn in code into a background buffer. Only the falling
 * piece and its ghost are drawn per frame. Korobeiniki plays on all
 * three voices of the sound chip, with a bass line and chords.
 *
 * Keys: arrows move and soft drop, UP or X rotate right, Z rotate left,
 * SPACE hard drop, P pause, M music on/off, ESC back / quit. On the
 * title LEFT/RIGHT pick the start level.
 *
 * JUNRIS /TEST [script] plays with a script (L R D U Z S X N + ticks),
 * logs to LOG.TXT, saves SHOT.PPM and exits. /TEST 0 shows the title.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include "jrgfx.h"
#include "jrkeys.h"

#define BW 10
#define BH 20
#define CW 5
#define CH 8
#define BX 56
#define BY 20
#define TICK (TMR_HZ / 60)
#define DAS_DELAY 14
#define DAS_RATE 5
#define LOCK_GRACE 10
#define KEY_P 0x19
#define KEY_M 0x32
#define NBLK 10          /* 1-7 pieces, 8 gray, 9 flash, 10 ghost */
#define MAXLVL 30

enum { G_TITLE, G_PLAY, G_CLEAR, G_OVER };

#define R(x, y, w, h, c)      fx_fill_rect(x, y, w, h, FX_SOLID2(c))
#define RD(x, y, w, h, a, b)  fx_fill_rect(x, y, w, h, FX_DITH(a, b))

/* ---- Pieces ------------------------------------------------------------ */

static const char *shapes[7][4] = {
  { "....XXXX........", "..X...X...X...X.", "........XXXX....", ".X...X...X...X.." },
  { ".XX..XX.........", ".XX..XX.........", ".XX..XX.........", ".XX..XX........." },
  { ".X..XXX.........", ".X...XX..X......", "....XXX..X......", ".X..XX...X......" },
  { ".XX.XX..........", ".X...XX...X.....", ".....XX.XX......", "X...XX...X......" },
  { "XX...XX.........", "..X..XX..X......", "....XX...XX.....", ".X..XX..X......." },
  { "X...XXX.........", ".XX..X...X......", "....XXX...X.....", ".X...X..XX......" },
  { "..X.XXX.........", ".X...X...XX.....", "....XXX.X.......", "XX...X...X......" },
};
static signed char pc[7][4][4][2];
static signed char ptop[7];

/* block colors: base, light edge, dark edge */
static const unsigned char bcol[NBLK + 1][3] = {
    { 0, 0, 0 }, { 11, 15, 3 }, { 14, 15, 6 }, { 13, 15, 5 }, { 10, 15, 2 },
    { 12, 15, 4 }, { 9, 15, 1 }, { 6, 14, 8 }, { 7, 15, 8 }, { 15, 15, 15 },
    { 8, 8, 8 } };
/* Blocks packed in the page format, 3 bytes per row, for an even and an
 * odd start pixel (the well is 5 pixels per cell, so both happen). The
 * byte that straddles a neighbor keeps the neighbor's nibble. */
static unsigned char pk[NBLK + 1][2][CH][3];
static unsigned bg;

static const unsigned char gframes[MAXLVL] = {
    48, 43, 38, 33, 28, 23, 18, 13, 8, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 1 };
static const int lscore[5] = { 0, 40, 100, 300, 1200 };

static void parse_pieces(void)
{
    int p, r, i, n;

    for (p = 0; p < 7; p++) {
        for (r = 0; r < 4; r++) {
            n = 0;
            for (i = 0; i < 16; i++)
                if (shapes[p][r][i] == 'X') {
                    pc[p][r][n][0] = (signed char)(i & 3);
                    pc[p][r][n][1] = (signed char)(i >> 2);
                    n++;
                }
        }
        ptop[p] = pc[p][0][0][1];
    }
}

static void pack_blocks(void)
{
    int id, x, y, c, p[CW];

    for (id = 0; id <= NBLK; id++)
        for (y = 0; y < CH; y++) {
            for (x = 0; x < CW; x++) {
                if (id == 0)
                    c = (x == 0 && y == 0) ? 8 : 0;     /* grid dot */
                else if (id == NBLK)
                    c = (x == 0 || x == CW - 1 || y == 0 || y == CH - 1) ? 8 : 0;
                else if (y == CH - 1 || x == CW - 1)
                    c = bcol[id][2];
                else if (y == 0 || x == 0)
                    c = bcol[id][1];
                else
                    c = bcol[id][0];
                p[x] = c;
            }
            pk[id][0][y][0] = (unsigned char)(p[0] << 4 | p[1]);
            pk[id][0][y][1] = (unsigned char)(p[2] << 4 | p[3]);
            pk[id][0][y][2] = (unsigned char)(p[4] << 4);
            pk[id][1][y][0] = (unsigned char)p[0];
            pk[id][1][y][1] = (unsigned char)(p[1] << 4 | p[2]);
            pk[id][1][y][2] = (unsigned char)(p[3] << 4 | p[4]);
        }
}

/* Draws block id at pixel x, y (y even) into a page-layout segment. */
static void blit_block(unsigned seg, int id, int x, int y)
{
    unsigned char far *p0 = (unsigned char far *)MK_FP(seg, (y >> 1) * 80 + (x >> 1));
    unsigned char far *p1 = p0 + 0x2000;
    const unsigned char *s = pk[id][x & 1][0];
    int r;

    if (x & 1) {
        for (r = 0; r < CH; r += 2) {
            p0[0] = (unsigned char)((p0[0] & 0xF0) | s[0]);
            p0[1] = s[1];
            p0[2] = s[2];
            p1[0] = (unsigned char)((p1[0] & 0xF0) | s[3]);
            p1[1] = s[4];
            p1[2] = s[5];
            s += 6;
            p0 += 80;
            p1 += 80;
        }
    } else {
        for (r = 0; r < CH; r += 2) {
            p0[0] = s[0];
            p0[1] = s[1];
            p0[2] = (unsigned char)((p0[2] & 0x0F) | s[2]);
            p1[0] = s[3];
            p1[1] = s[4];
            p1[2] = (unsigned char)((p1[2] & 0x0F) | s[5]);
            s += 6;
            p0 += 80;
            p1 += 80;
        }
    }
    if (fx_pages == 1 && seg == fx_seg)
        fx_dirty(x, y, x + CW - 1, y + CH - 1);
}

/* Movers (the falling piece and its ghost) are written straight into
 * the page and put back from the background next time that page is
 * drawn, one list per page. */
static struct { unsigned seg, ver; int n; unsigned char x[12], y[12]; } pl[2];
static int cur_pl;
static unsigned ver = 1;        /* bumped whenever the picture should change */

static void restore_block(unsigned seg, int x, int y)
{
    unsigned off = (y >> 1) * 80 + (x >> 1);
    unsigned char far *d0 = (unsigned char far *)MK_FP(seg, off);
    unsigned char far *s0 = (unsigned char far *)MK_FP(bg, off);
    unsigned char far *d1 = d0 + 0x2000, far *s1 = s0 + 0x2000;
    int r;

    for (r = 0; r < CH; r += 2) {
        d0[0] = s0[0]; d0[1] = s0[1]; d0[2] = s0[2];
        d1[0] = s1[0]; d1[1] = s1[1]; d1[2] = s1[2];
        d0 += 80; s0 += 80; d1 += 80; s1 += 80;
    }
    if (fx_pages == 1)
        fx_dirty(x, y, x + CW - 1, y + CH - 1);
}

static int page_index(void)
{
    int k;

    for (k = 0; k < 2 && pl[k].seg != fx_seg; k++)
        ;
    if (k == 2) {
        k = pl[0].seg ? 1 : 0;
        pl[k].seg = fx_seg;
        pl[k].n = 0;
        pl[k].ver = ver - 1;
    }
    return k;
}

static void movers_restore(int k)
{
    int i;

    for (i = 0; i < pl[k].n; i++)
        restore_block(fx_seg, pl[k].x[i], pl[k].y[i]);
    pl[k].n = 0;
    cur_pl = k;
}

static void mover_block(int id, int x, int y)
{
    blit_block(fx_seg, id, x, y);
    if (pl[cur_pl].n < 12) {
        pl[cur_pl].x[pl[cur_pl].n] = (unsigned char)x;
        pl[cur_pl].y[pl[cur_pl].n] = (unsigned char)y;
        pl[cur_pl].n++;
    }
}

/* ---- Sound: Korobeiniki on three voices, effects on the third --------- */

typedef struct { unsigned hz; unsigned char d; } Note;

static const Note s_rot[]    = { {1760,1},{0,0} };
static const Note s_line[]   = { {784,2},{988,2},{1175,2},{1568,5},{0,0} };
static const Note s_tetris[] = { {1047,3},{1319,3},{1568,3},{2093,3},{1568,2},{2093,9},{0,0} };
static const Note s_level[]  = { {1319,2},{1319,2},{1319,2},{1175,2},{1047,2},{988,2},{880,7},{0,0} };
static const Note s_over[]   = { {440,6},{415,6},{392,6},{370,6},{349,6},{330,14},{0,0} };
static const Note s_start[]  = { {880,2},{0,2},{880,2},{0,2},{1319,6},{0,0} };

/* melody: midi note (0 = rest), length in sixteenths; 16 bars */
static const unsigned char melody[][2] = {
    {76,4},{71,2},{72,2},{74,4},{72,2},{71,2},
    {69,4},{69,2},{72,2},{76,4},{74,2},{72,2},
    {71,6},{72,2},{74,4},{76,4},
    {72,4},{69,4},{69,4},{0,4},
    {0,2},{74,4},{77,2},{81,4},{79,2},{77,2},
    {76,6},{72,2},{76,4},{74,2},{72,2},
    {71,4},{71,2},{72,2},{74,4},{76,4},
    {72,4},{69,4},{69,4},{0,4},
    {76,8},{72,8},{74,8},{71,8},{72,8},{69,8},{68,8},{71,8},
    {76,8},{72,8},{74,8},{71,8},{72,4},{76,4},{81,8},{80,16},
    {0,0} };
/* per bar: bass root, chord third, chord fifth (midi) */
static const unsigned char chords[16][3] = {
    {45,60,64},{45,60,64},{52,56,59},{45,60,64},
    {50,53,57},{48,52,55},{52,56,59},{45,60,64},
    {45,60,64},{52,56,59},{45,60,64},{52,56,59},
    {45,60,64},{52,56,59},{45,60,64},{52,56,59} };
static const unsigned char bpat[8] = { 0, 12, 7, 12, 0, 12, 7, 12 };
static const unsigned fbase[12] = {
    110, 117, 123, 131, 139, 147, 156, 165, 175, 185, 196, 208 };

static int mus_on = 1, mus_play, mus_cnt, mus_unit, mel_i, mel_left, tps = 6;
static unsigned mel_hz;
static int mel_vol, mel_env;
static const Note *sfx_p;
static int sfx_left;
static unsigned sfx_hz;
static int noise_t;
/* snd_tick runs in the retrace interrupt (fx_irq_hook); the main thread
 * sets snd_busy while it touches the sound chip or the music state so the
 * two never interleave their chip writes. */
static volatile int snd_busy;
#define SND_LOCK()   (snd_busy = 1)
#define SND_UNLOCK() (snd_busy = 0)

static unsigned freq(int m)
{
    m -= 45;
    return fbase[m % 12] << (m / 12);
}

static void sfx(const Note *s)
{
    SND_LOCK();
    sfx_left = 0;
    sfx_p = s;
    SND_UNLOCK();
}

static void music_start(void)
{
    SND_LOCK();
    mus_play = 1;
    mus_cnt = 1;
    mus_unit = 0;
    mel_i = 0;
    mel_left = 0;
    mel_hz = 0;
    SND_UNLOCK();
}

static void snd_quiet(void)
{
    int v;

    SND_LOCK();
    mus_play = 0;
    sfx_p = NULL;
    for (v = 0; v < 3; v++)
        snd_tone(v, 440, 0);
    snd_noise(0, 0);
    noise_t = 0;
    mel_hz = 0;
    SND_UNLOCK();
}

static void music_tick(void)
{
    int bar, e;

    if (!mus_play || !mus_on)
        return;
    if (mel_hz && ++mel_env >= 6) {
        mel_env = 0;
        if (mel_vol > 9)
            snd_tone(0, mel_hz, --mel_vol);
    }
    if (--mus_cnt > 0) {
        if (mus_cnt == 1) {             /* a breath before the next note */
            if (mel_left == 0 && mel_hz) {
                snd_tone(0, mel_hz, 0);
                mel_hz = 0;             /* so the decay can't re-sound it */
            }
            if (mus_unit & 1) {
                snd_tone(1, 440, 0);
                if (!sfx_p)
                    snd_tone(2, 440, 0);
            }
        }
        return;
    }
    mus_cnt = tps;
    if (mel_left == 0) {
        if (melody[mel_i][1] == 0)
            mel_i = 0;
        mel_left = melody[mel_i][1];
        if (melody[mel_i][0]) {
            mel_hz = freq(melody[mel_i][0]);
            mel_vol = 13;
            mel_env = 0;
            snd_tone(0, mel_hz, mel_vol);
        } else {
            mel_hz = 0;
            snd_tone(0, 440, 0);
        }
        mel_i++;
    }
    mel_left--;
    if ((mus_unit & 1) == 0) {
        bar = (mus_unit >> 4) & 15;
        e = (mus_unit >> 1) & 7;
        snd_tone(1, freq(chords[bar][0] + bpat[e]), 10);
        if (!sfx_p)
            snd_tone(2, freq(chords[bar][1 + (e & 1)]), 6);
    }
    mus_unit = (mus_unit + 1) & 255;
}

static void snd_tick(void)
{
    if (noise_t && --noise_t == 0)
        snd_noise(0, 0);
    music_tick();
    if (!sfx_p)
        return;
    if (sfx_left > 0) {
        if (--sfx_left == 1 && sfx_hz)
            snd_tone(2, sfx_hz, 0);
        if (sfx_left > 0)
            return;
    }
    if (sfx_p->d == 0) {
        snd_tone(2, 440, 0);
        sfx_p = NULL;
        return;
    }
    sfx_hz = sfx_p->hz;
    sfx_left = sfx_p->d + 1;
    if (sfx_hz)
        snd_tone(2, sfx_hz, 12);
    else
        snd_tone(2, 440, 0);
    sfx_p++;
}

static void thud(void)
{
    SND_LOCK();
    snd_noise(6, 10);
    noise_t = 3;
    SND_UNLOCK();
}

/* Retrace hook: 60 Hz regardless of how long a frame takes. Skips a tick
 * (1/60 s, inaudible) when the main thread is mid-write to the chip. */
static void snd_irq(void)
{
    if (snd_busy)
        return;
    snd_tick();
}

/* ---- State ------------------------------------------------------------- */

static unsigned char board[BH][BW];
static int gstate, ticks, quit, testmode, paused;
static long score, hiscore;
static int lines, level, startlvl = 1;
static int cur, nxt, rot, px, py, gcnt, softcnt, locking, lockcnt;
static int dasdir, dascnt;
static unsigned char bag[7];
static int bagn;
static int clr_rows[4], nclr, clr_t;
static unsigned char saved[4][BW];
static int over_t, over_row;
static const char *msg;
static int msg_t, hud_t;
static struct { int l, r, d, cw, ccw, drop, best; } inp, pinp;
static const char *scr;
static int scr_left, scr_done;

static unsigned seed = 4242;
static unsigned rnd(void)
{
    seed = seed * 25173u + 13849u;
    return seed >> 4;
}

/* ---- Dirty rectangles (background buffer -> both pages) ---------------- */

#define MAXDQ 16
static struct { int x0, y0, x1, y1, n; } dq[MAXDQ];
static int ndq, fullref;

static void add_dirty(int x0, int y0, int x1, int y1)
{
    ver++;
    if (ndq == MAXDQ) {
        fullref = 2;
        return;
    }
    dq[ndq].x0 = x0; dq[ndq].y0 = y0;
    dq[ndq].x1 = x1; dq[ndq].y1 = y1;
    dq[ndq].n = 2;
    ndq++;
}

static void flush_dirty(void)
{
    int i, j = 0;

    if (fullref) {
        fx_copy_rect(fx_seg, bg, 0, 0, 159, 199);
        if (fx_pages == 1)
            fx_dirty(0, 0, 159, 199);
        fullref--;
        ndq = 0;
        return;
    }
    for (i = 0; i < ndq; i++) {
        fx_copy_rect(fx_seg, bg, dq[i].x0, dq[i].y0, dq[i].x1, dq[i].y1);
        if (fx_pages == 1)
            fx_dirty(dq[i].x0, dq[i].y0, dq[i].x1, dq[i].y1);
        if (--dq[i].n > 0)
            dq[j++] = dq[i];
    }
    ndq = j;
}

static int bg_pget(int x, int y)
{
    unsigned char far *p = (unsigned char far *)
        MK_FP(bg, ((y & 1) << 13) + (y >> 1) * 80 + (x >> 1));

    return (x & 1) ? (*p & 15) : (*p >> 4);
}

/* ---- Moscow ------------------------------------------------------------ */

static const unsigned char dprof[16] = {
    1, 2, 4, 7, 10, 13, 15, 16, 16, 16, 15, 13, 11, 9, 8, 7 };

/* An onion dome with swirling stripes, its cross above and drum below. */
static void dome(int xc, int yt, int w, int h, int c1, int c2)
{
    int r, x, hw, x0, x1;

    for (r = 0; r < h; r++) {
        hw = (dprof[r * 16 / h] * w + 16) / 32;
        x0 = xc - hw;
        x1 = xc + hw;
        for (x = x0; x <= x1; x++)
            fx_pset(x, yt + r, ((x - x0 + r) & 3) < 2 ? c1 : c2);
    }
    R(xc, yt - 4, 1, 4, 14);
    R(xc - 1, yt - 3, 3, 1, 14);
    R(xc - w / 4, yt + h, w / 2 + 1, 3, 15);
    R(xc - w / 4, yt + h + 1, w / 2 + 1, 1, 8);
}

static void circle(int xc, int yc, int rad, int c)
{
    int dy, hw;

    for (dy = -rad; dy <= rad; dy++) {
        hw = 0;
        while ((hw + 1) * (hw + 1) + dy * dy <= rad * rad)
            hw++;
        fx_hspan(yc + dy, xc - hw, xc + hw, FX_SOLID(c));
    }
}

static void tower(int x, int w, int ytop, int c)
{
    int i;

    R(x, ytop, w, 166 - ytop, c);
    for (i = ytop + 6; i < 160; i += 14) {
        R(x + 1, i, 1, 4, 15);
        R(x + w - 2, i, 1, 4, 15);
    }
}

static unsigned scene;          /* the pristine scene, drawn once */

static void render_scene(void)
{
    int i, x, y;

    fx_target(scene);
    /* sky, dusk to night */
    R(0, 0, 160, 56, 0);
    RD(0, 56, 160, 16, 0, 1);
    R(0, 72, 160, 32, 1);
    RD(0, 104, 160, 12, 1, 5);
    R(0, 116, 160, 20, 5);
    RD(0, 136, 160, 10, 5, 12);
    R(0, 146, 160, 37, 12);
    seed = 777;
    for (i = 0; i < 24; i++) {
        x = rnd() % 160;
        y = rnd() % 16;
        fx_pset(x, y, (i & 3) ? 7 : 15);
    }
    circle(46, 64, 4, 14);
    fx_pset(45, 63, 15);

    /* Red Square and the Kremlin wall */
    RD(0, 183, 160, 17, 8, 6);
    R(0, 183, 160, 1, 7);
    R(108, 152, 52, 31, 4);
    for (x = 108; x < 160; x += 6) {
        R(x, 148, 4, 4, 4);
        R(x + 1, 148, 2, 1, 12);
    }
    R(108, 162, 52, 1, 8);
    for (x = 111; x < 160; x += 8)
        R(x, 166, 1, 4, 0);
    /* a Stalinist skyscraper far behind */
    R(151, 86, 2, 10, 8);
    fx_pset(151, 84, 12);
    fx_pset(152, 84, 12);
    R(149, 96, 6, 16, 8);
    R(146, 112, 12, 16, 8);
    R(144, 128, 16, 20, 8);
    for (y = 114; y < 146; y += 4)
        for (x = 146; x < 158; x += 3)
            if (y >= 128 || x >= 147)
                fx_pset(x, y, 14);

    /* Spasskaya Tower */
    R(114, 166, 29, 17, 4);
    R(125, 168, 7, 15, 0);
    R(126, 166, 5, 2, 0);
    tower(116, 25, 118, 4);
    R(116, 118, 25, 2, 7);
    R(118, 102, 21, 16, 4);
    R(118, 102, 21, 1, 7);
    circle(128, 110, 5, 0);
    circle(128, 110, 4, 15);
    R(128, 107, 1, 4, 0);
    R(128, 110, 3, 1, 0);
    R(120, 92, 17, 10, 15);
    R(120, 92, 17, 1, 7);
    R(122, 94, 3, 6, 8);
    R(127, 94, 3, 6, 8);
    R(132, 94, 3, 6, 8);
    fx_tri(128, 64, 121, 92, 135, 92, 2);
    R(128, 66, 1, 26, 10);
    /* the red star */
    fx_pset(128, 58, 12);
    fx_pset(128, 59, 12);
    R(126, 60, 5, 1, 12);
    R(127, 61, 3, 1, 12);
    fx_pset(127, 62, 12);
    fx_pset(129, 62, 12);
    fx_pset(126, 63, 12);
    fx_pset(130, 63, 12);

    /* St. Basil's Cathedral */
    R(0, 166, 53, 17, 6);
    for (x = 2; x < 50; x += 6)
        R(x, 170, 3, 9, 0);
    R(0, 179, 53, 2, 7);
    tower(3, 10, 128, 4);
    dome(7, 108, 10, 16, 12, 15);
    tower(13, 8, 136, 4);
    dome(16, 122, 8, 12, 9, 15);
    tower(40, 11, 126, 4);
    dome(45, 104, 11, 18, 13, 15);
    tower(32, 8, 134, 4);
    dome(35, 118, 8, 14, 10, 14);
    tower(21, 11, 112, 4);
    R(22, 100, 9, 12, 15);
    R(22, 100, 9, 1, 7);
    R(24, 103, 1, 5, 8);
    R(28, 103, 1, 5, 8);
    fx_tri(26, 80, 19, 100, 33, 100, 2);
    R(26, 82, 1, 18, 10);
    dome(26, 70, 7, 10, 14, 6);

    /* the well */
    R(53, 17, 56, 166, 14);
    R(55, 19, 52, 162, 6);
    R(56, 20, 50, 160, 0);
    R(0, 186, 160, 12, 4);
    fx_text(36, 188, "J U N R I S", 14);
    fx_target(0);
}

static void draw_scene(void)
{
    fx_copy_rect(bg, scene, 0, 0, 159, 199);
    fullref = 2;
    ndq = 0;
}

/* ---- Board drawing ----------------------------------------------------- */

static void draw_rows(int y0, int y1)
{
    int x, y;

    if (y0 < 0) y0 = 0;
    if (y1 >= BH) y1 = BH - 1;
    if (y0 > y1)
        return;
    for (y = y0; y <= y1; y++)
        for (x = 0; x < BW; x++)
            blit_block(bg, board[y][x], BX + x * CW, BY + y * CH);
    add_dirty(BX, BY + y0 * CH, BX + BW * CW - 1, BY + (y1 + 1) * CH - 1);
}

static void draw_board(void)
{
    draw_rows(0, BH - 1);
}

static void well_text(int y, const char *s, int c)
{
    fx_text(BX + (BW * CW - strlen(s) * 8) / 2, y, s, c);
}

static void draw_preview(void)
{
    int i;

    fx_target(bg);
    R(111, 11, 24, 20, 6);
    R(112, 12, 22, 18, 0);
    fx_target(0);
    for (i = 0; i < 4; i++)
        blit_block(bg, nxt + 1, 113 + pc[nxt][0][i][0] * CW,
                   12 + pc[nxt][0][i][1] * CH);
    add_dirty(111, 11, 134, 30);
}

static void hud(void)
{
    char b[16];

    fx_target(bg);
    R(0, 0, 52, 56, 0);
    fx_text(4, 2, "SCORE", 14);
    sprintf(b, "%06ld", score);
    fx_text(2, 10, b, 15);
    fx_text(4, 20, "LINES", 14);
    sprintf(b, "%3d", lines);
    fx_text(10, 28, b, 15);
    fx_text(4, 38, "LEVEL", 14);
    sprintf(b, "%2d", level);
    fx_text(14, 46, b, 15);
    fx_target(0);
    add_dirty(0, 0, 51, 55);
    hud_t = 0;
}

static void hud_right(void)
{
    char b[16];

    fx_target(bg);
    R(108, 0, 52, 56, 0);
    fx_text(116, 2, "NEXT", 14);
    fx_text(116, 36, "HIGH", 14);
    sprintf(b, "%06ld", hiscore);
    fx_text(110, 44, b, 15);
    fx_target(0);
    add_dirty(108, 0, 159, 55);
    draw_preview();
}

static void big_text(int x, int y, const char *s, int c1, int c2)
{
    unsigned char bm[8][10];
    int n = strlen(s), i, j;

    memset(bm, 0, sizeof bm);
    fx_text(0, 0, s, 15);
    for (j = 0; j < 8; j++)
        for (i = 0; i < n * 8; i++)
            if (bg_pget(i, j))
                bm[j][i >> 3] |= (unsigned char)(0x80 >> (i & 7));
    R(0, 0, n * 8, 8, 0);
    for (j = 0; j < 8; j++)
        for (i = 0; i < n * 8; i++)
            if (bm[j][i >> 3] & (0x80 >> (i & 7)))
                R(x + i * 2, y + j * 4, 2, 4, j < 4 ? c1 : c2);
}

/* ---- Title ------------------------------------------------------------- */

static void title_level(void)
{
    char b[8];

    fx_target(bg);
    R(BX, 146, BW * CW, 8, 0);
    sprintf(b, "< %02d >", startlvl);
    well_text(146, b, 10);
    fx_target(0);
    add_dirty(BX, 146, BX + BW * CW - 1, 153);
}

static void to_title(void)
{
    char b[16];

    if (score > hiscore)
        hiscore = score;
    gstate = G_TITLE;
    msg = NULL;
    paused = 0;
    draw_scene();
    fx_target(bg);
    big_text(32, 2, "JUNRIS", 12, 14);
    fx_text(10, 40, "HIGH", 14);
    sprintf(b, "%06ld", hiscore);
    fx_text(2, 48, b, 15);
    fx_text(108, 40, "MOSCOW", 13);
    fx_text(116, 48, "1984", 13);
    well_text(40, "ARROWS", 11);
    well_text(48, "MOVE", 7);
    well_text(62, "X Z UP", 11);
    well_text(70, "ROTATE", 7);
    well_text(84, "SPACE", 11);
    well_text(92, "DROP", 7);
    well_text(106, "P  M", 11);
    well_text(114, "PAUSE", 7);
    well_text(122, "MUSIC", 7);
    well_text(138, "LEVEL", 14);
    fx_target(0);
    title_level();
    fullref = 2;
    if (!mus_play)
        music_start();
}

/* ---- Game -------------------------------------------------------------- */

static int bag_next(void)
{
    int i, j, t;

    if (bagn == 0) {
        for (i = 0; i < 7; i++)
            bag[i] = (unsigned char)i;
        for (i = 6; i > 0; i--) {
            j = rnd() % (i + 1);
            t = bag[i]; bag[i] = bag[j]; bag[j] = (unsigned char)t;
        }
        bagn = 7;
    }
    return bag[--bagn];
}

static int collides(int p, int r, int x, int y)
{
    int i, cx, cy;

    for (i = 0; i < 4; i++) {
        cx = x + pc[p][r][i][0];
        cy = y + pc[p][r][i][1];
        if (cx < 0 || cx >= BW || cy >= BH)
            return 1;
        if (cy >= 0 && board[cy][cx])
            return 1;
    }
    return 0;
}

static int gy;                  /* where the piece would land */

static void update_ghost(void)
{
    gy = py;
    while (!collides(cur, rot, px, gy + 1))
        gy++;
}

static void set_level(int lv)
{
    if (lv > MAXLVL)
        lv = MAXLVL;
    level = lv;
    tps = lv >= 20 ? 4 : lv >= 10 ? 5 : 6;
}

static void game_over(void)
{
    gstate = G_OVER;
    over_t = 0;
    over_row = BH - 1;
    msg = NULL;
    snd_quiet();
    sfx(s_over);
    if (score > hiscore) {
        hiscore = score;
        hud_right();
    }
    if (testmode)
        dbg_log("t=%d game over score=%ld lines=%d", ticks, score, lines);
}

static void spawn(void)
{
    cur = nxt;
    nxt = bag_next();
    rot = 0;
    px = 3;
    py = -ptop[cur];
    gcnt = 0;
    softcnt = 0;
    locking = 0;
    draw_preview();
    if (collides(cur, rot, px, py)) {
        game_over();
        return;
    }
    update_ghost();
    ver++;
}

static void new_game(void)
{
    memset(board, 0, sizeof board);
    score = 0;
    lines = 0;
    set_level(startlvl);
    bagn = 0;
    seed = testmode ? 1234 : (unsigned)tmr_ticks();
    gstate = G_PLAY;
    msg = NULL;
    paused = 0;
    dasdir = 0;
    draw_scene();
    draw_board();
    hud();
    nxt = bag_next();
    spawn();
    hud_right();
    fullref = 2;
    sfx(s_start);
    if (!mus_play)
        music_start();
}

static void lock_piece(void)
{
    int i, cx, cy, y, x, full, ymin = BH, ymax = -1, id = cur + 1;

    for (i = 0; i < 4; i++) {
        cx = px + pc[cur][rot][i][0];
        cy = py + pc[cur][rot][i][1];
        if (cy < 0) {
            game_over();
            return;
        }
        board[cy][cx] = (unsigned char)id;
        if (cy < ymin) ymin = cy;
        if (cy > ymax) ymax = cy;
    }
    draw_rows(ymin, ymax);
    thud();
    if (testmode)
        dbg_log("t=%d lock piece %d rot %d at %d,%d", ticks, cur, rot, px, py);
    nclr = 0;
    for (y = ymin; y <= ymax; y++) {
        full = 1;
        for (x = 0; x < BW; x++)
            if (!board[y][x])
                full = 0;
        if (full) {
            clr_rows[nclr] = y;
            memcpy(saved[nclr], board[y], BW);
            nclr++;
        }
    }
    if (nclr) {
        gstate = G_CLEAR;
        clr_t = 0;
        sfx(nclr == 4 ? s_tetris : s_line);
    } else
        spawn();
}

static void clear_tick(void)
{
    int i, x, y, src, flash, oldlv;

    clr_t++;
    if (clr_t % 5 == 0 && clr_t < 20) {
        flash = (clr_t / 5) & 1;
        for (i = 0; i < nclr; i++)
            for (x = 0; x < BW; x++)
                board[clr_rows[i]][x] = flash ? 9 : saved[i][x];
        for (i = 0; i < nclr; i++)
            draw_rows(clr_rows[i], clr_rows[i]);
        return;
    }
    if (clr_t < 20)
        return;
    /* drop the rows above each cleared one */
    src = BH - 1;
    for (y = BH - 1; y >= 0; y--) {
        while (src >= 0) {
            for (i = 0; i < nclr && clr_rows[i] != src; i++)
                ;
            if (i == nclr)
                break;
            src--;
        }
        if (src >= 0)
            memcpy(board[y], board[src], BW);
        else
            memset(board[y], 0, BW);
        src--;
    }
    draw_rows(0, clr_rows[nclr - 1]);
    score += (long)lscore[nclr] * level;
    lines += nclr;
    oldlv = level;
    if (startlvl + lines / 10 > level)
        set_level(startlvl + lines / 10);
    if (level != oldlv) {
        sfx(s_level);
        msg = "LEVEL UP";
        msg_t = 90;
    }
    hud();
    if (testmode)
        dbg_log("t=%d cleared %d lines, score=%ld lines=%d level=%d",
                ticks, nclr, score, lines, level);
    gstate = G_PLAY;
    spawn();
}

static int try_move(int dx, int dy)
{
    if (collides(cur, rot, px + dx, py + dy))
        return 0;
    px += dx;
    py += dy;
    if (dx)
        update_ghost();
    ver++;
    return 1;
}

static void try_rotate(int dir)
{
    static const int kick[5] = { 0, -1, 1, -2, 2 };
    int nr = (rot + dir) & 3, i;

    for (i = 0; i < 5; i++)
        if (!collides(cur, nr, px + kick[i], py)) {
            rot = nr;
            px += kick[i];
            update_ghost();
            ver++;
            if (!sfx_p)
                sfx(s_rot);
            return;
        }
}

static void play_tick(void)
{
    int hdir, n;

    if (msg && --msg_t <= 0) {
        msg = NULL;
        ver++;
    }
    if (inp.cw && !pinp.cw)
        try_rotate(1);
    if (inp.ccw && !pinp.ccw)
        try_rotate(-1);
    if (inp.drop && !pinp.drop) {
        n = gy - py;
        py = gy;
        score += n * 2;
        hud_t = 1;
        lock_piece();
        return;
    }
    hdir = (inp.l && inp.r) ? (inp.best == 2 ? 1 : -1) :
           inp.r ? 1 : inp.l ? -1 : 0;
    if (hdir != dasdir) {
        dasdir = hdir;
        if (hdir) {
            try_move(hdir, 0);
            dascnt = DAS_DELAY;
        }
    } else if (hdir && --dascnt <= 0) {
        try_move(hdir, 0);
        dascnt = DAS_RATE;
    }
    if (inp.d) {
        if (++softcnt >= 2) {
            softcnt = 0;
            if (try_move(0, 1)) {
                score++;
                hud_t = 1;
                gcnt = 0;
            } else {
                lock_piece();
                return;
            }
        }
    } else {
        softcnt = 1;
        if (++gcnt >= gframes[level - 1]) {
            gcnt = 0;
            if (!try_move(0, 1) && !locking) {
                locking = 1;
                lockcnt = LOCK_GRACE;
            }
        }
    }
    if (locking) {
        if (!collides(cur, rot, px, py + 1))
            locking = 0;
        else if (--lockcnt <= 0) {
            lock_piece();
            return;
        }
    }
    if (hud_t && ++hud_t > 8)
        hud();
}

static void over_tick(void)
{
    over_t++;
    if (over_row >= 0) {
        if (over_t % 3 == 0) {
            memset(board[over_row], 8, BW);
            draw_rows(over_row, over_row);
            over_row--;
            if (over_row < 0) {
                fx_target(bg);
                R(BX, 88, BW * CW, 24, 0);
                well_text(92, "GAME", 15);
                well_text(100, "OVER", 15);
                fx_target(0);
                add_dirty(BX, 88, BX + BW * CW - 1, 111);
            }
        }
        return;
    }
    if (over_t > 90 && ((inp.drop && !pinp.drop) || (inp.cw && !pinp.cw)))
        to_title();
}

static void title_tick(void)
{
    if (inp.drop && !pinp.drop) {
        new_game();
        return;
    }
    if (inp.l && !pinp.l && startlvl > 1) {
        startlvl--;
        title_level();
    }
    if (inp.r && !pinp.r && startlvl < 20) {
        startlvl++;
        title_level();
    }
}

/* ---- Input and ticks --------------------------------------------------- */

static void test_setup(void)
{
    int x, y;

    for (y = BH - 2; y < BH; y++)
        for (x = 0; x < BW - 1; x++)
            board[y][x] = (unsigned char)(1 + (x + y) % 7);
    cur = 0;
    rot = 0;
    px = 3;
    py = -ptop[0];
    draw_board();
}

/* Keys go through JRKEYS (jrkeys.h): taps are latched from the main
 * loop, lost key-releases are repaired, newest of left/right wins. The
 * last number is the longest useful hold in ticks: a piece reaches the
 * wall in under a second. */
static int k_l, k_r, k_d, k_cw, k_x, k_ccw, k_drop, k_esc, k_p, k_m;

static void keys_setup(void)
{
    jk_init();
    k_l = jk_add(KEY_LEFT, KEY_A, 1, 90);
    k_r = jk_add(KEY_RIGHT, KEY_D, 1, 90);
    k_d = jk_add(KEY_DOWN, KEY_S, 2, 150);
    k_cw = jk_add(KEY_UP, KEY_W, 0, 60);
    k_x = jk_add(KEY_X, 0, 0, 60);
    k_ccw = jk_add(KEY_Z, 0, 0, 60);
    k_drop = jk_add(KEY_SPACE, KEY_ENTER, 0, 60);
    k_esc = jk_add(KEY_ESC, 0, 0, 30);
    k_p = jk_add(KEY_P, 0, 0, 30);
    k_m = jk_add(KEY_M, 0, 0, 30);
}

static void read_input(void)
{
    int best;

    pinp = inp;
    if (testmode) {
        if (scr_left == 0) {
            memset(&inp, 0, sizeof inp);
            if (!scr || !*scr)
                scr_done = 1;
            else {
                for (; *scr && (*scr < '0' || *scr > '9'); scr++)
                    switch (*scr) {
                    case 'L': inp.l = 1; break;
                    case 'R': inp.r = 1; break;
                    case 'D': inp.d = 1; break;
                    case 'U': inp.cw = 1; break;
                    case 'Z': inp.ccw = 1; break;
                    case 'S': inp.drop = 1; break;
                    case 'X': test_setup(); break;
                    }
                while (*scr >= '0' && *scr <= '9')
                    scr_left = scr_left * 10 + (*scr++ - '0');
            }
        }
        if (scr_left)
            scr_left--;
        return;
    }
    jk_tick();
    inp.l = jk_down(k_l);
    inp.r = jk_down(k_r);
    inp.d = jk_down(k_d);
    best = jk_best(1);                      /* newest of left/right wins */
    inp.best = best < 0 ? 0 : best == k_l ? 1 : 2;
    inp.cw = jk_down(k_cw) | jk_down(k_x);
    inp.ccw = jk_down(k_ccw);
    inp.drop = jk_down(k_drop);
    if (jk_hit(k_esc)) {
        if (gstate == G_TITLE)
            quit = 1;
        else
            to_title();
    }
    if (jk_hit(k_p) && gstate == G_PLAY) {
        paused = !paused;
        ver++;
    }
    if (jk_hit(k_m)) {
        SND_LOCK();
        mus_on = !mus_on;
        if (!mus_on) {
            snd_tone(0, 440, 0);
            snd_tone(1, 440, 0);
            if (!sfx_p)
                snd_tone(2, 440, 0);
            mel_hz = 0;
        }
        SND_UNLOCK();
    }
}

static void tick(void)
{
    ticks++;
    if ((ticks & 15) == 0)
        ver++;                  /* blinking text */
    read_input();
    if (!fx_irq)
        snd_tick();             /* no retrace interrupt: tick it here */
    switch (gstate) {
    case G_TITLE: title_tick(); break;
    case G_PLAY:  play_tick();  break;
    case G_CLEAR: clear_tick(); break;
    case G_OVER:  over_tick();  break;
    }
}

/* ---- Frames ------------------------------------------------------------ */

static void draw_piece(void)
{
    int i, x, y;

    if (gy - py >= 2)           /* the ghost is drawn first, the piece over it */
        for (i = 0; i < 4; i++) {
            x = px + pc[cur][rot][i][0];
            y = gy + pc[cur][rot][i][1];
            if (y >= 0)
                mover_block(NBLK, BX + x * CW, BY + y * CH);
        }
    for (i = 0; i < 4; i++) {
        x = px + pc[cur][rot][i][0];
        y = py + pc[cur][rot][i][1];
        if (y >= 0)
            mover_block(cur + 1, BX + x * CW, BY + y * CH);
    }
}

static unsigned long prof[6];
static int nframes;
#define PROF(i) if (testmode) { t1 = tmr_ticks(); prof[i] += t1 - t0; t0 = t1; }

static void frame(void)
{
    unsigned long t0 = 0, t1;
    int k = page_index();

    if (pl[k].ver == ver && !fullref && !ndq)
        return;                 /* this page already shows it all */
    if (testmode)
        t0 = tmr_ticks();
    nframes++;
    fx_begin();
    PROF(0)
    fx_restore(bg);
    movers_restore(k);
    PROF(1)
    flush_dirty();
    PROF(2)
    switch (gstate) {
    case G_TITLE:
        if (ticks & 16) {
            well_text(162, "PRESS", 15);
            well_text(170, "SPACE", 15);
        }
        break;
    case G_PLAY:
        draw_piece();
        if (paused) {
            R(BX, 92, BW * CW, 16, 0);
            well_text(96, "PAUSED", 15);
        } else if (msg) {
            R(BX, 92, BW * CW, 16, 0);
            well_text(92, "LEVEL", 14);
            well_text(100, level < 10 ? " UP" : "UP!", 14);
        }
        break;
    default:
        break;
    }
    PROF(3)
    pl[k].ver = ver;
    fx_flip();
    PROF(4)
}

/* ---- Main -------------------------------------------------------------- */

static void run_test(int title)
{
    int t;
    unsigned long t0;

    dbg_log("pages=%d irq=%d", fx_pages, fx_irq);
    if (title) {
        to_title();
        for (t = 0; t < 40; t++) {
            tick();
            frame();
        }
    } else {
        startlvl = 1;
        dbg_log("drawing scene");
        new_game();
        dbg_log("scene ready");
        for (t = 0; !scr_done && t < 4000; t++) {
            t0 = tmr_ticks();
            tick();
            prof[5] += tmr_ticks() - t0;
            frame();
        }
        dbg_log("end st=%d score=%ld lines=%d level=%d piece=%d at %d,%d",
                gstate, score, lines, level, cur, px, py);
        dbg_log("%d frames, ms: begin %lu restore %lu flush %lu draw %lu "
                "flip %lu tick %lu", nframes, TMR_TICKS_MS(prof[0]),
                TMR_TICKS_MS(prof[1]), TMR_TICKS_MS(prof[2]),
                TMR_TICKS_MS(prof[3]), TMR_TICKS_MS(prof[4]),
                TMR_TICKS_MS(prof[5]));
    }
    frame();
    fx_begin();
    fx_screenshot("SHOT.PPM");
}

int main(int argc, char **argv)
{
    unsigned long next, now;
    int steps, title = 0;

    if (argc > 1 && stricmp(argv[1], "/TEST") == 0) {
        testmode = 1;
        if (argc > 2) {
            if (strcmp(argv[2], "0") == 0)
                title = 1;
            else
                scr = argv[2];
        } else
            scr = "X1U2N2R2N2R2N2R2N2R2N2S2N40S2N30L2N2L2N2S2N30D60N10S2N30";
        remove("LOG.TXT");
    }
    parse_pieces();
    if (fx_init(0)) {
        printf("Cannot start graphics (error %d)\n", gfx_err);
        return 1;
    }
    key_init();
    keys_setup();
    tmr_init();
    snd_init();
    bg = fx_alloc_buffer();
    scene = fx_alloc_buffer();
    if (!bg || !scene) {
        gfx_done();
        printf("Not enough memory\n");
        return 1;
    }
    pack_blocks();
    fx_text(40, 96, "ONE MOMENT", 7);
    fx_flip();
    render_scene();
    fx_track(1);
    if (fx_irq)
        fx_irq_hook(snd_irq);

    if (testmode)
        run_test(title);
    else {
        to_title();
        next = tmr_ticks();
        while (!quit) {
            jk_poll();
            now = tmr_ticks();
            steps = 0;
            while ((long)(now - next) >= 0 && steps < 4) {
                if (!paused)
                    tick();
                else
                    read_input();
                next += TICK;
                steps++;
            }
            if ((long)(now - next) > 0)
                next = now;
            if (steps)
                frame();
        }
    }
    fx_irq_hook(0);
    snd_quiet();
    gfx_done();
    return 0;
}
