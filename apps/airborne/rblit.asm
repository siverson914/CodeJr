; AIRBORNE: the PCjr screen's inner loops (airborne.c's render): copy a
; rectangle of the background into a video page, and OR an image's rows in.
; The page has two banks: row y is at (y & 1) * 2000h + (y >> 1) * 80.
;
; C: void rb_copy(struct rbp far *p); void rb_or(struct rbp far *p);
; struct rbp { src_off, src_seg, src_stride, dst_off, dst_seg, y, rows, bytes }
;   src: the first byte to copy (background: 80-byte rows; image: its rows)
;   dst_off: the byte column plus the page's offset of row y

	.8086

RBLIT_TEXT segment byte public 'CODE'
	assume cs:RBLIT_TEXT, ds:nothing, es:nothing

	public rb_copy_
	public rb_or_

stride	dw 0
y	dw 0
rows	dw 0
bytes	dw 0

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
	mov ax, [bx+4]
	mov cs:stride, ax
	mov ax, [bx+10]
	mov cs:y, ax
	mov ax, [bx+12]
	mov cs:rows, ax
	mov ax, [bx+14]
	mov cs:bytes, ax
	mov di, [bx+6]
	mov es, [bx+8]
	mov si, [bx]
	mov ds, [bx+2]
	cld
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

; the next row: from an even row +2000h, from an odd row -2000h+80
nextrow	macro
	local odd, done
	test byte ptr cs:y, 1
	jnz odd
	add di, 2000h
	jmp short done
odd:	sub di, 2000h - 80
done:	inc cs:y
	endm

rb_copy_ proc far
	setup
crow:	mov cx, cs:bytes
	push si
	push di
	shr cx, 1
	rep movsw
	jnc ceven
	movsb
ceven:	pop di
	pop si
	add si, cs:stride
	nextrow
	dec cs:rows
	jnz crow
	finish
rb_copy_ endp

rb_or_ proc far
	setup
orow:	mov cx, cs:bytes
	push si
	push di
obyte:	lodsb
	or es:[di], al
	inc di
	loop obyte
	pop di
	pop si
	add si, cs:stride
	nextrow
	dec cs:rows
	jnz orow
	finish
rb_or_ endp

RBLIT_TEXT ends
	end
