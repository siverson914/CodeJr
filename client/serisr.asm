; Receive interrupt handler for serial.c. 8088 only.
;
; In assembly because it runs once per received byte: the C version (a
; Watcom __interrupt function) saves every register and took roughly 170
; microseconds on a 4.77 MHz 8088, most of the 268 microseconds between
; bytes at 37287 baud. This one saves four registers and takes about 50.
;
; It moves every byte waiting in the UART into the ring buffer and counts
; line errors; the variables are defined in serial.c.

.8086
        name    serisr

DGROUP  group   _DATA
_DATA   segment word public 'DATA'
        extrn   _ser_io_base:word
        extrn   _ser_rxbuf:byte
        extrn   _ser_rx_head:word
        extrn   _ser_rx_tail:word
        extrn   _ser_rx_total:word
        extrn   _ser_lost:word
        extrn   _ser_uart_over:word
        extrn   _ser_frame_errs:word
        extrn   _ser_break_errs:word
_DATA   ends

RXMASK  equ     0FFFh           ; serial.c: RXSIZE - 1

_TEXT   segment byte public 'CODE'
        assume  cs:_TEXT, ds:DGROUP

        public  ser_isr_asm
ser_isr_asm proc far
        push    ax
        push    bx
        push    dx
        push    ds
        mov     ax, DGROUP
        mov     ds, ax
next:   mov     dx, _ser_io_base
        add     dx, 5                   ; line status
        in      al, dx
        test    al, 1                   ; data ready?
        jz      done
        mov     ah, al
        sub     dx, 5                   ; receive buffer
        in      al, dx
        test    ah, 1Eh                 ; overrun, parity, framing, break
        jnz     errs
store:  mov     bx, _ser_rx_head
        mov     _ser_rxbuf[bx], al
        inc     bx
        and     bx, RXMASK
        cmp     bx, _ser_rx_tail
        je      full                    ; buffer full: drop the byte
        mov     _ser_rx_head, bx
        add     word ptr _ser_rx_total, 1
        adc     word ptr _ser_rx_total+2, 0
        jmp     next
full:   inc     _ser_lost
        jmp     next
errs:   test    ah, 02h
        jz      e1
        inc     _ser_lost
        inc     _ser_uart_over
e1:     test    ah, 0Ch
        jz      e2
        inc     _ser_frame_errs
e2:     test    ah, 10h
        jz      store
        inc     _ser_break_errs
        jmp     store
done:   mov     al, 20h                 ; end of interrupt
        out     20h, al
        pop     ds
        pop     dx
        pop     bx
        pop     ax
        iret
ser_isr_asm endp

_TEXT   ends
        end
