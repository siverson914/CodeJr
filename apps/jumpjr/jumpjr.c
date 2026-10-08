/* JUMPJR.C - Jumpman Junior: a Jumpman-style platform game for the PCjr.
 *
 * 160x200, 16 colors, JRGFX fast path: the level is drawn once into a
 * background buffer; each frame only what moved is restored and redrawn.
 * The screen is a 40x24 grid of 4x8 cells under an 8 pixel status line.
 *
 * JUMPJR /TEST [level [script]] plays a script (R30RJ20U40: letters are
 * keys held, J jumps, W wins the level, the number is ticks), saves
 * SHOT.PPM and exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include "jrgfx.h"

#define COLS 40
#define ROWS 24
#define TOP  8
#define ROWY(r) (TOP + ((r) << 3))

/* map cell bits */
#define M_SOLID  0x01
#define M_CLIMB  0x06   /* 0 none, 1 ladder, 2 up rope, 3 down rope */
#define M_BOMB   0x08
#define M_GROUP  0x70   /* trigger group of a bomb or girder */
#define M_HIDDEN 0x80   /* girder that appears when its group fires */
#define C_LAD  1
#define C_UP   2
#define C_DOWN 3

#define KEY_P 0x19
#define TICK  (TMR_HZ / 30)   /* game ticks per second: 30 */
#define SPEED 20              /* run speed, 1/16 pixel per tick */
#define JLEN  18
#define MAXFALL 20

/* ---- Levels ------------------------------------------------------------ */

/* G row x0 x1: girder.  a-d: girder that vanishes, 1-4: girder that
 * appears, when bomb A-D is taken.  L x top bottom: ladder (2 wide).
 * U/V x top bottom: up/down rope. o x row: bomb.  S x row: start.
 * R x row: robot.  E x top bottom: lift (3 wide). */
typedef struct { char op; unsigned char a, b, c; } Cmd;
typedef struct {
    const char *name;
    const Cmd *cmd;
    unsigned char gl, gd, lad;  /* girder light/dark, ladder color */
    unsigned char maxb, bint;   /* bullets: how many, ticks between */
    unsigned char hail;         /* 1 in n chance of a hailstone per tick */
} Level;

static const Cmd lv1[] = {
    {'G',23,0,39},{'G',19,0,15},{'G',19,24,39},{'G',15,6,33},
    {'G',11,0,13},{'G',11,26,39},{'G',7,10,29},
    {'L',7,19,22},{'L',31,19,22},{'L',10,15,18},{'L',28,15,18},
    {'L',8,11,14},{'L',30,11,14},{'L',11,7,10},{'L',27,7,10},
    {'U',19,13,22},
    {'o',2,22},{'o',13,22},{'o',26,22},{'o',37,22},
    {'o',1,18},{'o',14,18},{'o',25,18},{'o',38,18},
    {'o',15,14},{'o',21,14},{'o',24,14},
    {'o',1,10},{'o',6,10},{'o',33,10},{'o',38,10},
    {'o',14,6},{'o',19,6},{'o',25,6},
    {'S',16,22},{0,0,0,0}
};
static const Cmd lv2[] = {
    {'G',23,0,39},{'G',19,0,30},{'G',15,9,39},{'G',11,0,30},{'G',7,9,39},
    {'L',28,19,22},{'L',10,15,18},{'L',28,11,14},{'L',10,7,10},
    {'V',33,15,22},{'V',4,11,18},{'V',36,7,14},
    {'R',12,18},{'R',25,14},{'R',12,10},
    {'o',10,22},{'o',20,22},{'o',37,22},
    {'o',1,18},{'o',16,18},{'o',22,18},
    {'o',14,14},{'o',24,14},{'o',38,14},
    {'o',1,10},{'o',14,10},{'o',22,10},
    {'o',16,6},{'o',24,6},{'o',31,6},{'o',38,6},
    {'S',2,22},{0,0,0,0}
};
static const Cmd lv3[] = {
    {'G',23,0,13},{'b',23,14,25},{'G',23,26,39},
    {'G',19,0,9},{'G',19,30,39},
    {'G',15,4,16},{'a',15,17,22},{'G',15,23,35},
    {'G',11,0,16},{'1',11,17,22},{'G',11,23,39},
    {'G',7,6,14},{'2',7,15,24},{'G',7,25,33},
    {'L',2,19,22},{'L',36,19,22},{'L',6,15,18},{'L',32,15,18},
    {'L',12,11,14},{'L',26,11,14},{'L',8,7,10},{'L',30,7,10},
    {'o',10,22},{'o',29,22},
    {'A',8,18},{'o',34,18},
    {'o',9,14},{'o',30,14},
    {'o',2,10},{'B',20,10},{'o',37,10},
    {'o',7,6},{'o',18,6},{'o',21,6},{'o',32,6},
    {'S',6,22},{0,0,0,0}
};
static const Cmd lv4[] = {
    {'G',23,0,39},
    {'G',19,3,12},{'G',19,16,23},{'G',19,27,36},
    {'G',15,0,8},{'G',15,13,26},{'G',15,31,39},
    {'G',11,4,16},{'G',11,23,35},
    {'G',7,0,10},{'G',7,15,24},{'G',7,29,39},
    {'L',7,19,22},{'L',31,19,22},{'L',19,15,18},
    {'L',5,11,14},{'L',33,11,14},{'L',8,7,10},{'L',30,7,10},
    {'U',10,13,18},{'U',29,13,18},{'U',22,5,14},
    {'V',38,7,22},{'V',1,7,22},
    {'o',14,22},{'o',25,22},
    {'o',4,18},{'o',17,18},{'o',35,18},
    {'o',14,16},{'o',25,16},
    {'o',3,14},{'o',15,14},{'o',24,14},{'o',36,14},
    {'o',12,10},{'o',27,10},
    {'o',4,6},{'o',17,6},{'o',20,6},{'o',35,6},
    {'S',19,22},{0,0,0,0}
};
static const Cmd lv5[] = {
    {'G',23,0,39},{'G',19,0,7},{'G',19,32,39},
    {'G',15,11,17},{'G',15,21,28},
    {'G',11,0,7},{'G',11,32,39},{'G',7,11,28},
    {'L',2,19,22},{'L',36,19,22},
    {'E',8,7,19},{'E',29,7,19},{'E',18,15,23},
    {'R',20,6},
    {'o',5,22},{'o',13,22},{'o',26,22},{'o',34,22},
    {'o',6,18},{'o',33,18},
    {'o',13,14},{'o',26,14},{'o',19,13},
    {'o',9,12},{'o',30,12},
    {'o',2,10},{'o',37,10},
    {'o',13,6},{'o',17,6},{'o',23,6},{'o',26,6},
    {'S',14,22},{0,0,0,0}
};
static const Cmd lv6[] = {
    {'G',23,0,39},{'G',19,2,37},{'G',15,6,33},{'G',11,10,29},
    {'G',7,14,25},{'G',3,17,22},
    {'L',34,19,22},{'L',8,15,18},{'L',26,11,14},{'L',16,7,10},
    {'L',20,3,6},
    {'V',4,19,22},{'V',31,15,18},{'V',12,11,14},
    {'R',20,18},{'R',20,14},{'R',20,10},
    {'o',12,22},{'o',26,22},
    {'o',3,18},{'o',20,18},{'o',36,18},
    {'o',7,14},{'o',32,14},
    {'o',10,10},{'o',29,10},
    {'o',14,6},{'o',25,6},{'o',18,2},
    {'S',1,22},{0,0,0,0}
};

