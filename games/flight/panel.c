/* JRFLIGHT instrument panel (rows 120-199): airspeed, attitude, altimeter,
 * heading tape, vertical speed and throttle. The static face is drawn
 * once and saved; each gauge is redrawn (and copied to video memory) only
 * when the value it shows changes. */
#include <stdio.h>
#include <stdlib.h>
#include "jrflight.h"

#define PY0 120
#define PH  (SCR_H - PY0)

static unsigned char far pbg[PH][ROWB];   /* saved static panel */
static int first = 1;                     /* everything needs drawing */

/* Gauge geometry (PCjr pixels are ~1.67x wider than tall). */
#define ASI_X 22
#define ASI_Y 148
#define ALT_X 137
#define ALT_Y 148
#define DRX 15
#define DRY 24
#define AI_X 80
#define AI_Y 150
#define ARX 17
#define ARY 27
#define TAPE_X0 54
#define TAPE_X1 106
#define TAPE_Y0 184
#define TAPE_Y1 196
#define VSI_X 155
#define THR_X 2
#define BAR_Y0 124
#define BAR_Y1 172

static int ahw[ARY * 2 + 1];   /* attitude ball half widths per row */
static int dhw[DRY * 2 + 1];   /* dial half widths */

static int isqrt(long v)
{
    long r = 0, b = 1L << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (int)r;
}

static void ellipse_tab(int *hw, int rx, int ry)
{
    int dy;
    for (dy = -ry; dy <= ry; dy++)
        hw[dy + ry] = (int)((long)rx * isqrt((long)ry * ry - (long)dy * dy) / ry);
}

static void ellipse_fill(int cx, int cy, const int *hw, int ry, unsigned col)
{
    int dy;
    for (dy = -ry; dy <= ry; dy++)
        r_span(cy + dy, cx - hw[dy + ry], cx + hw[dy + ry] + 1, col);
}

/* Point on a dial at binary angle a (0 = up, clockwise), radius pct %. */
static void dial_pt(int cx, int cy, int rx, int ry, unsigned a, int pct, int *x, int *y)
{
    *x = cx + mul14(SIN14(a), rx * pct / 100);
    *y = cy - mul14(COS14(a), ry * pct / 100);
}

static void dial_tick(int cx, int cy, unsigned a, int p0, int p1, int c)
{
    int x0, y0, x1, y1;
    dial_pt(cx, cy, DRX, DRY, a, p0, &x0, &y0);
    dial_pt(cx, cy, DRX, DRY, a, p1, &x1, &y1);
    r_line(x0, y0, x1, y1, c);
}

static void needle(int cx, int cy, unsigned a, int pct, int c)
{
    int x, y;
    dial_pt(cx, cy, DRX, DRY, a, pct, &x, &y);
    r_line(cx, cy, x, y, c);
}

static void restore(int x0, int y0, int x1, int y1)
{
    int y, b0 = x0 >> 1, n = (x1 >> 1) - b0 + 1;
    for (y = y0; y <= y1; y++) copyb(scr[y] + b0, pbg[y - PY0] + b0, n);
}

/* Needle bounding boxes: redraw and copy only what a needle touched. */
static int nb_old[2][4], nb_cur[2][4];

static void nbox_start(int *b) { b[0] = b[1] = 999; b[2] = b[3] = -999; }

static void nbox_pt(int *b, int x, int y)
{
    if (x < b[0]) b[0] = x;
    if (y < b[1]) b[1] = y;
    if (x > b[2]) b[2] = x;
    if (y > b[3]) b[3] = y;
}

static void nbox_add(int *b, int cx, int cy, unsigned a, int pct)
{
    int x, y;
    dial_pt(cx, cy, DRX, DRY, a, pct, &x, &y);
    nbox_pt(b, cx, cy);
    nbox_pt(b, x, y);
}

/* Restores the union of the old and new boxes; remembers the union. */
static void needle_update(int g, const int *b)
{
    int *o = nb_old[g], *u = nb_cur[g], i;
    for (i = 0; i < 4; i++) u[i] = first ? b[i] : (i < 2 ? (o[i] < b[i] ? o[i] : b[i]) : (o[i] > b[i] ? o[i] : b[i]));
    restore(u[0], u[1], u[2], u[3]);
    for (i = 0; i < 4; i++) o[i] = b[i];
}

