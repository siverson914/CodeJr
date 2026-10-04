/* CODEJR.EXE: the CodeJr client for the IBM PCjr.
 *
 * Dials the CodeJr server through the WiFi modem, then talks the CodeJr
 * protocol (cjp.c): chat with Claude, and files Claude writes are pulled
 * into the current directory (pull.c) with a progress bar.
 *
 *   CODEJR [/D:host:port] [/P:1|2] [/B:9600] [/F:37287] [/T:text] [/X:secs]
 *
 *   /D  server to dial (default: DIAL= line of C:\CODEJR\CODEJR.CFG)
 *   /P  BIOS serial port (default 1)
 *   /B  the modem's saved speed (default 9600)
 *   /F  switch the modem and the PCjr to this speed for the call, and back
 *       afterwards. The PCjr's exact speeds are 18643, 37287 and 55930.
 *   /T  type this line once connected    /X  exit after N seconds
 *   /S  save the screen to this file before exiting
 *   (/T, /X and /S are for automated tests)
 *
 * Keys: Enter sends, Backspace, Ctrl-C stops Claude, Alt-S shows link
 * counters, Alt-X hangs up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include <bios.h>
#include "serial.h"
#include "cjp.h"
#include "pull.h"
#include "screen.h"

#define VERSION "0.3"
#define TICKS_PER_SEC 18

static const unsigned char style_attr[] = {
	0x07, /* plain: light gray */
	0x0E, /* user: yellow */
	0x03, /* note: cyan */
	0x0C, /* warn: light red */
	0x0F, /* banner: bright white */
};

static cjp_link link;
static char input[300];
static int input_len;

/* Outgoing messages wait here when the send window is full. */
#define OUTQ 8
static struct {
	unsigned char type;
	unsigned len;
	unsigned char p[CJP_MAXPAYLOAD];
} outq[OUTQ];
static int outq_head, outq_n;

/* A file the server has offered; pulled from the main loop. */
static struct {
	int pending;
	char name[13];
	unsigned long size;
	unsigned chunks;
} offer;

static long line_baud = 9600; /* the speed in use, for rate displays */

static int connected, quitting;
static int cursor_dirty;
static unsigned long quit_at;

static unsigned long ticks(void)
{
	return *(unsigned long far *)MK_FP(0x40, 0x6C);
}

static void start_quit(void)
{
	if (!quitting) {
		quitting = 1;
		quit_at = ticks();
	}
}

static void link_write(const unsigned char *p, unsigned n, void *ctx)
{
	(void)ctx;
	while (n--)
		ser_putc(*p++);
}

static unsigned long link_now(void *ctx)
{
	(void)ctx;
	return ticks();
}

static void queue(unsigned char type, const void *p, unsigned n)
{
	int i;
	if (outq_n == OUTQ || n > CJP_MAXPAYLOAD)
		return;
	i = (outq_head + outq_n) % OUTQ;
	outq[i].type = type;
	outq[i].len = n;
	memcpy(outq[i].p, p, n);
	outq_n++;
}

static void flush_queue(void)
{
	while (outq_n && cjp_can_send(&link)) {
		cjp_send(&link, outq[outq_head].type, outq[outq_head].p, outq[outq_head].len);
		outq_head = (outq_head + 1) % OUTQ;
		outq_n--;
	}
}

static void counters(char *s)
{
	sprintf(s, "lost %u bytes (%u late interrupt), %u framing errors, %u bad frames, %u resent",
	        ser_overruns(), ser_uart_overruns(), ser_framing(), link.bad, link.resent);
}

/* Client-side messages go into the chat history (dark gray), so a status
 * update can't wipe them out. */
static void note(const char *s)
{
	scr_puts(s, 0x08);
	scr_putc('\n', 0x08);
}

/* Reports new losses as they happen, at most once a second. */
static void report_losses(void)
{
	static unsigned seen_lost, seen_bad, seen_frame;
	static unsigned long last;
	char s[120], c[100];

	if (ticks() - last < TICKS_PER_SEC)
		return;
	last = ticks();
	if (ser_overruns() == seen_lost && link.bad == seen_bad && ser_framing() == seen_frame)
		return;
	seen_lost = ser_overruns();
	seen_bad = link.bad;
	seen_frame = ser_framing();
	counters(c);
	sprintf(s, "[client] %s", c);
	note(s);
}

/* Is a key waiting? Reads the BIOS keyboard buffer pointers directly
 * instead of calling INT 16h on every loop. */
static int key_waiting(void)
{
	unsigned far *head = (unsigned far *)MK_FP(0x40, 0x1A);
	unsigned far *tail = (unsigned far *)MK_FP(0x40, 0x1C);
	return *head != *tail;
}

