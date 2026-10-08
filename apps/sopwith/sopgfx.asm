; Drawing inner loops for Sopwith 2 (WASM, small model).
; New file, written 2026-10-07 for the CodeJr PCjr port. The game's own
; putimage and drawground did this work in C, a byte or a pixel at a time;
; on a real PCjr the C around each sprite row and each ground pixel cost
; more than the drawing. The pictures are the same, byte for byte.

.8086
        name    sopgfx

DGROUP  group   _DATA

_DATA   segment word public 'DATA'
        public  _cj_bskip, _cj_bseg
_cj_bseg dw     0       ; the screen segment
_cj_bskip dw    0       ; source bytes to skip after each row (clipped at the
                        ; right edge), or negative: write the shifted-out byte
rowsleft dw     0
roww    dw      0
_DATA   ends

_TEXT   segment byte public 'CODE'
        assume  cs:_TEXT, ds:DGROUP

        public  cj_blit_, cj_ground_

; void cj_blit(off, src, tab, width, height, linediff)
;   cj_bseg = screen segment, DI = offset of the first row's first byte,
;   SI = sprite bytes, BX = table pair (256-aligned: page 0 the byte shifted
;   into place, page 1 the pixels shifted out), CX = bytes a row (> 0),
;   DX = rows (> 0), AX = distance to the next row; after each row it
;   becomes 80 minus itself (the two banks of the screen).
cj_blit_ proc near
        push    bp
        mov     bp, ax
        mov     es, _cj_bseg
        mov     rowsleft, dx
        mov     roww, cx
brow:   mov     cx, roww
        push    di
        xor     dl, dl
bbyte:  lodsb
        mov     bl, al
        mov     al, [bx]
        or      al, dl
        mov     dl, [bx + 256]
        xor     es:[di], al
        inc     di
        loop    bbyte
        mov     ax, _cj_bskip
        or      ax, ax
        js      btrail
        add     si, ax
        jmp     short badv
btrail: or      dl, dl
        jz      badv
        xor     es:[di], dl
badv:   pop     di
        add     di, bp
        neg     bp
        add     bp, 80
        dec     rowsleft
        jnz     brow
        pop     bp
        ret
cj_blit_ endp

; void cj_ground(seg, base, heights, rows)
;   AX = screen segment, DX = scroff (offset of byte column 0), SI = 320
;   ground heights, DI = the row offset table (words). Flips the pixels of
;   the ground line: in each column, from the height the line came in at to
;   this column's height.
cj_ground_ proc near
        push    bp
        mov     es, ax
        mov     bp, dx          ; scroff + byte column
        mov     al, [si]        ; the pen's height
        xor     ah, ah
        shl     ax, 1
        add     di, ax          ; DI -> row offset of the pen's row
        mov     al, [si]
        mov     dl, 0C0h        ; the pixel mask
        mov     cx, 320
gcol:   mov     ah, [si]
        inc     si
        cmp     ah, al
        je      gsame
        ja      gup
gdown:  mov     bx, [di]
        add     bx, bp
        xor     es:[bx], dl
        dec     al
        sub     di, 2
        cmp     al, ah
        jne     gdown
        jmp     short gnext
gup:    mov     bx, [di]
        add     bx, bp
        xor     es:[bx], dl
        inc     al
        add     di, 2
        cmp     al, ah
        jne     gup
        jmp     short gnext
gsame:  mov     bx, [di]
        add     bx, bp
        xor     es:[bx], dl
gnext:  shr     dl, 1
        shr     dl, 1
        jnz     gn2
        mov     dl, 0C0h
        inc     bp
gn2:    loop    gcol
        pop     bp
        ret
cj_ground_ endp

_TEXT   ends
        end
