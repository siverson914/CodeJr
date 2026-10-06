/* JRGFX: graphics, input, timing and sound for the IBM PCjr.
 *
 * The shared interface for CodeJr's game library (lib/) and the first two
 * games built on it (games/doom, games/flight). Games code against this
 * header; the library implements it. Changing a signature here means
 * changing every user, so add rather than change.
 *
 * Model: draw into an off-screen back buffer in ordinary (fast) RAM, then
 * gfx_present() copies it to the PCjr's video memory. Video memory lives
 * in the slow, shared low 128K, so drawing straight into it is slow and
 * flickers; one block copy per frame is the PCjr-friendly way.
 *
 * Build: Open Watcom, 16-bit DOS, large or medium memory model (the back
 * buffer is far data). Coordinates are in pixels of the current mode,
 * colors are 0-15 (the PCjr's 16-color palette).
 */
#ifndef JRGFX_H
#define JRGFX_H

/* ---- Graphics ---------------------------------------------------------- */

#define GFX_160x200 8 /* BIOS mode 8: 160x200, 16 colors, 16 KB  */
#define GFX_320x200 9 /* BIOS mode 9: 320x200, 16 colors, 32 KB  */

extern int gfx_w, gfx_h;                 /* size of the current mode   */
extern unsigned char far *gfx_back;      /* back buffer, 1 byte/pixel  */

int  gfx_init(int mode);                 /* 0 on success               */
extern int gfx_mode;                     /* current mode, 0 = text     */
extern int gfx_err;  /* why gfx_init failed: 1 bad mode, 2 no memory, 3 no 32K video RAM */
extern unsigned char gfx_pal[16];        /* palette as set (screenshots) */
extern unsigned gfx_vseg;                /* video segment gfx_present writes */
extern unsigned jr_rowoff[200];          /* video offset of row y in gfx_vseg:
                       mode 8: (y&1)*0x2000 + (y>>1)*80; mode 9: (y&3)*0x2000 + (y>>2)*160 */
void gfx_done(void);                     /* back to 80x25 text         */
/* gfx_init, key_init, tmr_init and snd_init register their *_done with
 * atexit(), so a normal exit() restores the machine. */

void gfx_clear(unsigned char c);
void gfx_pset(int x, int y, unsigned char c);           /* clipped */
unsigned char gfx_pget(int x, int y);
void gfx_hline(int x0, int x1, int y, unsigned char c); /* clipped */
void gfx_vline(int x, int y0, int y1, unsigned char c); /* clipped */
void gfx_line(int x0, int y0, int x1, int y1, unsigned char c); /* clipped */
void gfx_rect(int x, int y, int w, int h, unsigned char c);      /* outline */
void gfx_fill_rect(int x, int y, int w, int h, unsigned char c);
void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, unsigned char c);
void gfx_fill_poly(int n, const int *xy, unsigned char c); /* convex, xy pairs */

/* Sprites: w*h bytes of color indexes; `key` is transparent (or -1 for none). */
void gfx_blit(int x, int y, int w, int h, const unsigned char far *pix, int key);

/* Raycaster column (added): fills x, rows y0..y1 with tex[v >> 16], v
 * starting at `v` (16.16, texel units) for row y0 and adding `dv` per row.
 * Texel index wraps at 256 (tex must have 256 bytes, or keep v in range);
 * precision is 8.8 inside, so dv must be < 256.0. Clipped. */
void gfx_vtex(int x, int y0, int y1, const unsigned char far *tex, long v, long dv);

/* 8x8 text in the back buffer (ASCII 32-126). */
void gfx_text(int x, int y, const char *s, unsigned char c);

void gfx_vsync(void);                  /* wait for vertical retrace     */
void gfx_present(void);                /* copy the back buffer to video */
void gfx_present_rows(int y0, int y1); /* copy only these rows          */

/* PCjr palette registers: show logical color `index` as hardware color `rgbi`. */
void gfx_palette(int index, int rgbi);

