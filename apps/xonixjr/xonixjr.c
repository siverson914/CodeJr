/* XONIXJR.C - Xonix Junior: cut the sea into land, dodge the balls.
 *
 * 160x200, 16 colors, JRGFX fast path. The field is 80x88 cells of 2x2
 * pixels in a background buffer; each frame only the movers are drawn
 * over it and the cells that changed are copied to both video pages.
 *
 * XONIXJR /TEST [script] plays level 1 with a script (D20R10U20: letters
 * are keys held, N none, the number is ticks), logs to LOG.TXT, saves
 * SHOT.PPM and exits. /TEST 0 shows the title.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <i86.h>
#include "jrgfx.h"
#include "jrkeys.h"

#define GW 80
#define GH 88
#define TOTAL ((GW - 4) * (GH - 4))
#define TARGET 75
#define TICK (TMR_HZ / 24)
#define MAXBALL 8
#define MAXCRAWL 4
#define BIGCUT 800
#define KEY_P 0x19

enum { C_SEA, C_LAND, C_TRAIL, C_MARK, C_FRESH };
enum { G_TITLE, G_READY, G_PLAY, G_CLEAR, G_OVER };
enum { S_PLAYER, S_BALL, S_CRAWL, NSPR };

typedef struct { unsigned char sea, land, border, trail; } Scheme;
static const Scheme schemes[6] = {
    { 1, 9, 11, 15 }, { 2, 10, 11, 15 }, { 5, 13, 11, 15 },
    { 6, 14, 12, 15 }, { 3, 11, 9, 15 }, { 8, 7, 12, 14 } };
static const Scheme *sc = &schemes[0];

/* ---- Sprites (4x4, drawn one pixel around the cell) -------------------- */

static const char a_player[] = "ffff" "f..f" "f..f" "ffff";
static const char a_ball[]   = ".cc." "cfc4" "ccc4" ".44.";
static const char a_crawl[]  = "f00f" "0ff0" "0ff0" "f00f";

static unsigned char sprpix[NSPR][16];
static fx_sprite *spr[NSPR];

static int mkspr(int id, const char *art, int key)
{
    int i;
    char ch;

    for (i = 0; i < 16; i++) {
        ch = art[i];
        sprpix[id][i] = (unsigned char)
            (ch == '.' ? 0 : ch <= '9' ? ch - '0' : ch - 'a' + 10);
    }
    spr[id] = fx_compile(sprpix[id], 4, 4, key);
    return spr[id] != NULL;
}

/* ---- Sound ------------------------------------------------------------- */

typedef struct { unsigned hz; unsigned char d; } Note;

static const Note s_fill[]  = { {1568,1},{1319,1},{1047,1},{784,2},{0,0} };
static const Note s_big[]   = { {784,2},{1047,2},{1319,2},{1568,2},{2093,5},{0,0} };
static const Note s_die[]   = { {440,3},{349,3},{294,3},{220,8},{0,0} };
static const Note s_clear[] = { {523,3},{659,3},{784,3},{1047,3},{784,3},{1047,9},{0,0} };
static const Note s_over[]  = { {392,6},{330,6},{262,6},{196,12},{0,0} };
static const Note s_life[]  = { {1047,2},{1319,2},{1568,2},{2093,6},{0,0} };
static const Note s_ready[] = { {659,2},{0,2},{659,2},{0,2},{880,5},{0,0} };
static const Note m_title0[] = {
    {659,4},{659,4},{784,4},{659,4},{523,4},{587,4},{659,8},
    {698,4},{698,4},{880,4},{698,4},{587,4},{659,4},{698,8},
    {784,4},{659,4},{523,4},{659,4},{587,4},{494,4},{392,8},
    {523,4},{659,4},{784,4},{1047,4},{1047,8},{0,8},{0,0} };
static const Note m_title1[] = {
    {131,8},{196,8},{131,8},{196,8},{175,8},{220,8},{175,8},{220,8},
    {131,8},{196,8},{196,8},{247,8},{131,8},{196,8},{131,16},{0,0} };

static struct { const Note *p; int left; unsigned hz; int gap; } vc[3];
static int beep_t, noise_t;

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
    beep_t = noise_t = 0;
}