#define NLEV 6
static const Level levels[NLEV] = {
    { "EASY DOES IT",   lv1, 10, 2,  9, 1, 150, 0 },
    { "ROBOT PATROL",   lv2, 11, 3, 13, 1, 150, 0 },
    { "NOW YOU SEE IT", lv3, 13, 5, 11, 2, 120, 0 },
    { "HAILSTORM",      lv4,  9, 1, 10, 1, 150, 45 },
    { "GOING UP",       lv5, 14, 6, 11, 2, 120, 0 },
    { "THE PYRAMID",    lv6, 12, 4, 11, 2, 100, 80 }
};

/* ---- Sprites ----------------------------------------------------------- */

enum { S_STAND, S_RUN1R, S_RUN2R, S_RUN1L, S_RUN2L, S_CLIMB1, S_CLIMB2,
       S_JUMPR, S_JUMPL, S_DEAD, S_ROB1, S_ROB2, S_BUL, S_HAIL, NSPR };

static const char a_stand[] =
    "..cc.." ".cccc." ".1ee1." ".eeee." "999999" "e9999e"
    "e9999e" ".ffff." ".f..f." ".f..f." "cc..cc";
static const char a_run1[] =
    "..cc.." ".ccccc" ".ee1e." ".eeee." ".9999." "e9999e"
    ".9999." ".ffff." "ff..f." "f...ff" "cc...c";
static const char a_run2[] =
    "..cc.." ".ccccc" ".ee1e." ".eeee." ".9999." ".e99e."
    ".9999." ".ffff." "..ff.." "..ff.." "..ccc.";
static const char a_climb[] =
    "e.cc.." "ecccc." "9cccc." "9eeee." "99999e" ".9999e"
    ".9999." ".ffff." ".f..f." "cc..f." "....cc";
static const char a_jump[] =
    "e.cc.e" "9cccc9" "9ee1e9" ".eeee." ".9999." ".9999."
    ".9999." ".ffff." "ff..ff" "f....f" "c....c";
static const char a_rob1[] =
    "..ee.." ".dddd." "dedded" "dddddd" ".5555." "dddddd"
    ".d..d." "dd..dd";
static const char a_rob2[] =
    "..ee.." ".dddd." "dedded" "dddddd" ".5555." "dddddd"
    "..dd.." ".dddd.";
static const char a_bul[] = "ff" "ff";
static const char a_hail[] = "bf" "bb" "bb";

static unsigned char sprpix[NSPR][66];
static fx_sprite *spr[NSPR];

/* flip: 0 as drawn, 1 mirrored left-right, 2 upside down */
static int mkspr(int id, const char *art, int w, int h, int flip)
{
    int x, y;
    char ch;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            ch = art[(flip == 2 ? h - 1 - y : y) * w +
                     (flip == 1 ? w - 1 - x : x)];
            sprpix[id][y * w + x] = (unsigned char)
                (ch == '.' ? 0 : ch <= '9' ? ch - '0' : ch - 'a' + 10);
        }
    spr[id] = fx_compile(sprpix[id], w, h, 0);
    return spr[id] != NULL;
}

static int make_sprites(void)
{
    return mkspr(S_STAND, a_stand, 6, 11, 0) &&
           mkspr(S_RUN1R, a_run1, 6, 11, 0) &&
           mkspr(S_RUN2R, a_run2, 6, 11, 0) &&
           mkspr(S_RUN1L, a_run1, 6, 11, 1) &&
           mkspr(S_RUN2L, a_run2, 6, 11, 1) &&
           mkspr(S_CLIMB1, a_climb, 6, 11, 0) &&
           mkspr(S_CLIMB2, a_climb, 6, 11, 1) &&
           mkspr(S_JUMPR, a_jump, 6, 11, 0) &&
           mkspr(S_JUMPL, a_jump, 6, 11, 1) &&
           mkspr(S_DEAD, a_stand, 6, 11, 2) &&
           mkspr(S_ROB1, a_rob1, 6, 8, 0) &&
           mkspr(S_ROB2, a_rob2, 6, 8, 0) &&
           mkspr(S_BUL, a_bul, 2, 2, 0) &&
           mkspr(S_HAIL, a_hail, 2, 3, 0);
}

