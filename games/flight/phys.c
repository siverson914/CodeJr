/* JRFLIGHT flight model: a light trainer (think Cessna 152), integer only.
 *
 * State: airspeed V, flight-path angle gamma, angle of attack alpha, bank
 * phi, heading psi. The stick sets the angle of attack the airplane trims
 * to (as a real elevator does), lift = KL * V^2 * alpha, and:
 *   dV/dt     = thrust - drag - g sin(gamma)
 *   dgamma/dt = (L cos(phi) - g cos(gamma)) / V
 *   dpsi/dt   = L sin(phi) / (V cos(gamma))
 * Pitch attitude = gamma + alpha, so a slow airplane flies nose-high.
 * Past ~14 degrees alpha the wing stalls and lift falls away.
 * Angles are binary (65536 per circle) held as 16.16 longs. One step is
 * 1/8 second (DTSH). */
#include <stdlib.h>
#include <string.h>
#include "jrflight.h"

plane_t pl;
char msg[24];
int  msg_t;

/* Constants are 16.16 fixed point, written as integers (no floating point
 * anywhere, not even in constant expressions). */
#define DTSH       3            /* time step = 1/8 s = >> 3 */
#define FI(i)      ((fix)(i) << 16)
#define RAD2BAU    10430L       /* 65536 / (2 pi) */
#define G          642908L      /* 9.81 m/s^2 */
#define KL         6075L        /* 0.0927: lift per (m/s)^2 per radian */
#define ASTALL     15991L       /* 0.244 rad = 14 degrees */
#define CD0        29L          /* 0.00045 */
#define CDI        1966L        /* 0.03 */
#define F0_02      1311L
#define F0_03      1966L
#define F0_05      3277L
#define F0_2       13107L
#define F0_25      16384L
#define F0_5       32768L
#define F0_6       39322L
#define F1_2       78643L
#define F3_4       222822L
#define F3_5       229376L
#define GAMMAX     956956672L   /* 1.4 rad in binary angle << 16 */

static fix fsin(long ang) { return (fix)SIN14((unsigned)(ang >> 16)) << 2; }
static fix fcos(long ang) { return (fix)COS14((unsigned)(ang >> 16)) << 2; }
static long wrap(long a) { return (long)(int)(a >> 16) * 65536L + (a & 0xFFFFL); }
static fix clampf(fix v, fix lo, fix hi) { return v < lo ? lo : v > hi ? hi : v; }

void phys_reset(int scene)
{
    memset(&pl, 0, sizeof(pl));
    pl.trim = 0;
    switch (scene) {
    case 1:   /* cruising toward the town, mountains beyond */
        pl.pos.x = 900L << 8; pl.pos.y = 250L << 8; pl.pos.z = 700L << 8;
        pl.v = FI(55); pl.alpha = (355L << 16); pl.thr = 75;
        pl.hdg = 2184L << 16;     /* 012 degrees */
        break;
    case 2:   /* final approach to runway 36 at home */
        pl.pos.x = 0; pl.pos.y = 110L << 8; pl.pos.z = -1800L << 8;
        pl.v = FI(36); pl.thr = 30;
        pl.alpha = (834L << 16);
        pl.gam = -(522L << 16);
        break;
    case 3:   /* among the mountains */
        pl.pos.x = -2500L << 8; pl.pos.y = 1300L << 8; pl.pos.z = 4000L << 8;
        pl.v = FI(55); pl.alpha = (355L << 16); pl.thr = 80;
        pl.hdg = 0x0E000000L;     /* ~ 020 degrees */
        break;
    default:  /* lined up on runway 36 at home, engine idling */
        pl.pos.x = 0; pl.pos.y = 0; pl.pos.z = -470L << 8;
        pl.ground = 1;
        break;
    }
}

static void crash(const char *why)
{
    int i;
    pl.crashed = 24;      /* steps until reset */
    for (i = 0; why[i] && i < 23; i++) msg[i] = why[i];
    msg[i] = 0;
    msg_t = 20;
    pl.v = 0;
}

