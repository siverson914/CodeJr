; Lines in 320x200x4 for SHREDDY (8088, large model).
;
; kn_poly(struct kp far *p), p in DX:AX. Draws n steps from (x, y), each
; step a line, decoded from the frame data as it goes, and widens the
; page's per-row extents (what the next erase of this page clears):
;   +0  steps   far pointer to the steps; on return, just past them
;   +4  n       steps
;   +6  x, +8 y the start; on return, the end
;   +10 vseg    the page
;   +12 ext     far pointer: 200 bytes "first byte drawn" per row, then
;               200 bytes "last byte drawn"
;   +16 cpat    the color in all four pixel slots (0, 55h, AAh, FFh)
;   +17 thick   1: every pixel also one to the right (steep lines) or one
;               down (flat lines). The exporter never marks a line thick
;               that touches x 319 or y 199, so that pixel is on the screen.
; A step is one byte, dx in the high nibble and dy in the low (signed), or
; 80h then signed bytes dx, dy.
;
; kn_erase(struct ke far *p): clears the spans in ext on rows y0..y1 of the
; page and resets them (first 255, last 0):
;   +0 vseg, +2 ext (far), +6 y0, +8 y1
;
; Lines are Bresenham, both ends drawn, the same pixels as the exporter's
; preview: plot, step the major axis, error -= minor, and when it goes
; below 0 add major and step the minor axis.
;
; Rows alternate between two banks: row y is at (y & 1) * 2000h + (y / 2) * 80.
; Each line variant has a copy of its loop for even rows and one for odd
; rows, so the step to the next row is a constant in each copy:
;   down, from an even row: +2000h     from an odd row: -2000h + 80 (E050h)
;   up,   from an even row: +1FB0h     from an odd row: -2000h (E000h)
; and the row below is at +2000h from an even row, -1FB0h from an odd one.
; Registers in the pixel loops: ES:DI the byte, AH the color, AL scratch,
; CX pixels left, DX the error, SI minor, BP major, and the mask: BL for
; one pixel; for two side by side (steep thick lines) BH for the byte at DI
; and BL for the byte after it.
; Its own variables live in the code segment, so DS can point at the steps.
; Every register but AX is kept.

	.8086

SHLINE_TEXT segment byte public 'CODE'
	assume cs:SHLINE_TEXT, ds:nothing, es:nothing

	public kn_poly_
	public kn_erase_

pptr	dd 0            ; the parameter block
cnt	dw 0
px0	dw 0            ; the step being drawn: (px0, py0) to (px1, py1)
py0	dw 0
px1	dw 0
py1	dw 0
vseg	dw 0
extoff	dw 0
extseg	dw 0
cpat	db 0
thick	db 0
ysteep	db 0            ; draw sets it: 1 when the line was y-major
masks	db 0C0h, 30h, 0Ch, 03h
masks2	dw 0F000h, 3C00h, 0F00h, 03C0h   ; two pixels: BH at DI, BL at DI+1

; pixel = old ^ ((old ^ color) & mask)
PLOT macro
	mov al, es:[di]
	xor al, ah
	and al, bl
	xor es:[di], al
	endm
; the pixel and the one right of it
PLOT2 macro
	mov al, es:[di]
	xor al, ah
	and al, bh
	xor es:[di], al
	mov al, es:[di+1]
	xor al, ah
	and al, bl
	xor es:[di+1], al
	endm
