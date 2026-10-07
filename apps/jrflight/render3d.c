/* JRFLIGHT 3D: camera, sky/ground horizon bands, object transform,
 * frustum clipping, projection, back-face culling and painter's sort.
 *
 * Everything is 16-bit integer math. World positions are 32-bit (1/256 m)
 * but each object is brought into camera space at its own scale: near
 * objects in 1/8 m units, far ones in coarser units (perspective division
 * doesn't care about scale), so 16-bit camera coordinates never overflow
 * yet mountains 20 km away still show. */
#include <stdlib.h>
#include "jrflight.h"

#define NEAR 8
#define MAXCLIP 16

int r3_polys, r3_objs, r3_flags;
int lores, view_h = VIEW_ROWS, cy = VIEW_ROWS / 2, fy = 160;
int wx0, wx1 = SCR_W, wy0, wy1 = VIEW_ROWS, cx = SCR_W / 2, fx = 96;
int vsize, wide, ldmode;
unsigned char dith_and = 1;   /* in low detail each checker cell is 1 pixel x 2 lines */
static long bt[3];            /* band thresholds for this focal length */

/* Window sizes in screen pixels: full windshield, medium, small. */
static const struct { int x0, x1, y0, y1, f; } sizes[NSIZE] = {
    { 0, 160, 0, 120, 96 }, { 16, 144, 12, 108, 77 }, { 32, 128, 24, 96, 58 },
};

void r3_setview(void)
{
    const int lo = lores;
    view_h = lo ? VIEW_ROWS / 2 : VIEW_ROWS;
    wx0 = sizes[vsize].x0; wx1 = sizes[vsize].x1;
    wy0 = sizes[vsize].y0 >> lo; wy1 = sizes[vsize].y1 >> lo;
    cx = (wx0 + wx1) / 2; cy = (wy0 + wy1) / 2;
    fx = sizes[vsize].f;
    fy = lo ? fx * 5 / 6 : fx * 5 / 3;       /* keeps FX/FY = 0.6 per screen row */
    /* sky bands at -3.5, 0 and +4 degrees, in units of fx * 1.14 */
    bt[0] = -(long)fx * 16384L * 6 / 100;
    bt[1] = 0;
    bt[2] = (long)fx * 16384L * 7 / 100;
    r_view_target();
}

void r3_detail(int lo)
{
    lores = lo;
    r3_setview();
}
int st_verts, st_faces, st_clipped, st_back;

static int R[3][3];          /* camera rows: right, up, forward (1.14) */
static int Q[3][3];          /* the same in 1.15, for mulhi with doubled inputs */

int mulhi(int a, int b);     /* (a*b) >> 16: one IMUL, take DX */
#pragma aux mulhi = "imul dx" parm [ax] [dx] value [dx] modify exact [ax dx];
static vec3l cam;

/* Horizon bands: angle thresholds below/above the horizon and their colors. */
#define NBAND 4
#define band_t bt
static const unsigned band_c[NBAND] = {
    SOLID(2), DITH(2, 7), DITH(11, 9), SOLID(9)
};

void r3_camera(const vec3l *pos, unsigned yaw, int pitch, int roll, int view)
{
    int sy = SIN14(yaw), cy = COS14(yaw);
    int sp = SIN14(pitch), cp = COS14(pitch);
    int sr = SIN14(roll), cr = COS14(roll);
    int f[3], r0[3], u0[3], r[3], u[3], i;

    f[0] = mul14(sy, cp); f[1] = sp; f[2] = mul14(cy, cp);
    r0[0] = cy; r0[1] = 0; r0[2] = -sy;
    u0[0] = -mul14(sy, sp); u0[1] = cp; u0[2] = -mul14(cy, sp);
    for (i = 0; i < 3; i++) {
        r[i] = mul14(r0[i], cr) - mul14(u0[i], sr);
        u[i] = mul14(u0[i], cr) + mul14(r0[i], sr);
    }
    for (i = 0; i < 3; i++) {
        R[1][i] = u[i];
        switch (view) {
        case 1: R[0][i] = f[i];  R[2][i] = -r[i]; break;   /* left  */
        case 2: R[0][i] = -f[i]; R[2][i] = r[i];  break;   /* right */
        case 3: R[0][i] = -r[i]; R[2][i] = -f[i]; break;   /* back  */
        default: R[0][i] = r[i]; R[2][i] = f[i];  break;
        }
    }
    for (i = 0; i < 9; i++) {
        int v = R[i / 3][i % 3];
        Q[i / 3][i % 3] = v >= 16384 ? 32767 : v <= -16384 ? -32767 : v * 2;
    }
    cam = *pos;
}