static void snd_tick(void)
{
    int v;

    if (beep_t && --beep_t == 0)
        snd_tone(2, 440, 0);
    if (noise_t && --noise_t == 0)
        snd_noise(0, 0);
    for (v = 0; v < 2; v++) {
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

/* ---- State ------------------------------------------------------------- */

static unsigned bg;
static unsigned char grid[GH][GW];
static unsigned char qx[TOTAL + 8], qy[TOTAL + 8];
static int gstate, gtimer, level, lives, ticks, quit, testmode, paused;
static long score, hiscore, nextlife, claimed;
static int pct;
static const char *msg;
static int px, py, pdir, insea, trail_len, fresh_t;
static struct { int x, y, dx, dy; } ball[MAXBALL], crawl[MAXCRAWL];
static int nball, ncrawl;
static struct { int u, d, l, r, best; } inp;   /* best: newest press, 1-4 */
static int k_u, k_d, k_l, k_r;                  /* JRKEYS key indices */

static void keys_setup(void)
{
    jk_init();                  /* up/down are group 1, left/right group 2;
                                   2 s is the longest useful hold here */
    k_u = jk_add(KEY_UP, KEY_W, 1, 120);
    k_d = jk_add(KEY_DOWN, KEY_S, 1, 120);
    k_l = jk_add(KEY_LEFT, KEY_A, 2, 120);
    k_r = jk_add(KEY_RIGHT, KEY_D, 2, 120);
}
static const char *scr;
static int scr_left, scr_done;

static unsigned seed = 4242;
static unsigned rnd(void)
{
    seed = seed * 25173u + 13849u;
    return seed >> 4;
}

/* ---- Background and dirty rectangles ----------------------------------- */

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

#define is_border(x, y) ((x) < 2 || (x) >= GW - 2 || (y) < 2 || (y) >= GH - 2)
#define is_land(t) ((t) == C_LAND || (t) == C_FRESH)

/* Cell (x, y) is pixel byte x of rows 2y and 2y+1: two byte writes. */
static void draw_cell(int x, int y)
{
    unsigned char far *p = (unsigned char far *)MK_FP(bg, y * 80 + x);
    unsigned char c;

    switch (grid[y][x]) {
    case C_SEA:   c = sc->sea; break;
    case C_TRAIL: c = sc->trail; break;
    case C_FRESH: c = 15; break;
    default:      c = is_border(x, y) ? sc->border : sc->land; break;
    }
    c = FX_SOLID(c);
    p[0] = c;
    p[0x2000] = c;
}

static void draw_field(void)
{
    int x, y;

    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++)
            draw_cell(x, y);
    fullref = 2;
}

static void dirty_cell(int x, int y)
{
    add_dirty(x << 1, y << 1, (x << 1) + 1, (y << 1) + 1);
}

/* ---- HUD --------------------------------------------------------------- */

static void hud(void)
{
    char b[24];

    pct = (int)(claimed * 100L / TOTAL);
    sprintf(b, "%06ld  L%02d  x%d", score, level, lives);
    fx_target(bg);
    fx_fill_rect(0, 176, 160, 24, 0);
    fx_text(0, 180, b, 15);
    fx_fill_rect(2, 191, 100, 6, FX_SOLID2(sc->sea));
    if (pct)
        fx_fill_rect(2, 191, pct, 6, FX_SOLID2(sc->land));
    fx_fill_rect(2 + TARGET, 190, 1, 8, FX_SOLID2(15));
    sprintf(b, "%3d%%", pct);
    fx_text(116, 190, b, pct >= TARGET ? 10 : 14);
    fx_target(0);
    add_dirty(0, 176, 159, 199);
}

static void add_score(long n)
{
    score += n;
    if (score >= nextlife) {
        nextlife += 20000;
        lives++;
        sfx(1, s_life);
    }
}

/* ---- Levels ------------------------------------------------------------ */

static void place_player(void)
{
    px = GW / 2;
    py = 1;
    pdir = 0;
    insea = 0;
    trail_len = 0;
}

static void new_level(void)
{
    int x, y, i;

    sc = &schemes[(level - 1) % 6];
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++)
            grid[y][x] = is_border(x, y) ? C_LAND : C_SEA;
    claimed = 0;
    fresh_t = 0;
    nball = 1 + level;
    if (nball > MAXBALL) nball = MAXBALL;
    for (i = 0; i < nball; i++) {
        ball[i].x = 4 + rnd() % (GW - 8);
        ball[i].y = 24 + rnd() % (GH - 28);
        ball[i].dx = (rnd() & 1) ? 1 : -1;
        ball[i].dy = (rnd() & 1) ? 1 : -1;
    }
    ncrawl = 1 + (level - 1) / 2;
    if (ncrawl > MAXCRAWL) ncrawl = MAXCRAWL;
    for (i = 0; i < ncrawl; i++) {
        crawl[i].x = 10 + i * 20;
        crawl[i].y = GH - 2;
        crawl[i].dx = (i & 1) ? -1 : 1;
        crawl[i].dy = -1;
    }
    place_player();
    fx_target(bg);
    fx_clear(0);
    fx_target(0);
    draw_field();
    hud();
    gstate = G_READY;
    gtimer = 36;
    msg = "GET READY";
    sfx(0, s_ready);
}

