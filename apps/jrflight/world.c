/* JRFLIGHT scenery: two airports, a town, a lake, farm fields, a road and
 * a mountain range. Built at startup with integer math only.
 *
 * Axes: x = east, y = up, z = north, meters. Faces are listed clockwise
 * as seen from outside (that's how the back-face test knows the front). */
#include <stdlib.h>
#include "jrflight.h"

#define MAXOBJ 96
obj_t far *objs;
int nobjs;

static obj_t far *cur;
group_t groups[8];
int ngroups;

/* Groups let the renderer cull a whole town or airport with one test. */
static void group_begin(void)
{
    groups[ngroups].first = nobjs;
}

static void group_end(void)
{
    group_t *g = &groups[ngroups];
    long x0 = 0x7FFFFFFFL, x1 = -x0, z0 = x0, z1 = -x0, h = 0;
    int i;
    for (i = g->first; i < nobjs; i++) {
        long r = objs[i].radius;
        if (objs[i].x - r < x0) x0 = objs[i].x - r;
        if (objs[i].x + r > x1) x1 = objs[i].x + r;
        if (objs[i].z - r < z0) z0 = objs[i].z - r;
        if (objs[i].z + r > z1) z1 = objs[i].z + r;
        if (objs[i].y + r > h) h = objs[i].y + r;
    }
    g->n = nobjs - g->first;
    g->x = (x0 + x1) / 2; g->z = (z0 + z1) / 2; g->y = 0;
    /* L1-style bound like obj_end: generous but cheap */
    g->r = (int)((x1 - x0) / 2 + (z1 - z0) / 2 + h);
    ngroups++;
}

/* Shading: sun from the south-east, high. Each material has three shades. */
typedef struct { unsigned lit, mid, dark, top; } mat_t;

static const mat_t M_WHITE  = { DITH(15, 7), SOLID(7), DITH(7, 8), SOLID(15) };
static const mat_t M_TAN    = { DITH(14, 7), DITH(6, 7), DITH(6, 8), DITH(14, 15) };
static const mat_t M_BRICK  = { DITH(12, 4), SOLID(4), DITH(4, 8), DITH(12, 4) };
static const mat_t M_GRAY   = { SOLID(7), DITH(7, 8), SOLID(8), DITH(7, 15) };
static const mat_t M_BLUE   = { DITH(9, 3), SOLID(3), DITH(1, 8), DITH(11, 3) };
static const mat_t M_HANGAR = { DITH(7, 15), DITH(7, 8), SOLID(8), SOLID(7) };

static void obj_begin(long x, long y, long z, int flat)
{
    cur = &objs[nobjs++];
    cur->x = x; cur->y = y; cur->z = z;
    cur->flat = (unsigned char)flat;
    cur->nv = cur->nf = 0;
    cur->radius = 0;
    cur->detail = 0;
    cur->bx0 = cur->bx1 = cur->by1 = cur->bz0 = cur->bz1 = 0;
}

static int vtx(int x, int y, int z)       /* meters, relative to object */
{
    int i = cur->nv++;
    cur->vx[i] = x * 8; cur->vy[i] = y * 8; cur->vz[i] = z * 8;
    return i;
}

static void face(int n, const int *v, unsigned col)
{
    face_t far *f = &cur->f[cur->nf++];
    int i;
    f->n = (unsigned char)n;
    for (i = 0; i < n; i++) f->v[i] = (unsigned char)v[i];
    f->col = col;
}

static void quad(int a, int b, int c, int d, unsigned col)
{
    int v[4];
    v[0] = a; v[1] = b; v[2] = c; v[3] = d;
    face(4, v, col);
}

static void tri(int a, int b, int c, unsigned col)
{
    int v[3];
    v[0] = a; v[1] = b; v[2] = c;
    face(3, v, col);
}

