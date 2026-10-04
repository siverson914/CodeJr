/* JRFLIGHT raster: packed 4-bit drawing (two pixels per byte, high nibble
 * = left pixel), dithered span fills, convex polygons, 3x5 text, and
 * copying to PCjr video memory (mode 8: two 8K banks, even/odd rows). */
#include <i86.h>
#include "jrflight.h"

unsigned char far scr[SCR_H][ROWB];
int vdirect = 1;
int rstep[2] = { ROWB, ROWB };
int pdirty0 = 999, pdirty1 = -1;
unsigned r_shown_seg;   /* page on screen (last frame's fx_seg), 0 if unknown */

void r_view_target(void)
{
    if (vdirect && !lores) { rstep[0] = 0x2000; rstep[1] = ROWB - 0x2000; }
    else { rstep[0] = ROWB; rstep[1] = ROWB; }
}

unsigned char far *r_row(int y)
{
    if (vdirect && !ldmode && y < VIEW_H)
        return (unsigned char far *)MK_FP(fx_seg, lores ? y * ROWB : jr_rowoff[y]);
    return scr[y];
}

/* Fill word for wide pixels: the even byte column gets the left color of
 * the pattern, the odd one the right color (a 2x2-pixel checker). */
unsigned wide_word(unsigned char p)
{
    unsigned char a = (unsigned char)((p & 0xF0) | (p >> 4));
    unsigned char b = (unsigned char)((p & 0x0F) | (p << 4));
    return a | ((unsigned)b << 8);
}

void r_hfill(unsigned char far *row, int x0, int x1, unsigned char pat)
{
    if (wide) span80(row, x0, x1, wide_word(pat));
    else span_asm(row, x0, x1, pat);
}

/* One horizontal run, pixels x0..x1-1, already clipped to 0..160. */
void r_span(int y, int x0, int x1, unsigned col)
{
    span_asm(r_row(y), x0, x1, (y & 1) ? (unsigned char)(col >> 8) : (unsigned char)col);
}

/* Sets up one polygon edge from vertex i to j, positioned at row y. */
static long edge(const int *px, const int *py, int i, int j, int y, long *slope)
{
    int dy = py[j] - py[i];
    int dx = px[j] - px[i];
    long s;
    if (dx >= 0) s = uslope((unsigned)dx, (unsigned)dy);
    else s = -uslope((unsigned)-dx, (unsigned)dy);
    *slope = s;
    if (y == py[i]) return ((long)px[i] << 16) + 0x8000L;
    return ((long)px[i] << 16) + 0x8000L + s * (y - py[i]);
}

/* Convex polygon, any winding, clipped to rows ytop..ybot-1 and x 0..159. */
void r_poly(int n, const int *px, const int *py, unsigned col, int ytop, int ybot)
{
    int i, top = 0, ymin = py[0], ymax = py[0];
    int y, yend, ia, ib, ya, yb, j, stop;
    long xa = 0, xb = 0, da = 0, db = 0;

    for (i = 1; i < n; i++) {
        if (py[i] < ymin) { ymin = py[i]; top = i; }
        if (py[i] > ymax) ymax = py[i];
    }
    y = ymin < ytop ? ytop : ymin;
    yend = ymax > ybot ? ybot : ymax;
    if (y >= yend) return;
    ia = ib = top;
    ya = yb = ymin;
    while (y < yend) {
        while (ya <= y) {
            j = ia + 1; if (j == n) j = 0;
            if (py[j] > py[ia]) xa = edge(px, py, ia, j, y, &da);
            ya = py[j]; ia = j;
        }
        while (yb <= y) {
            j = ib - 1; if (j < 0) j = n - 1;
            if (py[j] > py[ib]) xb = edge(px, py, ib, j, y, &db);
            yb = py[j]; ib = j;
        }
        stop = ya < yb ? ya : yb;
        if (stop > yend) stop = yend;
        if (y < stop) {
            /* The scanline loop, in assembly: about 30 instructions a row
             * plus the span itself. */
            unsigned char far *row = r_row(y);
            int s0 = rstep[y & 1], s1 = rstep[(y & 1) ^ 1];
            unsigned char p = (y & 1) ? (unsigned char)(col >> 8) : (unsigned char)col;
            unsigned char q = (y & 1) ? (unsigned char)col : (unsigned char)(col >> 8);
            int cnt = stop - y;
            int k0 = wx0, k1 = wx1;
            unsigned pw, qw;
            unsigned char wd = (unsigned char)wide;
            if (!dith_and) p = q = (unsigned char)col;
            if (wd) { pw = wide_word(p); qw = wide_word(q); } else pw = qw = 0;
            y = stop;
            __asm {
                push ds
                les di, row
            L_row:
                mov si, word ptr xa+2
                mov cx, word ptr xb+2
                cmp si, cx
                jle L_ord
                xchg si, cx
            L_ord:
                cmp si, k0
                jge L_c0
                mov si, k0
            L_c0:
                cmp cx, k1
                jle L_c1
                mov cx, k1
            L_c1:
                cmp si, cx
                jl L_draw
                jmp L_next
            L_draw:
                cmp wd, 0
                je L_narrow
                push di
                inc si
                shr si, 1
                shr cx, 1
                sub cx, si
                jle L_w3
                add di, si
                mov ax, pw
                test si, 1
                jz L_w1
                xchg al, ah
            L_w1:
                shr cx, 1
                rep stosw
                jnc L_w3
                stosb
            L_w3:
                pop di
                jmp L_next
            L_narrow:
                mov al, p
                push di
                dec cx
                test si, 1
                jz L_s1
                mov bx, si
                shr bx, 1
                mov ah, es:[bx+di]
                and ah, 0F0h
                mov dl, al
                and dl, 0Fh
                or ah, dl
                mov es:[bx+di], ah
                inc si
            L_s1:
                test cl, 1
                jnz L_s2
                mov bx, cx
                shr bx, 1
                mov ah, es:[bx+di]
                and ah, 0Fh
                mov dl, al
                and dl, 0F0h
                or ah, dl
                mov es:[bx+di], ah
                dec cx
            L_s2:
                sub cx, si
                jl L_s3
                inc cx
                shr cx, 1
                shr si, 1
                add di, si
                mov ah, al
                shr cx, 1
                rep stosw
                jnc L_s3
                stosb
            L_s3:
                pop di
            L_next:
                mov ax, word ptr da
                add word ptr xa, ax
                mov ax, word ptr da+2
                adc word ptr xa+2, ax
                mov ax, word ptr db
                add word ptr xb, ax
                mov ax, word ptr db+2
                adc word ptr xb+2, ax
                add di, s0
                mov ax, s0
                xchg ax, s1
                mov s0, ax
                mov ax, pw
                xchg ax, qw
                mov pw, ax
                mov al, p
                mov ah, q
                mov p, ah
                mov q, al
                dec word ptr cnt
                jz L_done
                jmp L_row
            L_done:
                pop ds
            }
        }
    }
}