/* Sky and ground: for pixel (sx,sy) the world-vertical part of the view
 * ray is linear, v = A*(sx-CX) + B0 + sy*Bs, so every band edge is a
 * straight line, and all of them are parallel: each row's crossings step
 * by the same amount. One pass over the rows fills every band. */
static void draw_sky(void)
{
    long A = R[0][1];
    int ratio = lores ? 307 : 154;                 /* FX/FY per buffer row, 8.8 */
    long B0 = (long)R[2][1] * fx + ((lmul(R[1][1], cy - wy0) * ratio) >> 8);
    long Bs = -lmul(R[1][1], ratio) >> 8;
    long c[NBAND - 1], dc;
    int y, k;
    unsigned char far *row = r_row(wy0);
    unsigned char pat[NBAND][2];
#define r_hfill(r, a, b, p) (wide ? span80(r, a, b, wide_word(p)) : span_asm(r, a, b, p))

    for (k = 0; k < NBAND; k++) {
        pat[k][0] = (unsigned char)band_c[k];
        pat[k][1] = (unsigned char)(band_c[k] >> 8);
    }
    if (A == 0 || labs(Bs) > labs(A) * 160) {
        /* wings level (or nearly): every row is a single band */
        long B = B0;
        for (y = wy0; y < wy1; B += Bs, row += rstep[y & 1], y++) {
            for (k = 0; k < NBAND - 1 && band_t[k] <= B; k++) ;
            r_hfill(row, wx0, wx1, pat[k][y & dith_and]);
        }
        return;
    }
    dc = fix_div(-Bs, A);
    for (k = 0; k < NBAND - 1; k++) {
        long t = fix_div(band_t[k] - B0, A);
        if (t > 10000L * 65536L) t = 10000L * 65536L;
        if (t < -10000L * 65536L) t = -10000L * 65536L;
        c[k] = ((long)cx << 16) + 0x8000L + t;
    }
    for (y = wy0; y < wy1; row += rstep[y & 1], y++) {
        int x = wx0, e, odd = y & dith_and;
        if (A > 0) {
            for (k = 0; k < NBAND - 1; k++) {
                e = (int)(c[k] >> 16);
                c[k] += dc;
                if (e > wx1) e = wx1;
                if (e > x) { r_hfill(row, x, e, pat[k][odd]); x = e; }
            }
            if (x < wx1) r_hfill(row, x, wx1, pat[NBAND - 1][odd]);
        } else {
            for (k = NBAND - 2; k >= 0; k--) {
                e = (int)(c[k] >> 16);
                c[k] += dc;
                if (e > wx1) e = wx1;
                if (e > x) { r_hfill(row, x, e, pat[k + 1][odd]); x = e; }
            }
            if (x < wx1) r_hfill(row, x, wx1, pat[0][odd]);
        }
    }
}

/* ---- Objects ------------------------------------------------------------ */
static int vx[MAXV], vy[MAXV], vz[MAXV], px[MAXV], py[MAXV];
static unsigned char oc[MAXV];

