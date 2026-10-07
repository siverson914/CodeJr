/* JRFLIGHT: a 16-color filled-polygon flight simulator for the IBM PCjr.
 *
 *   JRFLIGHT              fly (see README.md for the keys)
 *   JRFLIGHT /DEMO:n      scripted autopilot flight for n frames, then exit
 *   JRFLIGHT /SCENE:s     start position: 0 runway, 1 cruise, 2 final
 *                         approach, 3 mountains
 *   JRFLIGHT /SHOT:a,b..  save SHOTnn.PPM at those frame numbers
 *   JRFLIGHT /VIEW:v      start view: 0 forward, 1 left, 2 right, 3 back
 *   JRFLIGHT /FPS         show frames per second in the corner
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <i86.h>
#include "jrflight.h"
#include "sintab.h"

#define K_1 0x02
#define K_2 0x03
#define K_3 0x04
#define K_4 0x05
#define K_P 0x19
#define K_R 0x13
#define K_B 0x30
#define K_LBRACKET 0x1A
#define K_RBRACKET 0x1B
#define K_F1 0x3B      /* F1-F3 (Fn+1..3 on the PCjr keyboard) */
#define K_F 0x21
#define FAST_SIZE 2    /* window size used by /FAST and the F key */

static int profile, bench, demo, demo_frames, scene, show_fps, nshots, shots[16];
static long frame;
static unsigned long prof[4];
static int page_shot;
static int need_reset, fxflags;

static void args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (*a == '/' || *a == '-') a++;
        strupr(a);
        if (!strncmp(a, "DEMO", 4)) { demo = 1; demo_frames = a[4] == ':' ? atoi(a + 5) : 300; }
        else if (!strncmp(a, "SCENE:", 6)) scene = atoi(a + 6);
        else if (!strncmp(a, "VIEW:", 5)) pl.view = atoi(a + 5) & 3;
        else if (!strcmp(a, "FPS")) show_fps = 1;
        else if (!strcmp(a, "LO")) r3_detail(1);
        else if (!strcmp(a, "COPY")) vdirect = 0;
        else if (!strncmp(a, "SIZE:", 5)) vsize = atoi(a + 5) % NSIZE;
        else if (!strcmp(a, "WIDE")) wide = 1;
        else if (!strcmp(a, "NARROW")) wide = 0;
        else if (!strcmp(a, "LD")) ldmode = 1;
        else if (!strcmp(a, "TEAR")) fxflags |= FX_TEAR;
        else if (!strcmp(a, "FAST")) { r3_detail(1); wide = 1; vsize = FAST_SIZE; }
        else if (!strcmp(a, "HI")) r3_detail(0);
        else if (!strcmp(a, "BENCH")) bench = 1;
        else if (!strcmp(a, "PROF")) profile = 1;
        else if (!strncmp(a, "SHOT:", 5)) {
            char *p = a + 5;
            while (*p && nshots < 16) {
                shots[nshots++] = atoi(p);
                while (*p && *p != ',') p++;
                if (*p) p++;
            }
        }
    }
}

/* ---- Stick and autopilot --------------------------------------------------- */
static fix toward(fix v, fix target, fix step)
{
    if (v < target) { v += step; if (v > target) v = target; }
    else if (v > target) { v -= step; if (v < target) v = target; }
    return v;
}

static fix clampf(fix v, fix lim)
{
    return v > lim ? lim : v < -lim ? -lim : v;
}