static void obj_end(void)
{
    long r = 0;
    int i;
    for (i = 0; i < cur->nf; i++) {
        face_t far *f = &cur->f[i];
        long ax = cur->vx[f->v[1]] - (long)cur->vx[f->v[0]], ay = cur->vy[f->v[1]] - (long)cur->vy[f->v[0]];
        long az = cur->vz[f->v[1]] - (long)cur->vz[f->v[0]], bx = cur->vx[f->v[2]] - (long)cur->vx[f->v[0]];
        long by = cur->vy[f->v[2]] - (long)cur->vy[f->v[0]], bz = cur->vz[f->v[2]] - (long)cur->vz[f->v[0]];
        long nx, ny, nz;
        /* scale the edges down first so the cross product fits 32 bits */
        while (labs(ax) > 16000 || labs(ay) > 16000 || labs(az) > 16000) { ax /= 2; ay /= 2; az /= 2; }
        while (labs(bx) > 16000 || labs(by) > 16000 || labs(bz) > 16000) { bx /= 2; by /= 2; bz /= 2; }
        nx = ay * bz - az * by; ny = az * bx - ax * bz; nz = ax * by - ay * bx;
        while (labs(nx) > 16000 || labs(ny) > 16000 || labs(nz) > 16000) { nx /= 2; ny /= 2; nz /= 2; }
        f->nx = (int)nx; f->ny = (int)ny; f->nz = (int)nz;
    }
    for (i = 0; i < cur->nv; i++) {
        long ax = labs(cur->vx[i]), ay = labs(cur->vy[i]), az = labs(cur->vz[i]);
        long m = ax + ay + az;        /* cheap upper bound of the length */
        if (m > r) r = m;
    }
    cur->radius = (int)(r / 8) + 1;
}

/* ---- Ground decals ------------------------------------------------------ */
static void rect_decal(int x0, int z0, int x1, int z1, unsigned col)
{
    quad(vtx(x0, 0, z0), vtx(x1, 0, z0), vtx(x1, 0, z1), vtx(x0, 0, z1), col);
}

static void field(int x0, int z0, int x1, int z1, unsigned col)
{
    int cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    obj_begin(cx, 0, cz, 1);
    rect_decal(x0 - cx, z0 - cz, x1 - cx, z1 - cz, col);
    obj_end();
}

/* A ground strip (river, road) of width w from (x0,z0) to (x1,z1). */
static void strip(int x0, int z0, int x1, int z1, int w, unsigned col)
{
    int cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    long dx = x1 - x0, dz = z1 - z0;
    long ax = labs(dx), az = labs(dz);
    long len = ax > az ? ax + az / 2 : az + ax / 2;   /* roughly the length */
    int px = (int)(-dz * w / 2 / len), pz = (int)(dx * w / 2 / len);
    obj_begin(cx, 0, cz, 1);
    quad(vtx(x0 - cx + px, 0, z0 - cz + pz), vtx(x1 - cx + px, 0, z1 - cz + pz),
         vtx(x1 - cx - px, 0, z1 - cz - pz), vtx(x0 - cx - px, 0, z0 - cz - pz), col);
    obj_end();
}

/* A runway centered on (x,z), along z if ns, else along x. Two objects:
 * the strip with threshold bars, then the centerline dashes. */
static void runway(long x, long z, int len, int ns)
{
    int h = len / 2, i;
    obj_begin(x, 0, z, 1);
    if (ns) rect_decal(-18, -h, 18, h, SOLID(8));
    else rect_decal(-h, -18, h, 18, SOLID(8));
    for (i = 0; i < 2; i++) {                          /* threshold bars */
        int a = i ? h - 34 : -h + 4;
        if (ns) { rect_decal(-12, a, -4, a + 30, SOLID(15)); rect_decal(4, a, 12, a + 30, SOLID(15)); }
        else { rect_decal(a, -12, a + 30, -4, SOLID(15)); rect_decal(a, 4, a + 30, 12, SOLID(15)); }
    }
    obj_end();
    obj_begin(x, 0, z, 1);
    cur->detail = 1500;
    for (i = -h + 60; i + 24 < h - 40 && cur->nv + 4 <= MAXV; i += 150) {
        if (ns) rect_decal(-1, i, 1, i + 24, SOLID(15));
        else rect_decal(i, -1, i + 24, 1, SOLID(15));
    }
    obj_end();
}