static unsigned char outcode(int x, int y, int z)
{
    unsigned char c = 0;
    int z2 = z + z;
    if (z < NEAR) c |= 1;
    if (x > z2) c |= 2;
    if (x < -z2) c |= 4;
    if (y > z2) c |= 8;
    if (y < -z2) c |= 16;
    return c;
}

unsigned udiv14(unsigned n, unsigned d);   /* (n << 14) / d, n <= d */
#pragma aux udiv14 = "mov dx,ax" "mov cl,2" "shr dx,cl" "mov cl,14" "shl ax,cl" "div bx" \
    parm [ax] [bx] value [ax] modify exact [ax cx dx];

/* Clips a polygon in camera space against the planes in `mask`, then
 * projects it. Returns the vertex count (0 if nothing is left). All 16-bit:
 * camera coordinates stay within +-8700, so plane distances fit an int.
 * Intersections are always computed from the inside vertex, so polygons
 * sharing an edge get identical points (no cracks). */
static int clip_project(int n, const unsigned char *idx, unsigned char mask,
                        int *ox, int *oy)
{
    static int buf[2][3][MAXCLIP];
    int (*a)[MAXCLIP] = buf[0], (*b)[MAXCLIP] = buf[1], (*t)[MAXCLIP];
    int d[MAXCLIP];
    int i, p, m;
    for (i = 0; i < n; i++) {
        a[0][i] = vx[idx[i]]; a[1][i] = vy[idx[i]]; a[2][i] = vz[idx[i]];
    }
    for (p = 0; p < 5; p++) {
        if (!(mask & (1 << p))) continue;
        for (i = 0; i < n; i++) {
            int z2 = a[2][i] * 2;
            switch (p) {
            case 0: d[i] = a[2][i] - NEAR; break;
            case 1: d[i] = z2 - a[0][i]; break;
            case 2: d[i] = z2 + a[0][i]; break;
            case 3: d[i] = z2 - a[1][i]; break;
            default: d[i] = z2 + a[1][i]; break;
            }
        }
        m = 0;
        for (i = 0; i < n && m < MAXCLIP - 1; i++) {
            int j = i + 1 == n ? 0 : i + 1;
            if (d[i] >= 0) { b[0][m] = a[0][i]; b[1][m] = a[1][i]; b[2][m] = a[2][i]; m++; }
            if ((d[i] >= 0) != (d[j] >= 0)) {
                int in = d[i] >= 0 ? i : j, out = in == i ? j : i, c;
                unsigned f = udiv14((unsigned)d[in], (unsigned)(d[in] - d[out]));
                for (c = 0; c < 3; c++)
                    b[c][m] = a[c][in] + mul14(a[c][out] - a[c][in], (int)f);
                if (p == 0 && b[2][m] < NEAR) b[2][m] = NEAR;
                m++;
            }
        }
        if (m < 3) return 0;
        n = m;
        t = a; a = b; b = t;
    }
    for (i = 0; i < n; i++) {
        int z = a[2][i] < NEAR ? NEAR : a[2][i];
        ox[i] = CX + muldiv(a[0][i], FX, z);
        oy[i] = CY - muldiv(a[1][i], FY, z);
    }
    return n;
}

/* Draws one object. (ox,oy,oz) is the object center relative to the
 * camera at the object's scale k, unrotated; (ccx,ccy,ccz) the same in
 * camera space. Solid faces are back-face culled in object space first,
 * so only vertices of front faces get transformed. */
