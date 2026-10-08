/* SWORDS.C - En garde! Two stick figures duel with swords on the PCjr.
 *
 * 160x200, 16 colors, 60 frames a second on the JRGFX fast path: the
 * scene is drawn once into a background buffer, and each frame only the
 * bytes the fighters touched last time are restored and redrawn.
 *
 * Moves: walk, jump, crouch (low guard), block (high guard), overhead
 * slash and low thrust. Block stops a slash, crouch stops a thrust, a
 * jump clears a thrust, and two blades meeting in the air clash. Each
 * fighter is run by the AI until you press 1 (left) or 2 (right).
 *
 * SWORDS /TEST [ticks] runs the AI duel headless, logs it to LOG.TXT,
 * saves SHOT*.PPM and exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jrgfx.h"

#define GY     176                  /* the ground line */
#define XMIN   (8 << 2)
#define XMAX   (151 << 2)
#define TICK   (TMR_HZ / 60)

enum { ST_IDLE, ST_WALK, ST_JUMP, ST_CROUCH, ST_BLOCK, ST_SLASH, ST_THRUST,
       ST_HIT, ST_STAGGER, ST_DEAD, ST_WIN };
enum { R_READY, R_FIGHT, R_KO };

#define SL_WIND 8
#define SL_ACT  6
#define SL_REC  10
#define TH_WIND 5
#define TH_ACT  5
#define TH_REC  12
#define HIT_T   14
#define STAG_T  22
#define JUMP_V  13

#define KEY_1 0x02
#define KEY_2 0x03
#define KEY_F 0x21
#define KEY_G 0x22
#define KEY_H 0x23
#define KEY_COMMA  0x33
#define KEY_PERIOD 0x34
#define KEY_SLASH  0x35

typedef struct { unsigned char l, r, up, dn, slash, thrust, block; } In;

typedef struct {
    int x, y, vx, vy;               /* 1/4 pixel; y is height above ground */
    int face, state, t, hp, ai, think, aggr, skill, col, hitdone, wins;
    int atkid, reacted, airatk;
    In in, pin;
} F;

/* A pose: joints relative to the point between the feet, facing right,
 * y up. hd head, nk neck, hp hip, ka/fa back knee and foot, kb/fb front
 * knee and foot, el/hn sword arm, tp sword tip, eo/ho other arm. */
typedef struct {
    signed char hd[2], nk[2], hp[2], ka[2], fa[2], kb[2], fb[2],
                el[2], hn[2], tp[2], eo[2], ho[2];
} Pose;

enum { P_IDLE1, P_IDLE2, P_WALK1, P_WALK2, P_WALK3, P_JUMP, P_CROUCH,
       P_BLOCK, P_SL1, P_SL2, P_SL3, P_TH1, P_TH2, P_TH3, P_HIT, P_STAG,
       P_DEAD, P_WIN, NPOSE };