static void nbox_present(int g)
{
    int *u = nb_cur[g];
    r_present_rect(u[0], u[1], u[2], u[3]);
}

#define ASI_ANG(kt) ((unsigned)((kt) * 364))     /* 2 degrees per knot */

void panel_init(void)
{
    int i, y;
    ellipse_tab(ahw, ARX, ARY);
    ellipse_tab(dhw, DRX, DRY);

    r_rect(0, PY0, SCR_W, PH, SOLID(8));
    r_rect(0, PY0, SCR_W, 2, SOLID(0));
    r_rect(0, PY0 + 2, SCR_W, 1, SOLID(7));

    /* dial faces with rims */
    for (i = 0; i < 2; i++) {
        int cx = i ? ALT_X : ASI_X, cy = i ? ALT_Y : ASI_Y;
        ellipse_tab(dhw, DRX + 1, DRY + 1);
        ellipse_fill(cx, cy, dhw, DRY + 1, SOLID(7));
        ellipse_tab(dhw, DRX, DRY);
        ellipse_fill(cx, cy, dhw, DRY, SOLID(0));
    }
    /* airspeed: ticks every 10 kt, green arc 50-120, yellow 130-150, red 160 */
    for (i = 0; i <= 160; i += 10) {
        int c = i >= 160 ? 12 : i >= 130 ? 14 : i >= 50 ? 10 : 15;
        dial_tick(ASI_X, ASI_Y, ASI_ANG(i), i % 50 ? 82 : 70, 98, c);
    }
    r_text(ASI_X - 3, ASI_Y + 10, "KT", 7);
    /* altimeter: ticks every 100 ft, numbers at 0, 5 */
    for (i = 0; i < 10; i++)
        dial_tick(ALT_X, ALT_Y, (unsigned)(i * 6554u), i % 5 ? 82 : 66, 98, 15);
    r_text(ALT_X - 3, ALT_Y + 10, "FT", 7);

    /* attitude ball rim */
    ellipse_tab(ahw, ARX + 1, ARY + 1);
    ellipse_fill(AI_X, AI_Y, ahw, ARY + 1, SOLID(7));
    ellipse_tab(ahw, ARX, ARY);

    /* heading tape window, VSI and throttle slots */
    r_rect(TAPE_X0 - 1, TAPE_Y0 - 1, TAPE_X1 - TAPE_X0 + 3, TAPE_Y1 - TAPE_Y0 + 3, SOLID(7));
    r_rect(VSI_X - 1, BAR_Y0 - 1, 6, BAR_Y1 - BAR_Y0 + 3, SOLID(7));
    r_rect(VSI_X, BAR_Y0, 4, BAR_Y1 - BAR_Y0 + 1, SOLID(0));
    r_text(VSI_X, BAR_Y1 + 3, "V", 7);
    r_text(VSI_X, BAR_Y1 + 9, "S", 7);
    r_rect(THR_X - 1, BAR_Y0 - 1, 5, BAR_Y1 - BAR_Y0 + 3, SOLID(7));
    r_text(THR_X - 1, BAR_Y1 + 3, "T", 7);
    r_text(THR_X - 1, BAR_Y1 + 9, "H", 7);

    for (y = 0; y < PH; y++) copyb(pbg[y], scr[y + PY0], ROWB);
}

/* Right-aligned decimal in a field of w characters (sprintf is slow). */
static char *fmt(char *buf, int v, int w, int plus)
{
    char *p = buf + w;
    int neg = v < 0;
    unsigned u = neg ? -v : v;
    *p = 0;
    do { *--p = (char)('0' + (int)(u % 10)); u /= 10; } while (u && p > buf);
    if ((neg || plus) && p > buf) *--p = neg ? '-' : '+';
    while (p > buf) *--p = ' ';
    return buf;
}

/* ---- Dynamic parts ------------------------------------------------------- */
static int last[10];

static int changed(int i, int v)
{
    if (!first && last[i] == v) return 0;
    last[i] = v;
    return 1;
}