/* ---- Sound ------------------------------------------------------------- */

typedef struct { unsigned hz; unsigned char d; } Note;

static const Note s_jump[] = { {392,1},{523,1},{659,1},{784,2},{0,0} };
static const Note s_bomb[] = { {1047,2},{1319,2},{1568,3},{0,0} };
static const Note s_trig[] = { {262,3},{392,3},{523,3},{784,5},{0,0} };
static const Note s_turn[] = { {1760,1},{1397,1},{1760,2},{0,0} };
static const Note s_tick[] = { {1319,1},{0,0} };
static const Note s_life[] = { {1047,3},{1319,3},{1568,3},{2093,6},{0,0} };
static const Note s_ready[] = { {392,3},{523,3},{659,3},{784,6},{0,0} };
static const Note s_over[] = { {392,6},{330,6},{262,6},{196,12},{0,0} };
static const Note s_die[] = {
    {784,3},{698,3},{622,3},{554,3},{494,3},{440,3},{392,3},{330,3},
    {262,4},{196,6},{131,10},{0,0} };
static const Note s_win0[] = {
    {523,4},{659,4},{784,4},{1047,8},{784,4},{1047,12},{0,0} };
static const Note s_win1[] = {
    {330,4},{392,4},{523,4},{659,8},{523,4},{659,12},{0,0} };
static const Note s_win2[] = { {262,12},{196,12},{262,12},{0,0} };
static const Note m_title0[] = {
    {659,4},{659,4},{784,4},{659,4},{523,4},{587,4},{659,8},
    {698,4},{698,4},{880,4},{698,4},{587,4},{659,4},{698,8},
    {784,4},{659,4},{523,4},{659,4},{587,4},{494,4},{392,8},
    {523,4},{659,4},{784,4},{1047,4},{1047,8},{0,8},{0,0} };
static const Note m_title1[] = {
    {131,8},{196,8},{131,8},{196,8},{175,8},{220,8},{175,8},{220,8},
    {131,8},{196,8},{196,8},{247,8},{131,8},{196,8},{131,16},{0,0} };

static struct { const Note *p; int left; unsigned hz; int gap; } vc[3];

static void sfx(int v, const Note *s)
{
    vc[v].p = s;
    vc[v].left = 0;
}

static void snd_quiet(void)
{
    int v;

    for (v = 0; v < 3; v++) {
        vc[v].p = NULL;
        snd_tone(v, 440, 0);
    }
}

static void snd_tick(void)
{
    int v;

    for (v = 0; v < 3; v++) {
        if (!vc[v].p)
            continue;
        if (vc[v].left > 0) {
            if (--vc[v].left == 1 && vc[v].gap)
                snd_tone(v, vc[v].hz, 0);   /* gap between notes */
            if (vc[v].left > 0)
                continue;
        }
        if (vc[v].p->d == 0) {
            snd_tone(v, 440, 0);
            vc[v].p = NULL;
            continue;
        }
        vc[v].hz = vc[v].p->hz;
        vc[v].gap = vc[v].p->d >= 3;
        vc[v].left = vc[v].p->d;
        if (vc[v].hz)
            snd_tone(v, vc[v].hz, v == 0 ? 13 : 10);
        else
            snd_tone(v, 440, 0);
        vc[v].p++;
    }
}

/* ---- Game state -------------------------------------------------------- */

enum { G_TITLE, G_READY, G_PLAY, G_DONE, G_OVER };
enum { P_GROUND, P_AIR, P_CLIMB, P_DEAD };

static unsigned bg;                 /* background buffer segment */
static unsigned char map[ROWS][COLS];
static const Level *lv;
static int gstate, gtimer, level, round, lives, bombs, bonus;
static long score, hiscore, nextlife;
static unsigned ticks;
static const char *msg;
static int quit, paused, testmode;

static int px, py;                  /* x in 1/16 pixels; y of the feet */
static int pstate, face, moving, anim;
static int ji, jdir, fally0, nocatch, cltype, onlift, dead_t;
static int startx, starty;

static const signed char jtab[JLEN] = {
    -3,-3,-2,-2,-2,-1,-1,-1,0,0,1,1,1,2,2,2,3,3 };

#define MAXB 4
static struct { int x, y; signed char dx, dy; char on, turned; } bl[MAXB];
static int bcool;
#define MAXH 4
static struct { int x, y; char on; } hl[MAXH];
#define MAXR 4
static struct { int x, y; signed char d; } rb[MAXR];
static int nrob;
#define MAXL 3
static struct { int x, y, y0, y1; signed char d; unsigned char wait; }
    lf[MAXL];
static int nlift;

static struct { int l, r, u, d, j, jedge; } inp;
static int lastj;
static const char *scr;             /* /TEST input script */
static int scr_left, scr_done;

static unsigned seed = 12345;
static unsigned rnd(void)
{
    seed = seed * 25173u + 13849u;
    return seed >> 4;
}

/* ---- Background and status line ---------------------------------------- */

/* Parts of the background that changed: copied to the page for two
 * frames, once for each video page. */
#define MAXDQ 16
static struct { int x0, y0, x1, y1, n; } dq[MAXDQ];
static int ndq, fullref;