static const Pose poses[NPOSE] = {
 /*        head    neck    hip     kneeA   footA   kneeB   footB   elbow   hand    tip     elbowO  handO */
 /*IDLE1*/{{0,29}, {0,24}, {0,14}, {-2,7}, {-3,0}, {2,7},  {3,0},  {3,19}, {5,17}, {9,25}, {-2,19},{-1,15}},
 /*IDLE2*/{{0,28}, {0,23}, {0,14}, {-2,7}, {-3,0}, {2,7},  {3,0},  {3,18}, {5,16}, {9,24}, {-2,18},{-1,14}},
 /*WALK1*/{{0,29}, {0,24}, {0,14}, {-2,7}, {-4,0}, {3,8},  {4,0},  {3,19}, {5,17}, {9,25}, {-2,19},{-1,15}},
 /*WALK2*/{{0,30}, {0,25}, {0,15}, {0,8},  {-1,0}, {1,9},  {2,1},  {3,20}, {5,18}, {9,26}, {-2,20},{-1,16}},
 /*WALK3*/{{0,29}, {0,24}, {0,14}, {3,8},  {4,0},  {-2,7}, {-4,0}, {3,19}, {5,17}, {9,25}, {-2,19},{-1,15}},
 /*JUMP */{{0,31}, {0,26}, {0,16}, {-1,11},{-3,8}, {3,11}, {2,8},  {3,22}, {4,24}, {6,35}, {-3,22},{-4,26}},
 /*CROUCH*/{{1,22},{1,17}, {0,8},  {-3,6}, {-4,0}, {4,5},  {4,0},  {4,13}, {6,10}, {13,7}, {-2,12},{0,9}},
 /*BLOCK*/{{-1,29},{-1,24},{0,14}, {-2,7}, {-3,0}, {2,7},  {3,0},  {3,19}, {5,16}, {6,28}, {-2,19},{0,16}},
 /*SL1  */{{-1,29},{-1,24},{0,14}, {-2,7}, {-3,0}, {2,7},  {3,0},  {1,23}, {-1,27},{-5,36},{-3,19},{-2,15}},
 /*SL2  */{{3,28}, {2,23}, {0,14}, {-2,7}, {-4,0}, {4,8},  {6,0},  {5,21}, {8,19}, {14,12},{-1,19},{-2,15}},
 /*SL3  */{{2,28}, {1,23}, {0,14}, {-2,7}, {-4,0}, {3,8},  {5,0},  {4,19}, {6,15}, {11,8}, {-1,19},{-2,15}},
 /*TH1  */{{-1,29},{-1,24},{0,14}, {-2,7}, {-3,0}, {2,7},  {3,0},  {-1,18},{-3,16},{3,16}, {-3,19},{-2,15}},
 /*TH2  */{{5,28}, {4,23}, {2,13}, {-3,6}, {-5,0}, {6,7},  {8,0},  {7,19}, {10,17},{17,17},{1,19}, {0,15}},
 /*TH3  */{{3,28}, {2,23}, {1,14}, {-3,6}, {-5,0}, {5,7},  {7,0},  {5,19}, {8,16}, {14,13},{0,19}, {-1,15}},
 /*HIT  */{{-4,27},{-3,23},{0,14}, {-2,7}, {-4,0}, {2,7},  {3,0},  {2,21}, {4,25}, {6,35}, {-4,22},{-6,25}},
 /*STAG */{{-3,28},{-2,23},{0,14}, {-2,7}, {-3,0}, {2,7},  {3,0},  {1,21}, {0,25}, {-4,34},{-4,20},{-5,16}},
 /*DEAD */{{-9,2}, {-6,2}, {2,2},  {6,3},  {10,2}, {5,1},  {9,0},  {-3,3}, {-2,5}, {-8,5}, {-4,1}, {-2,0}},
 /*WIN  */{{0,29}, {0,24}, {0,14}, {-2,7}, {-3,0}, {2,7},  {3,0},  {2,24}, {3,29}, {4,41}, {-3,19},{-1,15}},
};

/* ---- State ------------------------------------------------------------- */

static F f[2];
static unsigned bg;
static int rstate, rt, round, ticks, quit, testmode, fps, event;
static struct { int x, y, t, c; } sp[4];

static unsigned seed = 7;
static unsigned rnd(void)
{
    seed = seed * 25173u + 13849u;
    return seed >> 4;
}

/* ---- Sound ------------------------------------------------------------- */

static struct { int left, hz, dhz, vol; } sv[3];
static int nz_left;

static void play(int v, int hz, int dhz, int n, int vol)
{
    sv[v].left = n; sv[v].hz = hz; sv[v].dhz = dhz; sv[v].vol = vol;
}

static void noise(int type, int n)
{
    snd_noise(type, 12);
    nz_left = n;
}

static void snd_step(void)
{
    int v;

    for (v = 0; v < 3; v++)
        if (sv[v].left) {
            snd_tone(v, sv[v].hz, sv[v].vol);
            sv[v].hz += sv[v].dhz;
            if (sv[v].hz < 60) sv[v].hz = 60;
            if (--sv[v].left == 0)
                snd_tone(v, 440, 0);
        }
    if (nz_left && --nz_left == 0)
        snd_noise(0, 0);
}

static void snd_off(void)
{
    int v;

    for (v = 0; v < 3; v++) {
        sv[v].left = 0;
        snd_tone(v, 440, 0);
    }
    snd_noise(0, 0);
}

