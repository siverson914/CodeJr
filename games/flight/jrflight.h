/* JRFLIGHT: shared declarations.
 *
 * Rendering note: the 3D view and the panel are drawn into a *packed*
 * 4-bit screen buffer (two pixels per byte, the same format as PCjr video
 * mode 8) instead of JRGFX's byte-per-pixel gfx_back. That lets one
 * `rep stosb` fill two pixels per byte (and dither two colors for free),
 * and lets the frame go to video memory with a plain `rep movsw`, with no
 * per-pixel packing. JRGFX still provides mode setting, keyboard, timer,
 * sound, fixed-point helpers, the debug log and screenshots.
 */
#ifndef JRFLIGHT_H
#define JRFLIGHT_H

#include "jrgfx.h"

/* ---- Screen layout (160x200, 16 colors) -------------------------------- */
#define SCR_W   160
#define SCR_H   200
#define ROWB    80          /* bytes per packed row */
#define VIEW_ROWS 120       /* 3D view: screen rows 0..119; panel below */
/* View window (r3_setview): x wx0..wx1-1 in pixels, buffer rows wy0..wy1-1
 * (screen rows in high detail, half of them in low detail), its center and
 * focal lengths. The field of view is the same for every window size. */
extern int wx0, wx1, wy0, wy1, cx, fx;
extern int vsize;           /* 0 full windshield, 1 medium, 2 small */
extern int wide;            /* 1: 2-pixel-wide pixels (80 columns), byte spans */
extern int ldmode;          /* 1: /LD 160x100 line-doubled CRT mode (untested) */
#define NSIZE 3
#define CX cx
#define FX fx
/* Detail: high draws the view at 160x120. Low draws 160x60 and shows each
 * line twice (half the filling work, nearly square pixels). */
extern int lores;           /* 0 = high detail, 1 = low */
extern int view_h, cy, fy;  /* buffer rows of the view, center, focal (FY) */
extern unsigned char dith_and;  /* 1: dither alternates by row, 0: columns */
#define VIEW_H view_h
#define CY cy
#define FY fy
void r3_detail(int lo);
void r3_setview(void);     /* after changing lores, vsize or wide */
void r_frame(void);        /* cockpit frame around a smaller window */
void r_hfill(unsigned char far *row, int x0, int x1, unsigned char pat);  /* span, any pixel width */
unsigned wide_word(unsigned char pat);

extern unsigned char far scr[SCR_H][ROWB];

/* A "color" is a dither pattern: low byte for even rows, high for odd. */
#define SOLID(c)   ((unsigned)((c) * 0x11) | ((unsigned)((c) * 0x11) << 8))
#define DITH(a, b) ((unsigned)(((a) << 4) | (b)) | ((unsigned)(((b) << 4) | (a)) << 8))

/* ---- Small fast math (8088 inline assembly) ---------------------------- */
extern const int sintab[1024];   /* 1.14 fixed point, sintab.h */
/* Binary angles: 65536 per circle (unsigned wraps for free). */
#define SIN14(a) sintab[(((unsigned)(a)) & 0xFFFFu) >> 6]
#define COS14(a) sintab[(((unsigned)(a) + 16384u) & 0xFFFFu) >> 6]

int mul14(int a, int b);           /* (a*b) >> 14 */
#pragma aux mul14 = "imul dx" "shl ax,1" "rcl dx,1" "shl ax,1" "rcl dx,1" \
    parm [ax] [dx] value [dx] modify exact [ax dx];
int muldiv(int a, int b, int c);   /* a*b/c, caller guarantees it fits */
#pragma aux muldiv = "imul dx" "idiv bx" \
    parm [ax] [dx] [bx] value [ax] modify exact [ax dx];
long lmul(int a, int b);           /* 16x16 -> 32 */
#pragma aux lmul = "imul dx" parm [ax] [dx] value [dx ax] modify exact [ax dx];
int ldivi(long n, int d);          /* 32/16 -> 16, caller guarantees fit */
#pragma aux ldivi = "idiv bx" parm [dx ax] [bx] value [ax] modify exact [ax dx];
long uslope(unsigned n, unsigned d); /* (n << 16) / d, unsigned */
#pragma aux uslope = "xor dx,dx" "div bx" "mov cx,ax" "xor ax,ax" "div bx" \
    "mov dx,cx" parm [ax] [bx] value [dx ax] modify exact [ax cx dx];
/* One packed span: pixels x0..x1-1 (x0 < x1) of the row at ES:DI, with
 * pattern byte AL. Edge nibbles are merged, the middle is REP STOSW. */
void span_asm(unsigned char far *row, int x0, int x1, unsigned char pat);
#pragma aux span_asm = \
    "dec cx" \
    "test si,1" "jz SA1" \
    "mov bx,si" "shr bx,1" "mov ah,es:[bx+di]" "and ah,0F0h" \
    "mov dl,al" "and dl,0Fh" "or ah,dl" "mov es:[bx+di],ah" "inc si" \
    "SA1:" "test cl,1" "jnz SA2" \
    "mov bx,cx" "shr bx,1" "mov ah,es:[bx+di]" "and ah,0Fh" \
    "mov dl,al" "and dl,0F0h" "or ah,dl" "mov es:[bx+di],ah" "dec cx" \
    "SA2:" "sub cx,si" "jl SA3" \
    "inc cx" "shr cx,1" "shr si,1" "add di,si" "mov ah,al" \
    "shr cx,1" "rep stosw" "jnc SA3" "stosb" \
    "SA3:" \
    parm [es di] [si] [cx] [al] modify exact [ax bx cx dx si di];