static void draw_obj(const obj_t far *o, int k, int ox, int oy, int oz,
                     int ccx, int ccy, int ccz)
{
    int i, nv = o->nv, nf = o->nf;
    static unsigned char need[MAXV], front[MAXF];

    if (o->flat) {
        for (i = 0; i < nv; i++) need[i] = 1;
        for (i = 0; i < nf; i++) front[i] = 1;
    } else {
        for (i = 0; i < nv; i++) need[i] = 0;
        for (i = 0; i < nf; i++) {
            const face_t far *f = &o->f[i];
            int v0 = f->v[0], j;
            long dot = lmul(f->nx, -ox - (o->vx[v0] >> k))
                     + lmul(f->ny, -oy - (o->vy[v0] >> k))
                     + lmul(f->nz, -oz - (o->vz[v0] >> k));
            front[i] = dot > 0;
            if (dot > 0) for (j = 0; j < f->n; j++) need[f->v[j]] = 1;
            else st_back++;
        }
    }
    for (i = 0; i < nv; i++) {
        int x, y, z, X, Y, Z;
        if (!need[i]) continue;
        st_verts++;
        /* doubled inputs times the 1.15 matrix: (2x * q) >> 16 = x*q >> 15 */
        x = (o->vx[i] >> k) << 1; y = (o->vy[i] >> k) << 1; z = (o->vz[i] >> k) << 1;
        X = ccx + mulhi(x, Q[0][0]) + mulhi(z, Q[0][2]);
        Y = ccy + mulhi(x, Q[1][0]) + mulhi(z, Q[1][2]);
        Z = ccz + mulhi(x, Q[2][0]) + mulhi(z, Q[2][2]);
        if (y) {
            X += mulhi(y, Q[0][1]);
            Y += mulhi(y, Q[1][1]);
            Z += mulhi(y, Q[2][1]);
        }
        vx[i] = X; vy[i] = Y; vz[i] = Z;
        oc[i] = outcode(X, Y, Z);
        if (!oc[i]) {
            px[i] = CX + muldiv(X, FX, Z);
            py[i] = CY - muldiv(Y, FY, Z);
        }
    }
    for (i = 0; i < nf; i++) {
        const face_t far *f;
        unsigned char idx[6], cand = 0xFF, cor = 0;
        int sx[MAXCLIP], sy[MAXCLIP], n, j;
        if (!front[i]) continue;
        f = &o->f[i];
        n = f->n;
        for (j = 0; j < n; j++) {
            idx[j] = f->v[j];
            cand &= oc[idx[j]];
            cor |= oc[idx[j]];
        }
        if (cand) continue;
        st_faces++;
        if (cor) {
            st_clipped++;
            n = clip_project(n, idx, cor, sx, sy);
            if (n < 3) continue;
        } else {
            for (j = 0; j < n; j++) { sx[j] = px[idx[j]]; sy[j] = py[idx[j]]; }
        }
        if (!(r3_flags & 2)) r_poly(n, sx, sy, f->col, wy0, wy1);
        r3_polys++;
    }
}

#define MAXVIS 64
static struct vis { int o, k, x, y, z, dot, depth, rx, ry, rz; } vis[MAXVIS];
static unsigned char order[MAXVIS];

/* Culling records in near memory: position and radius in 1/8 m. */
#define MAXCULL 96
static struct { long x, y, z, r; int x4, y4, z4, r4, far4; unsigned char flat; } cl[MAXCULL];
static int ncl = -1;
static const long klim[8] = { 5000L, 10000L, 20000L, 40000L, 80000L, 160000L, 320000L, 0x7FFFFFFFL };

long lsar(long v, int k);       /* v >> k for small k */
#pragma aux lsar = "jcxz LS2" "LS1:" "sar dx,1" "rcr ax,1" "loop LS1" "LS2:" \
    parm [dx ax] [cx] value [dx ax] modify exact [ax cx dx];

static void cull_init(void)
{
    int i;
    for (i = 0; i < nobjs && i < MAXCULL; i++) {
        cl[i].x = objs[i].x * 8; cl[i].y = objs[i].y * 8; cl[i].z = objs[i].z * 8;
        cl[i].r = (long)objs[i].radius * 8;
        cl[i].flat = objs[i].flat;
        cl[i].far4 = objs[i].detail / 4;
        cl[i].x4 = (int)(objs[i].x >> 2); cl[i].y4 = (int)(objs[i].y >> 2); cl[i].z4 = (int)(objs[i].z >> 2);
        cl[i].r4 = objs[i].radius / 4 + 2;
    }
    ncl = i;
}