/* ---- Background and HUD ------------------------------------------------ */

#define MAXDQ 12
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

static void hud_health(int i)
{
    int w = f[i].hp * 64 / 100, c = f[i].hp > 30 ? 10 : 12;

    fx_target(bg);
    if (i == 0) {
        fx_fill_rect(4, 3, 64, 6, FX_SOLID2(4));
        if (w) fx_fill_rect(4, 3, w, 6, FX_SOLID2(c));
        add_dirty(4, 3, 67, 8);
    } else {
        fx_fill_rect(92, 3, 64, 6, FX_SOLID2(4));
        if (w) fx_fill_rect(156 - w, 3, w, 6, FX_SOLID2(c));
        add_dirty(92, 3, 155, 8);
    }
    fx_target(0);
}

static void hud_names(void)
{
    int i, n;

    fx_target(bg);
    fx_fill_rect(4, 11, 64, 8, 0);
    fx_fill_rect(92, 11, 64, 8, 0);
    fx_text(4, 11, f[0].ai ? "CPU1" : "P1", f[0].col);
    fx_text(124, 11, f[1].ai ? "CPU2" : "P2", f[1].col);
    n = f[0].wins > 5 ? 5 : f[0].wins;
    for (i = 0; i < n; i++)
        fx_fill_rect(40 + i * 5, 13, 3, 4, FX_SOLID2(14));
    n = f[1].wins > 5 ? 5 : f[1].wins;
    for (i = 0; i < n; i++)
        fx_fill_rect(116 - i * 5, 13, 3, 4, FX_SOLID2(14));
    fx_fill_rect(0, 184, 160, 16, 0);
    fx_text(0, 184, "P1: W A S D  F G H", f[0].ai ? 8 : 15);
    fx_text(0, 192, "P2: ARROWS  , . /", f[1].ai ? 8 : 15);
    fx_target(0);
    add_dirty(0, 11, 159, 18);
    add_dirty(0, 184, 159, 199);
}

static void hud_fps(void)
{
    char b[4];

    sprintf(b, "%2d", fps > 99 ? 99 : fps);
    fx_target(bg);
    fx_fill_rect(72, 3, 16, 8, 0);
    fx_text(72, 3, b, 7);
    fx_target(0);
    add_dirty(72, 3, 87, 10);
}

static void draw_scene(void)
{
    static const unsigned char moon[11] = { 1, 2, 3, 3, 3, 3, 3, 3, 3, 2, 1 };
    int i, x, y;

    fx_target(bg);
    fx_clear(0);
    for (i = 0; i < 24; i++) {
        x = rnd() % 160;
        y = 22 + rnd() % 100;
        fx_pset(x, y, (i & 3) ? 8 : 7);
    }
    for (i = 0; i < 11; i++)
        fx_hspan(30 + i, 128 - moon[i], 128 + moon[i], FX_SOLID(7));
    fx_hspan(32, 126, 127, FX_SOLID(15));
    fx_tri(0, 176, 28, 126, 62, 176, FX_SOLID2(8));
    fx_tri(44, 176, 92, 112, 140, 176, FX_SOLID2(8));
    fx_tri(118, 176, 150, 134, 159, 176, FX_SOLID2(8));
    fx_tri(0, 176, 16, 152, 46, 176, FX_SOLID2(1));
    fx_tri(70, 176, 110, 142, 159, 176, FX_SOLID2(1));
    fx_hspan(GY, 0, 159, FX_SOLID(10));
    fx_fill_rect(0, GY + 1, 160, 7, FX_DITH(2, 0));
    fx_text(68, 11, "FPS", 8);
    fx_target(0);
    hud_health(0);
    hud_health(1);
    hud_names();
    hud_fps();
    fullref = 2;
}

/* ---- Sparks ------------------------------------------------------------ */

static void spark(int x, int y, int c)
{
    int i, best = 0;

    for (i = 0; i < 4; i++)
        if (sp[i].t < sp[best].t)
            best = i;
    sp[best].x = x; sp[best].y = y; sp[best].t = 8; sp[best].c = c;
    event = 1;
}