static int keys(unsigned dt)
{
    fix step = (fix)dt * 197L;        /* 3.0 per second of key hold */
    fix ty = 0, tx = 0;
    static const unsigned char edge_keys[] = {
        K_1, K_2, K_3, K_4, K_P, K_R, KEY_D, K_LBRACKET, K_RBRACKET,
        KEY_W, KEY_MINUS, KEY_PLUS, K_F1, K_F1 + 1, K_F1 + 2, K_F, 0
    };
    static unsigned char prev[128];
    unsigned char pressed[128];
    int i;
    for (i = 0; edge_keys[i]; i++) {
        int k = edge_keys[i];
        pressed[k] = key_down[k] && !prev[k];
        prev[k] = key_down[k];
    }

    if (key_down[KEY_ESC]) return 0;
    if (key_down[KEY_UP]) ty = -FIX_ONE;          /* push: nose down */
    if (key_down[KEY_DOWN]) ty = FIX_ONE;         /* pull: nose up   */
    if (key_down[KEY_LEFT]) tx = -FIX_ONE;
    if (key_down[KEY_RIGHT]) tx = FIX_ONE;
    pl.elev = toward(pl.elev, ty, step);
    pl.ail = toward(pl.ail, tx, step);
    if (key_down[KEY_A] && pl.thr < 100) pl.thr += (int)(dt / 20 + 1);
    if (key_down[KEY_Z] && pl.thr > 0) pl.thr -= (int)(dt / 20 + 1);
    if (pl.thr > 100) pl.thr = 100;
    if (pl.thr < 0) pl.thr = 0;
    pl.brakes = key_down[KEY_SPACE] || key_down[K_B];
    if (pressed[K_1]) pl.view = 0;
    if (pressed[K_2]) pl.view = 1;
    if (pressed[K_3]) pl.view = 2;
    if (pressed[K_4]) pl.view = 3;
    if (pressed[K_LBRACKET] && pl.trim > -20) pl.trim--;
    if (pressed[K_RBRACKET] && pl.trim < 20) pl.trim++;
    if (pressed[K_R]) phys_reset(0);
    if (pressed[KEY_D] && !ldmode) { lores = !lores; need_reset = 1; }
    if (pressed[KEY_W]) { wide = !wide; need_reset = 1; }
    if (pressed[KEY_MINUS] && vsize < NSIZE - 1) { vsize++; need_reset = 1; }
    if (pressed[KEY_PLUS] && vsize > 0) { vsize--; need_reset = 1; }
    for (i = 0; i < NSIZE; i++)
        if (pressed[K_F1 + i]) { vsize = i; need_reset = 1; }
    if (pressed[K_F]) {                 /* the fastest combination, or back */
        if (lores && wide && vsize == FAST_SIZE) { wide = 0; vsize = 0; }
        else { lores = 1; wide = 1; vsize = FAST_SIZE; }
        need_reset = 1;
    }
    if (pressed[K_P]) {
        strcpy(msg, "PAUSED - P TO GO ON");
        msg_t = 2;
        return 2;
    }
    return 1;
}

/* Holds a pitch attitude (binary angle units) and a bank, like a pilot:
 * proportional plus a slow integral (trim) on pitch. */
static fix ap_trim;

static void hold(int pitch, int bank)
{
    int p = (int)((pl.gam + pl.alpha) >> 16);
    int r = (int)(pl.roll >> 16);
    long err = (long)(pitch - p);
    ap_trim = clampf(ap_trim + err * 3L, FIX_ONE / 2);
    pl.elev = clampf(err * 40L + ap_trim, FIX_ONE);
    pl.ail = clampf((long)(bank - r) * 20L, FIX_ONE);
}

#define DEG(d) ((int)((d) * 182L))

/* Scripted flight for /DEMO: takeoff, climb, turn, look around. */
static void autopilot(void)
{
    long t = frame;
    pl.brakes = 0;
    if (scene == 0) {
        pl.thr = 100;
        if (pl.ground && pl.v < ((fix)27 << 16)) { pl.elev = 0; pl.ail = 0; return; }
        if (t < 110) hold(DEG(10), 0);
        else if (t < 170) { pl.thr = 85; hold(DEG(6), DEG(-25)); }
        else { pl.thr = 80; hold(DEG(4), 0); }
        /* look around after the climb-out */
        if (t >= 60 && t < 66) pl.view = 1;
        else if (t >= 66 && t < 72) pl.view = 2;
        else if (t >= 72 && t < 78) pl.view = 3;
        else pl.view = 0;
    } else if (scene == 2) {
        pl.thr = 25;
        if (pl.ground) { pl.thr = 0; pl.brakes = 1; pl.elev = 0; }
        else if (pl.pos.y > (5L << 8)) hold(DEG(-1), 0);
        else { pl.thr = 0; hold(DEG(3), 0); }
    } else if (scene == 1) {
        hold(DEG(3), t > 40 ? DEG(30) : 0);
    } else {
        hold(DEG(3), t > 15 ? DEG(-20) : 0);
    }
}

