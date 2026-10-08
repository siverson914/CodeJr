/* MANDEL.C - Mandelbrot zoom demo for the IBM PCjr.
 *
 * 160x200 in 16 colors. The inner loop is 8088 assembly (MLOOP.ASM) in
 * 4.12 fixed point with table squares instead of multiplies. Each frame
 * is drawn progressively, 8x8 blocks first, then 4x4, 2x2 and single
 * pixels; a block whose four corners agree is filled without computing
 * its inside. The main cardioid and the big bulb are detected outright.
 * Palette cycling runs from the retrace interrupt while it works.
 *
 * Keys: SPACE next zoom step now, R back to the full view, 1-4 pick a
 * target, ESC quit. MANDEL /TEST [target] renders three frames of a
 * target, saves SHOT1-3.PPM and logs the timings to LOG.TXT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <i86.h>
#include "jrgfx.h"

int __cdecl mandel_pt(int cx, int cy, int maxit, unsigned t1, unsigned t2);

#define W 160
#define H 200
#define XS0 5368709L            /* 0.02 in 4.28: the full view, x step */
#define ZMAX 24                 /* deepest zoom before moving on */
#define KEY_R 0x13
#define KEY_1 0x02

typedef struct { const char *name; long cx, cy; } Target;
static const Target targets[] = {       /* centers in 4.28 fixed point */
    { "SEAHORSE VALLEY", -200064945L,  30252676L },
    { "ELEPHANT VALLEY",   73819750L,   1798518L },
    { "THE NEEDLE",      -469493613L,         0L },
    { "TOP SPIRALS",      -27138825L, 256704827L },
};
#define NT 4

/* logical colors 1-13 cycle, 14 is the text box, 15 white */
static const unsigned char pal[16] = {
    0, 1, 9, 3, 11, 15, 14, 12, 4, 5, 13, 7, 2, 10, 8, 15 };

static unsigned t1seg, t2seg;
static unsigned char far *itmap;        /* (H+1) rows of (W+1) counts */
static unsigned rowbase[H + 1];
#define IT(y, x) itmap[rowbase[y] + (x)]
static int xt[W + 1], yt[H + 1];        /* c for each column and row, 4.12 */
static unsigned long iters;
static int testmode, quit, action;
static char line1[24], line2[24];
static unsigned char kprev[128];

/* ---- Tables ------------------------------------------------------------ */

static unsigned alloc_seg64k(void)
{
    void __huge *p = halloc(4097L, 16);

    if (!p)
        return 0;
    return FP_SEG(p) + ((FP_OFF(p) + 15) >> 4);
}

static void fill_tables(void)
{
    long i, v;

    for (i = 0; i < 32768L; i++) {
        v = (i * i + 2048) >> 12;
        if (v > 32767L) v = 32767L;
        *(unsigned far *)MK_FP(t1seg, (unsigned)(i * 2)) = (unsigned)v;
    }
    for (i = -16384L; i < 16384L; i++) {
        v = (i * i + 8192) >> 14;
        *(unsigned far *)MK_FP(t2seg, (unsigned)(i * 2)) = (unsigned)v;
    }
    for (i = 0; i <= H; i++)
        rowbase[(int)i] = (unsigned)(i * (W + 1));
}

/* Inside the main cardioid or the period-2 bulb? Conservative, and
 * cheap for points outside their bounding boxes. */
static int inside(int x, int y)
{
    long xl = x, yl = y, y2, q;

    if (y < -2700 || y > 2700 || x < -5120 || x > 1024)
        return 0;
    y2 = yl * yl;
    if (x < -3072) {                    /* bulb: (x+1)^2 + y^2 < 1/16 */
        if (y < -1024 || y > 1024)
            return 0;
        return (xl + 4096) * (xl + 4096) + y2 < (1L << 20);
    }
    q = ((xl - 1024) * (xl - 1024) + y2 + 4095) >> 12;
    return q * (q + xl - 1024) < (y2 >> 2);
}

/* ---- Screen ------------------------------------------------------------ */