/* Bounding-sphere test of one object or group, done coarsely in 16-bit
 * (4 m units). Returns 0 = not visible, 1 = smaller than a pixel or two
 * (draw a dot), 2 = draw it. */
static int c4X, c4Y, c4Z;      /* center from the last cull4, 4 m units */

static int alt40;              /* camera height (m) * 40, for decal detail */

static int cull4(int x, int y, int z, int r, int flat)
{
    int X, Y, Z;
    x <<= 1; y <<= 1; z <<= 1;
    Z = mulhi(x, Q[2][0]) + mulhi(y, Q[2][1]) + mulhi(z, Q[2][2]);
    if (Z + r < 1) return 0;
    X = mulhi(x, Q[0][0]) + mulhi(y, Q[0][1]) + mulhi(z, Q[0][2]);
    if (lmul(X < 0 ? -X : X, 96) - lmul(Z, 80) > lmul(r, 125)) return 0;
    Y = mulhi(x, Q[1][0]) + mulhi(y, Q[1][1]) + mulhi(z, Q[1][2]);
    if (lmul(Y < 0 ? -Y : Y, 160) - lmul(Z, 60) > lmul(r, 171)) return 0;
    c4X = X; c4Y = Y; c4Z = Z;
    /* ground decals: skip when they would be under a pixel tall (seen
     * edge-on from low altitude); solids: a dot when tiny */
    if (flat) return lmul(r, alt40) < lmul(Z, Z) ? 1 : 2;
    if (lmul(r, FX) < lmul(Z, 2)) return 1;
    return 2;
}

/* Exact camera-space center at the object's own scale (1/8 m << k). */
static struct cres { int k, x, y, z, X, Y, Z; } cs;

static void place(long rx, long ry, long rz, long r8)
{
    long m, t;
    int k = 0;
    m = rx < 0 ? -rx : rx;
    t = ry < 0 ? -ry : ry; if (t > m) m = t;
    t = rz < 0 ? -rz : rz; if (t > m) m = t;
    m += r8;
    while (m > klim[k]) k++;
    cs.k = k;
    cs.x = (int)lsar(rx, k); cs.y = (int)lsar(ry, k); cs.z = (int)lsar(rz, k);
    {
        int x = cs.x << 1, y = cs.y << 1, z = cs.z << 1;
        cs.X = mulhi(x, Q[0][0]) + mulhi(y, Q[0][1]) + mulhi(z, Q[0][2]);
        cs.Y = mulhi(x, Q[1][0]) + mulhi(y, Q[1][1]) + mulhi(z, Q[1][2]);
        cs.Z = mulhi(x, Q[2][0]) + mulhi(y, Q[2][1]) + mulhi(z, Q[2][2]);
    }
}

