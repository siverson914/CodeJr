#include <dos.h>
#include <conio.h>
#include "serial.h"

/* UART registers, offsets from the base address. */
#define RBR 0 /* receive buffer (read) */
#define THR 0 /* transmit holding (write) */
#define IER 1 /* interrupt enable */
#define IIR 2 /* interrupt identification */
#define LCR 3 /* line control */
#define MCR 4 /* modem control */
#define LSR 5 /* line status */
#define MSR 6 /* modem status */

#define LSR_DR   0x01
#define LSR_OE   0x02
#define LSR_PE   0x04
#define LSR_FE   0x08
#define LSR_BI   0x10
#define LSR_THRE 0x20
#define LSR_TEMT 0x40

#define MCR_DTR  0x01
#define MCR_RTS  0x02
#define MCR_OUT2 0x08 /* gates the UART's interrupt line on PCs */

#define PIC_CMD  0x20
#define PIC_MASK 0x21
#define EOI      0x20

#define RXSIZE 4096 /* power of two */

/* Shared with the interrupt handler in serisr.asm. */
unsigned ser_io_base;
unsigned char ser_rxbuf[RXSIZE];
volatile unsigned ser_rx_head, ser_rx_tail;
volatile unsigned long ser_rx_total;
volatile unsigned ser_lost;          /* bytes lost: UART overrun or full buffer */
volatile unsigned ser_uart_over;     /* of those, lost in the UART */
volatile unsigned ser_frame_errs, ser_break_errs;

extern void __far ser_isr_asm(void);
#pragma aux ser_isr_asm "*";

#define base      ser_io_base
#define rxbuf     ser_rxbuf
#define rx_head   ser_rx_head
#define rx_tail   ser_rx_tail
#define rx_total  ser_rx_total
#define lost      ser_lost
#define uart_overruns ser_uart_over
#define framing   ser_frame_errs
#define breaks    ser_break_errs

static int irq;
static void (__interrupt __far *old_isr)(void);
static unsigned char old_mask_bit;
static int opened;
unsigned ser_divisor;           /* nonzero: force this baud divisor */
static unsigned used_divisor;

int ser_polled;                 /* nonzero: no interrupt, ser_poll() reads */
int ser_keep;                   /* nonzero: leave speed and format as found */
/* RTS off by default: on the PCjr, raising RTS corrupts what it receives
 * from the WiFi modem (see docs/JOURNAL.md). */
int ser_rts = 0;                /* 0 RTS off, 1 on, 2 on only while sending */

/* Moves every byte waiting in the UART into the ring buffer. */
static void drain_uart(void)
{
	unsigned char lsr;

	while ((lsr = inp(base + LSR)) & LSR_DR) {
		unsigned char c = inp(base + RBR);
		unsigned next = (rx_head + 1) & (RXSIZE - 1);

		if (lsr & LSR_OE) {
			lost++;
			uart_overruns++;
		}
		if (lsr & (LSR_FE | LSR_PE))
			framing++;
		if (lsr & LSR_BI)
			breaks++;
		if (next == rx_tail) {
			lost++;
		} else {
			rxbuf[rx_head] = c;
			rx_head = next;
			rx_total++;
		}
	}
}

void ser_poll(void)
{
	if (ser_polled)
		drain_uart();
}

/* Modem control bits while not sending. */
static unsigned char mcr_idle(void)
{
	unsigned char m = MCR_DTR;
	if (ser_rts == 1)
		m |= MCR_RTS;
	if (!ser_polled)
		m |= MCR_OUT2;
	return m;
}

static unsigned bios_baud_bits(long baud)
{
	switch (baud) {
	case 300:  return 2 << 5;
	case 600:  return 3 << 5;
	case 1200: return 4 << 5;
	case 2400: return 5 << 5;
	case 4800: return 6 << 5;
	default:   return 7 << 5; /* 9600 */
	}
}

/* The PCjr's UART runs from 1.7895 MHz, not the PC's 1.8432 MHz, so its
 * exact speeds are 111861 / divisor: 9322 (for "9600"), 18643, 37287,
 * 55930. The WiFi modem can be set to those exact rates. */
unsigned ser_set_baud(long baud)
{
	unsigned char far *model = (unsigned char far *)MK_FP(0xF000, 0xFFFE);
	unsigned long clock = (*model == 0xFD) ? 1789773UL : 1843200UL;
	unsigned div = (unsigned)((clock / 16 + baud / 2) / baud);

	if (div == 0)
		div = 1;
	while (!(inp(base + LSR) & LSR_TEMT))
		; /* let the last byte finish at the old speed */
	_disable();
	outp(base + LCR, 0x80);       /* DLAB on */
	outp(base + 0, div & 0xFF);
	outp(base + 1, div >> 8);
	outp(base + LCR, 0x03);       /* 8N1, DLAB off */
	_enable();
	used_divisor = div;
	return div;
}

/* Receives a burst by polling the UART in a tight loop with its interrupt
 * off, which keeps up with far higher speeds than one interrupt per byte.
 * Stops after `fends` bytes of value 0xC0 (frame delimiters), when buf is
 * full, or when the line goes quiet: `first_ticks` BIOS ticks (18.2/s)
 * before the first byte, `gap_ticks` after the last one. */
unsigned ser_burst_first, ser_burst_last;