; the pixel and the one below it (d: where the next row's byte is)
PLOTV macro d
	PLOT
	mov al, es:[di+d]
	xor al, ah
	and al, bl
	xor es:[di+d], al
	endm

; one pixel left or right: the mask moves 2 bits, and on to the next byte
; when it wraps (the carry of the second rotate)
XRIGHT macro
	ror bl, 1
	ror bl, 1
	adc di, 0
	endm
XLEFT macro
	rol bl, 1
	rol bl, 1
	sbb di, 0
	endm
; the same for a pair of pixels in BH:BL (BH first in pixel order)
XRIGHT2 macro
	local l
	shr bx, 1
	shr bx, 1
	or bh, bh
	jnz l
	inc di
	mov bh, bl
	xor bl, bl
l:
	endm
XLEFT2 macro
	local w, l
	test bh, 0C0h
	jnz w
	shl bx, 1
	shl bx, 1
	jmp short l
w:	dec di
	mov bl, bh
	shl bl, 1
	shl bl, 1
	rol bh, 1
	rol bh, 1
	and bh, 3
l:
	endm

; y-major: every pixel steps a row
YMAJ macro nm, plt, xstep, ke, ko
nm&E:
	plt
	add di, ke
	sub dx, si
	jns nm&1
	add dx, bp
	xstep
nm&1:
	dec cx
	jz nm&X
nm&O:
	plt
	add di, ko
	sub dx, si
	jns nm&2
	add dx, bp
	xstep
nm&2:
	dec cx
	jnz nm&E
nm&X:
	ret
	endm

; x-major: every pixel steps a column, some step a row
XMAJ macro nm, plte, plto, xstep, ke, ko
nm&E:
	plte
	xstep
	sub dx, si
	jns nm&1
	add dx, bp
	add di, ke
	dec cx
	jnz nm&O
	ret
nm&1:
	dec cx
	jnz nm&E
	ret
nm&O:
	plto
	xstep
	sub dx, si
	jns nm&2
	add dx, bp
	add di, ko
	dec cx
	jnz nm&E
	ret
nm&2:
	dec cx
	jnz nm&O
	ret
	endm

PLOTVE macro
	PLOTV 2000h
	endm
PLOTVO macro
	PLOTV 0E050h
	endm

; Draws (px0, py0) - (px1, py1), thick if cs:thick. Uses every register
; but DS; sets ysteep.
draw proc near
	xor bx, bx              ; (before the subs: it clears the sign flag)
	mov di, cs:px1
	sub di, cs:px0
	jns d1
	neg di
	or bl, 2                ; x falling
d1:	mov ax, cs:py1
	sub ax, cs:py0
	jns d2
	neg ax
	or bl, 1                ; y rising (going up the screen)
d2:	mov cs:ysteep, 0
	cmp ax, di
	jbe d3
	or bl, 4                ; y-major
	mov cs:ysteep, 1
	mov bp, ax
	mov si, di
	jmp short d4
d3:	mov bp, di
	mov si, ax
d4:	or si, si
	jnz d4a
	cmp cs:ysteep, 0        ; (a vertical line has no minor axis either)
	jne d4a
	cmp cs:thick, 0
	jne d4a
	jmp hspan               ; a thin horizontal line: whole bytes at a time
d4a:	mov cx, bp
	inc cx                  ; pixels
	mov dx, bp
	shr dx, 1               ; the error
	cmp cs:thick, 0
	je d6
	or bl, 8
d6:	mov al, byte ptr cs:py0
	and al, 1
	shl bl, 1
	or bl, al
	shl bx, 1
	push word ptr cs:entries[bx]
	mov ax, cs:py0
	shr ax, 1
	mov ah, 80
	mul ah                  ; (y / 2) * 80
	test byte ptr cs:py0, 1
	jz d5
	add ax, 2000h
d5:	mov di, cs:px0
	mov bx, di
	shr di, 1
	shr di, 1
	add di, ax
	and bx, 3
	cmp cs:ysteep, 0
	je d7
	cmp cs:thick, 0
	je d7
	shl bx, 1
	mov bx, cs:masks2[bx]
	jmp short d8
d7:	mov bl, cs:masks[bx]
d8:	mov es, cs:vseg
	mov ah, cs:cpat
	ret                     ; into the loop pushed above; it returns to our caller

	XMAJ xdd, PLOT, PLOT, XRIGHT, 2000h, 0E050h     ; right, down
	XMAJ xdu, PLOT, PLOT, XRIGHT, 1FB0h, 0E000h     ; right, up
	XMAJ xld, PLOT, PLOT, XLEFT,  2000h, 0E050h     ; left, down
	XMAJ xlu, PLOT, PLOT, XLEFT,  1FB0h, 0E000h     ; left, up
	YMAJ ydd, PLOT, XRIGHT, 2000h, 0E050h
	YMAJ ydu, PLOT, XRIGHT, 1FB0h, 0E000h
	YMAJ yld, PLOT, XLEFT,  2000h, 0E050h
	YMAJ ylu, PLOT, XLEFT,  1FB0h, 0E000h
	XMAJ tdd, PLOTVE, PLOTVO, XRIGHT, 2000h, 0E050h
	XMAJ tdu, PLOTVE, PLOTVO, XRIGHT, 1FB0h, 0E000h
	XMAJ tld, PLOTVE, PLOTVO, XLEFT,  2000h, 0E050h
	XMAJ tlu, PLOTVE, PLOTVO, XLEFT,  1FB0h, 0E000h
	YMAJ udd, PLOT2, XRIGHT2, 2000h, 0E050h
	YMAJ udu, PLOT2, XRIGHT2, 1FB0h, 0E000h
	YMAJ uld, PLOT2, XLEFT2,  2000h, 0E050h
	YMAJ ulu, PLOT2, XLEFT2,  1FB0h, 0E000h

; A thin horizontal line, (px0, py0) - (px1, py0): the masked first and
; last bytes and whole bytes between (rep stosb). Returns to draw's caller.
hspan:	mov ax, cs:px0
	mov dx, cs:px1
	cmp ax, dx
	jbe h1
	xchg ax, dx             ; AX = left x, DX = right x
h1:	mov bx, ax
	and bx, 3
	mov bl, cs:lmask[bx]    ; the left byte: from the pixel to its end
	mov si, dx
	and si, 3
	mov bh, cs:rmask[si]    ; the right byte: up to the pixel
	mov cl, 2
	shr ax, cl
	shr dx, cl
	mov cx, dx
	sub cx, ax              ; bytes after the first
	mov di, ax
	mov ax, cs:py0
	shr ax, 1
	mov ah, 80
	mul ah
	test byte ptr cs:py0, 1
	jz h2
	add ax, 2000h
h2:	add di, ax
	mov es, cs:vseg
	mov ah, cs:cpat
	or cx, cx
	jnz h3
	and bl, bh              ; one byte: both masks
	PLOT
	ret
h3:	PLOT                    ; first byte
	inc di
	dec cx
	mov al, ah
	rep stosb               ; the whole bytes
	mov bl, bh
	PLOT                    ; last byte
	ret
lmask	db 0FFh, 3Fh, 0Fh, 03h
rmask	db 0C0h, 0F0h, 0FCh, 0FFh

entries dw xddE, xddO, xduE, xduO, xldE, xldO, xluE, xluO
	dw yddE, yddO, yduE, yduO, yldE, yldO, yluE, yluO
	dw tddE, tddO, tduE, tduO, tldE, tldO, tluE, tluO
	dw uddE, uddO, uduE, uduO, uldE, uldO, uluE, uluO
draw endp

kn_poly_ proc far
	push bx
	push cx
	push dx
	push si
	push di
	push bp
	push ds
	push es
	mov word ptr cs:pptr, ax
	mov word ptr cs:pptr+2, dx
	mov ds, dx
	mov bx, ax
	mov ax, [bx+4]
	mov cs:cnt, ax
	mov ax, [bx+6]
	mov cs:px0, ax
	mov ax, [bx+8]
	mov cs:py0, ax
	mov ax, [bx+10]
	mov cs:vseg, ax
	mov ax, [bx+12]
	mov cs:extoff, ax
	mov ax, [bx+14]
	mov cs:extseg, ax
	mov ax, [bx+16]
	mov cs:cpat, al
	mov cs:thick, ah
	lds si, [bx]            ; DS:SI: the steps

step:	cmp cs:cnt, 0
	jne s0
	jmp done
s0:	dec cs:cnt
	lodsb
	cmp al, 80h
	jne s1
	lodsb
	cbw
	mov dx, ax              ; dx
	lodsb
	cbw                     ; dy
	jmp short s2
s1:	mov cl, 4
	mov bl, al              ; (not AH: cbw overwrites it)
	sar al, cl
	cbw
	mov dx, ax              ; dx: the high nibble
	mov al, bl
	shl al, cl
	sar al, cl
	cbw                     ; dy: the low nibble
s2:	add ax, cs:py0
	mov cs:py1, ax
	add dx, cs:px0
	mov cs:px1, dx
	push si
	push ds
	call draw

	; the rows this step touched: bytes ba..bb on rows ya..yb
	mov ax, cs:px0
	mov dx, cs:px1
	cmp ax, dx
	jbe e1
	xchg ax, dx
e1:	mov si, cs:py0
	mov cx, cs:py1
	cmp si, cx
	jbe e2
	xchg si, cx
e2:	cmp cs:thick, 0
	je e4
	cmp cs:ysteep, 0
	je e3
	inc dx                  ; steep and thick: one more pixel right
	jmp short e4
e3:	inc cx                  ; flat and thick: one more row down
e4:	shr ax, 1
	shr ax, 1
	shr dx, 1
	shr dx, 1
	mov ah, dl              ; AL = ba, AH = bb
	sub cx, si
	inc cx                  ; rows
	mov es, cs:extseg
	mov bx, cs:extoff
	add bx, si
e5:	cmp es:[bx], al
	jbe e6
	mov es:[bx], al
e6:	cmp es:[bx+200], ah
	jae e7
	mov es:[bx+200], ah
e7:	inc bx
	loop e5

	pop ds
	pop si
	mov ax, cs:px1
	mov cs:px0, ax
	mov ax, cs:py1
	mov cs:py0, ax
	jmp step

done:	les bx, cs:pptr
	mov es:[bx], si         ; past the steps
	mov ax, cs:px0
	mov es:[bx+6], ax
	mov ax, cs:py0
	mov es:[bx+8], ax
	pop es
	pop ds
	pop bp
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	retf
kn_poly_ endp

kn_erase_ proc far
	push bx
	push cx
	push dx
	push si
	push di
	push ds
	push es
	mov ds, dx
	mov bx, ax
	mov es, [bx]            ; the page
	mov dx, [bx+6]          ; y0
	mov cx, [bx+8]
	sub cx, dx
	jl r9
	inc cx                  ; rows
	lds si, [bx+2]          ; DS:SI: ext
	add si, dx
r1:	mov al, [si]
	mov ah, [si+200]
	cmp al, ah
	ja r8                   ; nothing on this row
	mov byte ptr [si], 255
	mov byte ptr [si+200], 0
	push cx
	mov bx, ax              ; BL first byte, BH last
	mov ax, dx
	shr ax, 1
	mov ah, 80
	mul ah
	test dl, 1
	jz r2
	add ax, 2000h
r2:	mov di, ax
	xor ax, ax
	mov al, bl
	add di, ax
	mov cl, bh
	sub cl, bl
	xor ch, ch
	inc cx                  ; bytes
	xor al, al
	rep stosb
	pop cx
r8:	inc si
	inc dx
	loop r1
r9:	pop es
	pop ds
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	retf
kn_erase_ endp

SHLINE_TEXT ends
	end