/* ---- Solids ------------------------------------------------------------- */
/* Box x0..x1, 0..h, z0..z1 in the current object. */
static void box(int x0, int x1, int y0, int y1, int z0, int z1, const mat_t *m, unsigned roof)
{
    int v0 = vtx(x0, y0, z0), v1 = vtx(x1, y0, z0), v2 = vtx(x1, y0, z1), v3 = vtx(x0, y0, z1);
    int v4 = vtx(x0, y1, z0), v5 = vtx(x1, y1, z0), v6 = vtx(x1, y1, z1), v7 = vtx(x0, y1, z1);
    quad(v4, v5, v1, v0, m->lit);    /* south */
    quad(v5, v6, v2, v1, m->mid);    /* east  */
    quad(v6, v7, v3, v2, m->dark);   /* north */
    quad(v7, v4, v0, v3, m->dark);   /* west  */
    if (roof) quad(v7, v6, v5, v4, roof);
}

static void set_box(int x0, int x1, int h, int z0, int z1)
{
    cur->bx0 = x0; cur->bx1 = x1; cur->by1 = h; cur->bz0 = z0; cur->bz1 = z1;
}

static void building(long x, long z, int w, int d, int h, const mat_t *m)
{
    obj_begin(x, 0, z, 0);
    box(-w / 2, w / 2, 0, h, -d / 2, d / 2, m, m->top);
    set_box(-w / 2, w / 2, h, -d / 2, d / 2);
    obj_end();
}

/* House with a gable roof, ridge running east-west. */
static void house(long x, long z, int w, int d, int h, const mat_t *m, unsigned rlit, unsigned rdark)
{
    int rh = h + d / 3, a, b;
    obj_begin(x, 0, z, 0);
    box(-w / 2, w / 2, 0, h, -d / 2, d / 2, m, 0);
    a = vtx(-w / 2, rh, 0); b = vtx(w / 2, rh, 0);
    /* roof slopes: south one faces the sun */
    quad(a, b, vtx(w / 2, h, -d / 2), vtx(-w / 2, h, -d / 2), rlit);
    quad(b, a, vtx(-w / 2, h, d / 2), vtx(w / 2, h, d / 2), rdark);
    /* gable ends */
    tri(b, vtx(w / 2, h, d / 2), vtx(w / 2, h, -d / 2), m->mid);
    tri(a, vtx(-w / 2, h, -d / 2), vtx(-w / 2, h, d / 2), m->dark);
    set_box(-w / 2, w / 2, rh, -d / 2, d / 2);
    obj_end();
}

static void church(long x, long z)
{
    int a;
    obj_begin(x, 0, z, 0);
    box(-12, 12, 0, 15, -21, 21, &M_WHITE, SOLID(8));
    box(-6, 6, 15, 33, -21, -9, &M_WHITE, 0);          /* tower */
    a = vtx(0, 54, -15);                               /* steeple */
    tri(a, vtx(6, 33, -21), vtx(-6, 33, -21), DITH(8, 7));
    tri(a, vtx(6, 33, -9), vtx(6, 33, -21), SOLID(8));
    tri(a, vtx(-6, 33, -9), vtx(6, 33, -9), DITH(8, 0));
    tri(a, vtx(-6, 33, -21), vtx(-6, 33, -9), DITH(8, 0));
    set_box(-12, 12, 33, -21, 21);
    obj_end();
}