static void bench_run(void)
{
    {
        int i;
        extern int r3_flags;
        static const int fl[10] = { 0, 1, 2, 4, 7, 15, 17, 32, 81, 145 };
        static const char *nm[10] = { "full", "no sky", "no fill", "no objects", "cull only", "cull no sort", "cull loop only", "sky only", "empty", "cull4 only" };
        vec3l eye = pl.pos;
        int j;
        eye.y += 2L << 8;
        r3_camera(&eye, (unsigned)(pl.hdg >> 16), (int)((pl.gam + pl.alpha) >> 16),
                  (int)(pl.roll >> 16), pl.view);
        for (j = 0; j < 10; j++) {
            unsigned long a;
            r3_flags = fl[j];
            a = tmr_ms();
            for (i = 0; i < 10; i++) r3_draw();
            {
                extern int st_verts, st_faces, st_clipped, st_back;
                dbg_log("bench %s: %lu ms/frame (%d polys, %d objs, %d verts, %d faces, %d clipped, %d back)",
                        nm[j], (tmr_ms() - a) / 10, r3_polys, r3_objs, st_verts, st_faces, st_clipped, st_back);
            }
        }
        r3_flags = 0;
        {
            extern void calib(unsigned n);
#pragma aux calib = "CL1:" "nop" "loop CL1" parm [cx] modify exact [cx];
            unsigned long a = tmr_ms();
            for (i = 0; i < 4; i++) calib(50000u);
            dbg_log("calib: 400000 instructions in %lu ms", tmr_ms() - a);
        }
        {
            unsigned long a = tmr_ms();
            for (i = 0; i < 10; i++) r_present_view();
            dbg_log("bench present: %lu ms", (tmr_ms() - a) / 10);
            a = tmr_ms();
            for (i = 0; i < 10; i++) phys_step();
            dbg_log("bench phys_step: %lu ms", (tmr_ms() - a) / 10);
            a = tmr_ms();
            for (i = 0; i < 10; i++) panel_update(1);
            dbg_log("bench panel full redraw: %lu ms", (tmr_ms() - a) / 10);
            a = tmr_ms();
            for (i = 0; i < 10; i++) panel_update(0);
            dbg_log("bench panel no change: %lu ms", (tmr_ms() - a) / 10);
            a = tmr_ms();
            for (i = 0; i < 10; i++) { pl.roll += 1L << 24; panel_update(0); }
            dbg_log("bench attitude redraw: %lu ms", (tmr_ms() - a) / 10);
            a = tmr_ms();
            for (i = 0; i < 10; i++) { pl.hdg += 1L << 24; panel_update(0); }
            dbg_log("bench heading redraw: %lu ms", (tmr_ms() - a) / 10);
            a = tmr_ms();
            for (i = 0; i < 10; i++) { pl.v += 1L << 16; panel_update(0); }
            dbg_log("bench airspeed redraw: %lu ms", (tmr_ms() - a) / 10);
        }
    }
}

/* ---- Main ------------------------------------------------------------------ */
/* New window size or detail: redraw the frame around the window and put
 * the whole static screen on both pages. */
static void view_reset(void)
{
    r3_setview();
    r_frame();
    r_present_all();
    if (!ldmode) fx_sync_rows(0, SCR_H - 1);
    pdirty0 = 999; pdirty1 = -1;
}

static void fps_text(unsigned fps10)
{
    char b[12];
    sprintf(b, "%u.%u FPS", fps10 / 10, fps10 % 10);
    r_text_sh(wx0 + 2, wy0 + 1, b, 15);
}

