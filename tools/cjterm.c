/* CJTERM: CodeJr's serial test terminal.
 *
 * A dumb terminal with our own serial driver, used to prove that the PCjr
 * and the WiFi modem talk in plain full duplex once we raise RTS ourselves.
 * Alt-S (and quitting) shows the modem's status lines and byte counts.
 *
 *   CJTERM [/P:1|2] [/B:9600] [/D:host:port] [/L:file] [/X:seconds]
 *
 *   /P  BIOS serial port (default 1)     /B  speed (default 9600)
 *   /D  dial host:port on start          /L  log received bytes to a file
 *   /X  exit after this many seconds (for automated tests)
 *   /V  force the UART baud divisor (experiments; 9600 is 12)
 *   /Q:1  poll the UART instead of using its interrupt (diagnostics)
 *   /K:1  keep the UART's speed and format as found (e.g. as Kermit left it)
 *   /R:n  RTS: 0 off (default), 1 on, 2 on only while sending (like Kermit's
 *         half duplex)
 *
 * Keys: Alt-X quit, Alt-S status, Alt-E local echo on/off,
 *       Alt-H hang up (+++ ATH).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <bios.h>
#include "serial.h"

static int echo;
static FILE *logf;

static unsigned long ticks(void)
{
	return *(unsigned long far *)MK_FP(0x40, 0x6C);
}

/* Prints one character through DOS (INT 21h, AH=06h: no Ctrl-C check).
 * DOS handles CR, LF, backspace and scrolling, and with ANSI.SYS loaded it
 * also handles colors and cursor movement. */
static void dos_putc(unsigned char c)
{
	union REGS r;
	if (c == 0xFF)
		return; /* AH=06h treats DL=FFh as "read a key" */
	r.h.ah = 6;
	r.h.dl = c;
	intdos(&r, &r);
}

static void dos_puts(const char *s)
{
	while (*s)
		dos_putc((unsigned char)*s++);
}

/* Shows one received byte. APC strings (ESC _ ... ESC \) are meant for
 * MS-Kermit and would print as garbage through ANSI.SYS, so skip them. */
static void show(unsigned char c)
{
	static int apc; /* 0 normal, 1 after ESC, 2 in APC, 3 ESC inside APC */

	switch (apc) {
	case 1:
		if (c == '_') {
			apc = 2;
			return;
		}
		apc = 0;
		dos_putc(0x1B);
		break;
	case 2:
		if (c == 0x1B)
			apc = 3;
		return;
	case 3:
		apc = c == '\\' ? 0 : 2;
		return;
	}
	if (c == 0x1B) {
		apc = 1;
		return;
	}
	dos_putc(c);
}

static void status(void)
{
	char s[160];
	unsigned char msr = ser_msr();

	sprintf(s, "\r\n[CJTERM %03X div %u LCR %02X RTS%d IRQ%d%s CTS:%s DSR:%s DCD:%s "
	        "rx %lu lost %u frame %u break %u echo %s]\r\n",
	        ser_base(), ser_div(), ser_lcr(), ser_rts, ser_irq(), ser_polled ? " polled" : "",
	        msr & MS_CTS ? "on" : "off", msr & MS_DSR ? "on" : "off",
	        msr & MS_DCD ? "on" : "off",
	        ser_rx_count(), ser_overruns(), ser_framing(), ser_breaks(),
	        echo ? "on" : "off");
	dos_puts(s);
}

static void hangup(void)
{
	unsigned long t = ticks();
	while (ticks() - t < 22) ; /* 1.2 s guard time */
	ser_puts("+++");
	t = ticks();
	while (ticks() - t < 22) ;
	ser_puts("ATH\r");
}

int main(int argc, char **argv)
{
	int port = 0, i;
	long baud = 9600;
	const char *dial = NULL;
	long exit_after = -1;
	unsigned long start;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (a[0] != '/' || !a[1] || a[2] != ':') {
			printf("usage: CJTERM [/P:1|2] [/B:9600] [/D:host:port] [/L:file] [/X:secs]\n");
			return 1;
		}
		switch (a[1] | 0x20) {
		case 'p': port = atoi(a + 3) - 1; break;
		case 'b': baud = atol(a + 3); break;
		case 'd': dial = a + 3; break;
		case 'l': logf = fopen(a + 3, "wb"); break;
		case 'x': exit_after = atol(a + 3); break;
		case 'v': ser_divisor = (unsigned)atoi(a + 3); break;
		case 'q': ser_polled = atoi(a + 3); break;
		case 'k': ser_keep = atoi(a + 3); break;
		case 'r': ser_rts = atoi(a + 3); break;
		}
	}

	ser_open(port, baud);
	dos_puts("CJTERM: Alt-X quit, Alt-S status, Alt-E echo, Alt-H hang up\r\n");
	status();
	if (dial) {
		ser_puts("ATDT");
		ser_puts(dial);
		ser_putc('\r');
	}

	start = ticks();
	for (;;) {
		int c;
		unsigned key;

		while ((c = ser_getc()) >= 0) {
			show((unsigned char)c);
			if (logf)
				fputc(c, logf);
		}

		if (_bios_keybrd(_KEYBRD_READY)) {
			key = _bios_keybrd(_KEYBRD_READ);
			if (key == 0x2D00) /* Alt-X */
				break;
			if (key == 0x1200) { /* Alt-E */
				echo = !echo;
			} else if (key == 0x1F00) { /* Alt-S */
				status();
			} else if (key == 0x2300) { /* Alt-H */
				dos_puts("\r\n[hanging up]\r\n");
				hangup();
			} else if (key & 0xFF) {
				ser_putc((unsigned char)key);
				if (echo) {
					dos_putc((unsigned char)key);
					if ((key & 0xFF) == '\r')
						dos_putc('\n');
				}
			}
		}

		if (exit_after >= 0 && (long)(ticks() - start) > exit_after * 182 / 10)
			break;
	}

	status();
	ser_close();
	if (logf)
		fclose(logf);
	return 0;
}