/* Writes the back buffer as a binary PPM (P6) with the PCjr's colors, for
 * headless testing and documentation. Returns 0 on success. */
int  gfx_screenshot(const char *path);

/* ---- Fast path (FX): native packed pages, page flipping (mode 8) -------
 * Everything here draws in the PCjr's own format: 2 pixels per byte (high
 * nibble = left pixel), row y at (y&1)*0x2000 + (y>>1)*80. There is no
 * gfx_present conversion. Normally the target (fx_seg) is the HIDDEN video
 * page and fx_flip() shows it at the next vertical retrace, so a frame costs
 * no copy at all. See STATUS.md "Fast path" for the guide and numbers.
 *
 * "pat" arguments are pattern bytes: FX_SOLID(c) for a solid color, or
 * FX_PAIR(l, r) for a 2-pixel horizontal dither. Polygon/rect "pat2" words
 * hold the even-row byte in the low half and the odd-row byte in the high
 * half: FX_SOLID2(c), FX_DITH(a, b) (checkerboard). */
#define FX_PAIR(l, r)  ((unsigned char)(((l) << 4) | ((r) & 15)))
#define FX_SOLID(c)    FX_PAIR(c, c)
#define FX_SOLID2(c)   ((unsigned)FX_SOLID(c) * 0x101u)
#define FX_DITH(a, b)  ((unsigned)FX_PAIR(a, b) | ((unsigned)FX_PAIR(b, a) << 8))

#define FX_BUFFER 1   /* draw into a RAM buffer, fx_flip copies the dirty parts */
#define FX_NOIRQ  2   /* poll for retrace instead of using IRQ5 */
#define FX_TEAR   4   /* flip at once, never wait for retrace (max fps) */
/* (tests only: 8 = pretend there is no second page, 16 = always claim it) */

int  fx_init(int flags);       /* gfx_init(8) + fast path. 0 on success   */
void fx_done(void);            /* called by gfx_done too                  */
extern unsigned fx_seg;        /* segment the fx_* functions draw into     */
extern int fx_pages;           /* 2: page flipping; 1: buffer + copy       */
extern int fx_irq;             /* 1 if flips happen in the IRQ5 handler    */
extern volatile unsigned fx_vbl; /* vertical retraces counted by IRQ5 (60 Hz) */
/* Runs fn in every vertical retrace (60 Hz) when fx_irq is 1, after the
 * flip and the palette: music, clocks. It runs on the library's own stack
 * with SS = DGROUP, so ordinary C is safe there even when the interrupt
 * lands inside DOS. Do NOT chain INT 0Dh yourself to call C: in the large
 * model the compiler reaches statics through SS, and inside a DOS call on
 * a real PCjr SS is DOS's. Keep fn short; no DOS or BIOS calls. 0 removes
 * it; fx_init clears it. */
void fx_irq_hook(void (*fn)(void));
void fx_flip(void);            /* show what was drawn; swap pages          */
void fx_begin(void);           /* wait until the new target is off screen  */
void fx_sync_rows(int y0, int y1); /* copy rows of fx_seg to the shown page (static HUDs) */
void fx_clip(int y0, int y1);  /* rows polygons/spans may touch (default 0-199) */

void fx_clear(unsigned pat2);
void fx_pset(int x, int y, int c);
void fx_hspan(int y, int x0, int x1, unsigned char pat);       /* clipped */
void fx_fill_rect(int x, int y, int w, int h, unsigned pat2);  /* clipped */
void fx_poly(int n, const int *xy, unsigned pat2);  /* convex, xy pairs   */
void fx_tri(int x0, int y0, int x1, int y1, int x2, int y2, unsigned pat2);
/* fx_poly with a pattern byte per screen row: rowpat200[y] fills row y.
 * Gradients and ordered dithers (alternate the pair by row parity) at the
 * same cost as a solid fill. */
