/* LEMMJR.C - Lemmings Junior: a Lemmings-style game for the IBM PCjr.
 *
 * 160x200, 16 colors, JRGFX fast path. The terrain lives in a background
 * buffer (and a type map for collisions); lemmings, cursor and text are
 * sprites restored every frame, and terrain changes are copied to both
 * video pages as small dirty rectangles.
 *
 * LEMMJR /TEST n [script] plays level n with a script of "tick:lem:skill"
 * entries (skills C F B K U H M D, e.g. 170:0:D,400:1:K), logs the
 * result to LOG.TXT, saves SHOT.PPM and exits. /TEST 0 shows the title.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <i86.h>
#include "jrgfx.h"

#define PW 160
#define PH 184                  /* playfield rows 0..183, HUD below */
#define MAXLEM 20
#define MAXFALL 60
#define NSKILL 8
#define TICK (TMR_HZ / 20)      /* 20 ticks a second; lemmings move on 10 */

enum { SK_CLIMB, SK_FLOAT, SK_BOMB, SK_BLOCK, SK_BUILD, SK_BASH, SK_MINE,
       SK_DIG };
static const char skchar[NSKILL + 1] = "CFBKUHMD";
static const char *skname[NSKILL] = {
    "CLIMBER", "FLOATER", "BOMBER", "BLOCKER", "BUILDER", "BASHER",
    "MINER", "DIGGER" };

enum { L_NONE, L_WALK, L_FALL, L_CLIMB, L_BLOCK, L_BUILD, L_BASH, L_MINE,
       L_DIG, L_SPLAT, L_BOOM };
enum { G_TITLE, G_INTRO, G_PLAY, G_RESULT, G_WON };

#define KEY_TAB 0x0F
#define KEY_P   0x19
#define KEY_N   0x31
#define KEY_1   0x02

typedef struct {
    int x, y;                   /* the foot pixel; ground is at y+1 */
    signed char dir, state;
    unsigned char climber, floater;
    int bomb;                   /* ticks to explosion, 0 = none */
    int fallen, work, bricks, anim;
} Lem;

/* ---- Levels ------------------------------------------------------------ */

/* D x y w h: dirt.  S: steel.  A: air (carve).  E x y: entrance trapdoor.
 * X x y: exit door, y = floor row. */
typedef struct { unsigned char op, x, y, w, h; } TCmd;
typedef struct {
    const char *name;
    const TCmd *cmd;
    unsigned char count, need, mins, secs, rate;
    unsigned char skills[NSKILL];
    unsigned char c1, c2;       /* dirt colors */
} Level;

static const TCmd t1[] = {
    {'D',0,120,160,64},{'A',40,150,120,26},{'S',0,176,40,8},
    {'E',12,70,0,0},{'X',130,175,0,0},{0,0,0,0,0} };
static const TCmd t2[] = {
    {'D',0,120,61,64},{'D',100,120,60,64},
    {'E',8,70,0,0},{'X',136,119,0,0},{0,0,0,0,0} };
static const TCmd t3[] = {
    {'D',0,150,160,34},{'D',80,90,16,60},{'S',0,176,160,8},
    {'E',10,100,0,0},{'X',130,149,0,0},{0,0,0,0,0} };
static const TCmd t4[] = {
    {'D',0,170,160,14},{'D',60,60,8,110},{'S',56,170,16,14},
    {'E',8,118,0,0},{'X',130,169,0,0},{0,0,0,0,0} };
static const TCmd t5[] = {
    {'D',0,70,101,10},{'D',70,50,8,20},{'D',0,120,131,64},
    {'D',140,120,20,64},{'A',105,150,25,26},{'S',0,120,40,6},
    {'E',10,30,0,0},{'X',112,175,0,0},{0,0,0,0,0} };

#define NLEV 5
static const Level levels[NLEV] = {
    { "JUST DIG",        t1, 10, 5, 3, 0, 50, {0,0,0,0,0,0,0,9}, 6, 4 },
    { "MIND THE GAP",    t2, 10, 5, 4, 0, 50, {0,0,2,2,4,0,0,0}, 2, 6 },
    { "BASH ON THROUGH", t3, 10, 6, 3, 0, 50, {1,0,1,1,2,3,0,0}, 4, 6 },
    { "UP AND OVER",     t4, 10, 8, 4, 0, 50, {9,9,0,0,0,0,0,0}, 3, 1 },
    { "THE WORKS",       t5, 12, 6, 5, 0, 50, {1,1,2,2,2,2,2,2}, 5, 1 }
};

/* ---- Sprites (4x8) ----------------------------------------------------- */

enum { SP_WALK1R, SP_WALK2R, SP_WALK1L, SP_WALK2L, SP_FALL, SP_FLOAT,
       SP_BLOCK, SP_DIG1, SP_DIG2, SP_BASHR, SP_BASHL, SP_CLIMBR, SP_CLIMBL,
       SP_BUILDR, SP_BUILDL, SP_SPLAT, SP_BOOM, NSPR };