static int poll_keys(void)
{
    int i;

    if (testmode)
        return 0;
    if (key_down[KEY_ESC])
        quit = 1;
    if (key_down[KEY_SPACE] && !kprev[KEY_SPACE])
        action = 1;
    if (key_down[KEY_R] && !kprev[KEY_R])
        action = 2;
    for (i = 0; i < NT; i++)
        if (key_down[KEY_1 + i] && !kprev[KEY_1 + i])
            action = 3 + i;
    kprev[KEY_SPACE] = key_down[KEY_SPACE];
    kprev[KEY_R] = key_down[KEY_R];
    for (i = 0; i < NT; i++)
        kprev[KEY_1 + i] = key_down[KEY_1 + i];
    return quit || action;
}

static void status_draw(void)
{
    fx_fill_rect(0, 184, 160, 16, FX_SOLID2(14));
    fx_text(0, 184, line1, 15);
    fx_text(0, 192, line2, 15);
    fx_dirty(0, 184, 159, 199);
}

static void flush(int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > 199) y1 = 199;
    if (y1 >= 184)
        status_draw();
    fx_dirty(0, y0, 159, y1);
    fx_flip();
}

static int wait_ms(unsigned long ms)
{
    unsigned long t0 = tmr_ms();

    if (testmode)
        return 0;
    while (tmr_ms() - t0 < ms)
        if (poll_keys())
            return 1;
    return 0;
}

/* ---- Rendering --------------------------------------------------------- */

static int color_of(int n, int maxit)
{
    return n >= maxit ? 0 : 1 + n % 13;
}

static void put(int x, int y, int s, int c)
{
    if (s == 1)
        fx_pset(x, y, c);
    else
        fx_fill_rect(x, y, s, s, FX_SOLID2(c));
}

/* Returns 1 if a key interrupted it. */
static int render(long cx28, long cy28, long xs28, int maxit)
{
    long ys28 = xs28 * 3 / 5;
    long x0 = cx28 - xs28 * 80, y0 = cy28 + ys28 * 100;
    int s, m, bx, by, n, v, cx0, cy0, c;

    for (bx = 0; bx <= W; bx++)
        xt[bx] = (int)((x0 + xs28 * bx + 32768L) >> 16);
    for (by = 0; by <= H; by++)
        yt[by] = (int)((y0 - ys28 * by + 32768L) >> 16);
    for (s = 8; s >= 1; s >>= 1) {
        m = s * 2;
        for (by = 0; by <= H; by += s) {
            for (bx = 0; bx <= W; bx += s) {
                if (s < 8) {
                    if (!(bx & (m - 1)) && !(by & (m - 1)))
                        continue;           /* done in the last pass */
                    cx0 = bx & ~(m - 1);
                    cy0 = by & ~(m - 1);
                    if (cx0 + m <= W && cy0 + m <= H) {
                        v = IT(cy0, cx0);
                        if (IT(cy0, cx0 + m) == v && IT(cy0 + m, cx0) == v &&
                            IT(cy0 + m, cx0 + m) == v) {
                            IT(by, bx) = (unsigned char)v;
                            put(bx, by, s, color_of(v, maxit));
                            continue;
                        }
                    }
                }
                if (inside(xt[bx], yt[by]))
                    n = maxit;
                else {
                    n = mandel_pt(xt[bx], yt[by], maxit, t1seg, t2seg);
                    iters += n;
                }
                IT(by, bx) = (unsigned char)n;
                c = color_of(n, maxit);
                put(bx, by, s, c);
            }
            if (((by + s) & 7) == 0) {
                flush(by + s - 8, by + s - 1);
                if (poll_keys())
                    return 1;
            }
        }
    }
    return 0;
}

static void set_status(const Target *t, long xs, long ms, int maxit)
{
    long z = XS0 * 10 / xs;

    strcpy(line1, t->name);
    if (ms < 0)
        sprintf(line2, "ZOOM %ld.%ldX  %d IT", z / 10, z % 10, maxit);
    else
        sprintf(line2, "ZOOM %ld.%ldX  %ld.%ldS", z / 10, z % 10,
                ms / 1000, (ms % 1000) / 100);
}

/* ---- Main -------------------------------------------------------------- */