static void draw_sparks(void)
{
    int i, k;

    for (i = 0; i < 4; i++) {
        if (!sp[i].t)
            continue;
        k = sp[i].t / 2 + 1;
        fx_line(sp[i].x - k, sp[i].y, sp[i].x + k, sp[i].y, sp[i].c);
        fx_line(sp[i].x, sp[i].y - 2 * k, sp[i].x, sp[i].y + 2 * k, sp[i].c);
        fx_line(sp[i].x - k, sp[i].y - k, sp[i].x + k, sp[i].y + k, sp[i].c);
        fx_line(sp[i].x - k, sp[i].y + k, sp[i].x + k, sp[i].y - k, sp[i].c);
    }
}

/* ---- Fighters ---------------------------------------------------------- */

static const Pose *pose_for(const F *p)
{
    int i;

    switch (p->state) {
    case ST_WALK:
        i = (p->t >> 2) & 3;
        return &poses[i == 0 ? P_WALK1 : i == 2 ? P_WALK3 : P_WALK2];
    case ST_JUMP:   return &poses[P_JUMP];
    case ST_CROUCH: return &poses[P_CROUCH];
    case ST_BLOCK:  return &poses[P_BLOCK];
    case ST_SLASH:
        return &poses[p->t < SL_WIND ? P_SL1 :
                      p->t < SL_WIND + SL_ACT ? P_SL2 : P_SL3];
    case ST_THRUST:
        return &poses[p->t < TH_WIND ? P_TH1 :
                      p->t < TH_WIND + TH_ACT ? P_TH2 : P_TH3];
    case ST_HIT:     return &poses[P_HIT];
    case ST_STAGGER: return &poses[P_STAG];
    case ST_DEAD:    return &poses[P_DEAD];
    case ST_WIN:     return &poses[P_WIN];
    default:         return &poses[(ticks >> 4) & 1 ? P_IDLE2 : P_IDLE1];
    }
}

static int active(const F *p)
{
    if (p->state == ST_SLASH)
        return p->t >= SL_WIND && p->t < SL_WIND + SL_ACT;
    if (p->state == ST_THRUST)
        return p->t >= TH_WIND && p->t < TH_WIND + TH_ACT;
    return 0;
}

static void draw_fig(const F *p)
{
    const Pose *q = pose_for(p);
    int bx = p->x >> 2, by = GY - (p->y >> 2), fc = p->face, c = p->col;
    int hx, hy, w, sc = 15;
#define X(j) (bx + q->j[0] * fc)
#define Y(j) (by - q->j[1])

    w = 4 - (p->y >> 5);
    if (w < 1) w = 1;
    fx_hspan(GY, bx - w, bx + w, FX_SOLID(8));
    if (active(p)) {
        const Pose *w0 = &poses[p->state == ST_SLASH ? P_SL1 : P_TH1];
        sc = 14;
        if (p->t < (p->state == ST_SLASH ? SL_WIND : TH_WIND) + 3)
            fx_line(bx + w0->tp[0] * fc, by - w0->tp[1], X(tp), Y(tp), 7);
    }
    fx_line(X(nk), Y(nk), X(hp), Y(hp), c);
    fx_line(X(hp), Y(hp), X(ka), Y(ka), c);
    fx_line(X(ka), Y(ka), X(fa), Y(fa), c);
    fx_line(X(hp), Y(hp), X(kb), Y(kb), c);
    fx_line(X(kb), Y(kb), X(fb), Y(fb), c);
    fx_line(X(nk), Y(nk), X(eo), Y(eo), c);
    fx_line(X(eo), Y(eo), X(ho), Y(ho), c);
    fx_line(X(nk), Y(nk), X(el), Y(el), c);
    fx_line(X(el), Y(el), X(hn), Y(hn), c);
    fx_line(X(hn), Y(hn), X(tp), Y(tp), sc);
    hx = X(hd); hy = Y(hd);
    fx_hspan(hy - 2, hx, hx, FX_SOLID(c));
    fx_hspan(hy - 1, hx - 1, hx + 1, FX_SOLID(c));
    fx_hspan(hy, hx - 1, hx + 1, FX_SOLID(c));
    fx_hspan(hy + 1, hx - 1, hx + 1, FX_SOLID(c));
    fx_hspan(hy + 2, hx, hx, FX_SOLID(c));
#undef X
#undef Y
}