void phys_step(void)
{
    fix arad, aeff, vsq, lift, sphi, cphi, sgam, cgam, thrust, drag, dv;
    fix atgt, eff, vh;
    long dgam, dpsi;
    int was_ground = pl.ground;

    if (pl.crashed) {
        if (--pl.crashed == 0) phys_reset(0);
        return;
    }
    vsq = fix_mul(pl.v, pl.v);
    sphi = fsin(pl.roll); cphi = fcos(pl.roll);
    sgam = fsin(pl.gam); cgam = fcos(pl.gam);

    /* elevator: the angle of attack the airplane wants, reached quickly */
    atgt = F0_03 + (fix)pl.trim * 131L + fix_mul(pl.elev, F0_2);
    eff = pl.v > FI(30) ? FIX_ONE : pl.v / 30;
    if (pl.ground) {
        if (atgt < 0) atgt = 0;
        if (atgt > F0_2) atgt = F0_2;
        if (pl.v < FI(18)) atgt = 0;
    }
    arad = pl.alpha / RAD2BAU;
    arad += fix_mul(fix_mul(atgt - arad, FI(4)), eff) >> DTSH;
    /* stall: the nose drops whatever the stick says */
    pl.stall = 0;
    aeff = arad;
    if (arad > ASTALL) {
        aeff = ASTALL - fix_mul(arad - ASTALL, FI(2));
        if (aeff < F0_05) aeff = F0_05;
        pl.stall = 1;
        arad -= F0_02;
    } else if (arad < -F0_2) {
        aeff = -F0_2;
    }
    pl.alpha = arad * RAD2BAU;

    lift = fix_mul(fix_mul(KL, vsq), aeff);
    thrust = fix_mul((fix)pl.thr * 655L, F3_4 - fix_mul(pl.v, F0_03));
    drag = fix_mul(CD0 + fix_mul(CDI, fix_mul(arad, arad)), vsq);
    dv = thrust - drag - fix_mul(G, sgam);
    if (pl.ground) {
        fix fr = F0_25 + (pl.brakes ? F3_5 : 0);
        if (pl.v > 0 || dv > fr) dv -= fr;
        else dv = 0;
    }
    pl.v += dv >> DTSH;
    if (pl.v < 0) pl.v = 0;

    /* roll and turn */
    if (pl.ground) {
        pl.roll = 0;
        if (pl.v > F0_5) {
            fix k = pl.v > FI(8) ? FIX_ONE : pl.v / 8;
            pl.hdg += fix_mul(fix_mul(pl.ail, F0_6), k) * RAD2BAU >> DTSH;
        }
    } else {
        pl.roll += fix_mul(fix_mul(pl.ail, F1_2), eff) * RAD2BAU >> DTSH;
        pl.roll = wrap(pl.roll);
    }
    if (pl.v > FI(5)) {
        fix cg = cgam < F0_2 ? F0_2 : cgam;
        dgam = fix_div(fix_mul(lift, cphi) - fix_mul(G, cgam), pl.v);
        dpsi = fix_div(fix_mul(lift, sphi), fix_mul(pl.v, cg));
        dgam = clampf(dgam, -FI(2), FI(2));
        dpsi = clampf(dpsi, -FI(2), FI(2));
    } else {
        dgam = -fix_mul(G, cgam) / 5;
        dpsi = 0;
    }
    if (!pl.ground) pl.hdg += dpsi * RAD2BAU >> DTSH;
    pl.hdg = (pl.hdg & 0xFFFFFFFFL);
    pl.gam += dgam * RAD2BAU >> DTSH;
    if (pl.ground && pl.gam < 0) pl.gam = 0;
    pl.gam = clampf(pl.gam, -GAMMAX, GAMMAX);

    /* move */
    sgam = fsin(pl.gam); cgam = fcos(pl.gam);
    vh = fix_mul(pl.v, cgam);
    pl.vs = fix_mul(pl.v, sgam);
    pl.pos.x += ((vh >> 8) * SIN14((unsigned)(pl.hdg >> 16))) >> (14 + DTSH);
    pl.pos.z += ((vh >> 8) * COS14((unsigned)(pl.hdg >> 16))) >> (14 + DTSH);
    pl.pos.y += pl.vs >> (8 + DTSH);

    if (pl.pos.y <= 0) {
        pl.pos.y = 0;
        if (!was_ground) {
            int pitch = (int)((pl.gam + pl.alpha) >> 16);
            if (pl.vs < -F3_5) crash("CRASH: TOO HARD");
            else if (labs(pl.roll >> 16) > 3600) crash("CRASH: WINGTIP");
            else if (pitch < -1100) crash("CRASH: NOSE FIRST");
            else { msg_t = 15; strcpy(msg, "TOUCHDOWN"); }
        }
        pl.ground = 1;
        if (pl.gam < 0) pl.gam = 0;
        pl.vs = 0;
    } else {
        pl.ground = 0;
    }
    {
        long x = pl.pos.x >> 8, y = pl.pos.y >> 8, z = pl.pos.z >> 8;
        if (y < world_height(x, z)) crash("CRASH: TERRAIN");
        else if (y < 60 && world_hit(x, y, z)) crash("CRASH: OBSTACLE");
    }
    if (msg_t > 0) msg_t--;
}