void fx_poly_rows(int n, const int *xy, const unsigned char *rowpat200);
/* fx_poly that copies its pixels from another buffer in the page layout
 * (e.g. from fx_alloc_buffer): a shadow drawn from a pre-darkened copy of
 * the floor darkens whatever pattern is under it, at block-copy speed. */
void fx_poly_src(int n, const int *xy, unsigned srcseg);
void fx_text(int x, int y, const char *s, int c);   /* 8x8 font           */
void fx_line(int x0, int y0, int x1, int y1, int c); /* same pixels as gfx_line */

/* Raycaster: 80 columns of 2 pixels. fx_col fills byte column bx (0-79),
 * rows y0..y1, with pattern byte pat. fx_texcol draws texture bytes
 * tex[v >> 8], v in 8.8 texel units stepping dv per row (tex holds packed
 * bytes, e.g. FX_SOLID(c), and wraps at 256). */
void fx_col(int bx, int y0, int y1, unsigned char pat);
void fx_texcol(int bx, int y0, int y1, const unsigned char far *tex, unsigned v, unsigned dv);
/* Whole view column, each pixel written at most once, and ceiling/floor
 * rows only where they changed since this page was last drawn. Set the
 * view rows and the per-row ceiling/floor bytes (200 entries) once with
 * fx_view; then per frame call fx_wall for every column 0-79. Wall rows
 * top..bot (may exceed the view; top > bot means no wall, then top is the
 * horizon). Any other fx_ drawing in the view invalidates the columns it
 * touches automatically; call fx_view again if rowpat changes. */
void fx_view(int y0, int y1, const unsigned char *rowpat);
void fx_wall(int bx, int top, int bot, const unsigned char far *tex, unsigned v, unsigned dv);
/* Low detail: 40 columns of 4 pixels (column bx2 covers bytes 2*bx2 and
 * 2*bx2+1); about a third less bus traffic per pixel than fx_wall. */
void fx_wall2(int bx2, int top, int bot, const unsigned char far *tex, unsigned v, unsigned dv);

/* Compiled sprites: the sprite becomes 8088 code (4 variants for x and y
 * parity) that stores its opaque pixels with immediate MOVs. pix is w*h
 * bytes (colors), key is the transparent color (-1: none). The pixel data
 * must stay valid (used when the sprite is clipped). NULL if out of memory. */
typedef struct fx_sprite fx_sprite;
fx_sprite *fx_compile(const unsigned char far *pix, int w, int h, int key);
void fx_sprite_draw(const fx_sprite *s, int x, int y);
void fx_sprite_free(fx_sprite *s);

/* Palette without waiting: with IRQ5 the registers are written in the next
 * retrace. fx_pal_set sets all 16; fx_pal_cycle rotates entries a..b. */
void fx_pal_set(const unsigned char *rgbi16);
void fx_pal_cycle(int a, int b, int dir);
/* Palette animation run by the retrace IRQ itself: range i (0-3) rotates
 * entries a..b by one step every `period` retraces (60 Hz), dir +1/-1,
 * so it moves at a steady speed whatever the frame rate. period 0 stops
 * range i. Setting any range restarts all of them in step. Without IRQ5
 * it steps once per fx_flip instead. */
void fx_pal_anim(int i, int a, int b, int period, int dir);
/* Shows entry `index` as hardware color rgbi for `retraces` frames of the
 * display (60 Hz), then puts it back: beat flashes, damage flashes. Up to
 * 4 at once; IRQ5 only. */
void fx_pal_flash(int index, int rgbi, int retraces);
/* 32 colors: rows 0..row-1 use top16 and the rest the normal palette.
 * The IRQ writes top16 in the retrace, then waits for the display to
 * reach `row` and rewrites the entries that differ (the PCjr blanks the
 * picture for the few microseconds that takes, so put the line where the
 * picture is black). The wait costs the CPU from the retrace to that row
 * on every frame. Returns fx_split_state: 1 running, 0 off (no IRQ5),
 * -1 given up because the display timing never came; fx_split(0, 0)
 * turns it off. Tried only in DOSBox so far. */