static void start_attack(F *me, int st)
{
    int v = me == &f[0] ? 0 : 2;

    me->state = st;
    me->t = 0;
    me->hitdone = 0;
    me->atkid++;
    me->in.slash = me->in.thrust = 0;
    if (st == ST_SLASH)
        play(v, 1400, -150, 6, 8);
    else
        play(v, 700, 90, 5, 8);
}

static void end_attack(F *me)
{
    me->state = me->y > 0 ? ST_JUMP : ST_IDLE;
    me->t = 0;
}

static void walk(F *me, const F *op, int dir, int spd)
{
    int nx = me->x + dir * spd;

    if ((op->x - me->x) * dir > 0 && abs(op->x - nx) < (7 << 2) &&
        abs(op->y - me->y) < (12 << 2) && op->state != ST_DEAD)
        return;
    if (nx < XMIN) nx = XMIN;
    if (nx > XMAX) nx = XMAX;
    me->x = nx;
}

static void check_hit(F *me, F *op, int kind)
{
    int dx = ((op->x - me->x) >> 2) * me->face, dy = (op->y - me->y) >> 2;
    int ox = op->x >> 2, oy = GY - (op->y >> 2) - 20;

    if (dx < 1 || dx > (kind == 1 ? 20 : 24))
        return;
    if (abs(dy) > (kind == 1 ? 18 : 8))
        return;
    if (op->state == ST_DEAD || op->state == ST_HIT || op->state == ST_WIN)
        return;
    me->hitdone = 1;
    if ((op->state == ST_SLASH || op->state == ST_THRUST) &&
        op->face == -me->face && op->t >= (op->state == ST_SLASH ? SL_WIND : TH_WIND) - 3 &&
        op->t < (op->state == ST_SLASH ? SL_WIND + SL_ACT : TH_WIND + TH_ACT)) {
        me->state = op->state = ST_STAGGER;
        me->t = op->t = 0;
        op->hitdone = 1;
        me->vx = -me->face * 6;
        op->vx = me->face * 6;
        spark((me->x + op->x) >> 3, oy, 15);
        play(1, 2800, -60, 8, 12);
        noise(4, 3);
        if (testmode) dbg_log("t=%d clash", ticks);
        return;
    }
    if ((kind == 1 && op->state == ST_BLOCK) ||
        (kind == 2 && op->state == ST_CROUCH)) {
        me->state = ST_STAGGER;
        me->t = 0;
        me->vx = -me->face * 6;
        spark(ox - op->face * 4, oy, 14);
        play(1, 2400, -40, 7, 12);
        noise(4, 2);
        if (testmode) dbg_log("t=%d parry by %d", ticks, op == &f[0] ? 1 : 2);
        return;
    }
    op->hp -= kind == 1 ? 25 : 20;
    if (op->hp < 0) op->hp = 0;
    op->state = ST_HIT;
    op->t = 0;
    op->vx = me->face * (op->hp ? 8 : 12);
    if (!op->hp) op->vy = 6;
    spark(ox, oy, 12);
    play(1, 320, -30, 6, 13);
    noise(6, 5);
    hud_health(op == &f[0] ? 0 : 1);
    if (testmode)
        dbg_log("t=%d %d hits %d with %s, hp=%d", ticks, me == &f[0] ? 1 : 2,
                op == &f[0] ? 1 : 2, kind == 1 ? "slash" : "thrust", op->hp);
}

static void ko(F *loser, F *winner)
{
    rstate = R_KO;
    rt = 0;
    if (winner->hp > 0 && winner->state != ST_DEAD) {
        winner->state = ST_WIN;
        winner->wins++;
    }
    play(0, 700, -14, 40, 13);
    hud_names();
    if (testmode)
        dbg_log("t=%d KO: %d down", ticks, loser == &f[0] ? 1 : 2);
}