/* A white line, only the pixels inside the attitude ball. */
static void ai_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0, sy = 1, err, e2;
    if (dy < 0) { dy = -dy; sy = -1; }
    err = dx - dy;
    for (;;) {
        int d = y0 - AI_Y, h;
        if (d >= -ARY && d <= ARY) {
            h = ahw[d + ARY];
            if (x0 - AI_X >= -h && x0 - AI_X <= h) {
                unsigned char far *b = &scr[y0][x0 >> 1];
                *b |= (x0 & 1) ? 0x0F : 0xF0;          /* white */
            }
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0++; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static void draw_ai(void)
{
    /* Same math as the outside horizon: v = (x-cx)*A + (cy-y)*U + F,
     * about one row per degree of pitch. */
    unsigned yaw = (unsigned)(pl.hdg >> 16);
    int pitch = (int)((pl.gam + pl.alpha) >> 16), roll = (int)(pl.roll >> 16);
    int sp = SIN14(pitch), cp = COS14(pitch), sr = SIN14(roll), cr = COS14(roll);
    /* world-up components of the body axes (yaw doesn't matter) */
    int ry = -mul14(cp, sr);           /* right.y */
    int uy = mul14(cp, cr);            /* up.y    */
    int fy = sp;                       /* fwd.y   */
    int A = (int)(lmul(ry, 5) / 3);       /* PCjr pixel aspect */
    long F = lmul(fy, 57);
    int dy;
    (void)yaw;

    /* v(x, dy) = (x - AI_X)*A + F - dy*uy; the sky/ground edge (v = 0)
     * crosses row dy at x = AI_X + (dy*uy - F)/A: linear in dy, so step it. */
    {
        unsigned char far *row = scr[AI_Y - ARY];
        long c = 0, dc = 0;
        int steep = A > 200 || A < -200;
        if (steep) {
            c = ((long)AI_X << 16) + 0x8000L + (fix_div(-F - lmul(ARY, uy), (long)A));
            dc = fix_div((long)uy, (long)A);
        }
        for (dy = -ARY; dy <= ARY; dy++, row += ROWB) {
            int hw = ahw[dy + ARY], x0 = AI_X - hw, x1 = AI_X + hw + 1, x;
            unsigned char sky = 0x99, gnd = 0x66;     /* light blue, brown */
            if (!steep) {
                long B = F - lmul(dy, uy);
                span_asm(row, x0, x1, B > 0 ? sky : gnd);
                continue;
            }
            x = (int)(c >> 16);
            c += dc;
            if (x < x0) x = x0;
            if (x > x1) x = x1;
            if (A > 0) {                   /* v grows to the right: ground left */
                if (x > x0) span_asm(row, x0, x, gnd);
                if (x1 > x) span_asm(row, x, x1, sky);
            } else {
                if (x > x0) span_asm(row, x0, x, sky);
                if (x1 > x) span_asm(row, x, x1, gnd);
            }
        }
    }
    /* horizon line and pitch ladder (+-10 degrees) */
    if (uy > 3000 || uy < -3000) {
        int k;
        for (k = -1; k <= 1; k++) {
            long off = (long)k * 10 * 16384L;      /* 10 deg ~ 10 rows */
            int w = k == 0 ? ARX : 6;
            int y0 = AI_Y + ldivi(lmul(-w, A) + F - off, uy);
            int y1 = AI_Y + ldivi(lmul(w, A) + F - off, uy);
            if ((y0 < AI_Y - ARY && y1 < AI_Y - ARY) || (y0 > AI_Y + ARY && y1 > AI_Y + ARY))
                continue;
            ai_line(AI_X - w, y0, AI_X + w, y1);
        }
    }
    /* fixed aircraft symbol */
    r_line(AI_X - 11, AI_Y, AI_X - 4, AI_Y, 14);
    r_line(AI_X + 4, AI_Y, AI_X + 11, AI_Y, 14);
    r_line(AI_X - 4, AI_Y, AI_X - 2, AI_Y + 2, 14);
    r_line(AI_X + 4, AI_Y, AI_X + 2, AI_Y + 2, 14);
    r_pset(AI_X, AI_Y, 14);
    /* bank marks at the top: 0, 30, 60 each side; pointer shows roll */
    {
        int k, x0, y0;
        for (k = -2; k <= 2; k++) {
            dial_pt(AI_X, AI_Y, ARX, ARY, (unsigned)(k * 5461), 100, &x0, &y0);
            r_pset(x0, y0, 15);
        }
        dial_pt(AI_X, AI_Y, ARX, ARY, (unsigned)(-roll), 88, &x0, &y0);
        r_pset(x0, y0, 14);
        r_pset(x0, y0 + 1, 14);
    }
}

static void draw_tape(int hdg)
{
    int d, x;
    static const char *lab[12] = { "N", "3", "6", "E", "12", "15", "S", "21", "24", "W", "30", "33" };
    r_rect(TAPE_X0, TAPE_Y0, TAPE_X1 - TAPE_X0 + 1, TAPE_Y1 - TAPE_Y0 + 1, SOLID(0));
    for (d = (hdg / 10 - 3) * 10; d <= hdg + 30; d += 10) {
        int dd = (d + 360) % 360;
        x = AI_X + (d - hdg) * 9 / 10;
        if (x < TAPE_X0 || x > TAPE_X1) continue;
        r_line(x, TAPE_Y1 - (dd % 30 ? 1 : 3), x, TAPE_Y1, 15);
        if (dd % 30 == 0) {
            const char *s = lab[dd / 30];
            int tx = x - r_textw(s) / 2;
            if (tx >= TAPE_X0 && tx + r_textw(s) <= TAPE_X1)
                r_text(tx, TAPE_Y0 + 2, s, dd % 90 ? 15 : 14);
        }
    }
    r_pset(AI_X, TAPE_Y0, 12);
    r_pset(AI_X - 1, TAPE_Y0, 12);
    r_pset(AI_X + 1, TAPE_Y0, 12);
    r_pset(AI_X, TAPE_Y0 + 1, 12);
}

void panel_update(int force)
{
    char buf[12];
    /* unit conversions by multiply and shift (long division is slow here) */
    int kt = (int)(((pl.v >> 8) * 497L) >> 16);             /* m/s -> knots */
    long ft = ((pl.pos.y >> 8) * 3359L) >> 10;              /* m -> feet */
    int fpm = (int)(((pl.vs >> 8) * 197L) >> 8);            /* m/s -> ft/min */
    int hdg = (int)(((pl.hdg >> 16) & 0xFFFFL) * 360L >> 16);
    int pitch = (int)((pl.gam + pl.alpha) >> 23), roll = (int)(pl.roll >> 23);  /* 0.7 deg steps */
    int dr;

    static int turn;
    int slot;
    if (force) first = 1;
    if (kt < 0) kt = 0;
    if (ft < 0) ft = 0;
    /* The attitude ball updates every frame; the other gauges take turns
     * (one per frame) so a busy frame never redraws the whole panel. */
    turn = (turn + 1) & 3;
    slot = first ? -1 : turn;

    /* airspeed: only the needle's old and new bounding boxes are touched */
    if ((slot < 0 || slot == 0) && changed(0, kt)) {
        int b[4];
        unsigned a = ASI_ANG(kt > 175 ? 175 : kt);
        nbox_start(b);
        nbox_add(b, ASI_X, ASI_Y, a, 88);
        needle_update(0, b);
        needle(ASI_X, ASI_Y, a, 88, 15);
        nbox_present(0);
        restore(ASI_X - 6, ASI_Y + DRY + 6, ASI_X + 6, ASI_Y + DRY + 10);
        r_text(ASI_X - 6, ASI_Y + DRY + 6, fmt(buf, kt, 3, 0), 15);
        r_present_rect(ASI_X - 6, ASI_Y + DRY + 6, ASI_X + 6, ASI_Y + DRY + 10);
    }
    /* altimeter: long needle 1000 ft/turn, short 10000 ft/turn */
    if (ft > 32000) ft = 32000;
    if ((slot < 0 || slot == 1) && changed(1, (int)ft / 10)) {
        int b[4], f = (int)ft;
        unsigned a1 = (unsigned)(((long)(f % 10000) * 26843L) >> 12);   /* *6.5536 */
        unsigned a2 = (unsigned)(((long)(f % 1000) * 268435L) >> 12);   /* *65.536 */
        nbox_start(b);
        nbox_add(b, ALT_X, ALT_Y, a1, 50);
        nbox_add(b, ALT_X, ALT_Y, a2, 90);
        needle_update(1, b);
        needle(ALT_X, ALT_Y, a1, 50, 14);
        needle(ALT_X, ALT_Y, a2, 90, 15);
        nbox_present(1);
        restore(ALT_X - 10, ALT_Y + DRY + 6, ALT_X + 10, ALT_Y + DRY + 10);
        r_text(ALT_X - 10, ALT_Y + DRY + 6, fmt(buf, f, 5, 0), 15);
        r_present_rect(ALT_X - 10, ALT_Y + DRY + 6, ALT_X + 10, ALT_Y + DRY + 10);
    }
    /* vertical speed bar, +-2000 fpm, and readout */
    if (fpm > 2000) fpm = 2000;
    if (fpm < -2000) fpm = -2000;
    if ((slot < 0 || slot == 2) && changed(2, fpm / 50)) {
        int mid = (BAR_Y0 + BAR_Y1) / 2, h = fpm * 24 / 2000;
        r_rect(VSI_X, BAR_Y0, 4, BAR_Y1 - BAR_Y0 + 1, SOLID(0));
        r_rect(VSI_X, mid, 4, 1, SOLID(7));
        if (h > 0) r_rect(VSI_X, mid - h, 4, h, SOLID(10));
        if (h < 0) r_rect(VSI_X, mid + 1, 4, -h, SOLID(12));
        restore(ALT_X - 18, 188, SCR_W - 1, 193);
        fmt(buf, fpm / 10 * 10, 5, 1);
        r_text(SCR_W - 1 - r_textw(buf), 189, buf, fpm < -800 ? 12 : 15);
        r_present_rect(ALT_X - 18, BAR_Y0, SCR_W - 1, 193);
    }
    /* throttle */
    if (changed(3, pl.thr)) {
        int h = pl.thr * (BAR_Y1 - BAR_Y0 + 1) / 100;
        r_rect(THR_X, BAR_Y0, 3, BAR_Y1 - BAR_Y0 + 1, SOLID(0));
        if (h) r_rect(THR_X, BAR_Y1 + 1 - h, 3, h, SOLID(14));
        r_present_rect(THR_X, BAR_Y0, THR_X + 3, BAR_Y1);
    }
    /* attitude */
    dr = (pitch & 0xFF) | (roll << 8);
    if (changed(4, dr)) {
        draw_ai();
        r_present_rect(AI_X - ARX, AI_Y - ARY, AI_X + ARX, AI_Y + ARY);
    }
    /* heading */
    if ((slot < 0 || slot == 3) && changed(5, hdg)) {
        draw_tape(hdg);
        r_present_rect(TAPE_X0, TAPE_Y0, TAPE_X1, TAPE_Y1);
    }
    /* status line: brakes, view, crash */
    dr = pl.brakes | (pl.view << 1) | (pl.stall << 3) | (pl.ground << 4);
    if (changed(6, dr)) {
        static const char *vn[4] = { "FWD", "LEFT", "RIGHT", "BACK" };
        restore(0, 192, 44, 199);
        restore(ALT_X - 18, 194, SCR_W - 1, 199);
        if (pl.brakes) r_text(2, 194, "BRAKE", 12);
        else if (pl.ground) r_text(2, 194, "GND", 10);
        r_text(SCR_W - 1 - r_textw(vn[pl.view]), 194, vn[pl.view], 11);
        r_present_rect(0, 192, 44, 199);
        r_present_rect(ALT_X - 18, 194, SCR_W - 1, 199);
    }
    first = 0;
}

/* Messages over the 3D view (redrawn every frame anyway). */
void view_overlay(void)
{
    if (msg_t > 0 && msg[0]) {
        int w = r_textw(msg);
        r_text_sh(CX - w / 2, wy0 + (lores ? 3 : 6), msg, (msg_t & 2) ? 14 : 12);
    }
    if (pl.stall && !pl.ground) r_text_sh(CX - 9, wy0 + (lores ? 9 : 16), "STALL", 12);
}
