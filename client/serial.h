/* Interrupt-driven serial port for the PCjr (and any PC 8250/16450 UART). */
#ifndef SERIAL_H
#define SERIAL_H

/* Modem status bits (UART MSR). */
#define MS_CTS 0x10
#define MS_DSR 0x20
#define MS_RI  0x40
#define MS_DCD 0x80

/* Opens BIOS serial port `bios_port` (0 = COM1, 1 = COM2) at `baud`, 8N1.
 * The BIOS programs the baud rate (the PCjr's UART clock differs from the
 * PC's), then we take over with our own receive interrupt and raise DTR and
 * RTS. Returns 0 on success. */
int ser_open(int bios_port, long baud);
void ser_close(void);

/* Changes speed; returns the UART divisor used. */
unsigned ser_set_baud(long baud);

/* Receives a burst by tight polling (see serial.c). Returns the count. */
unsigned ser_burst(unsigned char *buf, unsigned max, unsigned fends, unsigned first_ticks, unsigned gap_ticks);
/* Tick counts (low word of the BIOS clock) of the last burst's first and
 * last bytes; both equal the start time if nothing came. */
extern unsigned ser_burst_first, ser_burst_last;

int ser_getc(void);            /* next received byte, or -1 if none */
unsigned ser_rx_pending(void); /* bytes waiting in the receive buffer */
void ser_putc(unsigned char c);
void ser_puts(const char *s);

unsigned ser_base(void);       /* UART I/O address in use */
unsigned ser_div(void);        /* baud divisor in use */
extern unsigned ser_divisor;   /* set before ser_open to force a divisor */
int ser_irq(void);
unsigned char ser_msr(void);   /* current modem status lines */
unsigned long ser_rx_count(void);
unsigned ser_overruns(void);   /* bytes lost: UART overrun or full buffer */
unsigned ser_uart_overruns(void); /* of those, lost in the UART (late interrupt) */
unsigned ser_framing(void);    /* bytes with framing or parity errors */
unsigned ser_breaks(void);     /* break conditions seen */
unsigned char ser_lcr(void);   /* line control register (data bits etc.) */
extern int ser_polled;         /* set before ser_open: read by polling */
extern int ser_keep;           /* set before ser_open: keep speed/format */
extern int ser_rts;            /* RTS: 0 off (default), 1 on, 2 only while sending */
void ser_poll(void);

#endif