int main(int argc, char **argv)
{
    unsigned long t0, tlast, now, tstart;
    unsigned dt, acc16 = 0, fps10 = 0, frames_win = 0;
    unsigned long twin;
    int run = 1, i, paused = 0;
    long total_polys = 0;

    r3_detail(1);              /* low detail is the default: twice the speed */
    args(argc, argv);
    {
        int v = pl.view;
        phys_reset(scene);
        pl.view = v;
    }
    if (ldmode) {
        /* Trixter's 160x100 mode: BIOS mode 8, then the gate array's address
         * mode bits (3DFh bits 7:6) = 00, so rows are linear and each is
         * shown twice. UNTESTED: DOSBox 0.74 can't display it. One page, no
         * flipping, the view drawn in RAM and copied. */
        if (gfx_init(GFX_160x200)) { printf("JRFLIGHT: can't set 160x200x16 mode\n"); return 1; }
        {
            unsigned char v = *(unsigned char far *)MK_FP(0x40, 0x8A);
            v &= 0x3F;
            *(unsigned char far *)MK_FP(0x40, 0x8A) = v;
            outp(0x3DF, v);
        }
        vdirect = 0;
        lores = 1;
    } else if (fx_init(fxflags)) { printf("JRFLIGHT: can't set 160x200x16 mode\n"); return 1; }
    r3_setview();
    key_init();
    snd_init();
    tmr_init();
    world_build();
    panel_init();
    panel_update(1);
    view_reset();
    if (!demo) { strcpy(msg, "A/Z THROTTLE  ARROWS FLY"); msg_t = 30; }
    dbg_log("JRFLIGHT start scene %d demo %d objects %d", scene, demo_frames, nobjs);

    if (bench && !demo) { bench_run(); run = 0; }
    tstart = t0 = tlast = twin = tmr_ms();
    while (run) {
        now = tmr_ms();
        dt = (unsigned)(now - tlast);
        tlast = now;
        if (dt > 500) dt = 500;

        if (demo) {
            autopilot();
            for (i = 0; i < 2; i++) phys_step();          /* 1/4 s per frame */
            if (frame >= demo_frames) { run = 0; if (bench) bench_run(); }
        } else {
            int k = keys(dt);
            if (!k) break;
            if (k == 2) paused = !paused;
            if (!paused) {
                acc16 += dt * 8;          /* physics runs in 1/8 s steps */
                while (acc16 >= 1000) { phys_step(); acc16 -= 1000; }
            } else acc16 = 0;
        }

        {
            vec3l eye = pl.pos;
            eye.y += 2L << 8;
            r3_camera(&eye, (unsigned)(pl.hdg >> 16), (int)((pl.gam + pl.alpha) >> 16),
                      (int)(pl.roll >> 16), pl.view);
        }
        if (!ldmode) {
            static unsigned last_seg;
            fx_begin();                 /* the hidden page is ours to draw */
            /* with two pages, last frame's target is the one on screen now */
            r_shown_seg = (fx_pages == 2 && last_seg && last_seg != fx_seg) ? last_seg : 0;
            last_seg = fx_seg;
        }
        if (need_reset) { view_reset(); need_reset = 0; }
        if (page_shot && !ldmode) {
            /* the page now on screen is last frame's: check what was shown */
            char name[16];
            sprintf(name, "PAGE%02d.PPM", page_shot);
            fx_screenshot(name);
            page_shot = 0;
        }
        if (profile) {
            unsigned long a = tmr_ms(), b, c, d;
            r3_draw();
            b = tmr_ms();
            view_overlay();
            if (show_fps) fps_text(fps10);
            r_present_view();
            c = tmr_ms();
            panel_update(0);
            d = tmr_ms();
            prof[0] += a - now; prof[1] += b - a; prof[2] += c - b; prof[3] += d - c;
        } else {
            r3_draw();
            view_overlay();
            if (show_fps) fps_text(fps10);
            r_present_view();
            panel_update(0);
        }
        /* the panel must look the same on both pages */
        if (pdirty1 >= 0) {
            if (!ldmode) fx_sync_rows(pdirty0, pdirty1);
            pdirty0 = 999; pdirty1 = -1;
        }
        total_polys += r3_polys;

        /* engine note follows the throttle, stall horn when stalled */
        {
            static int last_thr = -1, last_stall = -1, last_crash;
            if (!!pl.crashed != last_crash) {        /* crunch, then silence */
                last_crash = !!pl.crashed;
                snd_noise(6, last_crash ? 15 : 0);
                last_thr = -1;
            }
            if (pl.thr != last_thr) { snd_tone(0, 110 + pl.thr, pl.crashed ? 0 : 5); last_thr = pl.thr; }
            if (pl.stall != last_stall) { snd_tone(1, 1400, pl.stall && !pl.ground ? 8 : 0); last_stall = pl.stall; }
        }

        for (i = 0; i < nshots; i++)
            if (shots[i] == frame) {
                char name[16];
                sprintf(name, "SHOT%02d.PPM", i + 1);
                r_shot(name);
                page_shot = i + 1;
                dbg_log("shot %s frame %ld view %d alt %ld spd %ld", name, frame, pl.view,
                        pl.pos.y >> 8, pl.v >> 16);
            }
        if (!ldmode) fx_flip();         /* shown at the next retrace (IRQ5) */
        frame++;
        frames_win++;
        if (now - twin >= 2000) {
            fps10 = (unsigned)(frames_win * 10000UL / (now - twin));
            if (demo) dbg_log("frame %ld fps %u.%u polys %d objs %d alt %ld spd %ld",
                              frame, fps10 / 10, fps10 % 10, r3_polys, r3_objs,
                              pl.pos.y >> 8, pl.v >> 16);
            twin = now; frames_win = 0;
        }
    }
    now = tmr_ms();
    {
        unsigned long el = now - tstart;
        if (el == 0) el = 1;
        dbg_log("JRFLIGHT end: %ld frames in %lu ms = %lu.%lu fps, %ld polys/frame",
                frame, el, frame * 1000UL / el, (frame * 10000UL / el) % 10,
                frame ? total_polys / frame : 0L);
    }
    if (frame && profile) dbg_log("ms/frame: physics+input %lu, 3d %lu, present %lu, panel %lu",
                       prof[0] / frame, prof[1] / frame, prof[2] / frame, prof[3] / frame);
    gfx_done();
    key_done();
    snd_done();
    tmr_done();
    return 0;
}
