; AIRBORNE: the inner loops of the game's own blitters (game.c), on the
; hidden 1-bit Mac screen: OR a sprite in, AND NOT it out, or both at once
; (the original's one-pass erase-and-draw for moving sprites, 4:2296).
; A word at a time, the sprite data in DS (CODE 4), the screen in ES.
;
; C: void ab_or(struct abp far *p); ab_bic; ab_bicor  (Watcom: p in DX:AX)
; struct abp { dst, src, dst2, src2, words, rows, gap, fbseg, codeseg }
;   dst/src: offsets of the screen and the sprite rows; dst2/src2: the old
;   image for ab_bicor (erased first, word by word, as the original);
;   words a row, rows, gap = bytes from one row's end to the next's start.

	.8086

ABLIT_TEXT segment byte public 'CODE'
	assume cs:ABLIT_TEXT, ds:nothing, es:nothing

	public ab_or_
	public ab_bic_
	public ab_bicor_

rows	dw 0
gap	dw 0
words	dw 0

; load the block at DX:AX: ES:DI dst, DS:SI src, BX dst2, BP src2
setup	macro
	push bx
	push cx
	push dx
	push si
	push di
	push bp
	push ds
	push es
	mov ds, dx
	mov bx, ax
	mov ax, [bx+8]
	mov cs:words, ax
	mov ax, [bx+10]
	mov cs:rows, ax
	mov ax, [bx+12]
	mov cs:gap, ax
	mov di, [bx]
	mov si, [bx+2]
	mov bp, [bx+6]
	mov es, [bx+14]
	mov ax, [bx+4]
	mov ds, [bx+16]
	mov bx, ax
	endm

finish	macro
	pop es
	pop ds
	pop bp
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	ret
	endm

ab_or_ proc far
	setup
	cld
	mov cx, cs:rows
	cmp cs:words, 2
	je or2
	cmp cs:words, 4
	je or4
	jmp short orrow
or2:	lodsw				; 2 words a row (32 pixels): unrolled
	or es:[di], ax
	lodsw
	or es:[di+2], ax
	add di, 64
	loop or2
	jmp short ordone
or4:	lodsw
	or es:[di], ax
	lodsw
	or es:[di+2], ax
	lodsw
	or es:[di+4], ax
	lodsw
	or es:[di+6], ax
	add di, 64
	loop or4
	jmp short ordone
orrow:	mov cx, cs:words
orw:	lodsw
	or es:[di], ax
	inc di
	inc di
	loop orw
	add di, cs:gap
	dec cs:rows
	jnz orrow
ordone:	finish
ab_or_ endp

ab_bic_ proc far
	setup
	cld
	mov cx, cs:rows
	cmp cs:words, 2
	je bic2
	cmp cs:words, 4
	je bic4
	jmp short bicrow
bic2:	lodsw
	not ax
	and es:[di], ax
	lodsw
	not ax
	and es:[di+2], ax
	add di, 64
	loop bic2
	jmp short bicdone
bic4:	lodsw
	not ax
	and es:[di], ax
	lodsw
	not ax
	and es:[di+2], ax
	lodsw
	not ax
	and es:[di+4], ax
	lodsw
	not ax
	and es:[di+6], ax
	add di, 64
	loop bic4
	jmp short bicdone
bicrow:	mov cx, cs:words
bicw:	lodsw
	not ax
	and es:[di], ax
	inc di
	inc di
	loop bicw
	add di, cs:gap
	dec cs:rows
	jnz bicrow
bicdone:	finish
ab_bic_ endp

; per word: AND NOT the old image (DS:BP at ES:BX), then OR the new
; (DS:SI at ES:DI): the same order as the original's loop
ab_bicor_ proc far
	setup
	cld
	mov cx, cs:rows
	cmp cs:words, 4
	je bc4
	cmp cs:words, 2
	je bc2
	jmp short bcrow
bc4:	mov ax, ds:[bp]			; 4 words a row (helicopters): unrolled
	not ax
	and es:[bx], ax
	lodsw
	or es:[di], ax
	mov ax, ds:[bp+2]
	not ax
	and es:[bx+2], ax
	lodsw
	or es:[di+2], ax
	mov ax, ds:[bp+4]
	not ax
	and es:[bx+4], ax
	lodsw
	or es:[di+4], ax
	mov ax, ds:[bp+6]
	not ax
	and es:[bx+6], ax
	lodsw
	or es:[di+6], ax
	add bp, 8
	add bx, 64
	add di, 64
	loop bc4
	jmp short bcdone
bc2:	mov ax, ds:[bp]
	not ax
	and es:[bx], ax
	lodsw
	or es:[di], ax
	mov ax, ds:[bp+2]
	not ax
	and es:[bx+2], ax
	lodsw
	or es:[di+2], ax
	add bp, 4
	add bx, 64
	add di, 64
	loop bc2
	jmp short bcdone
bcrow:	mov cx, cs:words
bcw:	mov ax, ds:[bp]
	not ax
	and es:[bx], ax
	inc bp
	inc bp
	inc bx
	inc bx
	mov ax, [si]
	or es:[di], ax
	inc si
	inc si
	inc di
	inc di
	loop bcw
	mov ax, cs:gap
	add di, ax
	add bx, ax
	dec cs:rows
	jnz bcrow
bcdone:	finish
ab_bicor_ endp

ABLIT_TEXT ends
	end