/* DOS 8.3 file names only: no paths, nothing odd. */
static int valid_name(const char *s)
{
	int base = 0, ext = -1;
	for (; *s; s++) {
		if (*s == '.') {
			if (ext >= 0 || base == 0)
				return 0;
			ext = 0;
		} else if (isalnum((unsigned char)*s) || strchr("_$~!#%&@^'()-", *s)) {
			if (ext >= 0) {
				if (++ext > 3)
					return 0;
			} else if (++base > 8) {
				return 0;
			}
		} else {
			return 0;
		}
	}
	return base > 0 && ext != 0;
}

static void io_send(const unsigned char *p, unsigned n, void *ctx)
{
	(void)ctx;
	while (n--)
		ser_putc(*p++);
}

static unsigned io_burst(unsigned char *buf, unsigned max, unsigned fends,
                         unsigned first, unsigned gap, void *ctx)
{
	(void)ctx;
	return ser_burst(buf, max, fends, first, gap);
}

static unsigned long pull_t0; /* tick count when the current file started */

static void progress(unsigned long got)
{
	char s[80], bar[21];
	unsigned long t = ticks() - pull_t0;
	/* Bits per second as the line counts them, 10 per byte (start and
	 * stop bits), so the figure compares directly with the baud rate:
	 * got*10 over t/18.2 seconds. */
	unsigned long bps = t ? got * 182UL / t : 0;
	int i, n = offer.size ? (int)(got * 20 / offer.size) : 20;

	for (i = 0; i < 20; i++)
		bar[i] = i < n ? '#' : '.';
	bar[20] = 0;
	sprintf(s, "Receiving %s [%s] %lu/%lu  %lu bps", offer.name, bar, got, offer.size, bps);
	scr_status(s);
}

/* Fetches the offered file chunk by chunk: ask for a chunk, then only
 * listen while the server sends it as one burst (the PCjr corrupts what
 * it receives while it transmits), and ask again for whatever arrived
 * damaged. The disk is written between bursts, when the line is quiet. */
static void pull_file(void)
{
	static pull_chunk pc;
	static unsigned char raw[9216], out[PULL_RAW];
	static const pull_io io = { io_send, io_burst, NULL };
	unsigned char r[1 + 40];
	char err[40], line[160];
	unsigned long got = 0, t0 = ticks(), secs;
	unsigned k, rounds = 0, n;
	FILE *f = NULL;

	err[0] = 0;
	pc.bad = 0;
	pull_t0 = t0;
	if (!valid_name(offer.name))
		strcpy(err, "not a DOS file name");
	else if ((f = fopen(offer.name, "wb")) == NULL)
		strcpy(err, "can't create file");
	progress(0);
	for (k = 0; !err[0] && k < offer.chunks; k++) {
		int len;

		if (key_waiting() && _bios_keybrd(_KEYBRD_READ) == 0x2D00) {
			strcpy(err, "cancelled");
			start_quit();
			break;
		}
		len = pull_fetch(&pc, k, &io, raw, sizeof raw, out, sizeof out, &rounds);
		if (len < 0) {
			strcpy(err, "the server stopped answering");
			break;
		}
		if (fwrite(out, 1, (size_t)len, f) != (size_t)len)
			strcpy(err, "disk full?");
		got += (unsigned long)len;
		progress(got);
	}
	if (f && fclose(f) != 0 && !err[0])
		strcpy(err, "can't close file");
	if (!err[0] && got != offer.size)
		strcpy(err, "size mismatch");
	if (err[0]) {
		if (f)
			remove(offer.name); /* don't leave half a file */
		r[0] = 1;
		n = (unsigned)strlen(err);
		memcpy(r + 1, err, n);
	} else {
		r[0] = 0;
		n = (unsigned)strlen(offer.name);
		memcpy(r + 1, offer.name, n);
	}
	queue(CJP_FILERESULT, r, n + 1);
	offer.pending = 0;

	secs = (ticks() - t0) * 10 / 182;
	if (err[0])
		sprintf(line, "[client] %s: %s", offer.name, err);
	else
	{
		unsigned long t = ticks() - t0, bps = t ? got * 182UL / t : 0;
		sprintf(line, "[client] %s: %lu bytes in %lu s (%lu bps), %u requests for %u chunks, "
		        "%u damaged blocks", offer.name, got, secs, bps,
		        rounds, offer.chunks, pc.bad);
	}
	note(line);
	scr_status(err[0] ? err : "Saved.");
}

static void file_offer(const cjp_frame *f)
{
	unsigned n;

	if (f->len < 7 || f->len - 6 > 12)
		return;
	n = f->len - 6;
	offer.size = f->payload[0] | (unsigned long)f->payload[1] << 8 |
	             (unsigned long)f->payload[2] << 16 | (unsigned long)f->payload[3] << 24;
	offer.chunks = f->payload[4] | ((unsigned)f->payload[5] << 8);
	memcpy(offer.name, f->payload + 6, n);
	offer.name[n] = 0;
	offer.pending = 1; /* pulled from the main loop, once this frame is acked */
}