static void step(F *me, F *op)
{
    In *in = &me->in;

    me->t++;
    switch (me->state) {
    case ST_IDLE:
    case ST_WALK:
        me->face = op->x >= me->x ? 1 : -1;
        if (in->slash) { start_attack(me, ST_SLASH); break; }
        if (in->thrust) { start_attack(me, ST_THRUST); break; }
        if (in->up) {
            me->state = ST_JUMP; me->t = 0; me->vy = JUMP_V;
            me->airatk = 0; in->up = 0;
            break;
        }
        if (in->block) { me->state = ST_BLOCK; break; }
        if (in->dn) { me->state = ST_CROUCH; break; }
        if (in->l || in->r) {
            if (me->state != ST_WALK) { me->state = ST_WALK; me->t = 0; }
            walk(me, op, in->l ? -1 : 1, 3);
        } else
            me->state = ST_IDLE;
        break;
    case ST_JUMP:
        if (in->l || in->r)
            walk(me, op, in->l ? -1 : 1, 2);
        if (in->slash && !me->airatk) {
            me->airatk = 1;
            start_attack(me, ST_SLASH);
        }
        break;
    case ST_CROUCH:
        me->face = op->x >= me->x ? 1 : -1;
        if (in->thrust) { start_attack(me, ST_THRUST); break; }
        if (in->slash) { start_attack(me, ST_SLASH); break; }
        if (!in->dn) me->state = ST_IDLE;
        break;
    case ST_BLOCK:
        if (!in->block) me->state = ST_IDLE;
        break;
    case ST_SLASH:
        if (me->t >= SL_WIND && me->t < SL_WIND + SL_ACT && !me->hitdone)
            check_hit(me, op, 1);
        if (me->state == ST_SLASH && me->t >= SL_WIND + SL_ACT + SL_REC)
            end_attack(me);
        break;
    case ST_THRUST:
        if (me->t >= TH_WIND && me->t < TH_WIND + TH_ACT && !me->hitdone)
            check_hit(me, op, 2);
        if (me->state == ST_THRUST && me->t >= TH_WIND + TH_ACT + TH_REC)
            end_attack(me);
        break;
    case ST_HIT:
        if (me->t >= HIT_T) {
            if (me->hp <= 0) {
                me->state = ST_DEAD;
                ko(me, op);
            } else
                me->state = ST_IDLE;
        }
        break;
    case ST_STAGGER:
        if (me->t >= STAG_T) me->state = ST_IDLE;
        break;
    }
}

static void physics(F *me)
{
    if (me->y > 0 || me->vy > 0) {
        me->y += me->vy;
        me->vy--;
        if (me->y <= 0) {
            me->y = 0;
            me->vy = 0;
            if (me->state == ST_JUMP)
                me->state = ST_IDLE;
        }
    }
    if (me->vx) {
        me->x += me->vx;
        if (me->vx > 0) me->vx--; else me->vx++;
    }
    if (me->x < XMIN) me->x = XMIN;
    if (me->x > XMAX) me->x = XMAX;
}

/* ---- Brains ------------------------------------------------------------ */

