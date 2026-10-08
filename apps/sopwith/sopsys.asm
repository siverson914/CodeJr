; Interrupt glue for Sopwith 2 on Open Watcom (WASM), small model.
; New file, written 2026-10-07 for the CodeJr PCjr port. It replaces the
; interrupt half of Andrew Jenner's sopasm.asm (A86), and differs in two ways:
;
; - The C routine runs on a private stack. C in the small model assumes
;   SS = DS, and a timer or keyboard interrupt can land while DOS or the BIOS
;   is on a stack of its own (the game polls the keyboard through DOS).
; - The keyboard handler saves AL on entry. On a PCjr, INT 9 is called by the
;   BIOS's own keyboard code with the scan code in AL; there is no byte to
;   read at port 60h.

.8086
        name    sopsys

DGROUP  group   _DATA, _BSS

_DATA   segment word public 'DATA'
        public  _timer_fn, _keyb_fn, _kb_al
_timer_fn dw    0               ; void (*)(void), called at every timer tick
_keyb_fn dw     0               ; void (*)(void), called at every key event
_kb_al  db      0               ; AL at INT 9 entry: the PCjr's scan code
keyb_on db      0
old8    dd      0
old9    dd      0
save8   dw      0, 0            ; SP, SS of the interrupted code
save9   dw      0, 0
_DATA   ends

_BSS    segment word public 'BSS'
        db      512 dup (?)
stack8  label   word
        db      512 dup (?)
stack9  label   word
_BSS    ends

_TEXT   segment byte public 'CODE'
        assume  cs:_TEXT, ds:DGROUP

        public  init_timer_, init_keyb_, restoreints_

; void init_timer(void (*f)(void))      f in AX (Watcom register convention)
init_timer_ proc near
        push    es
        push    bx
        mov     _timer_fn, ax
        xor     ax, ax
        mov     es, ax
        cli
        mov     ax, es:[20h]
        mov     word ptr old8, ax
        mov     ax, es:[22h]
        mov     word ptr old8 + 2, ax
        mov     word ptr es:[20h], offset newint8
        mov     es:[22h], cs
        sti
        pop     bx
        pop     es
        ret
init_timer_ endp

; void init_keyb(void (*f)(void))
init_keyb_ proc near
        push    es
        mov     _keyb_fn, ax
        xor     ax, ax
        mov     es, ax
        cli
        mov     ax, es:[24h]
        mov     word ptr old9, ax
        mov     ax, es:[26h]
        mov     word ptr old9 + 2, ax
        mov     word ptr es:[24h], offset newint9
        mov     es:[26h], cs
        mov     keyb_on, 1
        sti
        pop     es
        ret
init_keyb_ endp

; void restoreints(void)
restoreints_ proc near
        push    es
        push    ax
        xor     ax, ax
        mov     es, ax
        cli
        mov     ax, word ptr old8
        or      ax, word ptr old8 + 2
        jz      no8
        mov     ax, word ptr old8
        mov     es:[20h], ax
        mov     ax, word ptr old8 + 2
        mov     es:[22h], ax
no8:    cmp     keyb_on, 0
        je      no9
        mov     ax, word ptr old9
        mov     es:[24h], ax
        mov     ax, word ptr old9 + 2
        mov     es:[26h], ax
        mov     keyb_on, 0
no9:    sti
        pop     ax
        pop     es
        ret
restoreints_ endp

; The timer tick: the game's routine on our stack, then the BIOS's handler
; (which counts the clock and ends the interrupt).
newint8 proc far
        push    ax
        push    ds
        mov     ax, DGROUP
        mov     ds, ax
        mov     save8, sp
        mov     save8 + 2, ss
        mov     ss, ax
        mov     sp, offset DGROUP:stack8
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    es
        cld
        call    word ptr _timer_fn
        pop     es
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        mov     ss, save8 + 2
        mov     sp, save8
        pushf
        call    dword ptr old8
        pop     ds
        pop     ax
        iret
newint8 endp

newint9 proc far
        push    ax
        push    ds
        push    ax
        mov     ax, DGROUP
        mov     ds, ax
        pop     ax
        mov     _kb_al, al
        mov     ax, ds
        mov     save9, sp
        mov     save9 + 2, ss
        mov     ss, ax
        mov     sp, offset DGROUP:stack9
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    es
        cld
        call    word ptr _keyb_fn
        pop     es
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        mov     ss, save9 + 2
        mov     sp, save9
        pop     ds
        pop     ax
        ; on to the BIOS with the registers as they came (AL matters on a PCjr)
        push    ax
        push    ds
        push    bp
        mov     bp, sp
        mov     ax, DGROUP
        mov     ds, ax
        push    word ptr old9 + 2       ; build a far return to the BIOS handler
        push    word ptr old9
        mov     ds, [bp + 2]
        mov     ax, [bp + 4]
        mov     bp, [bp]
        retf    6                       ; jump to old9, dropping the 3 saved words
newint9 endp

_TEXT   ends
        end