static void handle(const cjp_frame *f)
{
	char s[CJP_MAXPAYLOAD + 1];
	unsigned i;

	switch (f->type) {
	case CJP_HELLO:
		connected = 1;
		scr_status("Connected.  Enter sends,  Ctrl-C stops Claude,  Alt-X hangs up.");
		break;
	case CJP_TEXT:
		if (f->len) {
			unsigned char attr = style_attr[f->payload[0] < 5 ? f->payload[0] : 0];
			for (i = 1; i < f->len; i++)
				scr_putc(f->payload[i], attr);
		}
		break;
	case CJP_STATUS:
		memcpy(s, f->payload, f->len);
		s[f->len] = 0;
		scr_status(s);
		break;
	case CJP_CLEAR:
		scr_clear_transcript();
		break;
	case CJP_FILEOFFER:
		file_offer(f);
		break;
	case CJP_BYE:
		start_quit();
		break;
	}
	cursor_dirty = 1; /* put the cursor back on the input line when quiet */
}

/* Pumps serial input into the link. Returns when nothing is waiting, or
 * as soon as a file is offered: the file is pulled before anything else. */
static void poll_serial(void)
{
	int c;

	while (!offer.pending && (c = ser_getc()) >= 0) {
		if (cjp_input(&link, (unsigned char)c)) {
			cjp_frame f = link.rx;
			handle(&f);
			cjp_ack(&link);
		}
	}
}

static void key(unsigned k)
{
	unsigned char ch = (unsigned char)k;

	if (k == 0x1F00) { /* Alt-S: connection counters, into the history */
		char s[120], c[100];
		counters(c);
		sprintf(s, "[client] %s", c);
		note(s);
	} else if (k == 0x2D00) { /* Alt-X */
		queue(CJP_BYE, NULL, 0);
		start_quit();
	} else if (ch == '\r') {
		if (input_len) {
			queue(CJP_INPUT, input, input_len);
			input_len = 0;
			input[0] = 0;
		}
	} else if (ch == 3) {
		unsigned char c = CJP_KEY_CTRLC;
		queue(CJP_KEY, &c, 1);
	} else if (ch == 8) {
		if (input_len)
			input[--input_len] = 0;
	} else if (ch >= 32 && ch < 127 && input_len < (int)sizeof(input) - 1) {
		input[input_len++] = (char)ch;
		input[input_len] = 0;
	}
	scr_input(input);
}

static int read_cfg(char *dial, int size)
{
	FILE *f = fopen("C:\\CODEJR\\CODEJR.CFG", "r");
	char line[100];
	int found = 0;
	if (!f)
		return 0;
	while (fgets(line, sizeof line, f)) {
		if (strncmp(line, "DIAL=", 5) == 0) {
			char *e = line + strlen(line);
			while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
				*--e = 0;
			strncpy(dial, line + 5, size - 1);
			dial[size - 1] = 0;
			found = 1;
		}
	}
	fclose(f);
	return found;
}

/* Waits up to `t` ticks for `want` in what the modem sends. */
static int expect(const char *want, unsigned t)
{
	char seen[16];
	unsigned long start = ticks();
	int c;

	memset(seen, ' ', sizeof seen - 1);
	seen[sizeof seen - 1] = 0;
	while (ticks() - start <= t) {
		while ((c = ser_getc()) >= 0) {
			memmove(seen, seen + 1, sizeof seen - 2);
			seen[sizeof seen - 2] = (char)c;
			if (strstr(seen, want))
				return 1;
		}
	}
	return 0;
}

static void settle(unsigned t)
{
	unsigned long start = ticks();
	while (ticks() - start <= t)
		while (ser_getc() >= 0)
			;
}

/* Moves the modem and the PCjr from speed `from` to `to`, and checks the
 * modem still answers. On failure the PCjr goes back to `from`. The modem
 * is not told to save the speed, so a power cycle always restores it. */
static int change_speed(long from, long to)
{
	char cmd[24];
	int i;

	sprintf(cmd, "ATB%ld\r", to);
	ser_puts(cmd);
	settle(6); /* the modem switches at once; its answer is unreadable */
	ser_set_baud(to);
	settle(1);
	for (i = 0; i < 3; i++) {
		ser_puts("AT\r");
		if (expect("OK", TICKS_PER_SEC))
			return 1;
	}
	ser_set_baud(from);
	return 0;
}