int fx_split(int row, const unsigned char *top16);
extern int fx_split_state;
extern unsigned fx_split_guard;   /* polls to spare at the line, last frame */

/* Moving objects over a static background, without clearing: after
 * fx_track(1), every fx_ drawing call records the bytes it touched, per
 * page (polygons exactly, row by row; other calls by bounding box). Each
 * frame, after fx_begin, fx_restore(bg) copies back from bg (a buffer in
 * the page layout holding the background) just what the hidden page drew
 * last time, then forgets it. Draw the background into both pages first
 * (e.g. fx_copy_rect from bg to each page). */
void fx_track(int on);
void fx_restore(unsigned bgseg);

/* Buffers in the same layout (for FX_BUFFER style rendering by hand). */
unsigned fx_alloc_buffer(void);           /* 16 KB, returns a segment    */
void fx_target(unsigned seg);             /* draw there; 0 = the page    */
void fx_copy_rect(unsigned dst, unsigned src, int x0, int y0, int x1, int y1);
void fx_dirty(int x0, int y0, int x1, int y1); /* mark area changed (FX_BUFFER) */
unsigned fx_copy_delta(unsigned dst, unsigned src); /* copy only changed words; returns words written */
int  fx_screenshot(const char *path);     /* PPM of the page on screen   */

/* ---- Fixed-point math (16.16) ------------------------------------------ */

typedef long fix;
#define FIX_ONE    65536L
#define INT2FIX(i) ((fix)(i) << 16)
#define FIX2INT(f) ((int)((f) >> 16))
fix fix_mul(fix a, fix b);
fix fix_div(fix a, fix b);

/* Angles are 0-1023 for a full circle. */
#define ANG_FULL 1024
fix fix_sin(int ang);
fix fix_cos(int ang);
fix fix_sqrt(fix a);

/* ---- Keyboard ---------------------------------------------------------- */

/* Raw key state, indexed by scan code (KEY_* below). key_init() installs
 * a keyboard handler so games can see several keys held at once. */
extern volatile unsigned char key_down[128];
void key_init(void);
void key_done(void);

#define KEY_ESC   0x01
#define KEY_ENTER 0x1C
#define KEY_SPACE 0x39
#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_LEFT  0x4B
#define KEY_RIGHT 0x4D
#define KEY_A     0x1E
#define KEY_D     0x20
#define KEY_S     0x1F
#define KEY_W     0x11
#define KEY_Q     0x10
#define KEY_Z     0x2C
#define KEY_X     0x2D
#define KEY_CTRL  0x1D
#define KEY_ALT   0x38
#define KEY_LSHIFT 0x2A
#define KEY_PLUS  0x0D /* = / + key */
#define KEY_MINUS 0x0C

/* ---- Time -------------------------------------------------------------- */

void tmr_init(void);                 /* faster tick for timing (optional) */
void tmr_done(void);
unsigned long tmr_ms(void);          /* milliseconds since tmr_init      */
/* Raw PIT counts since tmr_init (TMR_HZ per second, ~0.84 us each). Much
 * cheaper than tmr_ms (no 32-bit division): use it to time parts of a frame.
 * TMR_TICKS_MS converts a difference to ms (0.1% low, no division). */
unsigned long tmr_ticks(void);
#define TMR_HZ 1193182UL
#define TMR_TICKS_MS(t) ((((unsigned long)(t) >> 10) * 55UL) >> 6)

/* ---- Sound (the PCjr's TI SN76496: 3 tone voices + noise) --------------- */

void snd_init(void);
void snd_done(void);                     /* silence and restore           */
void snd_tone(int voice, unsigned hz, int vol); /* voice 0-2, vol 0-15, 0 = off */
void snd_noise(int type, int vol);       /* type 0-7 per the SN76496      */

/* ---- Debug log --------------------------------------------------------- */

/* Appends a line to LOG.TXT ("T=<ms> <text>"), for the CodeJr test loop. */
void dbg_log(const char *fmt, ...);

#endif