static void new_game(void)
{
    score = 0;
    nextlife = 20000;
    lives = 3;
    level = 1;
    if (!testmode)
        seed = (unsigned)tmr_ticks();
    new_level();
}

/* ---- Title ------------------------------------------------------------- */

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
    fx_fill_rect(0, 0, n * 8, 8, 0);
    for (j = 0; j < 8; j++)
        for (i = 0; i < n * 8; i++)
            if (bm[j][i >> 3] & (0x80 >> (i & 7)))
                fx_fill_rect(x + i * 2, y + j * 4, 2, 4,
                             FX_SOLID2(j < 4 ? c1 : c2));
}

static void to_title(void)
{
    char b[16];
    int i;

    snd_quiet();
    if (score > hiscore)
        hiscore = score;
    gstate = G_TITLE;
    msg = NULL;
    sc = &schemes[0];
    fx_target(bg);
    fx_clear(0);
    big_text(40, 12, "XONIX", 11, 9);
    fx_text(20, 50, "- J U N I O R -", 11);
    fx_text(12, 72, "CUT SEA INTO LAND", 7);
    fx_text(16, 82, "CLAIM 75% TO WIN", 7);
    fx_text(0, 92, "BALLS CUT YOUR TRAIL", 7);
    fx_text(8, 102, "CRAWLERS HUNT LAND", 7);
    fx_text(0, 118, "ARROWS MOVE, P PAUSE", 14);
    fx_text(44, 128, "ESC QUITS", 14);
    sprintf(b, "HIGH %06ld", hiscore);
    fx_text(36, 144, b, 10);
    fx_fill_rect(0, 160, 160, 16, FX_SOLID2(1));
    fx_target(0);
    fullref = 2;
    ndq = 0;
    nball = 4;
    for (i = 0; i < nball; i++) {
        ball[i].x = 10 + i * 20;
        ball[i].y = 82 + i;
        ball[i].dx = (i & 1) ? -1 : 1;
        ball[i].dy = (i & 2) ? -1 : 1;
    }
}

static void tick_title(void)
{
    int i;

    if (!vc[0].p) {
        sfx(0, m_title0);
        sfx(1, m_title1);
    }
    for (i = 0; i < nball; i++) {
        ball[i].x += ball[i].dx;
        ball[i].y += ball[i].dy;
        if (ball[i].x <= 1 || ball[i].x >= 78) ball[i].dx = -ball[i].dx;
        if (ball[i].y <= 81 || ball[i].y >= 86) ball[i].dy = -ball[i].dy;
    }
    if (key_down[KEY_SPACE] || key_down[KEY_ENTER])
        new_game();
}

/* ---- Play -------------------------------------------------------------- */