/* Dials and waits for the server's READY banner. Returns 0 on success. */
static int dial(const char *host)
{
	char seen[16] = "";
	unsigned long start = ticks(), last = 0;
	char s[80];

	sprintf(s, "Dialing %s ...   (Alt-X gives up)", host);
	scr_status(s);
	/* ATD, not ATDT: on Zimodem the T means "translate as TELNET", which
	 * alters binary data (it drops a NUL after a CR). */
	ser_puts("ATD");
	ser_puts(host);
	ser_putc('\r');
	for (;;) {
		int c;
		while ((c = ser_getc()) >= 0) {
			memmove(seen, seen + 1, sizeof seen - 2);
			seen[sizeof seen - 2] = (char)c;
			seen[sizeof seen - 1] = 0;
			if (strstr(seen, "READY"))
				return 0;
		}
		if (_bios_keybrd(_KEYBRD_READY) && _bios_keybrd(_KEYBRD_READ) == 0x2D00)
			return -1;
		if (ticks() - start > 60UL * TICKS_PER_SEC)
			return -1;
		if (ticks() - last > TICKS_PER_SEC) {
			sprintf(s, "Dialing %s ... %lus   (Alt-X gives up)", host,
			        (ticks() - start) / TICKS_PER_SEC);
			scr_status(s);
			last = ticks();
		}
	}
}

int main(int argc, char **argv)
{
	char host[80] = "";
	const char *auto_text = NULL;
	const char *shot = NULL;
	long exit_after = -1;
	int port = 0, i, sent_auto = 0;
	long baud = 9600, fast = 0;
	unsigned long start;
	char hello[48];

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (a[0] != '/' || !a[1] || a[2] != ':') {
			printf("usage: CODEJR [/D:host:port] [/P:1|2] [/B:9600] [/F:37287] [/T:text] [/X:secs]\n");
			return 1;
		}
		switch (a[1] | 0x20) {
		case 'd': strncpy(host, a + 3, sizeof host - 1); break;
		case 'p': port = atoi(a + 3) - 1; break;
		case 'b': baud = atol(a + 3); break;
		case 'f': fast = atol(a + 3); break;
		case 't': auto_text = a + 3; break;
		case 'x': exit_after = atol(a + 3); break;
		case 's': shot = a + 3; break;
		}
	}
	if (!host[0] && !read_cfg(host, sizeof host)) {
		printf("CODEJR: no server to dial. Use /D:host:port or put DIAL=host:port\n"
		       "in C:\\CODEJR\\CODEJR.CFG\n");
		return 1;
	}

	ser_open(port, baud); /* RTS stays off: see docs/JOURNAL.md */
	scr_init("CodeJr " VERSION "  -  Claude Code for the IBM PCjr");
	start = ticks();
	line_baud = baud;
	if (fast && fast != baud) {
		char s[80];
		sprintf(s, "Switching the modem to %ld baud...", fast);
		scr_status(s);
		if (change_speed(baud, fast)) {
			line_baud = fast;
		} else {
			sprintf(s, "[client] The modem didn't answer at %ld baud; staying at %ld.", fast, baud);
			note(s);
		}
	}
	if (dial(host) != 0) {
		if (line_baud != baud)
			change_speed(line_baud, baud);
		ser_close();
		scr_done();
		printf("CODEJR: no answer from %s\n", host);
		return 1;
	}

	cjp_init(&link, link_write, link_now, NULL, 2 * TICKS_PER_SEC);
	sprintf(hello, "CODEJR/2 client " VERSION " baud=%ld", line_baud);
	queue(CJP_HELLO, hello, (unsigned)strlen(hello));
	scr_status("Connecting...");

	while (!(quitting && outq_n == 0 && cjp_idle(&link))) {
		poll_serial();
		if (offer.pending)
			pull_file();
		if (key_waiting())
			key(_bios_keybrd(_KEYBRD_READ));
		if (cursor_dirty && ser_rx_pending() == 0) {
			scr_input(input);
			cursor_dirty = 0;
		}
		report_losses();
		if (auto_text && connected && !sent_auto) {
			queue(CJP_INPUT, auto_text, (unsigned)strlen(auto_text));
			sent_auto = 1;
		}
		flush_queue();
		cjp_tick(&link);
		if (exit_after >= 0 && (long)(ticks() - start) > exit_after * 182 / 10) {
			queue(CJP_BYE, NULL, 0);
			start_quit();
			exit_after = -1;
		}
		/* Don't wait forever for the last acks if the line is gone. */
		if (quitting && ticks() - quit_at > 5UL * TICKS_PER_SEC)
			break;
	}

	if (line_baud != baud) {
		/* The server hangs up after BYE; then the modem takes commands. */
		scr_status("Putting the modem back to its usual speed...");
		expect("NO CARRIER", 3 * TICKS_PER_SEC);
		change_speed(line_baud, baud);
	}
	ser_close();
	if (shot)
		scr_save(shot);
	scr_done();
	printf("CodeJr: hung up. Programs are in this directory.\n");
	return 0;
}