unsigned ser_burst(unsigned char *buf, unsigned max, unsigned fends, unsigned first_ticks, unsigned gap_ticks)
{
	unsigned far *tick = (unsigned far *)MK_FP(0x40, 0x6C);
	unsigned lsrp = base + LSR, rbrp = base + RBR;
	unsigned n = 0, idle = 0, wait = first_ticks, mark = *tick;
	unsigned char ier = (unsigned char)inp(base + IER);
	int c, got = 0;

	ser_burst_first = ser_burst_last = mark;
	if (fends == 0)
		fends = 0xFFFF;
	outp(base + IER, 0);
	/* Anything the interrupt handler already took. */
	while (n < max && (c = ser_getc()) >= 0) {
		buf[n++] = (unsigned char)c;
		got = 1;
		if (c == 0xC0 && --fends == 0)
			goto out;
	}
	while (n < max) {
		unsigned char lsr = (unsigned char)inp(lsrp);

		if (lsr & LSR_DR) {
			unsigned char ch = (unsigned char)inp(rbrp);

			if (lsr & LSR_OE) {
				lost++;
				uart_overruns++;
			}
			ser_burst_last = *tick;
			if (n == 0)
				ser_burst_first = ser_burst_last;
			buf[n++] = ch;
			got = 1;
			idle = 0;
			if (ch == 0xC0 && --fends == 0)
				break;
		} else if ((++idle & 0xFF) == 0) {
			/* Quiet for 256 polls (a few ms): time to check the clock. */
			if (got) {
				mark = *tick;
				wait = gap_ticks;
				got = 0;
			} else if (*tick - mark >= wait) {
				break;
			}
		}
	}
out:
	rx_total += n;
	outp(base + IER, ier);
	return n;
}

int ser_open(int bios_port, long baud)
{
	union REGS r;
	unsigned far *bda = (unsigned far *)MK_FP(0x40, 0);
	int vec;

	base = bda[bios_port];
	if (base == 0)
		base = 0x2F8; /* the PCjr's built-in port, if the BIOS didn't list it */
	irq = (base == 0x3F8 || base == 0x3E8) ? 4 : 3;

	if (ser_keep) {
		unsigned char lcr;
		_disable();
		lcr = (unsigned char)inp(base + LCR);
		outp(base + LCR, lcr | 0x80);
		used_divisor = inp(base + 0) | (inp(base + 1) << 8);
		outp(base + LCR, lcr);
		_enable();
		goto speed_done;
	}

	/* Let the BIOS set speed and 8N1 (bits: baud, no parity, 1 stop, 8 data). */
	r.h.ah = 0;
	r.h.al = (unsigned char)(bios_baud_bits(baud) | 0x03);
	r.x.dx = bios_port;
	int86(0x14, &r, &r);

	/* Then set the divisor ourselves: the PCjr BIOS can't do 9600. */
	if (ser_divisor) {
		_disable();
		outp(base + LCR, 0x80);       /* DLAB on */
		outp(base + 0, ser_divisor & 0xFF);
		outp(base + 1, ser_divisor >> 8);
		outp(base + LCR, 0x03);       /* 8N1, DLAB off */
		_enable();
		used_divisor = ser_divisor;
	} else {
		ser_set_baud(baud);
	}
speed_done:

	vec = 8 + irq;
	_disable();
	outp(base + LCR, inp(base + LCR) & 0x7F); /* make sure DLAB is off */
	if (ser_polled) {
		outp(base + MCR, mcr_idle());
		outp(base + IER, 0);
	} else {
		old_isr = _dos_getvect(vec);
		_dos_setvect(vec, (void (__interrupt __far *)(void))ser_isr_asm);
		old_mask_bit = inp(PIC_MASK) & (1 << irq);
		outp(PIC_MASK, inp(PIC_MASK) & ~(1 << irq));
		outp(base + MCR, mcr_idle());
		outp(base + IER, 0x01);           /* interrupt on received data */
	}
	while (inp(base + LSR) & LSR_DR)
		inp(base + RBR);
	inp(base + IIR);
	_enable();
	opened = 1;
	return 0;
}

void ser_close(void)
{
	if (!opened)
		return;
	_disable();
	outp(base + IER, 0);
	/* Keep DTR up so the modem doesn't hang up just because we exited. */
	outp(base + MCR, MCR_DTR | MCR_RTS);
	if (!ser_polled) {
		outp(PIC_MASK, (inp(PIC_MASK) & ~(1 << irq)) | old_mask_bit);
		_dos_setvect(8 + irq, old_isr);
	}
	_enable();
	opened = 0;
}

int ser_getc(void)
{
	int c;

	ser_poll();
	if (rx_tail == rx_head)
		return -1;
	c = rxbuf[rx_tail];
	rx_tail = (rx_tail + 1) & (RXSIZE - 1);
	return c;
}

void ser_putc(unsigned char c)
{
	if (ser_rts == 2)
		outp(base + MCR, mcr_idle() | MCR_RTS);
	while (!(inp(base + LSR) & LSR_THRE))
		;
	outp(base + THR, c);
	if (ser_rts == 2) {
		/* Like MS-Kermit's half duplex: drop RTS once the byte is out. */
		while (!(inp(base + LSR) & LSR_TEMT))
			;
		outp(base + MCR, mcr_idle());
	}
}

void ser_puts(const char *s)
{
	while (*s)
		ser_putc((unsigned char)*s++);
}

unsigned ser_rx_pending(void) { return (rx_head - rx_tail) & (RXSIZE - 1); }

unsigned ser_base(void) { return base; }
unsigned ser_div(void) { return used_divisor; }
int ser_irq(void) { return irq; }
unsigned char ser_msr(void) { return (unsigned char)inp(base + MSR); }
unsigned long ser_rx_count(void) { return rx_total; }
unsigned ser_overruns(void) { return lost; }
unsigned ser_framing(void) { return framing; }
unsigned ser_uart_overruns(void) { return uart_overruns; }
unsigned ser_breaks(void) { return breaks; }
unsigned char ser_lcr(void) { return (unsigned char)inp(base + LCR); }