static void add_dirty(int x0, int y0, int x1, int y1)
{
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

static int solid(int c, int r)
{
    if ((unsigned)c >= COLS || (unsigned)r >= ROWS)
        return 0;
    return map[r][c] & M_SOLID;
}

static int climb(int c, int r)
{
    if ((unsigned)c >= COLS || (unsigned)r >= ROWS)
        return 0;
    return (map[r][c] >> 1) & 3;
}

static int yrow(int y)
{
    return y < TOP ? -1 : (y - TOP) >> 3;
}

/* Draws one map cell into the current target (the background). */
static void draw_cell(int c, int r, int clear)
{
    int x = c << 2, y = ROWY(r), t, i, gl, gd;
    unsigned char m = map[r][c];

    if (clear)
        fx_fill_rect(x, y, 4, 8, 0);
    if (m & M_SOLID) {
        gl = (m & M_GROUP) ? 15 : lv->gl;
        gd = (m & M_GROUP) ? 7 : lv->gd;
        fx_fill_rect(x, y, 4, 1, FX_SOLID2(gl));
        fx_fill_rect(x, y + 1, 4, 1, (unsigned)FX_PAIR(gl, gd) * 0x101u);
        fx_fill_rect(x, y + 2, 4, 1, FX_SOLID2(gd));
    } else if (m & M_HIDDEN) {
        fx_pset(x, y, 8);
        fx_pset(x + 2, y, 8);
    }
    t = (m >> 1) & 3;
    if (t == C_LAD) {
        fx_fill_rect(x, y + 3, 4, 1, FX_SOLID2(7));
        fx_fill_rect(x, y + 7, 4, 1, FX_SOLID2(7));
        if (climb(c - 1, r) != C_LAD)
            for (i = 0; i < 8; i++)
                fx_pset(x, y + i, lv->lad);
        if (climb(c + 1, r) != C_LAD)
            for (i = 0; i < 8; i++)
                fx_pset(x + 3, y + i, lv->lad);
    } else if (t) {
        for (i = 0; i < 8; i++)
            fx_pset(x + 2, y + i, (i & 3) == 1 ? 15 : t == C_UP ? 14 : 12);
    }
    if (m & M_BOMB) {
        t = (m & M_GROUP) ? 14 : 15;
        fx_pset(x + 1, y + 3, 12);
        fx_fill_rect(x, y + 5, 4, 2, FX_SOLID2(t));
        fx_pset(x + 1, y + 4, t); fx_pset(x + 2, y + 4, t);
        fx_pset(x + 1, y + 7, t); fx_pset(x + 2, y + 7, t);
    }
}

static void put_num(char *s, long v, int n)
{
    s[n] = 0;
    while (n--) {
        s[n] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
}

static void hud_part(int x, int w, const char *s, int col)
{
    fx_target(bg);
    fx_fill_rect(x, 0, w, 8, 0);
    fx_text(x, 0, s, col);
    fx_target(0);
    add_dirty(x, 0, x + w - 1, 7);
}

static void hud_score(void)
{
    char b[8];

    put_num(b, score, 6);
    hud_part(0, 48, b, 15);
}

static void hud_lives(void)
{
    char b[4];

    b[0] = 'x';
    put_num(b + 1, lives > 9 ? 9 : lives, 1);
    hud_part(88, 16, b, 14);
}

static void hud_bonus(void)
{
    char b[8];

    put_num(b, bonus, 4);
    hud_part(128, 32, b, 10);
}

static void hud_all(void)
{
    char b[8];

    b[0] = 'L';
    put_num(b + 1, level + 1 + round * NLEV, 2);
    hud_part(56, 24, b, 11);
    hud_score();
    hud_lives();
    hud_bonus();
}

static void add_score(int n)
{
    score += n;
    if (score >= nextlife) {
        nextlife += 10000;
        lives++;
        hud_lives();
        sfx(1, s_life);
    }
    hud_score();
}

/* ---- Levels ------------------------------------------------------------ */

static void load_level(int n)
{
    const Cmd *p;
    int r, c;

    lv = &levels[n];
    memset(map, 0, sizeof map);
    nrob = nlift = bombs = 0;
    startx = 4; starty = ROWY(23);
    for (p = lv->cmd; p->op; p++) {
        switch (p->op) {
        case 'G':
            for (c = p->b; c <= p->c; c++) map[p->a][c] |= M_SOLID;
            break;
        case 'a': case 'b': case 'c': case 'd':
            for (c = p->b; c <= p->c; c++)
                map[p->a][c] |= M_SOLID | ((p->op - 'a' + 1) << 4);
            break;
        case '1': case '2': case '3': case '4':
            for (c = p->b; c <= p->c; c++)
                map[p->a][c] |= M_HIDDEN | ((p->op - '1' + 1) << 4);
            break;
        case 'L':
            for (r = p->b; r <= p->c; r++) {
                map[r][p->a] |= C_LAD << 1;
                map[r][p->a + 1] |= C_LAD << 1;
            }
            break;
        case 'U':
            for (r = p->b; r <= p->c; r++) map[r][p->a] |= C_UP << 1;
            break;
        case 'V':
            for (r = p->b; r <= p->c; r++) map[r][p->a] |= C_DOWN << 1;
            break;
        case 'o':
            map[p->b][p->a] |= M_BOMB;
            bombs++;
            break;
        case 'A': case 'B': case 'C': case 'D':
            map[p->b][p->a] |= M_BOMB | ((p->op - 'A' + 1) << 4);
            bombs++;
            break;
        case 'S':
            startx = p->a << 2;
            starty = ROWY(p->b + 1);
            break;
        case 'R':
            if (nrob < MAXR) {
                rb[nrob].x = p->a << 2;
                rb[nrob].y = ROWY(p->b + 1);
                rb[nrob].d = (nrob & 1) ? -1 : 1;
                nrob++;
            }
            break;
        case 'E':
            if (nlift < MAXL) {
                lf[nlift].x = p->a << 2;
                lf[nlift].y0 = ROWY(p->b);
                lf[nlift].y1 = ROWY(p->c);
                lf[nlift].y = lf[nlift].y1;
                lf[nlift].d = -1;
                lf[nlift].wait = (unsigned char)(20 + nlift * 15);
                nlift++;
            }
            break;
        }
    }
    fx_target(bg);
    fx_clear(0);
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            if (map[r][c])
                draw_cell(c, r, 0);
    fx_target(0);
    ndq = 0;
    fullref = 2;
}

static void reset_player(void)
{
    int i;

    px = startx << 4;
    py = starty;
    pstate = P_GROUND;
    face = 1;
    moving = nocatch = 0;
    onlift = -1;
    for (i = 0; i < MAXB; i++) bl[i].on = 0;
    for (i = 0; i < MAXH; i++) hl[i].on = 0;
    bcool = 60;
}

static void start_level(void)
{
    snd_quiet();
    load_level(level);
    bonus = 2000;
    hud_all();
    reset_player();
    gstate = G_READY;
    gtimer = 45;
    msg = lv->name;
    sfx(0, s_ready);
}

static void new_game(void)
{
    score = 0;
    nextlife = 10000;
    lives = 5;
    level = round = 0;
    if (!testmode)
        seed = (unsigned)tmr_ticks();
    start_level();
}

/* A bomb's group fired: its girders vanish or appear. */
static void trigger(int g)
{
    int r, c, x0, x1 = 0;
    unsigned char m;

    fx_target(bg);
    for (r = 0; r < ROWS; r++) {
        x0 = -1;
        for (c = 0; c < COLS; c++) {
            m = map[r][c];
            if ((m & M_GROUP) != (g << 4) || (m & M_BOMB))
                continue;
            if (m & M_HIDDEN)
                m = (unsigned char)((m & ~(M_HIDDEN | M_GROUP)) | M_SOLID);
            else
                m &= ~(M_SOLID | M_GROUP);
            map[r][c] = m;
            draw_cell(c, r, 1);
            if (x0 < 0) x0 = c;
            x1 = c;
        }
        if (x0 >= 0)
            add_dirty(x0 << 2, ROWY(r), (x1 << 2) + 3, ROWY(r) + 7);
    }
    fx_target(0);
}

/* ---- Title ------------------------------------------------------------- */

static int bgpix(int x, int y)
{
    unsigned char far *p = (unsigned char far *)
        MK_FP(bg, ((y & 1) ? 0x2000 : 0) + (y >> 1) * 80 + (x >> 1));

    return (x & 1) ? (*p & 15) : (*p >> 4);
}

/* 8x8 text enlarged to 16x32 per letter, drawn into the background. */
static void big_text(int x, int y, const char *s)
{
    static const unsigned char grad[8] = { 14, 14, 14, 12, 12, 12, 4, 4 };
    unsigned char bm[8][10];
    int n = strlen(s), i, j;

    memset(bm, 0, sizeof bm);
    fx_text(0, 0, s, 15);
    for (j = 0; j < 8; j++)
        for (i = 0; i < n * 8; i++)
            if (bgpix(i, j))
                bm[j][i >> 3] |= (unsigned char)(0x80 >> (i & 7));
    fx_fill_rect(0, 0, n * 8, 8, 0);
    for (j = 0; j < 8; j++)
        for (i = 0; i < n * 8; i++)
            if (bm[j][i >> 3] & (0x80 >> (i & 7)))
                fx_fill_rect(x + i * 2, y + j * 4, 2, 4,
                             FX_SOLID2(grad[j]));
}

static int tx, tdir;                /* the title's runner */

static void to_title(void)
{
    char b[12];
    int c;

    snd_quiet();
    if (score > hiscore)
        hiscore = score;
    gstate = G_TITLE;
    msg = NULL;
    lv = &levels[0];
    memset(map, 0, sizeof map);
    fx_target(bg);
    fx_clear(0);
    big_text(24, 14, "JUMPMAN");
    fx_text(20, 52, "- J U N I O R -", 11);
    fx_text(8, 76, "ARROWS: RUN, CLIMB", 7);
    fx_text(36, 88, "SPACE: JUMP", 7);
    fx_text(4, 100, "P: PAUSE  ESC: QUIT", 7);
    fx_text(4, 120, "GRAB ALL THE BOMBS!", 14);
    strcpy(b, "HIGH ");
    put_num(b + 5, hiscore, 6);
    fx_text(36, 136, b, 10);
    for (c = 0; c < COLS; c++) {
        map[22][c] = M_SOLID;
        draw_cell(c, 22, 0);
    }
    fx_target(0);
    ndq = 0;
    fullref = 2;
    tx = 0;
    tdir = 1;
}

static void tick_title(void)
{
    tx += tdir;
    if (tx <= 0 || tx >= 154)
        tdir = -tdir;
    if (!vc[0].p) {
        sfx(0, m_title0);
        sfx(1, m_title1);
    }
    if (inp.jedge || key_down[KEY_ENTER])
        new_game();
}

/* ---- The player -------------------------------------------------------- */

/* Standing on a lift or a girder? Snaps onto a lift just under the feet. */
static int on_ground(void)
{
    int x = px >> 4, i, r;

    onlift = -1;
    for (i = 0; i < nlift; i++)
        if (py >= lf[i].y && py <= lf[i].y + 2 &&
            x + 4 >= lf[i].x && x + 1 <= lf[i].x + 11) {
            py = lf[i].y;
            onlift = i;
            return 1;
        }
    if (py & 7)
        return 0;
    r = yrow(py);
    return solid((x + 1) >> 2, r) || solid((x + 4) >> 2, r);
}

/* why: 'f' fell, 'r' robot, 'b' bullet, 'h' hail */
static void kill(int why)
{
    if (pstate == P_DEAD)
        return;
    if (testmode)
        dbg_log("killed by %c at x=%d y=%d", why, px >> 4, py);
    pstate = P_DEAD;
    dead_t = 0;
    py = (py + 1) & ~1;
    sfx(0, s_die);
}

static void grab(int c, int r, int t)
{
    if (t == C_LAD) {
        if (climb(c - 1, r) == C_LAD)
            c--;
        px = ((c << 2) + 1) << 4;
    } else
        px = ((c << 2) - 1) << 4;
    cltype = t;
    pstate = P_CLIMB;
}

/* Looks for a ladder or rope of a type in `mask` (bit per type) within
 * reach in row r, and grabs it. */
static int try_grab(int r, int mask)
{
    static const signed char off[3] = { 3, 1, 4 };
    int i, c, t;

    for (i = 0; i < 3; i++) {
        c = ((px >> 4) + off[i]) >> 2;
        t = climb(c, r);
        if (t && (mask & (1 << t))) {
            grab(c, r, t);
            return 1;
        }
    }
    return 0;
}

static void fall(int ji0, int dir)
{
    pstate = P_AIR;
    ji = ji0;
    jdir = dir;
    if (dir)
        face = dir;
    fally0 = py;
}

/* Step off a ladder or rope: onto a girder if one is at foot level,
 * otherwise a small hop sideways. */
static void leave(int dir)
{
    int d, save = py;

    for (d = 0; d <= 3; d++) {
        py = save + d;
        if (on_ground()) break;
        py = save - d;
        if (on_ground()) break;
    }
    face = dir;
    if (d <= 3) {
        pstate = P_GROUND;
        return;
    }
    py = save;
    fall(4, dir);
    nocatch = 10;
}

static void clamp_x(void)
{
    if (px < 0) px = 0;
    if (px > 154 * 16) px = 154 * 16;
}

static void tick_player(void)
{
    int x = px >> 4, c = (x + 3) >> 2, i, dy, landed;
    int dir = inp.r - inp.l;

    switch (pstate) {
    case P_GROUND:
        moving = 0;
        if (!on_ground()) {
            fall(JLEN, 0);
            break;
        }
        if (inp.jedge) {
            fall(0, dir);
            sfx(0, s_jump);
            break;
        }
        if (inp.u &&
            try_grab(yrow(py - 6), (1 << C_LAD) | (1 << C_UP)))
            break;
        if (inp.d) {
            if (try_grab(yrow(py), (1 << C_LAD) | (1 << C_DOWN))) {
                py++;
                break;
            }
            if (try_grab(yrow(py - 6), 1 << C_DOWN))
                break;
        }
        if (dir) {
            face = dir;
            px += dir * SPEED;
            clamp_x();
            moving = 1;
            anim++;
        }
        break;

    case P_AIR:
        if (nocatch)
            nocatch--;
        px += jdir * SPEED;
        clamp_x();
        dy = ji < JLEN ? jtab[ji++] : 3;
        landed = 0;
        if (dy < 0)
            py += dy;
        else
            for (i = 0; i < dy && !landed; i++) {
                py++;
                landed = on_ground();
            }
        if (landed) {
            if (py - fally0 > MAXFALL)
                kill('f');
            else
                pstate = P_GROUND;
            break;
        }
        if (py > 216) {
            kill('f');
            break;
        }
        if (!nocatch)
            try_grab(yrow(py - 6), (1 << C_UP) | (1 << C_DOWN) |
                     ((inp.u || inp.d) ? 1 << C_LAD : 0));
        break;

    case P_CLIMB:
        if (inp.jedge && dir) {
            fall(0, dir);
            nocatch = 10;
            sfx(0, s_jump);
            break;
        }
        if (dir) {
            leave(dir);
            break;
        }
        if (inp.u && cltype != C_DOWN) {
            if (climb(c, yrow(py - 2)) == cltype ||
                climb(c, yrow(py - 1)) == cltype)
                py--;
        } else if (inp.d && cltype != C_UP) {
            if (climb(c, yrow(py)) == cltype)
                py++;
            else if (on_ground())
                pstate = P_GROUND;
            else if (cltype != C_LAD) {
                fall(JLEN, 0);      /* slid off the end of the rope */
                nocatch = 10;
            }
        }
        if (pstate == P_CLIMB && climb(c, yrow(py - 1)) == 0 && on_ground())
            pstate = P_GROUND;
        break;

    case P_DEAD:
        dead_t++;
        if (py < 230 && !on_ground())
            py += 2;
        break;
    }
}

static void collect(void)
{
    int x = px >> 4, c0 = (x + 1) >> 2, c1 = (x + 4) >> 2;
    int r0 = yrow(py - 10), r1 = yrow(py - 1), r, c, g;

    for (r = r0; r <= r1; r++)
        for (c = c0; c <= c1; c++) {
            if ((unsigned)r >= ROWS || (unsigned)c >= COLS ||
                !(map[r][c] & M_BOMB))
                continue;
            g = (map[r][c] & M_GROUP) >> 4;
            map[r][c] &= ~(M_BOMB | M_GROUP);
            fx_target(bg);
            draw_cell(c, r, 1);
            fx_target(0);
            add_dirty(c << 2, ROWY(r), (c << 2) + 3, ROWY(r) + 7);
            bombs--;
            add_score(100);
            sfx(0, s_bomb);
            if (g) {
                trigger(g);
                sfx(2, s_trig);
            }
        }
}

/* ---- Lifts and enemies ------------------------------------------------- */

static void tick_lifts(void)
{
    int i;

    if (ticks & 1)
        return;
    for (i = 0; i < nlift; i++) {
        if (lf[i].wait) {
            lf[i].wait--;
            continue;
        }
        lf[i].y += lf[i].d;
        if (pstate == P_GROUND && onlift == i)
            py += lf[i].d;
        if (lf[i].y <= lf[i].y0 || lf[i].y >= lf[i].y1) {
            lf[i].d = (signed char)-lf[i].d;
            lf[i].wait = 20;
        }
    }
}

/* Does the box x0..x1, y0..y1 touch the player? */
static int hits(int x0, int y0, int x1, int y1)
{
    int x = px >> 4;

    return pstate != P_DEAD && x1 >= x + 1 && x0 <= x + 4 &&
           y1 >= py - 10 && y0 <= py - 1;
}

static void tick_enemies(void)
{
    int i, nx, lead, cx = (px >> 4) + 3, cy = py - 6, n;

    for (i = 0; i < nrob; i++) {
        if (ticks & 1) {
            nx = rb[i].x + rb[i].d;
            lead = rb[i].d > 0 ? nx + 5 : nx;
            if (nx < 0 || nx > 154 || !solid(lead >> 2, yrow(rb[i].y)))
                rb[i].d = (signed char)-rb[i].d;
            else
                rb[i].x = nx;
        }
        if (hits(rb[i].x + 1, rb[i].y - 6, rb[i].x + 4, rb[i].y - 1))
            kill('r');
    }

    /* bullets drift in, then turn and fire when they line up with you */
    n = lv->maxb + round;
    if (n > MAXB) n = MAXB;
    if (bcool)
        bcool--;
    else
        for (i = 0; i < n; i++)
            if (!bl[i].on) {
                bl[i].on = 1;
                bl[i].turned = 0;
                bl[i].dx = bl[i].dy = 0;
                switch (rnd() % 3) {
                case 0:
                    bl[i].x = 0; bl[i].dx = 1;
                    bl[i].y = TOP + 8 + rnd() % 176;
                    break;
                case 1:
                    bl[i].x = 157; bl[i].dx = -1;
                    bl[i].y = TOP + 8 + rnd() % 176;
                    break;
                default:
                    bl[i].y = TOP; bl[i].dy = 1;
                    bl[i].x = 4 + rnd() % 150;
                    break;
                }
                bcool = lv->bint - round * 20;
                if (bcool < 40) bcool = 40;
                bcool += rnd() % 32;
                break;
            }
    for (i = 0; i < MAXB; i++) {
        if (!bl[i].on)
            continue;
        if (!bl[i].turned && pstate != P_DEAD) {
            if (bl[i].dx && abs(bl[i].x - cx) <= 1 &&
                abs(bl[i].y - cy) > 12) {
                bl[i].dx = 0;
                bl[i].dy = (signed char)(cy > bl[i].y ? 2 : -2);
                bl[i].turned = 1;
                sfx(1, s_turn);
            } else if (bl[i].dy && abs(bl[i].y - cy) <= 2 &&
                       abs(bl[i].x - cx) > 12) {
                bl[i].dy = 0;
                bl[i].dx = (signed char)(cx > bl[i].x ? 2 : -2);
                bl[i].turned = 1;
                sfx(1, s_turn);
            }
        }
        if (bl[i].turned || (ticks & 1)) {
            bl[i].x += bl[i].dx;
            bl[i].y += bl[i].dy;
        }
        if (bl[i].x < 0 || bl[i].x > 158 || bl[i].y < TOP || bl[i].y > 198)
            bl[i].on = 0;
        else if (hits(bl[i].x, bl[i].y, bl[i].x + 1, bl[i].y + 1))
            kill('b');
    }

    if (lv->hail && rnd() % lv->hail == 0)
        for (i = 0; i < MAXH; i++)
            if (!hl[i].on) {
                hl[i].on = 1;
                hl[i].x = 2 + rnd() % 156;
                hl[i].y = TOP;
                break;
            }
    for (i = 0; i < MAXH; i++) {
        if (!hl[i].on)
            continue;
        hl[i].y += 2;
        if (hl[i].y > 196)
            hl[i].on = 0;
        else if (hits(hl[i].x, hl[i].y, hl[i].x + 1, hl[i].y + 2))
            kill('h');
    }
}

/* ---- One game tick ----------------------------------------------------- */

static void read_input(void)
{
    if (testmode) {
        if (scr_left == 0) {
            inp.l = inp.r = inp.u = inp.d = inp.j = 0;
            if (!scr || !*scr)
                scr_done = 1;
            else {
                for (; *scr && (*scr < '0' || *scr > '9'); scr++)
                    switch (*scr) {
                    case 'L': inp.l = 1; break;
                    case 'R': inp.r = 1; break;
                    case 'U': inp.u = 1; break;
                    case 'D': inp.d = 1; break;
                    case 'J': inp.j = 1; break;
                    case 'W': bombs = 0; break;   /* win the level */
                    }
                while (*scr >= '0' && *scr <= '9')
                    scr_left = scr_left * 10 + (*scr++ - '0');
            }
        }
        if (scr_left)
            scr_left--;
    } else {
        inp.l = key_down[KEY_LEFT] || key_down[KEY_A];
        inp.r = key_down[KEY_RIGHT] || key_down[KEY_D];
        inp.u = key_down[KEY_UP] || key_down[KEY_W];
        inp.d = key_down[KEY_DOWN] || key_down[KEY_S];
        inp.j = key_down[KEY_SPACE] || key_down[KEY_ALT] ||
                key_down[KEY_CTRL];
    }
    inp.jedge = inp.j && !lastj;
    lastj = inp.j;
}

static void tick_play(void)
{
    tick_lifts();
    tick_player();
    if (pstate == P_DEAD) {
        if (dead_t > 60) {
            lives--;
            hud_lives();
            if (lives <= 0) {
                gstate = G_OVER;
                gtimer = 100;
                msg = "GAME OVER";
                sfx(0, s_over);
            } else {
                reset_player();
                gstate = G_READY;
                gtimer = 30;
                msg = "GET READY";
            }
            return;
        }
    } else {
        collect();
        if (bombs == 0) {
            gstate = G_DONE;
            gtimer = 50;
            msg = "WELL DONE!";
            sfx(0, s_win0);
            sfx(1, s_win1);
            sfx(2, s_win2);
            return;
        }
        if (ticks % 30 == 0 && bonus > 0) {
            bonus -= 10;
            hud_bonus();
        }
    }
    tick_enemies();
}

static void tick(void)
{
    int n;

    read_input();
    snd_tick();
    switch (gstate) {
    case G_TITLE:
        tick_title();
        break;
    case G_READY:
        if (--gtimer <= 0) {
            gstate = G_PLAY;
            msg = NULL;
        }
        break;
    case G_PLAY:
        tick_play();
        break;
    case G_DONE:
        if (gtimer > 0)
            gtimer--;               /* let the fanfare play first */
        else if (bonus > 0) {
            n = bonus > 50 ? 50 : bonus;
            bonus -= n;
            add_score(n);
            hud_bonus();
            sfx(1, s_tick);
        } else if (--gtimer < -30) {
            if (++level == NLEV) {
                level = 0;
                round++;
            }
            start_level();
        }
        break;
    case G_OVER:
        if (--gtimer <= 0)
            to_title();
        break;
    }
    ticks++;
}

/* ---- Drawing ----------------------------------------------------------- */

static void draw_msg(const char *s, int y, int col)
{
    int w = strlen(s) * 8, x = (160 - w) / 2;

    fx_fill_rect(x - 2, y - 2, w + 4, 12, 0);
    fx_text(x, y, s, col);
}

static void draw_play(void)
{
    int i, s, x = px >> 4;

    for (i = 0; i < nlift; i++) {
        fx_fill_rect(lf[i].x, lf[i].y, 12, 1, FX_SOLID2(15));
        fx_fill_rect(lf[i].x, lf[i].y + 1, 12, 2, FX_SOLID2(7));
    }
    for (i = 0; i < nrob; i++)
        fx_sprite_draw(spr[S_ROB1 + ((ticks >> 3) & 1)],
                       rb[i].x, rb[i].y - 8);
    switch (pstate) {
    case P_GROUND:
        if (!moving) s = S_STAND;
        else if (face > 0) s = (anim & 4) ? S_RUN1R : S_RUN2R;
        else s = (anim & 4) ? S_RUN1L : S_RUN2L;
        break;
    case P_AIR:
        s = face > 0 ? S_JUMPR : S_JUMPL;
        break;
    case P_CLIMB:
        s = (py & 4) ? S_CLIMB1 : S_CLIMB2;
        break;
    default:
        s = (dead_t & 4) ? S_DEAD : S_STAND;
        break;
    }
    if (py < 211)
        fx_sprite_draw(spr[s], x, py - 11);
    for (i = 0; i < MAXB; i++)
        if (bl[i].on)
            fx_sprite_draw(spr[S_BUL], bl[i].x, bl[i].y);
    for (i = 0; i < MAXH; i++)
        if (hl[i].on)
            fx_sprite_draw(spr[S_HAIL], hl[i].x, hl[i].y);
    if (paused)
        draw_msg("PAUSED", 96, 15);
    else if (msg)
        draw_msg(msg, 96, 14);
}

static void draw_title(void)
{
    int rx = tx - 40 * tdir;

    fx_sprite_draw(spr[tdir > 0 ? ((tx & 4) ? S_RUN1R : S_RUN2R)
                                : ((tx & 4) ? S_RUN1L : S_RUN2L)],
                   tx, ROWY(22) - 11);
    if (rx >= 0 && rx <= 154)
        fx_sprite_draw(spr[S_ROB1 + ((tx >> 3) & 1)], rx, ROWY(22) - 8);
    if (ticks & 16)
        fx_text(36, 156, "PRESS SPACE", 15);
}

static void frame(void)
{
    fx_begin();
    fx_restore(bg);
    flush_dirty();
    if (gstate == G_TITLE)
        draw_title();
    else
        draw_play();
    fx_flip();
}

/* ---- Main -------------------------------------------------------------- */

static void run_test(int tlevel)
{
    unsigned t;

    remove("LOG.TXT");
    dbg_log("pages=%d irq=%d", fx_pages, fx_irq);
    if (tlevel < 1 || tlevel > NLEV) {
        to_title();
        for (t = 0; t < 40; t++) {
            tick();
            frame();
        }
    } else {
        new_game();
        level = tlevel - 1;
        start_level();
        gstate = G_PLAY;
        msg = NULL;
        for (t = 0; !scr_done && t < 3000; t++) {
            tick();
            frame();
            if ((t & 7) == 0)
                dbg_log("t=%u x=%d y=%d st=%d g=%d", t, px >> 4, py,
                        pstate, gstate);
        }
        dbg_log("end x=%d y=%d st=%d g=%d lives=%d bombs=%d score=%ld",
                px >> 4, py, pstate, gstate, lives, bombs, score);
    }
    frame();
    fx_begin();
    fx_screenshot("SHOT.PPM");
}

int main(int argc, char **argv)
{
    unsigned long next, now;
    int steps, escwas = 0, pwas = 0, tlevel = 1;

    if (argc > 1 && stricmp(argv[1], "/TEST") == 0) {
        testmode = 1;
        if (argc > 2) tlevel = atoi(argv[2]);
        if (argc > 3) scr = argv[3];
    }
    if (fx_init(0)) {
        printf("Cannot start graphics (error %d)\n", gfx_err);
        return 1;
    }
    key_init();
    tmr_init();
    snd_init();
    bg = fx_alloc_buffer();
    if (!bg || !make_sprites()) {
        gfx_done();
        printf("Not enough memory\n");
        return 1;
    }
    fx_track(1);

    if (testmode)
        run_test(tlevel);
    else {
        to_title();
        next = tmr_ticks();
        while (!quit) {
            if (key_down[KEY_ESC] && !escwas) {
                if (gstate == G_TITLE)
                    quit = 1;
                else
                    to_title();
                paused = 0;
            }
            escwas = key_down[KEY_ESC];
            if (key_down[KEY_P] && !pwas && gstate != G_TITLE) {
                paused = !paused;
                if (paused)
                    snd_quiet();
            }
            pwas = key_down[KEY_P];

            now = tmr_ticks();
            steps = 0;
            while ((long)(now - next) >= 0 && steps < 4) {
                if (!paused)
                    tick();
                next += TICK;
                steps++;
            }
            if ((long)(now - next) > 0)
                next = now;         /* too slow to catch up: let it go */
            if (steps)
                frame();
        }
    }
    snd_quiet();
    gfx_done();
    return 0;
}