static const char a_walk1[] = ".aa." ".aa." ".cc." ".cc." ".99." "9999" ".99." "9..9";
static const char a_walk2[] = ".aa." ".aa." ".cc." ".cc." ".99." ".99." ".99." ".99.";
static const char a_fall[]  = "9aa9" "9aa9" ".cc." ".cc." ".99." ".99." ".99." "9..9";
static const char a_float[] = "eeee" "e77e" ".aa." ".cc." ".99." ".99." ".99." "9..9";
static const char a_block[] = ".aa." ".aa." ".cc." "9cc9" "9999" ".99." ".99." "9..9";
static const char a_dig1[]  = ".aa." ".aa." ".cc." ".cc." "9999" ".99." ".9.7" "9..7";
static const char a_dig2[]  = ".aa." ".aa." ".cc." ".cc." ".99." "9999" ".99." "9.7.";
static const char a_bash[]  = ".aa." ".aa." ".cc." ".cc7" ".999" "9997" ".99." "9..9";
static const char a_climb[] = "..a9" ".aa9" ".cc." ".99." ".99." ".99." ".9.9" "..9.";
static const char a_build[] = ".aa." ".aa." ".cc." ".99c" ".999" ".99." ".99." "9..9";
static const char a_splat[] = "...." "...." "...." "...." "...." "9aa9" "9cc9" "9999";
static const char a_boom[]  = "e.e." ".fe." "ecfe" "fefe" ".ef." "ecfe" "e.e." ".c.c";

static unsigned char sprpix[NSPR][32];
static fx_sprite *spr[NSPR];

static int mkspr(int id, const char *art, int flip)
{
    int x, y;
    char ch;

    for (y = 0; y < 8; y++)
        for (x = 0; x < 4; x++) {
            ch = art[y * 4 + (flip ? 3 - x : x)];
            sprpix[id][y * 4 + x] = (unsigned char)
                (ch == '.' ? 0 : ch <= '9' ? ch - '0' : ch - 'a' + 10);
        }
    spr[id] = fx_compile(sprpix[id], 4, 8, 0);
    return spr[id] != NULL;
}

static int make_sprites(void)
{
    return mkspr(SP_WALK1R, a_walk1, 0) && mkspr(SP_WALK2R, a_walk2, 0) &&
           mkspr(SP_WALK1L, a_walk1, 1) && mkspr(SP_WALK2L, a_walk2, 1) &&
           mkspr(SP_FALL, a_fall, 0) && mkspr(SP_FLOAT, a_float, 0) &&
           mkspr(SP_BLOCK, a_block, 0) && mkspr(SP_DIG1, a_dig1, 0) &&
           mkspr(SP_DIG2, a_dig2, 0) && mkspr(SP_BASHR, a_bash, 0) &&
           mkspr(SP_BASHL, a_bash, 1) && mkspr(SP_CLIMBR, a_climb, 0) &&
           mkspr(SP_CLIMBL, a_climb, 1) && mkspr(SP_BUILDR, a_build, 0) &&
           mkspr(SP_BUILDL, a_build, 1) && mkspr(SP_SPLAT, a_splat, 0) &&
           mkspr(SP_BOOM, a_boom, 0);
}

/* ---- Sound ------------------------------------------------------------- */

typedef struct { unsigned hz; unsigned char d; } Note;

static const Note s_go[]     = { {523,3},{659,3},{784,6},{0,0} };
static const Note s_pick[]   = { {1568,1},{0,0} };
static const Note s_assign[] = { {1047,1},{1568,2},{0,0} };
static const Note s_no[]     = { {220,2},{0,0} };
static const Note s_exit[]   = { {1319,1},{1760,1},{2093,2},{0,0} };
static const Note s_splat[]  = { {200,2},{150,2},{100,4},{0,0} };
static const Note s_tick[]   = { {880,1},{0,0} };
static const Note s_win[]    = { {523,4},{659,4},{784,4},{1047,8},{784,4},{1047,12},{0,0} };
static const Note s_lose[]   = { {392,6},{330,6},{262,6},{196,12},{0,0} };
static const Note m_title0[] = {
    {659,4},{659,4},{784,4},{659,4},{523,4},{587,4},{659,8},
    {698,4},{698,4},{880,4},{698,4},{587,4},{659,4},{698,8},
    {784,4},{659,4},{523,4},{659,4},{587,4},{494,4},{392,8},
    {523,4},{659,4},{784,4},{1047,4},{1047,8},{0,8},{0,0} };
static const Note m_title1[] = {
    {131,8},{196,8},{131,8},{196,8},{175,8},{220,8},{175,8},{220,8},
    {131,8},{196,8},{196,8},{247,8},{131,8},{196,8},{131,16},{0,0} };

static struct { const Note *p; int left; unsigned hz; int gap; } vc[3];
static int noise_t;

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
    snd_noise(0, 0);
    noise_t = 0;
}