/* Plain C reference in 4.12, to check the assembly against. */
static int ref_pt(int cx, int cy, int maxit)
{
    long x = cx, y = cy, x2, y2;
    int n;

    for (n = 0; n < maxit; n++) {
        x2 = (x * x) >> 12;
        y2 = (y * y) >> 12;
        if (x2 + y2 > 16384)
            return n;
        y = ((x * y) >> 11) + cy;
        x = x2 - y2 + cx;
    }
    return maxit;
}

static void run_test(int tgt)
{
    static const long zooms[3] = { XS0, XS0 / 8, XS0 / 24 };
    static const int its[3] = { 32, 64, 96 };
    static const int pts[6][2] = { {-2048,0}, {-4096,1024}, {1024,2048},
                                   {-3000,2400}, {-5900,0}, {-300,2600} };
    unsigned long t0;
    char name[16];
    int i;

    dbg_log("pages=%d irq=%d", fx_pages, fx_irq);
    for (i = 0; i < 6; i++)
        dbg_log("c=(%d,%d)/4096: asm %d  ref %d", pts[i][0], pts[i][1],
                mandel_pt(pts[i][0], pts[i][1], 100, t1seg, t2seg),
                ref_pt(pts[i][0], pts[i][1], 100));
    for (i = 0; i < 3; i++) {
        iters = 0;
        set_status(&targets[tgt], zooms[i], -1, its[i]);
        t0 = tmr_ms();
        render(targets[tgt].cx, targets[tgt].cy, zooms[i], its[i]);
        t0 = tmr_ms() - t0;
        set_status(&targets[tgt], zooms[i], (long)t0, its[i]);
        flush(184, 199);
        dbg_log("frame %d: zoom %ldx maxit %d: %lu ms, %lu iterations",
                i + 1, XS0 / zooms[i], its[i], t0, iters);
        sprintf(name, "SHOT%d.PPM", i + 1);
        fx_screenshot(name);
    }
}

int main(int argc, char **argv)
{
    const Target *t;
    long xs = XS0;
    unsigned long t0;
    int tgt = 0, step = 0, maxit, r, ttgt = 0;

    if (argc > 1 && stricmp(argv[1], "/TEST") == 0) {
        testmode = 1;
        if (argc > 2)
            ttgt = atoi(argv[2]) - 1;
        if (ttgt < 0 || ttgt >= NT)
            ttgt = 0;
        remove("LOG.TXT");
    }
    if (fx_init(FX_BUFFER | FX_TEAR)) {
        printf("Cannot start graphics (error %d)\n", gfx_err);
        return 1;
    }
    key_init();
    tmr_init();
    t1seg = alloc_seg64k();
    t2seg = alloc_seg64k();
    itmap = (unsigned char far *)_fmalloc((W + 1) * (H + 1));
    if (!t1seg || !t2seg || !itmap) {
        gfx_done();
        printf("Not enough memory (needs about 200K free)\n");
        return 1;
    }
    strcpy(line1, "MANDELBROT JUNIOR");
    strcpy(line2, "BUILDING TABLES...");
    flush(184, 199);
    fill_tables();
    fx_pal_set(pal);
    if (!testmode)
        fx_pal_anim(0, 1, 13, 4, 1);

    if (testmode) {
        run_test(ttgt);
    } else {
        while (!quit) {
            t = &targets[tgt];
            maxit = 32 + step * 8;
            set_status(t, xs, -1, maxit);
            t0 = tmr_ms();
            r = render(t->cx, t->cy, xs, maxit);
            if (!r) {
                set_status(t, xs, (long)(tmr_ms() - t0), maxit);
                flush(184, 199);
                wait_ms(2000);
            }
            if (quit)
                break;
            if (action == 2) {
                xs = XS0; step = 0;
            } else if (action >= 3) {
                tgt = action - 3; xs = XS0; step = 0;
            } else if (xs <= XS0 / ZMAX) {
                if (!action)
                    wait_ms(2500);
                tgt = (tgt + 1) % NT; xs = XS0; step = 0;
            } else {
                xs = xs * 2 / 3; step++;
            }
            action = 0;
        }
    }
    gfx_done();
    return 0;
}