static void ai_think(F *me, const F *op)
{
    In *in = &me->in;
    int ad = abs(op->x - me->x) >> 2, r, left = op->x < me->x;

    /* react once to each attack the opponent starts */
    if ((op->state == ST_SLASH || op->state == ST_THRUST) &&
        op->atkid != me->reacted && ad < 32) {
        me->reacted = op->atkid;
        r = rnd() % 100;
        if (r < me->skill) {
            memset(in, 0, sizeof *in);
            if (op->state == ST_SLASH)
                in->block = 1;
            else if (rnd() & 3)
                in->dn = 1;
            else
                in->up = 1;
            me->think = 18;
            return;
        }
        if (r < me->skill + 15) {           /* counter instead */
            memset(in, 0, sizeof *in);
            if (op->state == ST_SLASH) in->thrust = 1; else in->slash = 1;
            me->think = 8;
            return;
        }
    }
    if (--me->think > 0)
        return;
    me->think = 5 + rnd() % 10;
    memset(in, 0, sizeof *in);
    if (op->state == ST_DEAD)
        return;
    if (ad > 28) {
        if (left) in->l = 1; else in->r = 1;
        if (rnd() % 10 == 0) in->up = 1;
        me->think = 8 + rnd() % 8;
    } else if (ad < 9) {
        r = rnd() % 100;
        if (r < 40) in->thrust = 1;
        else if (r < 60) in->slash = 1;
        else { if (left) in->r = 1; else in->l = 1; me->think = 10; }
    } else {
        r = rnd() % 100;
        if (r < me->aggr) {
            if (op->state == ST_BLOCK) in->thrust = 1;
            else if (op->state == ST_CROUCH || op->state == ST_JUMP) in->slash = 1;
            else if (rnd() & 1) in->slash = 1;
            else in->thrust = 1;
        } else if (r < me->aggr + 15) {
            in->block = 1;
            me->think = 10 + rnd() % 10;
        } else if (r < me->aggr + 25) {
            if (left) in->r = 1; else in->l = 1;
        } else if (r < me->aggr + 32) {
            in->up = 1;
            if (left) in->l = 1; else in->r = 1;
        } else if (r < me->aggr + 40) {
            if (left) in->l = 1; else in->r = 1;
        }
    }
}

static void human(F *me, int which)
{
    In raw, *in = &me->in;

    if (which == 0) {
        raw.l = key_down[KEY_A]; raw.r = key_down[KEY_D];
        raw.up = key_down[KEY_W]; raw.dn = key_down[KEY_S];
        raw.slash = key_down[KEY_F]; raw.thrust = key_down[KEY_G];
        raw.block = key_down[KEY_H];
    } else {
        raw.l = key_down[KEY_LEFT]; raw.r = key_down[KEY_RIGHT];
        raw.up = key_down[KEY_UP]; raw.dn = key_down[KEY_DOWN];
        raw.slash = key_down[KEY_COMMA]; raw.thrust = key_down[KEY_PERIOD];
        raw.block = key_down[KEY_SLASH];
    }
    in->l = raw.l; in->r = raw.r; in->dn = raw.dn; in->block = raw.block;
    in->up = raw.up && !me->pin.up;
    in->slash = raw.slash && !me->pin.slash;
    in->thrust = raw.thrust && !me->pin.thrust;
    me->pin = raw;
}

/* ---- Rounds ------------------------------------------------------------ */

static void reset_round(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        f[i].x = (i ? 118 : 42) << 2;
        f[i].y = f[i].vx = f[i].vy = 0;
        f[i].face = i ? -1 : 1;
        f[i].state = ST_IDLE;
        f[i].t = f[i].think = f[i].hitdone = 0;
        f[i].hp = 100;
        memset(&f[i].in, 0, sizeof(In));
        f[i].aggr = (i ? 45 : 55) + rnd() % 25;
        f[i].skill = (i ? 45 : 35) + rnd() % 25;
        hud_health(i);
    }
    for (i = 0; i < 4; i++) sp[i].t = 0;
    rstate = R_READY;
    rt = 0;
    round++;
}

static void tick(void)
{
    int i;

    ticks++;
    snd_step();
    for (i = 0; i < 4; i++)
        if (sp[i].t) sp[i].t--;
    if (rstate == R_READY) {
        rt++;
        if (rt == 60) play(0, 660, 40, 8, 12);
        if (rt >= 100) rstate = R_FIGHT;
        return;
    }
    for (i = 0; i < 2; i++) {
        if (rstate == R_FIGHT) {
            if (f[i].ai) ai_think(&f[i], &f[1 - i]);
            else human(&f[i], i);
        } else
            memset(&f[i].in, 0, sizeof(In));
    }
    for (i = 0; i < 2; i++)
        if (f[i].state != ST_DEAD && f[i].state != ST_WIN)
            step(&f[i], &f[1 - i]);
    for (i = 0; i < 2; i++)
        physics(&f[i]);
    if (rstate == R_KO && ++rt > 150)
        reset_round();
}

/* ---- Drawing ----------------------------------------------------------- */

static void draw_msg(const char *s, int y, int c)
{
    fx_text(80 - strlen(s) * 4, y, s, c);
}