static void die(void)
{
    int x, y;

    lives--;
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++)
            if (grid[y][x] == C_TRAIL) {
                grid[y][x] = C_SEA;
                draw_cell(x, y);
            }
    fullref = 2;
    place_player();
    snd_noise(4, 13);
    noise_t = 6;
    hud();
    if (lives <= 0) {
        gstate = G_OVER;
        gtimer = 90;
        msg = "GAME OVER";
        sfx(0, s_over);
    } else {
        gstate = G_READY;
        gtimer = 36;
        msg = "OUCH!";
        sfx(0, s_die);
    }
    if (testmode)
        dbg_log("t=%d died, lives=%d", ticks, lives);
}

/* The trail closed: everything the balls can't reach becomes land. */
static int do_fill(void)
{
    int head = 0, tail = 0, i, x, y, n = 0;

    for (i = 0; i < nball; i++) {
        x = ball[i].x; y = ball[i].y;
        if (grid[y][x] == C_SEA) {
            grid[y][x] = C_MARK;
            qx[tail] = (unsigned char)x; qy[tail++] = (unsigned char)y;
        }
    }
    while (head < tail) {
        x = qx[head]; y = qy[head++];
        if (grid[y][x - 1] == C_SEA) {
            grid[y][x - 1] = C_MARK;
            qx[tail] = (unsigned char)(x - 1); qy[tail++] = (unsigned char)y;
        }
        if (grid[y][x + 1] == C_SEA) {
            grid[y][x + 1] = C_MARK;
            qx[tail] = (unsigned char)(x + 1); qy[tail++] = (unsigned char)y;
        }
        if (grid[y - 1][x] == C_SEA) {
            grid[y - 1][x] = C_MARK;
            qx[tail] = (unsigned char)x; qy[tail++] = (unsigned char)(y - 1);
        }
        if (grid[y + 1][x] == C_SEA) {
            grid[y + 1][x] = C_MARK;
            qx[tail] = (unsigned char)x; qy[tail++] = (unsigned char)(y + 1);
        }
    }
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++) {
            if (grid[y][x] == C_SEA || grid[y][x] == C_TRAIL) {
                grid[y][x] = C_FRESH;
                draw_cell(x, y);
                n++;
            } else if (grid[y][x] == C_MARK)
                grid[y][x] = C_SEA;
        }
    fresh_t = 8;
    fullref = 2;
    claimed += n;
    return n;
}

static void settle_fresh(void)
{
    int x, y;

    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++)
            if (grid[y][x] == C_FRESH) {
                grid[y][x] = C_LAND;
                draw_cell(x, y);
            }
    fullref = 2;
}

static int ball_at(int x, int y)
{
    int i;

    for (i = 0; i < nball; i++)
        if (ball[i].x == x && ball[i].y == y)
            return 1;
    return 0;
}

static int crawl_at(int x, int y)
{
    int i;

    for (i = 0; i < ncrawl; i++)
        if (crawl[i].x == x && crawl[i].y == y)
            return 1;
    return 0;
}

static void step_player(void)
{
    static const int ddx[5] = { 0, 0, 0, -1, 1 };
    static const int ddy[5] = { 0, -1, 1, 0, 0 };
    static const int opp[5] = { 0, 2, 1, 4, 3 };
    int want = inp.best ? inp.best :
               inp.u ? 1 : inp.d ? 2 : inp.l ? 3 : inp.r ? 4 : 0;
    int nx, ny, t, n;

    if (insea) {
        if (want && want != opp[pdir])
            pdir = want;
    } else
        pdir = want;
    if (!pdir)
        return;
    nx = px + ddx[pdir];
    ny = py + ddy[pdir];
    if (nx < 0 || nx >= GW || ny < 0 || ny >= GH)
        return;
    t = grid[ny][nx];
    if (t == C_TRAIL || ball_at(nx, ny)) {
        die();
        return;
    }
    px = nx;
    py = ny;
    if (t == C_SEA) {
        insea = 1;
        trail_len++;
        grid[py][px] = C_TRAIL;
        draw_cell(px, py);
        dirty_cell(px, py);
        snd_tone(2, 300 + (trail_len < 150 ? trail_len : 150) * 8, 5);
        beep_t = 1;
    } else {
        if (insea) {
            insea = 0;
            trail_len = 0;
            n = do_fill();
            if (n >= BIGCUT) {
                add_score((long)n * level * 3);
                msg = "BIG CUT! X3";
                gtimer = 30;
                sfx(0, s_big);
            } else {
                add_score((long)n * level);
                sfx(0, s_fill);
            }
            hud();
            if (testmode)
                dbg_log("t=%d fill %d cells, pct=%d score=%ld", ticks, n,
                        pct, score);
            if (pct >= TARGET) {
                add_score((long)(pct - TARGET) * level * 20);
                hud();
                gstate = G_CLEAR;
                gtimer = 70;
                msg = "LEVEL CLEAR!";
                snd_quiet();
                sfx(0, s_clear);
                return;
            }
        }
        if (crawl_at(px, py))
            die();
    }
}