static void tower(long x, long z)
{
    static const mat_t glass = { DITH(11, 3), DITH(3, 9), DITH(1, 3), SOLID(8) };
    obj_begin(x, 0, z, 0);
    box(-3, 3, 0, 18, -3, 3, &M_WHITE, 0);
    box(-5, 5, 18, 23, -5, 5, &glass, SOLID(8));
    set_box(-5, 5, 23, -5, 5);
    obj_end();
}

static void hangar(long x, long z)
{
    int a, b;
    obj_begin(x, 0, z, 0);
    box(-20, 20, 0, 10, -15, 15, &M_HANGAR, 0);
    a = vtx(-20, 16, 0); b = vtx(20, 16, 0);
    quad(a, b, vtx(20, 10, -15), vtx(-20, 10, -15), DITH(4, 12));
    quad(b, a, vtx(-20, 10, 15), vtx(20, 10, 15), SOLID(4));
    tri(b, vtx(20, 10, 15), vtx(20, 10, -15), DITH(7, 8));
    tri(a, vtx(-20, 10, -15), vtx(-20, 10, 15), SOLID(8));
    /* big door on the west side */
    quad(vtx(-21, 8, 10), vtx(-21, 8, -10), vtx(-21, 0, -10), vtx(-21, 0, 10), SOLID(0));
    set_box(-20, 20, 16, -15, 15);
    obj_end();
}

/* Pine: a small dark green pyramid. */
static void tree(long x, long z, int h)
{
    int a, w = h / 3;
    obj_begin(x, 0, z, 0);
    a = vtx(0, h, 0);
    {
        int b0 = vtx(-w, 0, -w), b1 = vtx(w, 0, -w), b2 = vtx(w, 0, w), b3 = vtx(-w, 0, w);
        tri(a, b1, b0, DITH(2, 10));
        tri(a, b2, b1, SOLID(2));
        tri(a, b3, b2, DITH(2, 0));
        tri(a, b0, b3, DITH(2, 0));
    }
    obj_end();
}

/* ---- Mountains ---------------------------------------------------------- */
#define MAXMTN 8
static struct { long x, z; int b, h, rot; } mtn[MAXMTN];
static int nmtn;

/* A four-sided pyramid, rotated, with forest, rock and snow bands. */
static void mountain(long x, long z, int b, int h, int rot)
{
    static const int tier[1] = { 68 };          /* snow line, percent of height */
    static const unsigned mats[2][3] = {
        /* lit, mid, dark per band */
        { DITH(6, 7), SOLID(6), DITH(6, 8) },
        { SOLID(15), DITH(15, 7), SOLID(7) },
    };
    int c[2][4], peak, i, t, s = SIN14(rot), co = COS14(rot);
    /* base corners SW, SE, NE, NW (clockwise from above, looking north) */
    static const int cx[4] = { -1, 1, 1, -1 }, cz[4] = { -1, -1, 1, 1 };

    obj_begin(x, 0, z, 0);
    peak = vtx(0, h, 0);
    for (t = 0; t < 2; t++) {
        int pct = t == 0 ? 0 : tier[t - 1];
        int r = (int)((long)b * (100 - pct) / 100), y = (int)((long)h * pct / 100);
        for (i = 0; i < 4; i++) {
            int lx = cx[i] * r, lz = cz[i] * r;
            c[t][i] = vtx(mul14(lx, co) + mul14(lz, s), y, mul14(lz, co) - mul14(lx, s));
        }
    }
    for (i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        /* the face between corners i and j faces direction rot + {S,E,N,W} */
        unsigned ang = (unsigned)rot + 32768u - (unsigned)i * 16384u;  /* S,E,N,W */
        int light = COS14(ang - 24576u);            /* sun from the south-east */
        int sh = light > 6000 ? 0 : light > -6000 ? 1 : 2;
        quad(c[1][i], c[1][j], c[0][j], c[0][i], mats[0][sh]);
        tri(peak, c[1][j], c[1][i], mats[1][sh]);
    }
    obj_end();
    if (nmtn < MAXMTN) {
        mtn[nmtn].x = x; mtn[nmtn].z = z; mtn[nmtn].b = b; mtn[nmtn].h = h; mtn[nmtn].rot = rot;
        nmtn++;
    }
}