void r_pset(int x, int y, int c)
{
    unsigned char far *b;
    if ((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
    b = r_row(y) + (x >> 1);
    if (x & 1) *b = (*b & 0xF0) | c;
    else *b = (*b & 0x0F) | (c << 4);
}

void r_line(int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 - x0, dy = y1 - y0, sx = 1, sy = 1, err, e2;
    unsigned char hi = (unsigned char)(c << 4);
    if (dx < 0) { dx = -dx; sx = -1; }
    if (dy < 0) { dy = -dy; sy = -1; }
    err = dx - dy;
    for (;;) {
        if ((unsigned)x0 < SCR_W && (unsigned)y0 < SCR_H) {
            unsigned char far *b = &scr[y0][x0 >> 1];   /* lines: panel only */
            if (x0 & 1) *b = (*b & 0xF0) | (unsigned char)c;
            else *b = (*b & 0x0F) | hi;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void r_rect(int x, int y, int w, int h, unsigned col)
{
    int x1 = x + w;
    if (x < 0) x = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (x >= x1) return;
    for (; h > 0; h--, y++)
        if ((unsigned)y < SCR_H) r_span(y, x, x1, col);
}

/* ---- 3x5 font ---------------------------------------------------------- */
static const char glyph_chars[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-.%/:!='";
static const unsigned char glyphs[][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,3,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,2,2},{7,5,7,5,7},{7,5,7,1,7},
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},
    {7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
    {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},
    {6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},
    {7,1,2,4,7},{0,2,7,2,0},{0,0,7,0,0},{0,0,0,0,2},{5,1,2,4,5},
    {1,1,2,4,4},{0,2,0,2,0},{2,2,2,0,2},{0,7,0,7,0},{2,2,0,0,0}
};

int r_text(int x, int y, const char *s, int c)
{
    static signed char map[96];
    static int ready;
    unsigned char hi = (unsigned char)(c << 4);
    if (!ready) {
        int i;
        for (i = 0; i < 96; i++) map[i] = -1;
        for (i = 0; glyph_chars[i]; i++) map[glyph_chars[i] - 32] = (signed char)i;
        ready = 1;
    }
    for (; *s; s++, x += 4) {
        int ch = *s, r, g, k;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 127 || (g = map[ch - 32]) < 0) continue;
        if (x < 0 || x + 2 >= SCR_W || y < 0 || y + 4 >= SCR_H) continue;
        for (r = 0; r < 5; r++) {
            unsigned char bits = glyphs[g][r];
            unsigned char far *row = r_row(y + r);
            for (k = 0; k < 3; k++, bits <<= 1) {
                if (bits & 4) {
                    int px = x + k;
                    unsigned char far *b = row + (px >> 1);
                    if (px & 1) *b = (*b & 0xF0) | (unsigned char)c;
                    else *b = (*b & 0x0F) | hi;
                }
            }
        }
    }
    return x;
}

int r_textw(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n * 4 - 1;
}

void r_text_sh(int x, int y, const char *s, int c)
{
    r_text(x + 1, y + 1, s, 0);
    r_text(x, y, s, c);
}

/* ---- Video ------------------------------------------------------------- */

/* Screen row y in video memory (the hidden page). In /LD mode rows are
 * linear and each is shown twice, so screen rows 2r and 2r+1 share row r. */
static unsigned char far *vrow(int y)
{
    unsigned seg = gfx_vseg ? gfx_vseg : 0xB800;
    if (ldmode) return (unsigned char far *)MK_FP(seg, (y >> 1) * ROWB);
    return (unsigned char far *)MK_FP(seg, jr_rowoff[y]);
}

void r_present_view(void)
{
    int y, b0 = wx0 >> 1, nw = (wx1 - wx0) >> 2;   /* window widths are multiples of 4 */
    if (vdirect && !ldmode) {
        /* already on the hidden page; low detail doubles bank 0 into bank 1 */
        if (lores)
            for (y = wy0; y < wy1; y++)
                copyw((unsigned char far *)MK_FP(fx_seg, 0x2000 + y * ROWB + b0),
                      (unsigned char far *)MK_FP(fx_seg, y * ROWB + b0), nw);
        return;
    }
    if (lores) {
        /* each buffer row r becomes screen rows 2r and 2r+1 */
        for (y = wy0; y < wy1; y++) {
            copyw(vrow(2 * y) + b0, scr[y] + b0, nw);
            if (!ldmode) copyw(vrow(2 * y + 1) + b0, scr[y] + b0, nw);
        }
    } else {
        for (y = wy0; y < wy1; y++) copyw(vrow(y) + b0, scr[y] + b0, nw);
    }
}

void r_present_rect(int x0, int y0, int x1, int y1)
{
    int b0 = x0 >> 1, n = (x1 >> 1) - b0 + 1, y;
    for (y = y0; y <= y1; y++)
        if (!ldmode || !(y & 1)) copyb(vrow(y) + b0, scr[y] + b0, n);
    if (r_shown_seg) {
        /* panel parts go to both pages at once: no full-row sync needed */
        for (y = y0; y <= y1; y++)
            copyb((unsigned char far *)MK_FP(r_shown_seg, jr_rowoff[y] + b0), scr[y] + b0, n);
        return;
    }
    if (y0 < pdirty0) pdirty0 = y0;
    if (y1 > pdirty1) pdirty1 = y1;
}

void r_present_all(void)
{
    int y;
    for (y = 0; y < SCR_H; y++)
        if (!ldmode || !(y & 1)) copyw(vrow(y), scr[y], ROWB / 2);
}

/* A cockpit frame around a window smaller than the windshield: dark metal
 * with a lighter inner edge, drawn in scr in screen coordinates. */
static void scr_rect(int x, int y, int w, int h, unsigned col)
{
    for (; h > 0; h--, y++)
        span_asm(scr[y], x, x + w, (y & 1) ? (unsigned char)(col >> 8) : (unsigned char)col);
}

void r_frame(void)
{
    int sx0 = wx0, sx1 = wx1, sy0 = wy0 << lores, sy1 = wy1 << lores;
    scr_rect(0, 0, SCR_W, VIEW_ROWS, DITH(8, 0));
    if (sx0 > 0) {
        scr_rect(sx0 - 2, sy0 - 2, sx1 - sx0 + 4, 2, SOLID(7));
        scr_rect(sx0 - 2, sy1, sx1 - sx0 + 4, 2, SOLID(7));
        scr_rect(sx0 - 2, sy0, 2, sy1 - sy0, SOLID(7));
        scr_rect(sx1, sy0, 2, sy1 - sy0, SOLID(7));
        scr_rect(sx0 - 6, sy0 + (sy1 - sy0) / 2, 2, 2, SOLID(15));   /* rivets */
        scr_rect(sx1 + 4, sy0 + (sy1 - sy0) / 2, 2, 2, SOLID(15));
    }
    scr_rect(sx0, sy0, sx1 - sx0, sy1 - sy0, SOLID(0));
}

/* Unpacks the composed hidden page into JRGFX's byte-per-pixel back buffer
 * and saves it as a PPM: exactly what the next flip will show. */
int r_shot(const char *path)
{
    int x, y;
    unsigned char far *d = gfx_back;
    for (y = 0; y < SCR_H; y++) {
        unsigned char far *v = vrow(y);
        for (x = 0; x < ROWB; x++) {
            unsigned char b = v[x];
            *d++ = b >> 4;
            *d++ = b & 15;
        }
    }
    return gfx_screenshot(path);
}