static void step_balls(void)
{
    int i, tx, ty, hx, hy, hd;

    if (level < 3 && ticks % 3 == 0)
        return;
    if (level < 6 && ticks % 5 == 0)
        return;
    for (i = 0; i < nball; i++) {
        tx = ball[i].x + ball[i].dx;
        ty = ball[i].y + ball[i].dy;
        hx = grid[ball[i].y][tx];
        hy = grid[ty][ball[i].x];
        hd = grid[ty][tx];
        if (hx == C_TRAIL || hy == C_TRAIL || hd == C_TRAIL) {
            die();
            return;
        }
        if (hx != C_SEA) ball[i].dx = -ball[i].dx;
        if (hy != C_SEA) ball[i].dy = -ball[i].dy;
        if (hx == C_SEA && hy == C_SEA && hd != C_SEA) {
            ball[i].dx = -ball[i].dx;
            ball[i].dy = -ball[i].dy;
        }
        tx = ball[i].x + ball[i].dx;
        ty = ball[i].y + ball[i].dy;
        if (grid[ty][tx] == C_SEA) {
            ball[i].x = tx;
            ball[i].y = ty;
        }
    }
}

static int landc(int x, int y)
{
    if (x < 0 || x >= GW || y < 0 || y >= GH)
        return 0;
    return is_land(grid[y][x]);
}

static void step_crawlers(void)
{
    int i, tx, ty, hx, hy, hd;

    if (ticks & 1)
        return;
    for (i = 0; i < ncrawl; i++) {
        tx = crawl[i].x + crawl[i].dx;
        ty = crawl[i].y + crawl[i].dy;
        hx = landc(tx, crawl[i].y);
        hy = landc(crawl[i].x, ty);
        hd = landc(tx, ty);
        if (!hx) crawl[i].dx = -crawl[i].dx;
        if (!hy) crawl[i].dy = -crawl[i].dy;
        if (hx && hy && !hd) {
            crawl[i].dx = -crawl[i].dx;
            crawl[i].dy = -crawl[i].dy;
        }
        tx = crawl[i].x + crawl[i].dx;
        ty = crawl[i].y + crawl[i].dy;
        if (landc(tx, ty)) {
            crawl[i].x = tx;
            crawl[i].y = ty;
        }
        if (!insea && crawl[i].x == px && crawl[i].y == py) {
            die();
            return;
        }
    }
}

/* ---- Input and ticks --------------------------------------------------- */

static void read_input(void)
{
    if (testmode) {
        if (scr_left == 0) {
            inp.l = inp.r = inp.u = inp.d = 0;
            if (!scr || !*scr)
                scr_done = 1;
            else {
                for (; *scr && (*scr < '0' || *scr > '9'); scr++)
                    switch (*scr) {
                    case 'L': inp.l = 1; break;
                    case 'R': inp.r = 1; break;
                    case 'U': inp.u = 1; break;
                    case 'D': inp.d = 1; break;
                    }
                while (*scr >= '0' && *scr <= '9')
                    scr_left = scr_left * 10 + (*scr++ - '0');
            }
        }
        if (scr_left)
            scr_left--;
        return;
    }
    /* The PCjr keyboard can lose a key-release, which would leave a
     * direction "held" forever. JRKEYS (jrkeys.h) repairs that: the
     * newest press wins, releasing up or down frees both (same for left
     * and right), a press on a stuck key is still seen, and a key held
     * longer than 2 s is ignored until it is pressed again. */
    {
        int b;

        jk_tick();
        inp.u = jk_down(k_u);
        inp.d = jk_down(k_d);
        inp.l = jk_down(k_l);
        inp.r = jk_down(k_r);
        b = jk_best(-1);
        inp.best = b < 0 ? 0 : b == k_u ? 1 : b == k_d ? 2 : b == k_l ? 3 : 4;
    }
}

