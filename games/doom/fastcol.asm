; JRDOOM inner loop: draws one 2-pixel-wide view column as vertical runs.
; 8088 only. Called from render.c (see the #pragma aux there).
;
; draw_runs(es:di = column in the view buffer (row 0),
;           dx:si = runs: len, keep-mask, even-row byte, odd-row byte; len 0 ends,
;           bx = top of the first run in rows*16 (may be negative),
;           cx = rows*16 per unit of len)
; keep-mask 0 = opaque, FFh = transparent, else bits kept from the screen.
; The view is drawn straight into the hidden video page (fx_seg): even rows,
; then odd rows BANK (8K) bytes later, 80 bytes a row.
; Opaque runs jump into an unrolled block: 2 instructions per pair of rows.

.8086
        name    fastcol

VIEW_H  equ     144
BANK    equ     2000h          ; odd rows: the PCjr page layout

fastcol_TEXT segment byte public 'CODE'
        assume  cs:fastcol_TEXT

rowtab  label word
        dw      0,80,160,240,320,400,480,560
        dw      640,720,800,880,960,1040,1120,1200
        dw      1280,1360,1440,1520,1600,1680,1760,1840
        dw      1920,2000,2080,2160,2240,2320,2400,2480
        dw      2560,2640,2720,2800,2880,2960,3040,3120
        dw      3200,3280,3360,3440,3520,3600,3680,3760
        dw      3840,3920,4000,4080,4160,4240,4320,4400
        dw      4480,4560,4640,4720,4800,4880,4960,5040
        dw      5120,5200,5280,5360,5440,5520,5600,5680
        dw      5760
; jtab[i] = entry that writes 72-i row pairs
jtab    label word
        dw      offset u0,offset u1,offset u2,offset u3,offset u4,offset u5
        dw      offset u6,offset u7,offset u8,offset u9,offset u10,offset u11
        dw      offset u12,offset u13,offset u14,offset u15,offset u16,offset u17
        dw      offset u18,offset u19,offset u20,offset u21,offset u22,offset u23
        dw      offset u24,offset u25,offset u26,offset u27,offset u28,offset u29
        dw      offset u30,offset u31,offset u32,offset u33,offset u34,offset u35
        dw      offset u36,offset u37,offset u38,offset u39,offset u40,offset u41
        dw      offset u42,offset u43,offset u44,offset u45,offset u46,offset u47
        dw      offset u48,offset u49,offset u50,offset u51,offset u52,offset u53
        dw      offset u54,offset u55,offset u56,offset u57,offset u58,offset u59
        dw      offset u60,offset u61,offset u62,offset u63,offset u64,offset u65
        dw      offset u66,offset u67,offset u68,offset u69,offset u70,offset u71
cbase   dw      0
step    dw      0
clip_h  dw      VIEW_H          ; rows in the view window (set_clip)
clip_h16 dw     VIEW_H*16

; set_clip(ax = rows): the view window height used by draw_runs.
        public  set_clip_
set_clip_ proc far
        mov     cs:clip_h,ax
        push    cx
        mov     cl,4
        shl     ax,cl
        pop     cx
        mov     cs:clip_h16,ax
        ret
set_clip_ endp

        public  draw_runs_
draw_runs_ proc far
        push    bp
        push    ds
        mov     ds,dx
        mov     cs:cbase,di
        mov     cs:step,cx
        mov     bp,bx           ; bp = y (rows*16)
        mov     cl,4
next:
        lodsw                   ; al = len, ah = keep
        or      al,al
        jz      done
        mov     ch,ah
        xor     ah,ah
        mul     word ptr cs:step
        mov     dx,bp
        add     bp,ax
        sar     dx,cl           ; r0
        mov     ax,bp
        sar     ax,cl           ; r1
        cmp     ax,cs:clip_h
        jle     c1
        mov     ax,cs:clip_h
c1:     or      dx,dx
        jns     c2
        xor     dx,dx
c2:     sub     ax,dx           ; rows to fill
        jle     skip
        mov     di,dx
        and     di,0FFFEh
        mov     di,cs:rowtab[di]
        add     di,cs:cbase
        or      ch,ch
        jnz     masked
        test    dl,1
        mov     dx,ax           ; dx = rows
        lodsw                   ; al = even-row byte, ah = odd-row byte
        jz      o_even
        mov     es:[di+BANK],ah
        add     di,80
        dec     dx
o_even: mov     bx,dx
        shr     bx,1            ; row pairs
        jz      o_tail
        neg     bx
        add     bx,72
        shl     bx,1
        sub     di,cs:rowtab[bx]
        call    word ptr cs:jtab[bx]
o_tail: test    dl,1
        jz      o_next
        mov     es:[di],al
o_next: cmp     bp,cs:clip_h16
        jl      next
done:   pop     ds
        pop     bp
        ret

skip:   add     si,2
        cmp     bp,cs:clip_h16
        jl      next
        jmp     short done

masked: cmp     ch,0FFh
        je      m_skip
        test    dl,1
        mov     dx,ax
        lodsw                   ; al = byte (same for both rows)
        mov     bx,dx
        jz      m_even
        mov     ah,es:[di+BANK]
        and     ah,ch
        or      ah,al
        mov     es:[di+BANK],ah
        add     di,80
        dec     bx
m_even: mov     dx,bx
        shr     bx,1
        jz      m_tail
m_loop: mov     ah,es:[di]
        and     ah,ch
        or      ah,al
        mov     es:[di],ah
        mov     ah,es:[di+BANK]
        and     ah,ch
        or      ah,al
        mov     es:[di+BANK],ah
        add     di,80
        dec     bx
        jnz     m_loop
m_tail: test    dl,1
        jz      o_next
        mov     ah,es:[di]
        and     ah,ch
        or      ah,al
        mov     es:[di],ah
        jmp     short o_next
m_skip: add     si,2
        jmp     short o_next

; 72 x { mov es:[di+k*80],al / mov es:[di+BANK+k*80],ah }, all with
; 16-bit displacements; entry k writes pairs k..71, then moves di on.
; Called (near) through jtab with di moved back by (72 - pairs) rows.
u0:     db      26h,88h,85h,0,0,26h,88h,0A5h,0,32
u1:     db      26h,88h,85h,80,0,26h,88h,0A5h,80,32
u2:     db      26h,88h,85h,160,0,26h,88h,0A5h,160,32
u3:     db      26h,88h,85h,240,0,26h,88h,0A5h,240,32
u4:     db      26h,88h,85h,64,1,26h,88h,0A5h,64,33
u5:     db      26h,88h,85h,144,1,26h,88h,0A5h,144,33
u6:     db      26h,88h,85h,224,1,26h,88h,0A5h,224,33
u7:     db      26h,88h,85h,48,2,26h,88h,0A5h,48,34
u8:     db      26h,88h,85h,128,2,26h,88h,0A5h,128,34
u9:     db      26h,88h,85h,208,2,26h,88h,0A5h,208,34
u10:     db      26h,88h,85h,32,3,26h,88h,0A5h,32,35
u11:     db      26h,88h,85h,112,3,26h,88h,0A5h,112,35
u12:     db      26h,88h,85h,192,3,26h,88h,0A5h,192,35
u13:     db      26h,88h,85h,16,4,26h,88h,0A5h,16,36
u14:     db      26h,88h,85h,96,4,26h,88h,0A5h,96,36
u15:     db      26h,88h,85h,176,4,26h,88h,0A5h,176,36
u16:     db      26h,88h,85h,0,5,26h,88h,0A5h,0,37
u17:     db      26h,88h,85h,80,5,26h,88h,0A5h,80,37
u18:     db      26h,88h,85h,160,5,26h,88h,0A5h,160,37
u19:     db      26h,88h,85h,240,5,26h,88h,0A5h,240,37
u20:     db      26h,88h,85h,64,6,26h,88h,0A5h,64,38
u21:     db      26h,88h,85h,144,6,26h,88h,0A5h,144,38
u22:     db      26h,88h,85h,224,6,26h,88h,0A5h,224,38
u23:     db      26h,88h,85h,48,7,26h,88h,0A5h,48,39
u24:     db      26h,88h,85h,128,7,26h,88h,0A5h,128,39
u25:     db      26h,88h,85h,208,7,26h,88h,0A5h,208,39
u26:     db      26h,88h,85h,32,8,26h,88h,0A5h,32,40
u27:     db      26h,88h,85h,112,8,26h,88h,0A5h,112,40
u28:     db      26h,88h,85h,192,8,26h,88h,0A5h,192,40
u29:     db      26h,88h,85h,16,9,26h,88h,0A5h,16,41
u30:     db      26h,88h,85h,96,9,26h,88h,0A5h,96,41
u31:     db      26h,88h,85h,176,9,26h,88h,0A5h,176,41
u32:     db      26h,88h,85h,0,10,26h,88h,0A5h,0,42
u33:     db      26h,88h,85h,80,10,26h,88h,0A5h,80,42
u34:     db      26h,88h,85h,160,10,26h,88h,0A5h,160,42
u35:     db      26h,88h,85h,240,10,26h,88h,0A5h,240,42
u36:     db      26h,88h,85h,64,11,26h,88h,0A5h,64,43
u37:     db      26h,88h,85h,144,11,26h,88h,0A5h,144,43
u38:     db      26h,88h,85h,224,11,26h,88h,0A5h,224,43
u39:     db      26h,88h,85h,48,12,26h,88h,0A5h,48,44
u40:     db      26h,88h,85h,128,12,26h,88h,0A5h,128,44
u41:     db      26h,88h,85h,208,12,26h,88h,0A5h,208,44
u42:     db      26h,88h,85h,32,13,26h,88h,0A5h,32,45
u43:     db      26h,88h,85h,112,13,26h,88h,0A5h,112,45
u44:     db      26h,88h,85h,192,13,26h,88h,0A5h,192,45
u45:     db      26h,88h,85h,16,14,26h,88h,0A5h,16,46
u46:     db      26h,88h,85h,96,14,26h,88h,0A5h,96,46
u47:     db      26h,88h,85h,176,14,26h,88h,0A5h,176,46
u48:     db      26h,88h,85h,0,15,26h,88h,0A5h,0,47
u49:     db      26h,88h,85h,80,15,26h,88h,0A5h,80,47
u50:     db      26h,88h,85h,160,15,26h,88h,0A5h,160,47
u51:     db      26h,88h,85h,240,15,26h,88h,0A5h,240,47
u52:     db      26h,88h,85h,64,16,26h,88h,0A5h,64,48
u53:     db      26h,88h,85h,144,16,26h,88h,0A5h,144,48
u54:     db      26h,88h,85h,224,16,26h,88h,0A5h,224,48
u55:     db      26h,88h,85h,48,17,26h,88h,0A5h,48,49
u56:     db      26h,88h,85h,128,17,26h,88h,0A5h,128,49
u57:     db      26h,88h,85h,208,17,26h,88h,0A5h,208,49
u58:     db      26h,88h,85h,32,18,26h,88h,0A5h,32,50
u59:     db      26h,88h,85h,112,18,26h,88h,0A5h,112,50
u60:     db      26h,88h,85h,192,18,26h,88h,0A5h,192,50
u61:     db      26h,88h,85h,16,19,26h,88h,0A5h,16,51
u62:     db      26h,88h,85h,96,19,26h,88h,0A5h,96,51
u63:     db      26h,88h,85h,176,19,26h,88h,0A5h,176,51
u64:     db      26h,88h,85h,0,20,26h,88h,0A5h,0,52
u65:     db      26h,88h,85h,80,20,26h,88h,0A5h,80,52
u66:     db      26h,88h,85h,160,20,26h,88h,0A5h,160,52
u67:     db      26h,88h,85h,240,20,26h,88h,0A5h,240,52
u68:     db      26h,88h,85h,64,21,26h,88h,0A5h,64,53
u69:     db      26h,88h,85h,144,21,26h,88h,0A5h,144,53
u70:     db      26h,88h,85h,224,21,26h,88h,0A5h,224,53
u71:     db      26h,88h,85h,48,22,26h,88h,0A5h,48,54
        add     di,5760         ; 72 * 80: pairs * 80 past where di started
        retn
draw_runs_ endp


; draw_wall_runs(es:di = column in the view buffer (row 0),
;                dx:si = runs: end texel (1-16), even-row byte, odd-row byte; 0 ends,
;                bx = offset (in the runs segment) of 17 bytes: the screen row
;                     of each texel boundary for this wall height, clipped)
        public  draw_wall_runs_
draw_wall_runs_ proc far
        push    bp
        push    ds
        mov     ds,dx
        mov     dl,[bx]         ; r0: first row
        xor     dh,dh
        mov     bp,dx
        and     bp,0FFFEh
        add     di,cs:rowtab[bp]
w_next: lodsb                   ; end texel of this run
        or      al,al
        jz      w_done
        xlat                    ; al = r1, the row it ends on
        mov     cl,al
        lodsw                   ; al = even-row byte, ah = odd-row byte
        sub     cl,dl           ; rows
        jz      w_next
        mov     ch,dl
        add     dl,cl
        test    ch,1
        jz      w_even
        mov     es:[di+BANK],ah
        add     di,80
        dec     cl
w_even: xor     ch,ch
        mov     bp,cx
        shr     bp,1            ; row pairs
        jz      w_tail
        neg     bp
        add     bp,72
        shl     bp,1
        sub     di,cs:rowtab[bp]
        call    word ptr cs:jtab[bp]
w_tail: test    cl,1
        jz      w_next
        mov     es:[di],al
        jmp     w_next
w_done: pop     ds
        pop     bp
        ret
draw_wall_runs_ endp


; cast_col(dx:bx = CastCtx far *, ax = column): casts one ray and stores
; its wall height, map cell, texture column, material and side in the
; context (see the CastCtx struct in render.c for the layout).
C_PX    equ     0
C_PY    equ     2
C_DIRX  equ     4
C_DIRY  equ     6
C_PLX   equ     8
C_PLY   equ     10
C_START equ     12
C_MAP   equ     14
C_WALLK equ     18
C_CAMX  equ     20
C_WALLH equ     180
C_HIDX  equ     340
C_HU    equ     500
C_HMAT  equ     580
C_HSIDE equ     660
C_DOPEN equ     740
C_DMAT  equ     804

k_ctx   dw      0
k_col   dw      0
k_rdx   dw      0
k_rdy   dw      0
k_ddx   dw      0
k_ddy   dw      0
k_stepy dw      0
k_sdx   dw      0
k_sdy   dw      0
k_map   dw      0

        public  cast_col_
cast_col_ proc far
        push    bp
        push    ds
        push    si
        push    di
        mov     ds,dx
        mov     si,bx
        mov     cs:k_ctx,bx
        mov     cs:k_col,ax
        mov     bx,ax
        shl     bx,1
        mov     cx,[si+bx+C_CAMX]
        mov     ax,[si+C_PLX]           ; rdx = dirx + plx * camx >> 14
        imul    cx
        shl     ax,1
        rcl     dx,1
        shl     ax,1
        rcl     dx,1
        add     dx,[si+C_DIRX]
        mov     cs:k_rdx,dx
        mov     ax,[si+C_PLY]           ; rdy = diry + ply * camx >> 14
        imul    cx
        shl     ax,1
        rcl     dx,1
        shl     ax,1
        rcl     dx,1
        add     dx,[si+C_DIRY]
        mov     cs:k_rdy,dx
        ; x: distance between x crossings, distance to the first one
        mov     ax,cs:k_rdx
        cwd
        xor     ax,dx
        sub     ax,dx                   ; |rdx|
        mov     bx,ax
        mov     ax,16384
        cmp     bx,256
        jb      kx1
        mov     dx,64
        xor     ax,ax
        div     bx                      ; 0x400000 / |rdx|
kx1:    mov     cs:k_ddx,ax
        mov     bx,[si+C_PX]
        and     bx,255
        mov     bp,-1
        cmp     cs:k_rdx,0
        jl      kx2
        neg     bx
        add     bx,256
        mov     bp,1
kx2:    mul     bx
        mov     al,ah
        mov     ah,dl
        mov     cs:k_sdx,ax
        ; y
        mov     ax,cs:k_rdy
        cwd
        xor     ax,dx
        sub     ax,dx
        mov     bx,ax
        mov     ax,16384
        cmp     bx,256
        jb      ky1
        mov     dx,64
        xor     ax,ax
        div     bx
ky1:    mov     cs:k_ddy,ax
        mov     bx,[si+C_PY]
        and     bx,255
        mov     cs:k_stepy,-32
        cmp     cs:k_rdy,0
        jl      ky2
        neg     bx
        add     bx,256
        mov     cs:k_stepy,32
ky2:    mul     bx
        mov     al,ah
        mov     ah,dl
        mov     di,ax                   ; di = sdy
        mov     bx,[si+C_START]
        add     bx,[si+C_MAP]
        mov     es,[si+C_MAP+2]
        mov     si,cs:k_sdx             ; si = sdx
        mov     cx,cs:k_ddx
        mov     dx,cs:k_ddy
k_loop: cmp     si,di
        jae     k_y
        add     si,cx
        add     bx,bp
        mov     al,es:[bx]
        or      al,al
        jz      k_loop
        xor     ah,ah                   ; side 0: crossed an x line
        jmp     short k_hit
k_y:    add     di,dx
        add     bx,cs:k_stepy
        mov     al,es:[bx]
        or      al,al
        jz      k_loop
        mov     ah,1                    ; side 1: crossed a y line
k_hit:  mov     cs:k_sdx,si
        mov     cs:k_sdy,di
        mov     cs:k_map,bx
        mov     si,cs:k_ctx
        test    al,80h
        jnz     k_door
        or      ah,ah
        jnz     k_wy
        mov     bx,cs:k_sdx             ; perp = sdx - ddx
        sub     bx,cs:k_ddx
        mov     cx,cs:k_rdy
        mov     dx,[si+C_PY]
        call    k_u                     ; u = py + perp * rdy
        cmp     cs:k_rdx,0
        jle     k_fin
        not     dl
        jmp     short k_fin
k_wy:   mov     bx,cs:k_sdy             ; perp = sdy - ddy
        sub     bx,cs:k_ddy
        mov     cx,cs:k_rdx
        mov     dx,[si+C_PX]
        call    k_u                     ; u = px + perp * rdx
        cmp     cs:k_rdy,0
        jge     k_fin
        not     dl
k_fin:  ; bx = perp, dl = u, al = material, ah = side
        mov     cx,cs:k_map
        sub     cx,[si+C_MAP]
        mov     di,bx                   ; di = perp
        mov     bx,cs:k_col
        mov     [si+bx+C_HU],dl
        mov     [si+bx+C_HMAT],al
        mov     [si+bx+C_HSIDE],ah
        shl     bx,1
        mov     [si+bx+C_HIDX],cx
        cmp     di,26
        jae     k_f1
        mov     di,26
k_f1:   mov     ax,[si+C_WALLK]
        xor     dx,dx
        div     di
        mov     [si+bx+C_WALLH],ax
        pop     di
        pop     si
        pop     ds
        pop     bp
        ret

k_door: ; doors sit mid-cell and slide open sideways
        push    ax
        or      ah,ah
        jnz     kd_y
        mov     bx,cs:k_ddx
        shr     bx,1
        neg     bx
        add     bx,cs:k_sdx             ; half = sdx - ddx / 2
        cmp     bx,cs:k_sdy
        jae     kd_pass                 ; leaves the cell before the door
        mov     cx,cs:k_rdy
        mov     dx,[si+C_PY]
        jmp     short kd_u
kd_y:   mov     bx,cs:k_ddy
        shr     bx,1
        neg     bx
        add     bx,cs:k_sdy
        cmp     bx,cs:k_sdx
        jae     kd_pass
        mov     cx,cs:k_rdx
        mov     dx,[si+C_PX]
kd_u:   call    k_u                     ; dl = u
        pop     ax
        mov     di,bx                   ; di = half
        mov     bx,ax
        and     bx,3Fh
        xor     dh,dh
        shl     bx,1
        sub     dx,[si+bx+C_DOPEN]
        jb      kd_back                 ; through the open part
        shr     bx,1
        mov     al,[si+bx+C_DMAT]
        mov     bx,di
        jmp     k_fin
kd_pass: pop    ax
kd_back: mov    si,cs:k_sdx
        mov     di,cs:k_sdy
        mov     bx,cs:k_map
        mov     cx,cs:k_ddx
        mov     dx,cs:k_ddy
        jmp     k_loop

; dl = (dx + bx * cx >> 14) & 255; keeps ax, bx
k_u:    push    ax
        push    dx
        mov     ax,bx
        imul    cx
        shl     ax,1
        rcl     dx,1
        shl     ax,1
        rcl     dx,1
        pop     ax
        add     dx,ax
        pop     ax
        retn
cast_col_ endp

; draw_wall_runs2: draw_wall_runs for low detail, 2 bytes (4 pixels) wide.
w_r0    db      0
jtab2   label word
        dw      offset v0,offset v1,offset v2,offset v3,offset v4,offset v5
        dw      offset v6,offset v7,offset v8,offset v9,offset v10,offset v11
        dw      offset v12,offset v13,offset v14,offset v15,offset v16,offset v17
        dw      offset v18,offset v19,offset v20,offset v21,offset v22,offset v23
        dw      offset v24,offset v25,offset v26,offset v27,offset v28,offset v29
        dw      offset v30,offset v31,offset v32,offset v33,offset v34,offset v35
        dw      offset v36,offset v37,offset v38,offset v39,offset v40,offset v41
        dw      offset v42,offset v43,offset v44,offset v45,offset v46,offset v47
        dw      offset v48,offset v49,offset v50,offset v51,offset v52,offset v53
        dw      offset v54,offset v55,offset v56,offset v57,offset v58,offset v59
        dw      offset v60,offset v61,offset v62,offset v63,offset v64,offset v65
        dw      offset v66,offset v67,offset v68,offset v69,offset v70,offset v71
        public  draw_wall_runs2_
draw_wall_runs2_ proc far
        push    bp
        push    ds
        mov     ds,dx
        mov     al,[bx]         ; r0: first row
        mov     cs:w_r0,al
        xor     ah,ah
        mov     bp,ax
        and     bp,0FFFEh
        add     di,cs:rowtab[bp]
x_next: lodsb
        or      al,al
        jz      x_done
        xlat                    ; al = r1
        mov     cl,al
        lodsw                   ; al = even-row byte, ah = odd-row byte
        mov     ch,cs:w_r0
        sub     cl,ch           ; rows
        jz      x_next
        add     cs:w_r0,cl
        mov     dl,ah
        mov     dh,ah           ; dx = odd-row word
        mov     ah,al           ; ax = even-row word
        test    ch,1
        jz      x_even
        mov     es:[di+BANK],dx
        add     di,80
        dec     cl
x_even: xor     ch,ch
        mov     bp,cx
        shr     bp,1
        jz      x_tail
        neg     bp
        add     bp,72
        shl     bp,1
        sub     di,cs:rowtab[bp]
        call    word ptr cs:jtab2[bp]
x_tail: test    cl,1
        jz      x_next
        mov     es:[di],ax
        jmp     x_next
x_done: pop     ds
        pop     bp
        ret
; 72 x { mov es:[di+k*80],ax / mov es:[di+BANK+k*80],dx }
v0:     db      26h,89h,85h,0,0,26h,89h,95h,0,32
v1:     db      26h,89h,85h,80,0,26h,89h,95h,80,32
v2:     db      26h,89h,85h,160,0,26h,89h,95h,160,32
v3:     db      26h,89h,85h,240,0,26h,89h,95h,240,32
v4:     db      26h,89h,85h,64,1,26h,89h,95h,64,33
v5:     db      26h,89h,85h,144,1,26h,89h,95h,144,33
v6:     db      26h,89h,85h,224,1,26h,89h,95h,224,33
v7:     db      26h,89h,85h,48,2,26h,89h,95h,48,34
v8:     db      26h,89h,85h,128,2,26h,89h,95h,128,34
v9:     db      26h,89h,85h,208,2,26h,89h,95h,208,34
v10:     db      26h,89h,85h,32,3,26h,89h,95h,32,35
v11:     db      26h,89h,85h,112,3,26h,89h,95h,112,35
v12:     db      26h,89h,85h,192,3,26h,89h,95h,192,35
v13:     db      26h,89h,85h,16,4,26h,89h,95h,16,36
v14:     db      26h,89h,85h,96,4,26h,89h,95h,96,36
v15:     db      26h,89h,85h,176,4,26h,89h,95h,176,36
v16:     db      26h,89h,85h,0,5,26h,89h,95h,0,37
v17:     db      26h,89h,85h,80,5,26h,89h,95h,80,37
v18:     db      26h,89h,85h,160,5,26h,89h,95h,160,37
v19:     db      26h,89h,85h,240,5,26h,89h,95h,240,37
v20:     db      26h,89h,85h,64,6,26h,89h,95h,64,38
v21:     db      26h,89h,85h,144,6,26h,89h,95h,144,38
v22:     db      26h,89h,85h,224,6,26h,89h,95h,224,38
v23:     db      26h,89h,85h,48,7,26h,89h,95h,48,39
v24:     db      26h,89h,85h,128,7,26h,89h,95h,128,39
v25:     db      26h,89h,85h,208,7,26h,89h,95h,208,39
v26:     db      26h,89h,85h,32,8,26h,89h,95h,32,40
v27:     db      26h,89h,85h,112,8,26h,89h,95h,112,40
v28:     db      26h,89h,85h,192,8,26h,89h,95h,192,40
v29:     db      26h,89h,85h,16,9,26h,89h,95h,16,41
v30:     db      26h,89h,85h,96,9,26h,89h,95h,96,41
v31:     db      26h,89h,85h,176,9,26h,89h,95h,176,41
v32:     db      26h,89h,85h,0,10,26h,89h,95h,0,42
v33:     db      26h,89h,85h,80,10,26h,89h,95h,80,42
v34:     db      26h,89h,85h,160,10,26h,89h,95h,160,42
v35:     db      26h,89h,85h,240,10,26h,89h,95h,240,42
v36:     db      26h,89h,85h,64,11,26h,89h,95h,64,43
v37:     db      26h,89h,85h,144,11,26h,89h,95h,144,43
v38:     db      26h,89h,85h,224,11,26h,89h,95h,224,43
v39:     db      26h,89h,85h,48,12,26h,89h,95h,48,44
v40:     db      26h,89h,85h,128,12,26h,89h,95h,128,44
v41:     db      26h,89h,85h,208,12,26h,89h,95h,208,44
v42:     db      26h,89h,85h,32,13,26h,89h,95h,32,45
v43:     db      26h,89h,85h,112,13,26h,89h,95h,112,45
v44:     db      26h,89h,85h,192,13,26h,89h,95h,192,45
v45:     db      26h,89h,85h,16,14,26h,89h,95h,16,46
v46:     db      26h,89h,85h,96,14,26h,89h,95h,96,46
v47:     db      26h,89h,85h,176,14,26h,89h,95h,176,46
v48:     db      26h,89h,85h,0,15,26h,89h,95h,0,47
v49:     db      26h,89h,85h,80,15,26h,89h,95h,80,47
v50:     db      26h,89h,85h,160,15,26h,89h,95h,160,47
v51:     db      26h,89h,85h,240,15,26h,89h,95h,240,47
v52:     db      26h,89h,85h,64,16,26h,89h,95h,64,48
v53:     db      26h,89h,85h,144,16,26h,89h,95h,144,48
v54:     db      26h,89h,85h,224,16,26h,89h,95h,224,48
v55:     db      26h,89h,85h,48,17,26h,89h,95h,48,49
v56:     db      26h,89h,85h,128,17,26h,89h,95h,128,49
v57:     db      26h,89h,85h,208,17,26h,89h,95h,208,49
v58:     db      26h,89h,85h,32,18,26h,89h,95h,32,50
v59:     db      26h,89h,85h,112,18,26h,89h,95h,112,50
v60:     db      26h,89h,85h,192,18,26h,89h,95h,192,50
v61:     db      26h,89h,85h,16,19,26h,89h,95h,16,51
v62:     db      26h,89h,85h,96,19,26h,89h,95h,96,51
v63:     db      26h,89h,85h,176,19,26h,89h,95h,176,51
v64:     db      26h,89h,85h,0,20,26h,89h,95h,0,52
v65:     db      26h,89h,85h,80,20,26h,89h,95h,80,52
v66:     db      26h,89h,85h,160,20,26h,89h,95h,160,52
v67:     db      26h,89h,85h,240,20,26h,89h,95h,240,52
v68:     db      26h,89h,85h,64,21,26h,89h,95h,64,53
v69:     db      26h,89h,85h,144,21,26h,89h,95h,144,53
v70:     db      26h,89h,85h,224,21,26h,89h,95h,224,53
v71:     db      26h,89h,85h,48,22,26h,89h,95h,48,54
        add     di,5760
        retn
draw_wall_runs2_ endp

; draw_runs2: draw_runs for low detail, 2 bytes wide, opaque runs only
; (world sprites have no half-transparent bytes).
        public  draw_runs2_
draw_runs2_ proc far
        push    bp
        push    ds
        mov     ds,dx
        mov     cs:cbase,di
        mov     cs:step,cx
        mov     bp,bx
        mov     cl,4
r2_next: lodsw                  ; al = len, ah = keep
        or      al,al
        jz      r2_done
        mov     ch,ah
        xor     ah,ah
        mul     word ptr cs:step
        mov     dx,bp
        add     bp,ax
        sar     dx,cl           ; r0
        mov     ax,bp
        sar     ax,cl           ; r1
        cmp     ax,cs:clip_h
        jle     r2_a1
        mov     ax,cs:clip_h
r2_a1:  or      dx,dx
        jns     r2_a2
        xor     dx,dx
r2_a2:  sub     ax,dx
        jle     r2_skip
        or      ch,ch
        jnz     r2_skip         ; transparent
        mov     di,dx
        and     di,0FFFEh
        mov     di,cs:rowtab[di]
        add     di,cs:cbase
        mov     bx,ax           ; rows
        test    dl,1
        lodsw
        mov     dl,ah
        mov     dh,ah           ; dx = odd-row word
        mov     ah,al           ; ax = even-row word
        jz      r2_even
        mov     es:[di+BANK],dx
        add     di,80
        dec     bx
r2_even: push   bx
        shr     bx,1
        jz      r2_tail
        neg     bx
        add     bx,72
        shl     bx,1
        sub     di,cs:rowtab[bx]
        call    word ptr cs:jtab2[bx]
r2_tail: pop    bx
        test    bl,1
        jz      r2_chk
        mov     es:[di],ax
r2_chk: cmp     bp,cs:clip_h16
        jl      r2_next
r2_done: pop    ds
        pop     bp
        ret
r2_skip: add    si,2
        jmp     short r2_chk
draw_runs2_ endp

fastcol_TEXT ends
        end