void r3_draw(void)
{
    int i, j, g, nvis = 0;
    long cx8 = cam.x >> 5, cy8 = cam.y >> 5, cz8 = cam.z >> 5;
    int cx4 = (int)(cam.x >> 10), cy4 = (int)(cam.y >> 10), cz4 = (int)(cam.z >> 10);
    alt40 = cam.y > (800L << 8) ? 32000 : (int)(cam.y >> 8) * 40 + 80;
    r3_polys = r3_objs = 0;
    st_verts = st_faces = st_clipped = st_back = 0;
    if (ncl < 0) cull_init();
    if (!(r3_flags & 1)) draw_sky();
    if (r3_flags & 32) return;

    for (i = 0, g = 0; i < ((r3_flags & 64) ? 0 : ncl); i++) {
        int c, x4, y4, z4;
        if (g < ngroups && i == groups[g].first) {
            /* whole group (a town, an airport) culled or too small to see */
            c = cull4((int)((groups[g].x - (cx8 >> 3)) >> 2), (int)((groups[g].y - (cy8 >> 3)) >> 2),
                      (int)((groups[g].z - (cz8 >> 3)) >> 2), groups[g].r / 4 + 2, 0);
            if (c != 2) { i += groups[g].n - 1; g++; continue; }
            g++;
        }
        x4 = cl[i].x4 - cx4; y4 = cl[i].y4 - cy4; z4 = cl[i].z4 - cz4;
        if (cl[i].far4) {
            int d = x4 < 0 ? -x4 : x4, e = z4 < 0 ? -z4 : z4;
            if (e > d) d = e;
            if (d - cl[i].r4 > cl[i].far4) continue;
        }
        c = cull4(x4, y4, z4, cl[i].r4, cl[i].flat);
        if (!c || (c == 1 && cl[i].flat)) continue;
        if (nvis == MAXVIS) break;
        if (r3_flags & 128) continue;
        if (c == 1) {
            /* a dot: the coarse center is plenty */
            vis[nvis].dot = 1; vis[nvis].o = i;
            vis[nvis].x = c4X; vis[nvis].y = c4Y; vis[nvis].z = c4Z;
            vis[nvis].depth = c4Z;              /* 4 m = 32/8 m, >> 5 */
            nvis++;
            continue;
        }
        place(cl[i].x - cx8, cl[i].y - cy8, cl[i].z - cz8, cl[i].r);
        if (cs.k > 6) continue;
        vis[nvis].dot = c == 1;
        vis[nvis].o = i; vis[nvis].k = cs.k;
        vis[nvis].rx = cs.x; vis[nvis].ry = cs.y; vis[nvis].rz = cs.z;
        vis[nvis].x = cs.X; vis[nvis].y = cs.Y; vis[nvis].z = cs.Z;
        vis[nvis].depth = cl[i].flat ? 0x7FFF : (int)lsar((long)cs.Z << cs.k, 5);
        nvis++;
    }
    if (r3_flags & 16) return;
    /* Painter's order: ground decals (in world order) first, then solids far
     * to near. The solids start in last frame's order, so the insertion
     * sort has almost nothing to do. */
    {
        int nd = 0, ns = 0, o;
        static unsigned char prev[MAXVIS], nprev;
        static signed char where[MAXCULL];
        for (i = 0; i < ncl; i++) where[i] = -1;
        for (i = 0; i < nvis; i++) {
            if (vis[i].depth == 0x7FFF) order[nd++] = (unsigned char)i;
            else where[vis[i].o] = (signed char)i;
        }
        ns = nd;
        for (i = 0; i < nprev; i++) {
            o = prev[i];
            if (where[o] >= 0) { order[ns++] = (unsigned char)where[o]; where[o] = -1; }
        }
        for (i = 0; i < nvis; i++)
            if (vis[i].depth != 0x7FFF && where[vis[i].o] >= 0) order[ns++] = (unsigned char)i;
        for (i = nd + 1; i < ns && !(r3_flags & 8); i++) {
            unsigned char t = order[i];
            int d = vis[t].depth;
            for (j = i; j > nd && vis[order[j - 1]].depth < d; j--) order[j] = order[j - 1];
            order[j] = t;
        }
        nprev = 0;
        for (i = nd; i < ns; i++) prev[nprev++] = (unsigned char)vis[order[i]].o;
    }
    for (j = 0; j < nvis; j++) {
        struct vis *v = &vis[order[j]];
        if (v->dot) {
            int z = v->z;
            if (z >= 1) {
                int sx = CX + muldiv(v->x, FX, z), sy = CY - muldiv(v->y, FY, z);
                if (sy >= wy0 && sy < wy1 && sx >= wx0 && sx < wx1)
                    r_pset(sx, sy, objs[v->o].f[0].col & 15);
            }
            continue;
        }
        if (!(r3_flags & 4)) draw_obj(&objs[v->o], v->k, v->rx, v->ry, v->rz, v->x, v->y, v->z);
        r3_objs++;
    }
}