static void tick(void)
{
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
        if (fresh_t && --fresh_t == 0)
            settle_fresh();
        if (msg && --gtimer <= 0)
            msg = NULL;
        step_player();
        if (gstate != G_PLAY) break;
        step_balls();
        if (gstate != G_PLAY) break;
        step_crawlers();
        break;
    case G_CLEAR:
        if (fresh_t && --fresh_t == 0)
            settle_fresh();
        if (--gtimer <= 0) {
            level++;
            new_level();
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

    fx_fill_rect(x - 4, y - 3, w + 8, 14, 0);
    fx_text(x, y, s, col);
}

static void draw_play(void)
{
    int i;

    for (i = 0; i < nball; i++)
        fx_sprite_draw(spr[S_BALL], (ball[i].x << 1) - 1, (ball[i].y << 1) - 1);
    for (i = 0; i < ncrawl; i++)
        fx_sprite_draw(spr[S_CRAWL], (crawl[i].x << 1) - 1, (crawl[i].y << 1) - 1);
    if (gstate != G_OVER && (gstate != G_READY || (ticks & 4)))
        fx_sprite_draw(spr[S_PLAYER], (px << 1) - 1, (py << 1) - 1);
    if (paused)
        draw_msg("PAUSED", 84, 15);
    else if (msg)
        draw_msg(msg, 84, 14);
}

static void draw_title(void)
{
    int i;

    for (i = 0; i < nball; i++)
        fx_sprite_draw(spr[S_BALL], (ball[i].x << 1) - 1, (ball[i].y << 1) - 1);
    if (ticks & 16)
        fx_text(36, 184, "PRESS SPACE", 15);
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

static void run_test(int title)
{
    int t;

    dbg_log("pages=%d irq=%d", fx_pages, fx_irq);
    if (title) {
        to_title();
        for (t = 0; t < 40; t++) {
            tick();
            frame();
        }
    } else {
        new_game();
        gstate = G_PLAY;
        msg = NULL;
        for (t = 0; !scr_done && t < 3000; t++) {
            tick();
            frame();
            if ((t % 25) == 0)
                dbg_log("t=%d x=%d y=%d sea=%d pct=%d st=%d", t, px, py,
                        insea, pct, gstate);
        }
        dbg_log("end x=%d y=%d pct=%d score=%ld lives=%d level=%d st=%d",
                px, py, pct, score, lives, level, gstate);
    }
    frame();
    fx_begin();
    fx_screenshot("SHOT.PPM");
}

int main(int argc, char **argv)
{
    unsigned long next, now;
    int steps, escwas = 0, pwas = 0, title = 0;

    if (argc > 1 && stricmp(argv[1], "/TEST") == 0) {
        testmode = 1;
        if (argc > 2) {
            if (strcmp(argv[2], "0") == 0)
                title = 1;
            else
                scr = argv[2];
        } else
            scr = "D20R10U20N20D30L20U30N40";
        remove("LOG.TXT");
    }
    if (fx_init(0)) {
        printf("Cannot start graphics (error %d)\n", gfx_err);
        return 1;
    }
    key_init();
    keys_setup();
    tmr_init();
    snd_init();
    bg = fx_alloc_buffer();
    if (!bg || !mkspr(S_PLAYER, a_player, 0) || !mkspr(S_BALL, a_ball, 0) ||
        !mkspr(S_CRAWL, a_crawl, -1)) {
        gfx_done();
        printf("Not enough memory\n");
        return 1;
    }
    fx_track(1);

    if (testmode)
        run_test(title);
    else {
        to_title();
        next = tmr_ticks();
        while (!quit) {
            jk_poll();              /* catches taps shorter than a tick */
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
                next = now;
            if (steps)
                frame();
        }
    }
    snd_quiet();
    gfx_done();
    return 0;
}
