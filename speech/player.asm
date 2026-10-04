; Sample player for SAY.EXE (IBM PCjr, 8088).
;
; say.c copies this whole segment to offset 0 of a DOS block above 128 KB
; and calls it there with a far call. The low 128 KB is shared with the
; video gate array (6 clocks per byte against 4), so the code, its stack,
; the level tables and the samples all live up high. Everything is
; addressed relative to CS, so the copy runs as is.
;
; Block layout (offsets from the block segment):
;   0000  this code and its parameters (up to 2 KB)
;   0800  level tables: 256 bytes each for voices 0, 1, 2 (OUT values)
;   0B00  stack (SP starts at 0E00h)
;   0E00  samples, one byte each, any length (ES steps 64 KB at a time)
;
; Timing: PIT channel 0 is set to mode 2 with the sample divisor, and only
; IRQ0 is unmasked.
;   pwm: the loop does STI, HLT, CLI. The 8088 wakes on the interrupt, the
;     handler sends EOI, and the sample goes out a fixed number of clocks
;     after the tick: a few clocks of jitter, which PWM needs, since a late
;     write is a wrong pulse width.
;   chip: interrupts stay off and the loop polls the 8259 (OCW3 poll
;     command: the read acknowledges the request), then sends EOI. That
;     skips taking an interrupt, about 150 clocks per sample on the real
;     PCjr, which was too much at 9.3 kHz (SAY 1.0 was late on 72% of
;     samples). The cost is up to one poll (about 10 us) of jitter, which
;     a level-based DAC tolerates.
; The per-sample path is kept short because the 8088 is bus-bound: every
; code byte costs 4 clocks, and each SN76496 write holds the CPU in READY
; wait states for about 9 us (32 of the chip's clocks). See README.md for
; the clock budget.
;
; Stopwatch (chip only): PIT channel 2 free-runs (mode 2, 65536) with the
; speaker data bit off, and is read every 256 samples, so SAY can report
; the rate actually achieved.
;
; Method 1 (chip): the three SN76496 tone voices hold period 1 (about
; 111 kHz, far above hearing), so each acts as a steady level and its
; 4-bit attenuation register is a crude DAC. The three outputs add, so a
; sample byte picks one of 256 voice combinations (three table lookups)
; for about 8 bits of level instead of 4.
;
; Method 2 (pwm): PIT channel 2 in mode 0, low byte only. Each write
; drops the speaker line, which rises again after that many PIT counts,
; so the sample byte is a pulse width within one sample period.
;
; Lateness counter: if the tick is already waiting when the loop gets
; back to it (the IRR for pwm, the first poll for chip), the previous
; sample took longer than a sample period. SAY reports the count.
;
; The keyboard still works (it is an NMI on the PCjr): every 256 samples
; the loop checks the BIOS keyboard buffer and stops if a key is waiting.

.8086

; Its own group, so the linker gives it a frame of its own and every
; internal offset is relative to sp_begin (it would otherwise share a
; frame with the C code, and the offsets would be off after the copy).
PLAYGRP group PLAYER_TEXT

PLAYER_TEXT segment para public 'CODE'
	assume cs:PLAYGRP

public sp_begin, sp_end, sp_dec

sp_begin:
	jmp short main		; far call target, offset 0
	nop
	nop
p_method	dw 0		; 04  1 = chip, 2 = pwm
p_divisor	dw 0		; 06  PIT counts per sample
p_cntlo		dw 0		; 08  samples to play
p_cnthi		dw 0		; 0A
p_dataseg	dw 0		; 0C  segment of the first sample
p_donelo	dw 0		; 0E  out: samples played
p_donehi	dw 0		; 10
p_abort		dw 0		; 12  out: 1 if a key stopped it
p_late		dw 0		; 14  out: samples whose tick came before the loop was ready
p_tlo		dw 0		; 16  out: PIT counts elapsed (chip only)
p_thi		dw 0		; 18
p_flags		dw 0		; 1A  in: bit 0 = chip method on interrupt timing (/H)
d_inseg		dw 0		; 1C  ADPCM decoder (sp_dec): codes at d_inseg:d_inoff
d_inoff		dw 0		; 1E
d_outseg	dw 0		; 20  samples out at d_outseg:d_outoff
d_outoff	dw 0		; 22
d_bytes		dw 0		; 24  code bytes (3-bit: 3-byte groups) to decode this call
d_bits		dw 0		; 26  4, 3 or 2
d_state		dw 0		; 28  in/out: SS offset of the DELTA row for the step index
d_p		dw 0		; 2A  in/out: predictor - 8000h (signed)
t_prev		dw 0
sv_ss		dw 0
sv_sp		dw 0
sv_v8off	dw 0
sv_v8seg	dw 0
sv_imr		db 0
sv_61		db 0

isr:
	push ax
	mov al,20h
	out 20h,al
	pop ax
	iret

main:
	push bp
	push si
	push di
	push ds
	push es
	push bx
	push cx
	push dx
	pushf
	cli
	mov cs:sv_ss,ss
	mov cs:sv_sp,sp
	mov ax,cs
	mov ss,ax
	mov sp,0E00h
	mov cs:p_abort,0
	mov cs:p_tlo,0
	mov cs:p_thi,0

	xor ax,ax			; IRQ0 vector -> isr
	mov es,ax
	mov ax,es:[20h]
	mov cs:sv_v8off,ax
	mov ax,es:[22h]
	mov cs:sv_v8seg,ax
	mov word ptr es:[20h],offset isr
	mov es:[22h],cs
	in al,21h			; only IRQ0
	mov cs:sv_imr,al
	mov al,0FEh
	out 21h,al
	mov al,0Ah			; OCW3: reads of port 20h return the IRR
	out 20h,al
	in al,61h
	mov cs:sv_61,al

	mov al,34h			; channel 0, mode 2, the sample rate
	out 43h,al
	mov ax,cs:p_divisor
	out 40h,al
	mov al,ah
	out 40h,al

	mov ax,cs			; DS = tables, ES:SI = samples
	mov ds,ax
	mov es,cs:p_dataseg
	xor si,si
	xor di,di			; late samples
	xor bx,bx
	mov cx,cs:p_cntlo		; LOOP counts CX; BP counts the 64K passes
	mov bp,cs:p_cnthi
	or cx,cx
	jnz have_n
	dec bp
have_n:
	sti				; take any tick that came in during setup,
	hlt				; so the late count starts clean
	cli
	cmp cs:p_method,2
	jne chip
	jmp pwm

; ---- method 1: three SN76496 voices --------------------------------------

chip:
	mov al,9Fh			; all four voices silent
	out 0C0h,al
	mov al,0BFh
	out 0C0h,al
	mov al,0DFh
	out 0C0h,al
	mov al,0FFh
	out 0C0h,al
	mov al,81h			; tone periods = 1
	out 0C0h,al
	xor al,al
	out 0C0h,al
	mov al,0A1h
	out 0C0h,al
	xor al,al
	out 0C0h,al
	mov al,0C1h
	out 0C0h,al
	xor al,al
	out 0C0h,al
	mov al,0B4h			; stopwatch: channel 2, mode 2, 65536
	out 43h,al
	xor al,al
	out 42h,al
	out 42h,al
	mov al,cs:sv_61			; mux bits 6:5 = 11 (SN76496), channel 2
	or al,61h			; gate on, speaker data off
	and al,0FDh
	out 61h,al
	call rd2
	mov cs:t_prev,ax

	test cs:p_flags,1		; /H: interrupt timing, as in SAY 1.0
	jz c_next
	jmp h_next

c_next:					; the per-sample path: 44 bytes on time
	mov al,0Ch			; poll: is the tick here already?
	out 20h,al
	in al,20h
	test al,80h
	jnz c_late
c_wait:
	mov al,0Ch
	out 20h,al
	in al,20h
	test al,80h
	jz c_wait
c_go:
	mov al,20h			; EOI
	out 20h,al
	lods byte ptr es:[si]
	mov bl,al
	mov bh,8
	mov al,[bx]
	out 0C0h,al
	inc bh
	mov al,[bx]
	out 0C0h,al
	inc bh
	mov al,[bx]
	out 0C0h,al
	test si,0FFh
	jz c_rare
c_loop:
	loop c_next
	sub bp,1
	jnc c_next
	jmp done
c_late:
	inc di
	jmp short c_go
c_rare:
	call rare
	je c_loop
	jmp key

h_next:					; /H: the interrupt-timed chip loop
	in al,20h
	test al,1
	jz h_go
	inc di
h_go:
	sti
	hlt
	cli
	lods byte ptr es:[si]
	mov bl,al
	mov bh,8
	mov al,[bx]
	out 0C0h,al
	inc bh
	mov al,[bx]
	out 0C0h,al
	inc bh
	mov al,[bx]
	out 0C0h,al
	test si,0FFh
	jz h_rare
h_loop:
	loop h_next
	sub bp,1
	jnc h_next
	jmp done
h_rare:
	call rare
	je h_loop
	jmp key

; ---- method 2: PWM on PIT channel 2 ---------------------------------------

pwm:
	mov al,90h			; channel 2, low byte only, mode 0
	out 43h,al
	mov al,1
	out 42h,al
	mov al,cs:sv_61			; mux = 8253 channel 2, gate and data on
	and al,9Fh
	or al,03h
	out 61h,al

p_next:					; the per-sample path: 22 bytes
	in al,20h
	test al,1
	jz p_go
	inc di
p_go:
	sti
	hlt
	cli
	lods byte ptr es:[si]
	out 42h,al
	test si,0FFh
	jz p_rare
p_loop:
	loop p_next
	sub bp,1
	jnc p_next
	jmp done
p_rare:
	call rare
	je p_loop
	jmp key

; Every 256 samples: the stopwatch (chip only), the next 64 KB, and the
; keyboard. Returns ZF set if no key is waiting.
rare:
	cmp cs:p_method,2
	je r_seg
	call tick
r_seg:
	or si,si
	jnz r_key
	mov ax,es
	add ax,1000h
	mov es,ax
r_key:
	push ds
	xor ax,ax
	mov ds,ax
	mov ax,ds:[41Ah]
	cmp ax,ds:[41Ch]
	pop ds
	ret

; channel 2 count in AX
rd2:
	mov al,80h
	out 43h,al
	in al,42h
	mov ah,al
	in al,42h
	xchg al,ah
	ret

; add the PIT counts since the last read to p_thi:p_tlo
tick:
	call rd2
	mov dx,cs:t_prev
	mov cs:t_prev,ax
	sub dx,ax
	add cs:p_tlo,dx
	adc cs:p_thi,0
	ret

key:
	mov cs:p_abort,1
done:
	cmp cs:p_method,2
	je done2
	call tick
done2:
	mov al,9Fh			; silence the chip
	out 0C0h,al
	mov al,0BFh
	out 0C0h,al
	mov al,0DFh
	out 0C0h,al
	mov al,0FFh
	out 0C0h,al
	mov al,cs:sv_61			; speaker and mux as they were
	out 61h,al
	mov al,36h			; channel 0 back to the BIOS's mode 3, 65536
	out 43h,al
	xor al,al
	out 40h,al
	out 40h,al
	xor ax,ax
	mov ds,ax
	mov ax,cs:sv_v8off
	mov ds:[20h],ax
	mov ax,cs:sv_v8seg
	mov ds:[22h],ax
	mov al,cs:sv_imr
	out 21h,al
	mov cs:p_late,di
	mov ax,es			; played = (ES - first) * 16 + SI
	sub ax,cs:p_dataseg
	mov dx,ax
	mov cl,4
	shl ax,cl
	mov cl,12
	shr dx,cl
	add ax,si
	adc dx,0
	mov cs:p_donelo,ax
	mov cs:p_donehi,dx
	mov ss,cs:sv_ss
	mov sp,cs:sv_sp
	popf
	pop dx
	pop cx
	pop bx
	pop es
	pop ds
	pop di
	pop si
	pop bp
	retf

; ---- ADPCM decoder ---------------------------------------------------------
;
; sp_dec: decodes d_bytes bytes of 4-, 3- or 2-bit codes (for 3-bit,
; d_bytes counts 3-byte groups of 8 codes) (see adpcm_step in
; mksnd.py) from d_inseg:d_inoff to one sample byte each at
; d_outseg:d_outoff. say.c calls it once per chunk of at most 32 KB of
; output, with SS = DGROUP, where it has built two tables for the code
; size: DELTA (the signed predictor change for each step index and code)
; and, NEXTOFF bytes after it, NEXT (the DELTA row of the next step
; index). So each sample is a lookup, an add with a clamp on overflow, and
; a store. The predictor is kept as P - 8000h, so a signed overflow is
; exactly the decoder's clamp to 0..65535.

NEXTOFF equ 2848			; 89 step indexes x 16 codes x 2 bytes

sp_dec:
	push bp
	push si
	push di
	push ds
	push es
	push bx
	push cx
	push dx
	mov ds,cs:d_inseg
	mov si,cs:d_inoff
	mov es,cs:d_outseg
	mov di,cs:d_outoff
	mov cx,cs:d_bytes
	mov bp,cs:d_state
	mov dx,cs:d_p
	cld
	or cx,cx
	jnz dec_go
	jmp dec_done
dec_go:
	cmp cs:d_bits,3
	jne dec_n3
	jmp dec3
dec_n3:
	cmp cs:d_bits,2
	jne dec4
	jmp dec2

dec4:					; two codes per byte, high nibble first
	lodsb
	mov ah,al
	shr al,1
	shr al,1
	shr al,1
	and al,1Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d4a
	mov dx,7FFFh
	js d4a
	mov dx,8000h
d4a:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,ah
	and al,0Fh
	shl al,1
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d4b
	mov dx,7FFFh
	js d4b
	mov dx,8000h
d4b:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	loop dec4
	jmp dec_done

dec2:					; four codes per byte, high bits first
	lodsb
	mov ah,al
	rol al,1
	rol al,1
	rol al,1
	and al,6
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d2a
	mov dx,7FFFh
	js d2a
	mov dx,8000h
d2a:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,ah
	shr al,1
	shr al,1
	shr al,1
	and al,6
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d2b
	mov dx,7FFFh
	js d2b
	mov dx,8000h
d2b:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,ah
	shr al,1
	and al,6
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d2c
	mov dx,7FFFh
	js d2c
	mov dx,8000h
d2c:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,ah
	shl al,1
	and al,6
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d2d
	mov dx,7FFFh
	js d2d
	mov dx,8000h
d2d:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	loop dec2_far
	jmp dec_done
dec2_far:
	jmp dec2

dec3:					; eight codes per three bytes, high bits first;
					; each code is shifted out of its byte (or byte
					; pair) by a fixed amount, so no bit buffer
	mov al,[si]
	shr al,1
	shr al,1
	shr al,1
	shr al,1
	and al,0Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d3a
	mov dx,7FFFh
	js d3a
	mov dx,8000h
d3a:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,[si]
	shr al,1
	and al,0Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d3b
	mov dx,7FFFh
	js d3b
	mov dx,8000h
d3b:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov ax,[si]
	xchg al,ah
	shr ax,1
	shr ax,1
	shr ax,1
	shr ax,1
	shr ax,1
	shr ax,1
	and al,0Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d3c
	mov dx,7FFFh
	js d3c
	mov dx,8000h
d3c:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,[si+1]
	shr al,1
	shr al,1
	shr al,1
	and al,0Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d3d
	mov dx,7FFFh
	js d3d
	mov dx,8000h
d3d:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,[si+1]
	and al,0Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d3e
	mov dx,7FFFh
	js d3e
	mov dx,8000h
d3e:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov ax,[si+1]
	xchg al,ah
	shr ax,1
	shr ax,1
	shr ax,1
	shr ax,1
	shr ax,1
	and al,0Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d3f
	mov dx,7FFFh
	js d3f
	mov dx,8000h
d3f:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,[si+2]
	shr al,1
	shr al,1
	and al,0Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d3g
	mov dx,7FFFh
	js d3g
	mov dx,8000h
d3g:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	mov al,[si+2]
	shl al,1
	and al,0Eh
	mov bx,bp
	add bl,al
	add dx,ss:[bx]
	jno d3h
	mov dx,7FFFh
	js d3h
	mov dx,8000h
d3h:
	mov bp,ss:[bx+NEXTOFF]
	mov al,dh
	xor al,80h
	stosb
	add si,3
	loop dec3_far
	jmp dec_done
dec3_far:
	jmp dec3

dec_done:
	mov cs:d_state,bp
	mov cs:d_p,dx
	mov cs:d_inoff,si
	mov cs:d_outoff,di
	pop dx
	pop cx
	pop bx
	pop es
	pop ds
	pop di
	pop si
	pop bp
	retf

sp_end:

PLAYER_TEXT ends
	end