static void snd_tick(void)
{
    int v;

    if (noise_t && --noise_t == 0)
        snd_noise(0, 0);
    for (v = 0; v < 3; v++) {
        if (!vc[v].p)
            continue;
        if (vc[v].left > 0) {
            if (--vc[v].left == 1 && vc[v].gap)
                snd_tone(v, vc[v].hz, 0);
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

static void boom_sound(void)
{
    snd_noise(4, 15);
    noise_t = 8;
}

/* ---- State ------------------------------------------------------------- */

static unsigned bg;
static unsigned char far *terr;
static const Level *lv;
static int level, gstate, ticks, quit, testmode;
static Lem lem[MAXLEM];
static int released, saved, dead, out, timeleft, rate, nextrel, won;
static int skills[NSKILL], sel;
static int cx, cy, follow, paused, nuke_arm, overtimer;
static int ex, ey, xx, xfy;
static int hud1_dirty;
static unsigned char kprev[128];

static const char *scr;             /* /TEST script */
static const char *autosk;          /* "ALL=CF": skills every lemming gets */
static int scr_tick, scr_lem, scr_skill;

/* ---- Background and dirty rectangles ----------------------------------- */

#define MAXDQ 24
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

static unsigned char far *bgp(int x, int y)
{
    return (unsigned char far *)
        MK_FP(bg, ((y & 1) << 13) + (y >> 1) * 80 + (x >> 1));
}

static void bg_pset(int x, int y, int c)
{
    unsigned char far *p = bgp(x, y);

    if (x & 1)
        *p = (unsigned char)((*p & 0xF0) | c);
    else
        *p = (unsigned char)((*p & 0x0F) | (c << 4));
}

static int bg_pget(int x, int y)
{
    unsigned char far *p = bgp(x, y);

    return (x & 1) ? (*p & 15) : (*p >> 4);
}

/* ---- Terrain ----------------------------------------------------------- */

/* types: 0 air, 1 dirt, 2 steel, 3 brick */
static int tget(int x, int y)
{
    if (x < 0 || x >= PW)
        return 1;               /* the screen edge is a wall */
    if (y < 0 || y >= PH)
        return 0;
    return terr[y * PW + x];
}

#define solid(x, y) (tget(x, y) != 0)

static void tset(int x, int y, int t)
{
    int c;

    if (x < 0 || x >= PW || y < 0 || y >= PH)
        return;
    terr[y * PW + x] = (unsigned char)t;
    switch (t) {
    case 0:  c = 0; break;
    case 2:  c = (y & 3) == 0 ? 7 : 8; break;
    case 3:  c = (x & 1) ? 12 : 4; break;
    default: c = (y & 3) == 0 && ((x + ((y >> 1) & 2)) & 3) == 0
                 ? lv->c2 : lv->c1; break;
    }
    bg_pset(x, y, c);
}

static void fill_terrain(int x0, int y0, int x1, int y1, int t)
{
    int x, y;

    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            tset(x, y, t);
}

/* Clears non-steel terrain in the box; returns 1 if steel was in it. */
static int clear_rect(int x0, int y0, int x1, int y1)
{
    int x, y, t, steel = 0, any = 0;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= PW) x1 = PW - 1;
    if (y1 >= PH) y1 = PH - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            t = terr[y * PW + x];
            if (t == 2)
                steel = 1;
            else if (t) {
                tset(x, y, 0);
                any = 1;
            }
        }
    if (any)
        add_dirty(x0, y0, x1, y1);
    return steel;
}

static int has_type(int x0, int y0, int x1, int y1, int type)
{
    int x, y;

    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (tget(x, y) == type)
                return 1;
    return 0;
}

static int has_solid(int x0, int y0, int x1, int y1)
{
    int x, y;

    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (tget(x, y))
                return 1;
    return 0;
}

/* The blast: an ellipse 9 wide, 13 tall, mostly above the feet. */
static void blast(int x, int y)
{
    int dx, dy, t;

    for (dy = -6; dy <= 6; dy++)
        for (dx = -4; dx <= 4; dx++)
            if (9 * dx * dx + 4 * dy * dy <= 144) {
                t = tget(x + dx, y - 3 + dy);
                if (t && t != 2 && x + dx >= 0 && x + dx < PW)
                    tset(x + dx, y - 3 + dy, 0);
            }
    add_dirty(x - 4, y - 9, x + 4, y + 3);
}

/* ---- HUD --------------------------------------------------------------- */

static void hud_part(int x, int y, int w, const char *s, int col)
{
    fx_target(bg);
    fx_fill_rect(x, y, w, 8, 0);
    fx_text(x, y, s, col);
    fx_target(0);
    add_dirty(x, y, x + w - 1, y + 7);
}

static void hud_status(void)
{
    char b[24];

    if (nuke_arm)
        strcpy(b, "NUKE? PRESS N AGAIN");
    else
        sprintf(b, "OUT %2d IN %3d%% %d:%02d", out, saved * 100 / lv->count,
                timeleft / 60, timeleft % 60);
    hud_part(0, 184, 160, b, nuke_arm ? 12 : 15);
    hud1_dirty = 0;
}

static void hud_skills(void)
{
    char b[4];
    int i, c;

    fx_target(bg);
    fx_fill_rect(0, 192, 160, 8, 0);
    for (i = 0; i < NSKILL; i++) {
        b[0] = skchar[i];
        b[1] = (char)('0' + (skills[i] > 9 ? 9 : skills[i]));
        b[2] = 0;
        c = i == sel ? 14 : skills[i] ? 15 : 8;
        fx_text(i * 16, 192, b, c);
    }
    sprintf(b, "R%02d", rate);
    fx_text(136, 192, b, 11);
    fx_target(0);
    add_dirty(0, 192, 159, 199);
}

/* ---- Level setup ------------------------------------------------------- */

static void draw_door(void)
{
    fx_target(bg);
    /* entrance: a trapdoor */
    fx_fill_rect(ex, ey, 10, 1, FX_SOLID2(7));
    fx_fill_rect(ex, ey + 1, 10, 2, FX_SOLID2(6));
    fx_fill_rect(ex - 1, ey - 1, 12, 1, FX_SOLID2(8));
    /* exit: an arch with a lamp */
    fx_fill_rect(xx, xfy - 12, 10, 13, FX_SOLID2(13));
    fx_fill_rect(xx + 2, xfy - 10, 6, 11, FX_SOLID2(0));
    fx_fill_rect(xx + 3, xfy - 10, 4, 1, FX_SOLID2(14));
    fx_fill_rect(xx + 4, xfy - 14, 2, 2, FX_SOLID2(14));
    fx_target(0);
}

static void load_level(int n)
{
    const TCmd *p;
    int i;

    lv = &levels[n];
    _fmemset(terr, 0, PW * PH);
    fx_target(bg);
    fx_clear(0);
    fx_target(0);
    ex = 10; ey = 20; xx = 140; xfy = 180;
    for (p = lv->cmd; p->op; p++)
        switch (p->op) {
        case 'D': fill_terrain(p->x, p->y, p->x + p->w - 1, p->y + p->h - 1, 1); break;
        case 'S': fill_terrain(p->x, p->y, p->x + p->w - 1, p->y + p->h - 1, 2); break;
        case 'A': fill_terrain(p->x, p->y, p->x + p->w - 1, p->y + p->h - 1, 0); break;
        case 'E': ex = p->x; ey = p->y; break;
        case 'X': xx = p->x; xfy = p->y; break;
        }
    draw_door();
    for (i = 0; i < MAXLEM; i++)
        lem[i].state = L_NONE;
    for (i = 0; i < NSKILL; i++)
        skills[i] = lv->skills[i];
    released = saved = dead = out = 0;
    timeleft = lv->mins * 60 + lv->secs;
    rate = lv->rate;
    nextrel = ticks + 20;
    sel = 0;
    while (sel < NSKILL - 1 && skills[sel] == 0)
        sel++;
    cx = ex + 5; cy = ey + 20;
    follow = -1;
    paused = nuke_arm = 0;
    overtimer = 0;
    hud_status();
    hud_skills();
    ndq = 0;
    fullref = 2;
}

/* ---- Lemming logic ----------------------------------------------------- */

static void die(Lem *l)
{
    l->state = L_NONE;
    dead++;
    out--;
    hud1_dirty = 1;
}

static void explode(Lem *l)
{
    blast(l->x, l->y);
    l->state = L_BOOM;
    l->work = 4;
    boom_sound();
}

static int blocked(Lem *l)
{
    int i, dx;
    Lem *b;

    for (i = 0; i < MAXLEM; i++) {
        b = &lem[i];
        if (b == l || b->state != L_BLOCK)
            continue;
        dx = (b->x - l->x) * l->dir;
        if (dx >= 1 && dx <= 3 && abs(b->y - l->y) <= 6)
            return 1;
    }
    return 0;
}

static void start_fall(Lem *l)
{
    l->state = L_FALL;
    l->fallen = 0;
}

static void step_lem(Lem *l)
{
    int nx, k, i, spd;

    if (l->bomb && l->state != L_BOOM && l->state != L_SPLAT) {
        if (--l->bomb == 0) {
            explode(l);
            return;
        }
    }
    switch (l->state) {
    case L_WALK:
        l->anim++;
        if (blocked(l))
            l->dir = (signed char)-l->dir;
        nx = l->x + l->dir;
        if (solid(nx, l->y)) {
            for (k = 1; k <= 4; k++)
                if (!solid(nx, l->y - k))
                    break;
            if (k <= 4 && nx >= 0 && nx < PW) {
                l->x = nx;
                l->y -= k;
            } else if (l->climber && nx >= 0 && nx < PW)
                l->state = L_CLIMB;
            else
                l->dir = (signed char)-l->dir;
        } else {
            l->x = nx;
            for (k = 0; k <= 3; k++)
                if (solid(l->x, l->y + 1 + k))
                    break;
            if (k <= 3)
                l->y += k;
            else
                start_fall(l);
        }
        if (l->state == L_WALK && abs(l->x - (xx + 5)) <= 1 &&
            l->y <= xfy && l->y >= xfy - 4) {
            l->state = L_NONE;
            saved++;
            out--;
            hud1_dirty = 1;
            sfx(1, s_exit);
        }
        break;

    case L_FALL:
        spd = (l->floater && l->fallen >= 8) ? 2 : 3;
        for (i = 0; i < spd; i++) {
            if (solid(l->x, l->y + 1)) {
                if (l->fallen > MAXFALL && !l->floater) {
                    l->state = L_SPLAT;
                    l->work = 10;
                    sfx(2, s_splat);
                } else
                    l->state = L_WALK;
                break;
            }
            l->y++;
            l->fallen++;
            if (l->y >= PH) {
                die(l);
                break;
            }
        }
        break;

    case L_CLIMB:
        if (solid(l->x, l->y - 8) || l->y <= 0) {
            l->dir = (signed char)-l->dir;
            start_fall(l);
            break;
        }
        l->y--;
        if (!solid(l->x + l->dir, l->y)) {
            l->x += l->dir;
            l->state = L_WALK;
        }
        break;

    case L_BLOCK:
        if (!solid(l->x, l->y + 1))
            start_fall(l);
        break;

    case L_BUILD:
        if (++l->work < 8)
            break;
        l->work = 0;
        if (l->bricks == 0) {
            l->state = L_WALK;
            break;
        }
        nx = l->x + 2 * l->dir;
        if (nx < 1 || nx >= PW - 1 || solid(l->x + l->dir, l->y - 1) ||
            solid(nx, l->y - 1) || solid(nx, l->y - 8)) {
            l->dir = (signed char)-l->dir;
            l->state = L_WALK;
            break;
        }
        for (k = 1; k <= 4; k++)
            if (!tget(l->x + k * l->dir, l->y))
                tset(l->x + k * l->dir, l->y, 3);
        add_dirty(l->x - 4, l->y, l->x + 4, l->y);
        l->bricks--;
        l->x = nx;
        l->y--;
        sfx(2, s_tick);
        break;

    case L_BASH:
        l->anim++;
        if (!solid(l->x, l->y + 1)) {
            start_fall(l);
            break;
        }
        nx = l->x + l->dir;
        if (nx < 0 || nx >= PW) {
            l->state = L_WALK;
            break;
        }
        if (l->dir > 0 ? has_type(nx, l->y - 8, nx + 1, l->y, 2)
                       : has_type(nx - 1, l->y - 8, nx, l->y, 2)) {
            l->state = L_WALK;      /* steel: give up */
            sfx(2, s_no);
            break;
        }
        if (l->dir > 0 ? !has_solid(nx, l->y - 6, nx + 4, l->y)
                       : !has_solid(nx - 4, l->y - 6, nx, l->y)) {
            l->state = L_WALK;      /* nothing left to bash */
            break;
        }
        if (l->dir > 0)
            clear_rect(nx, l->y - 8, nx + 3, l->y);
        else
            clear_rect(nx - 3, l->y - 8, nx, l->y);
        l->x = nx;
        break;

    case L_MINE:
        l->anim++;
        if (++l->work < 2)
            break;
        l->work = 0;
        nx = l->x + 2 * l->dir;
        if (nx < 1 || nx >= PW - 1) {
            l->state = L_WALK;
            break;
        }
        if (l->dir > 0 ? has_type(l->x + 1, l->y - 7, l->x + 4, l->y + 1, 2)
                       : has_type(l->x - 4, l->y - 7, l->x - 1, l->y + 1, 2)) {
            l->state = L_WALK;
            sfx(2, s_no);
            break;
        }
        if (l->dir > 0)
            clear_rect(l->x + 1, l->y - 7, l->x + 4, l->y + 1);
        else
            clear_rect(l->x - 4, l->y - 7, l->x - 1, l->y + 1);
        l->x = nx;
        l->y++;
        if (l->y >= PH) {
            die(l);
            break;
        }
        if (!solid(l->x, l->y + 1))
            start_fall(l);
        break;

    case L_DIG:
        l->anim++;
        if (++l->work < 2)
            break;
        l->work = 0;
        if (has_type(l->x - 2, l->y + 1, l->x + 2, l->y + 1, 2)) {
            l->state = L_WALK;
            sfx(2, s_no);
            break;
        }
        clear_rect(l->x - 2, l->y + 1, l->x + 2, l->y + 1);
        l->y++;
        if (l->y >= PH) {
            die(l);
            break;
        }
        if (!solid(l->x, l->y + 1))
            start_fall(l);
        break;

    case L_SPLAT:
    case L_BOOM:
        if (--l->work <= 0)
            die(l);
        break;
    }
}

static int can_assign(Lem *l, int s)
{
    int st = l->state;

    if (st == L_NONE || st == L_SPLAT || st == L_BOOM)
        return 0;
    switch (s) {
    case SK_CLIMB: return !l->climber;
    case SK_FLOAT: return !l->floater;
    case SK_BOMB:  return l->bomb == 0;
    case SK_BLOCK: return st == L_WALK;
    case SK_BUILD:              /* a builder may be given more bricks */
        return st == L_WALK || (st >= L_BUILD && st <= L_DIG);
    default:
        if (st == L_WALK)
            return 1;
        return st >= L_BUILD && st <= L_DIG && st != L_BUILD + (s - SK_BUILD);
    }
}

static int assign(Lem *l, int s)
{
    if (skills[s] <= 0 || !can_assign(l, s))
        return 0;
    switch (s) {
    case SK_CLIMB: l->climber = 1; break;
    case SK_FLOAT: l->floater = 1; break;
    case SK_BOMB:  l->bomb = 50; break;
    case SK_BLOCK: l->state = L_BLOCK; break;
    case SK_BUILD: l->state = L_BUILD; l->bricks = 12; l->work = 0; break;
    case SK_BASH:  l->state = L_BASH; break;
    case SK_MINE:  l->state = L_MINE; l->work = 0; break;
    case SK_DIG:   l->state = L_DIG; l->work = 0; break;
    }
    skills[s]--;
    hud_skills();
    sfx(1, s_assign);
    return 1;
}

static void spawn(void)
{
    Lem *l = &lem[released];

    memset(l, 0, sizeof *l);
    l->x = ex + 5;
    l->y = ey + 3;
    l->dir = 1;
    l->state = L_FALL;
    released++;
    out++;
    hud1_dirty = 1;
    if (testmode && autosk) {
        const char *q;
        for (q = autosk; *q && *q != ','; q++)
            if (strchr(skchar, *q))
                assign(l, (int)(strchr(skchar, *q) - skchar));
    }
}

static int under_cursor(Lem *l)
{
    return l->state != L_NONE && abs(l->x - cx) <= 4 && abs(l->y - 4 - cy) <= 6;
}

static void nuke(void)
{
    int i, n = 0;

    for (i = 0; i < MAXLEM; i++)
        if (lem[i].state != L_NONE && lem[i].bomb == 0)
            lem[i].bomb = 20 + (n++) * 3;
    dead += lv->count - released;
    released = lv->count;
    hud1_dirty = 1;
}

/* ---- Input ------------------------------------------------------------- */

static int kedge(int k)
{
    return key_down[k] && !kprev[k];
}

static void keys_done(void)
{
    int i;

    for (i = 0; i < 128; i++)
        kprev[i] = key_down[i];
}

static void script_next(void)
{
    const char *p = scr;

    scr_tick = -1;
    if (!p || !*p)
        return;
    if (strncmp(p, "ALL=", 4) == 0) {
        autosk = p + 4;
        while (*p && *p != ',') p++;
        if (*p) p++;
        scr = p;
        script_next();
        return;
    }
    scr_tick = atoi(p);
    while (*p && *p != ':') p++;
    if (*p) p++;
    scr_lem = atoi(p);
    while (*p && *p != ':') p++;
    if (*p) p++;
    scr_skill = (int)(strchr(skchar, *p) ? strchr(skchar, *p) - skchar : 0);
    while (*p && *p != ',') p++;
    if (*p) p++;
    scr = p;
}

static void play_input(void)
{
    int i, j, d;
    Lem *l;

    if (testmode) {
        while (scr_tick >= 0 && ticks >= scr_tick) {
            l = &lem[scr_lem];
            if (assign(l, scr_skill))
                dbg_log("t=%d lem %d gets %s at x=%d y=%d", ticks, scr_lem,
                        skname[scr_skill], l->x, l->y);
            else
                dbg_log("t=%d lem %d CANNOT get %s (st=%d x=%d y=%d left=%d)",
                        ticks, scr_lem, skname[scr_skill], l->state,
                        l->x, l->y, skills[scr_skill]);
            script_next();
        }
        return;
    }
    d = 0;
    if (key_down[KEY_LEFT] || key_down[KEY_A]) { cx -= 2; d = 1; }
    if (key_down[KEY_RIGHT] || key_down[KEY_D]) { cx += 2; d = 1; }
    if (key_down[KEY_UP] || key_down[KEY_W]) { cy -= 2; d = 1; }
    if (key_down[KEY_DOWN] || key_down[KEY_S]) { cy += 2; d = 1; }
    if (d)
        follow = -1;
    if (kedge(KEY_TAB)) {
        for (j = 1; j <= MAXLEM; j++) {
            i = (follow + j) % MAXLEM;
            if (lem[i].state != L_NONE) {
                follow = i;
                break;
            }
        }
        sfx(2, s_pick);
    }
    if (follow >= 0) {
        if (lem[follow].state == L_NONE)
            follow = -1;
        else {
            cx = lem[follow].x;
            cy = lem[follow].y - 4;
        }
    }
    if (cx < 2) cx = 2;
    if (cx > PW - 3) cx = PW - 3;
    if (cy < 4) cy = 4;
    if (cy > PH - 5) cy = PH - 5;
    for (i = 0; i < NSKILL; i++)
        if (kedge(KEY_1 + i) && sel != i) {
            sel = i;
            hud_skills();
            sfx(2, s_pick);
        }
    if (kedge(KEY_SPACE) || kedge(KEY_ENTER)) {
        for (i = 0; i < MAXLEM; i++)
            if (under_cursor(&lem[i]) && can_assign(&lem[i], sel))
                break;
        if (i == MAXLEM || !assign(&lem[i], sel))
            sfx(2, s_no);
    }
    if (key_down[KEY_PLUS] && (ticks & 1) && rate < 99) {
        rate++;
        hud_skills();
    }
    if (key_down[KEY_MINUS] && (ticks & 1) && rate > lv->rate) {
        rate--;
        hud_skills();
    }
    if (kedge(KEY_N)) {
        if (nuke_arm) {
            nuke();
            nuke_arm = 0;
        } else
            nuke_arm = 40;
        hud1_dirty = 1;
    }
}

/* ---- Game flow --------------------------------------------------------- */

static void show_intro(void)
{
    char b[24];

    gstate = G_INTRO;
    lv = &levels[level];
    fx_target(bg);
    fx_clear(0);
    sprintf(b, "LEVEL %d", level + 1);
    fx_text(80 - strlen(b) * 4, 30, b, 11);
    fx_text(80 - strlen(lv->name) * 4, 46, lv->name, 14);
    sprintf(b, "%d LEMMINGS", lv->count);
    fx_text(80 - strlen(b) * 4, 76, b, 15);
    sprintf(b, "SAVE %d%%", lv->need * 100 / lv->count);
    fx_text(80 - strlen(b) * 4, 90, b, 15);
    sprintf(b, "TIME %d:%02d", lv->mins, lv->secs);
    fx_text(80 - strlen(b) * 4, 104, b, 15);
    fx_text(16, 140, "SPACE: LET'S GO!", 10);
    fx_target(0);
    fullref = 2;
    ndq = 0;
}

static void start_play(void)
{
    load_level(level);
    gstate = G_PLAY;
    sfx(0, s_go);
}

static void show_result(void)
{
    char b[24];
    int pct = saved * 100 / lv->count, need = lv->need * 100 / lv->count;

    won = saved >= lv->need;
    gstate = G_RESULT;
    snd_quiet();
    fx_target(bg);
    fx_clear(0);
    fx_text(80 - strlen(lv->name) * 4, 30, lv->name, 14);
    sprintf(b, "YOU SAVED  %3d%%", pct);
    fx_text(20, 60, b, 15);
    sprintf(b, "YOU NEEDED %3d%%", need);
    fx_text(20, 74, b, 15);
    if (won)
        fx_text(80 - 5 * 4, 104, "WELL DONE!", 10);
    else
        fx_text(80 - 5 * 4, 104, "TRY AGAIN!", 12);
    fx_text(16, 140, "SPACE: CONTINUE", 7);
    fx_target(0);
    fullref = 2;
    ndq = 0;
    sfx(0, won ? s_win : s_lose);
}

static void show_won(void)
{
    gstate = G_WON;
    fx_target(bg);
    fx_clear(0);
    fx_text(80 - 9 * 4, 50, "ALL LEVELS DONE!", 14);
    fx_text(80 - 8 * 4, 70, "YOU ARE A TRUE", 15);
    fx_text(80 - 9 * 4, 84, "LEMMING KEEPER", 15);
    fx_text(16, 140, "SPACE: TITLE", 7);
    fx_target(0);
    fullref = 2;
    ndq = 0;
}

/* 8x8 text enlarged to 16x32 per letter, drawn into the background. */
static void big_text(int x, int y, const char *s)
{
    static const unsigned char grad[8] = { 10, 10, 10, 2, 2, 2, 8, 8 };
    unsigned char bm[8][20];
    int n = strlen(s), i, j;

    memset(bm, 0, sizeof bm);
    fx_text(0, 0, s, 15);
    for (j = 0; j < 8; j++)
        for (i = 0; i < n * 8; i++)
            if (bg_pget(i, j))
                bm[j][i >> 3] |= (unsigned char)(0x80 >> (i & 7));
    fx_fill_rect(0, 0, n * 8, 8, 0);
    for (j = 0; j < 8; j++)
        for (i = 0; i < n * 8; i++)
            if (bm[j][i >> 3] & (0x80 >> (i & 7)))
                fx_fill_rect(x + i * 2, y + j * 4, 2, 4, FX_SOLID2(grad[j]));
}

static int tx[4];

static void to_title(void)
{
    int i;

    snd_quiet();
    gstate = G_TITLE;
    lv = &levels[0];
    fx_target(bg);
    fx_clear(0);
    big_text(16, 10, "LEMMINGS");
    fx_text(20, 48, "- J U N I O R -", 11);
    fx_text(0, 68, "ARROWS: MOVE CURSOR", 7);
    fx_text(0, 78, "TAB: NEXT LEMMING", 7);
    fx_text(0, 88, "1-8: PICK A SKILL", 7);
    fx_text(0, 98, "SPACE: GIVE IT", 7);
    fx_text(0, 108, "+ -: RATE  P: PAUSE", 7);
    fx_text(0, 118, "N N: NUKE  ESC: QUIT", 7);
    fx_text(0, 134, "GET THEM TO THE EXIT", 14);
    fx_fill_rect(0, 160, 160, 8, FX_DITH(6, 4));
    fx_target(0);
    fullref = 2;
    ndq = 0;
    for (i = 0; i < 4; i++)
        tx[i] = i * 30;
}

static void tick(void)
{
    int i;

    snd_tick();
    if (!testmode && kedge(KEY_ESC)) {
        if (gstate == G_TITLE)
            quit = 1;
        else
            to_title();
        ticks++;
        return;
    }
    switch (gstate) {
    case G_TITLE:
        if (!vc[0].p) {
            sfx(0, m_title0);
            sfx(1, m_title1);
        }
        if (ticks & 1)
            for (i = 0; i < 4; i++)
                if (++tx[i] > 170)
                    tx[i] = -10;
        if (kedge(KEY_SPACE) || kedge(KEY_ENTER)) {
            level = 0;
            show_intro();
        }
        break;
    case G_INTRO:
        if (kedge(KEY_SPACE) || kedge(KEY_ENTER))
            start_play();
        break;
    case G_PLAY:
        if (kedge(KEY_P)) {
            paused = !paused;
            if (paused)
                snd_quiet();
        }
        if (paused)
            break;
        play_input();
        if (nuke_arm && --nuke_arm == 0)
            hud1_dirty = 1;
        if (released < lv->count && (long)(ticks - nextrel) >= 0) {
            spawn();
            nextrel = ticks + 4 + (99 - rate) / 2;
        }
        if (ticks & 1)
            for (i = 0; i < MAXLEM; i++)
                if (lem[i].state != L_NONE)
                    step_lem(&lem[i]);
        if (ticks % 20 == 0 && timeleft > 0) {
            timeleft--;
            hud1_dirty = 1;
        }
        if (hud1_dirty)
            hud_status();
        if (overtimer) {
            if (--overtimer == 0)
                show_result();
        } else if ((released == lv->count && out == 0) || timeleft <= 0)
            overtimer = 30;
        break;
    case G_RESULT:
        if (kedge(KEY_SPACE) || kedge(KEY_ENTER)) {
            if (won && ++level == NLEV)
                show_won();
            else
                show_intro();
        }
        break;
    case G_WON:
        if (kedge(KEY_SPACE) || kedge(KEY_ENTER))
            to_title();
        break;
    }
    ticks++;
}

/* ---- Drawing ----------------------------------------------------------- */

static void draw_lem(Lem *l)
{
    int s;
    char b[2];

    switch (l->state) {
    case L_WALK:
        s = (l->dir > 0 ? SP_WALK1R : SP_WALK1L) + ((l->anim & 2) ? 1 : 0);
        break;
    case L_FALL:
        s = (l->floater && l->fallen >= 8) ? SP_FLOAT : SP_FALL;
        break;
    case L_CLIMB: s = l->dir > 0 ? SP_CLIMBR : SP_CLIMBL; break;
    case L_BLOCK: s = SP_BLOCK; break;
    case L_BUILD: s = l->dir > 0 ? SP_BUILDR : SP_BUILDL; break;
    case L_BASH:
        s = (l->anim & 2) ? (l->dir > 0 ? SP_BASHR : SP_BASHL)
                          : (l->dir > 0 ? SP_WALK2R : SP_WALK2L);
        break;
    case L_MINE:
        s = (l->anim & 2) ? (l->dir > 0 ? SP_BASHR : SP_BASHL) : SP_DIG1;
        break;
    case L_DIG:   s = (l->anim & 2) ? SP_DIG1 : SP_DIG2; break;
    case L_SPLAT: s = SP_SPLAT; break;
    default:      s = SP_BOOM; break;
    }
    if (l->y - 7 >= 0 && l->y < PH)
        fx_sprite_draw(spr[s], l->x - 1, l->y - 7);
    if (l->bomb && l->y - 16 >= 0 && l->x >= 3 && l->x <= PW - 6) {
        b[0] = (char)('0' + (l->bomb + 9) / 10);
        b[1] = 0;
        fx_text(l->x - 3, l->y - 16, b, 15);
    }
}

static void draw_play(void)
{
    int i, c = 15;

    for (i = 0; i < MAXLEM; i++)
        if (lem[i].state != L_NONE) {
            draw_lem(&lem[i]);
            if (under_cursor(&lem[i]))
                c = 10;
        }
    if (follow >= 0)
        c = 14;
    fx_fill_rect(cx - 4, cy - 5, 9, 1, FX_SOLID2(c));
    fx_fill_rect(cx - 4, cy + 5, 9, 1, FX_SOLID2(c));
    fx_fill_rect(cx - 4, cy - 4, 1, 9, FX_SOLID2(c));
    fx_fill_rect(cx + 4, cy - 4, 1, 9, FX_SOLID2(c));
    if (paused) {
        fx_fill_rect(54, 86, 52, 12, FX_SOLID2(0));
        fx_text(56, 88, "PAUSED", 15);
    }
}

static void draw_title(void)
{
    int i;

    for (i = 0; i < 4; i++)
        fx_sprite_draw(spr[(tx[i] & 2) ? SP_WALK1R : SP_WALK2R], tx[i], 152);
    if (ticks & 16)
        fx_text(36, 180, "PRESS SPACE", 15);
}

static void frame(void)
{
    fx_begin();
    fx_restore(bg);
    flush_dirty();
    if (gstate == G_TITLE)
        draw_title();
    else if (gstate == G_PLAY)
        draw_play();
    fx_flip();
}

/* ---- Main -------------------------------------------------------------- */

static void run_test(int tlevel)
{
    int t, i;

    dbg_log("pages=%d irq=%d", fx_pages, fx_irq);
    if (tlevel < 1 || tlevel > NLEV) {
        to_title();
        for (t = 0; t < 40; t++) {
            tick();
            frame();
        }
    } else {
        level = tlevel - 1;
        start_play();
        script_next();
        for (t = 0; gstate == G_PLAY && t < (scr ? 8000 : 300); t++) {
            tick();
            if ((t & 3) == 0)
                frame();
            if (t % 100 == 0) {
                char line[160], *p = line;
                p += sprintf(p, "t=%d out=%d in=%d dead=%d", t, out, saved, dead);
                for (i = 0; i < MAXLEM && p - line < 130; i++)
                    if (lem[i].state != L_NONE)
                        p += sprintf(p, " %d:%d,%d,%d", i, lem[i].x, lem[i].y,
                                     lem[i].state);
                dbg_log("%s", line);
            }
            if (gstate != G_PLAY)
                break;
        }
        dbg_log("end t=%d saved=%d need=%d dead=%d out=%d time=%d", t, saved,
                lv->need, dead, out, timeleft);
        if (gstate != G_PLAY) {
            gstate = G_PLAY;        /* one last look at the field */
            frame();
            gstate = G_RESULT;
        }
    }
    frame();
    fx_begin();
    fx_screenshot("SHOT.PPM");
}

int main(int argc, char **argv)
{
    unsigned long next, now;
    int steps, tlevel = 1, r;

    if (argc > 1 && stricmp(argv[1], "/TEST") == 0) {
        testmode = 1;
        if (argc > 2) tlevel = atoi(argv[2]);
        if (argc > 3) scr = argv[3];
        remove("LOG.TXT");
    }
    r = fx_init(testmode ? FX_TEAR : 0);
    if (r) {
        r = fx_init(FX_BUFFER | FX_NOIRQ);
        dbg_log("fx_init retry: %d (gfx_err %d)", r, gfx_err);
    }
    if (r) {
        printf("Cannot start graphics (error %d)\n", gfx_err);
        return 1;
    }
    key_init();
    tmr_init();
    snd_init();
    bg = fx_alloc_buffer();
    terr = (unsigned char far *)_fmalloc(PW * PH);
    if (!bg || !terr || !make_sprites()) {
        gfx_done();
        printf("Not enough memory\n");
        dbg_log("out of memory: bg=%u terr=%p", bg, terr);
        return 1;
    }
    fx_track(1);

    if (testmode)
        run_test(tlevel);
    else {
        to_title();
        next = tmr_ticks();
        while (!quit) {
            now = tmr_ticks();
            steps = 0;
            while ((long)(now - next) >= 0 && steps < 4) {
                tick();
                keys_done();
                next += TICK;
                steps++;
            }
            if ((long)(now - next) > 0)
                next = now;
            if (steps)
                frame();
        }
    }
    snd_quiet();
    gfx_done();
    return 0;
}