int world_height(long x, long z)
{
    int i, best = 0;
    for (i = 0; i < nmtn; i++) {
        long dx = x - mtn[i].x, dz = z - mtn[i].z;
        int lx, lz, s, c, m, hh;
        if (labs(dx) > mtn[i].b * 3L / 2 || labs(dz) > mtn[i].b * 3L / 2) continue;
        s = SIN14(mtn[i].rot); c = COS14(mtn[i].rot);
        /* inverse of the build rotation */
        lx = mul14((int)dx, c) - mul14((int)dz, s);
        lz = mul14((int)dz, c) + mul14((int)dx, s);
        lx = abs(lx); lz = abs(lz);
        m = lx > lz ? lx : lz;
        if (m >= mtn[i].b) continue;
        hh = (int)((long)mtn[i].h * (mtn[i].b - m) / mtn[i].b);
        if (hh > best) best = hh;
    }
    return best;
}

int world_hit(long x, long y, long z)
{
    int i;
    for (i = 0; i < nobjs; i++) {
        const obj_t far *o = &objs[i];
        long dx, dz;
        if (o->by1 == 0) continue;
        dx = x - o->x; dz = z - o->z;
        if (dx >= o->bx0 && dx <= o->bx1 && dz >= o->bz0 && dz <= o->bz1 && y <= o->by1)
            return 1;
    }
    return 0;
}