static void frame(void)
{
    static char b[16];

    fx_begin();
    fx_restore(bg);
    flush_dirty();
    draw_fig(&f[0]);
    draw_fig(&f[1]);
    draw_sparks();
    if (rstate == R_READY) {
        if (rt < 60) {
            sprintf(b, "ROUND %d", round);
            draw_msg(b, 70, 15);
            if (f[0].ai && f[1].ai)
                draw_msg("PRESS 1 OR 2 TO PLAY", 84, 7);
        } else
            draw_msg("FIGHT!", 70, (rt & 4) ? 14 : 15);
    } else if (rstate == R_KO) {
        if (rt < 60)
            draw_msg("K.O.!", 70, (rt & 4) ? 12 : 15);
        else if (f[0].state == ST_WIN)
            draw_msg(f[0].ai ? "CPU1 WINS" : "P1 WINS", 70, f[0].col);
        else if (f[1].state == ST_WIN)
            draw_msg(f[1].ai ? "CPU2 WINS" : "P2 WINS", 70, f[1].col);
        else
            draw_msg("DOUBLE K.O.", 70, 15);
    }
    fx_flip();
}

/* ---- Main -------------------------------------------------------------- */

static void run_test(int n)
{
    int t, shots = 0;
    char name[16];

    dbg_log("pages=%d irq=%d", fx_pages, fx_irq);
    for (t = 0; t < n; t++) {
        event = 0;
        tick();
        if ((t & 1) == 0 || event)
            frame();
        if (event && shots < 2) {
            sprintf(name, "SHOT%d.PPM", ++shots);
            fx_begin();
            fx_screenshot(name);
        }
        if (t % 200 == 0)
            dbg_log("t=%d r=%d  1: x=%d y=%d st=%d hp=%d  2: x=%d y=%d st=%d hp=%d",
                    t, rstate, f[0].x >> 2, f[0].y >> 2, f[0].state, f[0].hp,
                    f[1].x >> 2, f[1].y >> 2, f[1].state, f[1].hp);
    }
    frame();
    fx_begin();
    fx_screenshot("SHOT.PPM");
    dbg_log("end round=%d wins=%d/%d hp=%d/%d", round, f[0].wins, f[1].wins,
            f[0].hp, f[1].hp);
}

int main(int argc, char **argv)
{
    unsigned long next, now, fpst;
    int steps, frames = 0, k1 = 0, k2 = 0, n = 1500;

    if (argc > 1 && stricmp(argv[1], "/TEST") == 0) {
        testmode = 1;
        if (argc > 2) n = atoi(argv[2]);
        remove("LOG.TXT");
    }
    if (fx_init(testmode ? FX_TEAR : 0)) {
        printf("Cannot start graphics (error %d)\n", gfx_err);
        return 1;
    }
    key_init();
    tmr_init();
    snd_init();
    bg = fx_alloc_buffer();
    if (!bg) {
        gfx_done();
        printf("Not enough memory\n");
        return 1;
    }
    fx_track(1);
    if (!testmode)
        seed = (unsigned)tmr_ticks();
    f[0].ai = f[1].ai = 1;
    f[0].col = 11;
    f[1].col = 12;
    fps = 60;
    draw_scene();
    reset_round();

    if (testmode)
        run_test(n);
    else {
        next = fpst = tmr_ticks();
        while (!quit) {
            if (key_down[KEY_ESC])
                quit = 1;
            if (key_down[KEY_1] && !k1) { f[0].ai = !f[0].ai; hud_names(); }
            k1 = key_down[KEY_1];
            if (key_down[KEY_2] && !k2) { f[1].ai = !f[1].ai; hud_names(); }
            k2 = key_down[KEY_2];
            now = tmr_ticks();
            steps = 0;
            while ((long)(now - next) >= 0 && steps < 3) {
                tick();
                next += TICK;
                steps++;
            }
            if ((long)(now - next) > 0)
                next = now;
            frame();
            frames++;
            if (now - fpst >= TMR_HZ) {
                fps = frames;
                frames = 0;
                fpst += TMR_HZ;
                hud_fps();
            }
        }
    }
    snd_off();
    gfx_done();
    return 0;
}