/* Wide-pixel span: byte columns (x0+1)/2 .. x1/2-1 of the row at ES:DI,
 * AX = fill word for an even byte column (swapped for odd starts). */
void span80(unsigned char far *row, int x0, int x1, unsigned w);
#pragma aux span80 = \
    "inc si" "shr si,1" "shr cx,1" "sub cx,si" "jle S8E" \
    "add di,si" "test si,1" "jz S8A" "xchg al,ah" \
    "S8A:" "shr cx,1" "rep stosw" "jnc S8E" "stosb" \
    "S8E:" \
    parm [es di] [si] [cx] [ax] modify exact [ax cx si di];
void fillb(unsigned char far *p, unsigned n, unsigned char v);
#pragma aux fillb = "rep stosb" parm [es di] [cx] [al] modify exact [di cx];
void copyb(unsigned char far *d, const unsigned char far *s, unsigned n);
#pragma aux copyb = "push ds" "mov ds,dx" "rep movsb" "pop ds" \
    parm [es di] [dx si] [cx] modify exact [si di cx];
void copyw(unsigned char far *d, const unsigned char far *s, unsigned n);
#pragma aux copyw = "push ds" "mov ds,dx" "rep movsw" "pop ds" \
    parm [es di] [dx si] [cx] modify exact [si di cx];

/* ---- Where the view lives ----------------------------------------------
 * Direct (default): the 3D view is drawn straight into JRGFX's hidden video
 * page (fx_seg), which fx_flip() then shows at the next retrace: no copy.
 * High detail uses the page's two-bank row layout; low detail draws its 60
 * rows into bank 0 (screen rows 0,2,4..) and copies them to bank 1.
 * /COPY keeps the old path: draw in scr[], copy to the hidden page. */
extern int vdirect;
extern int rstep[2];          /* row y -> y+1 pointer step, by parity of y */
unsigned char far *r_row(int y);
void r_view_target(void);     /* recompute after a detail or mode change */

/* ---- raster.c ----------------------------------------------------------- */
void r_span(int y, int x0, int x1, unsigned col);   /* x1 exclusive, clipped */
void r_poly(int n, const int *px, const int *py, unsigned col, int ytop, int ybot);
void r_pset(int x, int y, int c);
void r_line(int x0, int y0, int x1, int y1, int c);
void r_rect(int x, int y, int w, int h, unsigned col);
int  r_text(int x, int y, const char *s, int c);    /* 3x5 font, returns end x */
int  r_textw(const char *s);
void r_text_sh(int x, int y, const char *s, int c);  /* with black shadow */
void r_present_view(void);
void r_present_rect(int x0, int y0, int x1, int y1); /* inclusive pixels */
void r_present_all(void);
extern int pdirty0, pdirty1;
extern unsigned r_shown_seg;  /* panel rows copied this frame (sync range) */
int  r_shot(const char *path);

/* ---- render3d.c --------------------------------------------------------- */
typedef struct { long x, y, z; } vec3l;   /* world, 1/256 m */
void r3_camera(const vec3l *pos, unsigned yaw, int pitch, int roll, int view);
void r3_draw(void);
extern int r3_polys, r3_objs;             /* stats of the last frame */

/* ---- world.c ------------------------------------------------------------ */
#define MAXV 40
#define MAXF 20
typedef struct {
    unsigned char n;           /* vertex count */
    unsigned char v[6];        /* vertex indexes, clockwise seen from front */
    unsigned col;              /* dither pattern */
    int nx, ny, nz;            /* outward normal (solids), for culling */
} face_t;
typedef struct {
    long x, y, z;              /* position, meters */
    int  radius;               /* bounding sphere, meters */
    unsigned char flat;        /* 1 = ground decal (drawn first, no culling) */
    int  detail;               /* if nonzero: not drawn beyond this many meters */
    unsigned char nv, nf;
    int  bx0, bx1, by1, bz0, bz1;        /* collision box, meters, relative */
    int  vx[MAXV], vy[MAXV], vz[MAXV];   /* vertices, 1/8 m, relative */
    face_t f[MAXF];
} obj_t;
extern obj_t far *objs;
extern int nobjs;
typedef struct { int first, n; long x, y, z; int r; } group_t;  /* meters */
extern group_t groups[];
extern int ngroups;
void world_build(void);
int  world_height(long x, long z);         /* terrain height (m) at x,z (m) */
int  world_hit(long x, long y, long z);    /* 1 if inside a solid object */

/* ---- panel.c ------------------------------------------------------------ */
void panel_init(void);
void panel_update(int force);
void view_overlay(void);

/* ---- flight model (flight.c) -------------------------------------------- */
typedef struct {
    vec3l pos;                 /* 1/256 m, y = altitude of the wheels */
    fix   v;                   /* airspeed m/s */
    long  hdg;                 /* binary angle << 16 */
    long  gam, alpha, roll;    /* path angle, angle of attack, bank (BAU<<16) */
    int   thr;                 /* throttle 0..100 */
    fix   elev, ail;           /* stick, -1..1 */
    int   trim;                /* elevator trim, -50..50 */
    int   ground, brakes, crashed, stall, view;
    fix   vs;                  /* vertical speed m/s */
} plane_t;
extern plane_t pl;
void phys_reset(int scene);
void phys_step(void);
extern char msg[24];
extern int  msg_t;

#endif