void world_build(void)
{
    /* Juniper: a small town on a grid of streets (sizes exaggerated a
     * little, as old simulators did, so it reads at 160 pixels across). */
    static const struct { int x, z, w, d, h, kind; } town[] = {
        { -70, 110, 30, 22, 12, 0 }, { -70, 190, 26, 22, 11, 1 }, { -70, 270, 30, 24, 12, 2 },
        {  70, 130, 34, 24, 14, 1 }, {  70, 210, 24, 22, 11, 0 }, { -80, 420, 36, 30, 45, 3 },
        {  85, 390, 40, 34, 70, 4 }, {  75, 500, 30, 24, 12, 2 }, { -80, 560, 26, 22, 11, 0 },
        { 210, -80, 30, 26, 12, 1 }, { 300, -80, 26, 22, 11, 2 }, { -220, -80, 34, 22, 12, 0 },
        { -230, 80, 40, 30, 20, 3 }, { 220, 90, 44, 30, 16, 1 },
    };
    static const struct { int x0, z0, x1, z1; unsigned col; } fields[] = {
        { -900, -700, -300, -100, DITH(6, 2) },  { -900, 100, -300, 600, DITH(14, 2) },
        { 300, 600, 900, 1300, DITH(6, 2) },     { -700, 900, -200, 1500, DITH(2, 8) },
        { 350, -700, 900, -200, DITH(14, 6) },   { -250, -1500, 250, -900, DITH(10, 2) },
        { 700, -1400, 1300, -800, DITH(6, 14) }, { -1500, -900, -1000, -200, DITH(14, 2) },
        { 700, 1500, 1100, 2100, DITH(14, 2) },  { 1300, 1300, 1900, 1800, DITH(6, 2) },
        { 600, 2800, 1100, 3300, SOLID(6) },     { 1400, 2700, 2000, 3200, DITH(10, 2) },
        { 2200, 1900, 2800, 2500, DITH(14, 6) }, { -400, 2000, 300, 2600, DITH(6, 2) },
    };
    static const int river[][2] = {
        { -1700, 9000 }, { -1400, 7600 }, { -1900, 6200 }, { -1500, 5000 }, { -1900, 4050 },
    };
    int i;
    objs = (obj_t far *)malloc(sizeof(obj_t) * MAXOBJ);
    nobjs = 0;
    ngroups = 0;

    /* Farm fields (drawn first: the ground layer), one object each so the
     * renderer can cull them separately. */
    for (i = 0; i < (int)(sizeof(fields) / sizeof(fields[0])); i++)
        field(fields[i].x0, fields[i].z0, fields[i].x1, fields[i].z1, fields[i].col);

    /* A river from the mountains down to the lake. */
    for (i = 0; i + 1 < (int)(sizeof(river) / sizeof(river[0])); i++)
        strip(river[i][0], river[i][1], river[i + 1][0], river[i + 1][1], 22, DITH(1, 9));

    /* Lake. */
    obj_begin(-2200, 0, 3200, 1);
    {
        int v[8];
        static const int ox[8] = { 0, 520, 740, 560, 0, -600, -760, -500 };
        static const int oz[8] = { 900, 640, 0, -700, -950, -620, 50, 700 };
        for (i = 0; i < 8; i++) v[7 - i] = vtx(ox[i], 0, oz[i]);
        face(6, v, DITH(1, 9));   /* split the octagon in two convex pieces */
        {
            int w[4];
            w[0] = v[5]; w[1] = v[6]; w[2] = v[7]; w[3] = v[0];
            face(4, w, DITH(1, 9));
        }
    }
    obj_end();

    /* Road from the home airport east, then north to the town. */
    obj_begin(0, 0, 0, 1);
    rect_decal(140, -6, 1206, 6, SOLID(8));
    rect_decal(1192, -6, 1208, 2950, SOLID(8));
    rect_decal(700, 2292, 1700, 2308, SOLID(8));
    rect_decal(900, 2592, 1500, 2608, SOLID(8));
    obj_end();

    /* Home airport. */
    obj_begin(0, 0, 0, 1);
    rect_decal(40, -120, 140, 40, DITH(7, 8));      /* apron */
    rect_decal(15, -40, 40, -20, DITH(7, 8));       /* taxiway */
    obj_end();
    runway(0, 0, 1000, 1);
    hangar(100, -80);
    tower(70, 20);
    building(120, 10, 14, 10, 5, &M_TAN);

    /* Town of Juniper, around (1200, 2300). */
    group_begin();
    for (i = 0; i < (int)(sizeof(town) / sizeof(town[0])); i++) {
        long x = 1200 + town[i].x, z = 2300 + town[i].z;
        switch (town[i].kind) {
        case 0: house(x, z, town[i].w, town[i].d, town[i].h, &M_WHITE, DITH(4, 12), SOLID(4)); break;
        case 1: house(x, z, town[i].w, town[i].d, town[i].h, &M_TAN, DITH(8, 7), SOLID(8)); break;
        case 2: house(x, z, town[i].w, town[i].d, town[i].h, &M_BRICK, DITH(8, 7), SOLID(8)); break;
        case 3: building(x, z, town[i].w, town[i].d, town[i].h, &M_GRAY); break;
        default: building(x, z, town[i].w, town[i].d, town[i].h, &M_BLUE); break;
        }
    }
    church(1200 + 200, 2300 + 280);
    group_end();

    /* Trees by the lake. */
    group_begin();
    tree(-1300, 2600, 18); tree(-1340, 2660, 22); tree(-1280, 2720, 16);
    tree(-1400, 3900, 20); tree(-1460, 3960, 24); tree(-3000, 2500, 20);
    group_end();

    /* Second airport: east-west runway north-east of town. */
    group_begin();
    runway(3200, 6500, 800, 0);
    hangar(3300, 6420);
    building(3150, 6430, 12, 10, 5, &M_WHITE);
    group_end();

    /* Mountains to the north and north-west. */
    mountain(-4500, 9500, 2500, 1500, 3000);
    mountain(-1200, 12000, 2600, 1800, 9000);
    mountain(2600, 13500, 2700, 1600, 20000);
    mountain(-7500, 6500, 2200, 1100, 14000);
    mountain(6800, 11500, 2300, 1300, 5000);
}
