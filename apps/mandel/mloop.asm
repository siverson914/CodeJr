; MLOOP.ASM - the Mandelbrot inner loop for the 8088 (used by MANDEL.C).
;
; int __cdecl mandel_pt(int cx, int cy, int maxit, unsigned t1, unsigned t2);
;
; c and the orbit are 4.12 fixed point (12 fraction bits, range +-8).
; An 8088 IMUL takes about 130 cycles, so there is no multiply at all:
; squares come from tables, and x*y = (x+y)^2/4 - (x-y)^2/4.
;   t1: word at offset 2*i = (i/4096)^2 in 4.12, i = 0..32767 (|x| up to 8)
;   t2: word at offset 2*i = (i/4096)^2 / 4 in 4.12, i = -16384..16383
;       (negative i wrap into the top half of the segment)
; Returns how many iterations the point took to escape, or maxit.

        .8086
MLOOP_TEXT SEGMENT WORD PUBLIC 'CODE'
        ASSUME  CS:MLOOP_TEXT

cyv     DW      0
maxv    DW      0

        PUBLIC  _mandel_pt
_mandel_pt PROC FAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        mov     ax, [bp+8]
        mov     cs:[cyv], ax        ; cy
        mov     di, ax              ; y = cy
        mov     ax, [bp+10]
        mov     cs:[maxv], ax
        mov     cx, ax              ; iterations left
        mov     ds, [bp+12]         ; t1
        mov     es, [bp+14]         ; t2
        mov     si, [bp+6]          ; x = cx
        mov     bp, si              ; cx stays in bp

top:    mov     ax, si
        cwd
        xor     ax, dx
        sub     ax, dx              ; |x|
        add     ax, ax
        xchg    ax, bx
        mov     bx, [bx]            ; bx = x*x
        mov     ax, di
        cwd
        xor     ax, dx
        sub     ax, dx              ; |y|
        add     ax, ax
        xchg    ax, bx              ; ax = x*x, bx = 2|y|
        mov     dx, [bx]            ; dx = y*y
        mov     bx, ax
        add     bx, dx              ; x*x + y*y, unsigned
        cmp     bx, 16384           ; > 4.0 : escaped
        ja      done
        sub     ax, dx
        add     ax, bp              ; new x = x*x - y*y + cx
        mov     bx, si
        add     bx, di
        add     bx, bx
        mov     dx, es:[bx]         ; (x+y)^2 / 4
        sub     si, di
        add     si, si
        mov     bx, si
        mov     bx, es:[bx]         ; (x-y)^2 / 4
        sub     dx, bx              ; x*y
        add     dx, dx
        add     dx, cs:[cyv]        ; new y = 2*x*y + cy
        mov     di, dx
        mov     si, ax
        loop    top

done:   mov     ax, cs:[maxv]
        sub     ax, cx              ; iterations done
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
_mandel_pt ENDP

MLOOP_TEXT ENDS
        END
