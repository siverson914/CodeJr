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
 *   /R:1  put the modem back to its saved speed and exit (before MS-Kermit)
 *   /F  switch the modem and the PCjr to this speed, and leave them there
 *       (default 37287; /F:9600 stays at the saved speed). The
 *       PCjr's exact speeds are 18643, 37287 and 55930. On the real PCjr,
 *       37287 is fastest: at 55930 it misses bytes.
 *   /L  set by C:\CODEJR.BAT, which runs whatever /run picked and calls
 *       back afterwards (it reads C:\CODEJR\RUNNEXT.BAT)
 *   /T  type this line once connected    /X  exit after N seconds
 *   /S  save the screen to this file before exiting
 *   (/T, /X and /S are for automated tests)
 *
 * Keys: Enter sends, Backspace, Ctrl-C stops CodeJr, Alt-S shows link
 * counters, Alt-X hangs up. /run lists the programs here and runs one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include <bios.h>
#include <io.h>
#include <direct.h>
#include <errno.h>
#include <signal.h>
#include <malloc.h>
#include <process.h>
#include <fcntl.h>
#include "serial.h"
#include "cjp.h"
#include "pull.h"
#include "screen.h"

#define VERSION "1.0.25"
#define TICKS_PER_SEC 18
#define PULL_TMP "CJPULL.$$$" /* a pull lands here, then is renamed */
#define PULL_OLD "CJPULL.OLD" /* the copy being replaced, until the new one is in */

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
	char name[22]; /* NAME.EXT or FOLDER\NAME.EXT */
	unsigned long size;
	unsigned chunks;
	/* A DELTAOFFER: the chunks are an edit script for our copy, which
	 * must have this size and CRC; the result must have new_crc. */
	int delta;
	unsigned long script, base_size;
	unsigned base_crc, new_crc;
} offer;

static long line_baud = 9600; /* the speed in use, for rate displays */

static int connected, quitting;
/* The server said "ui" in its HELLO: we echo typed lines ourselves and
 * show the prompt only between its READY and the next line sent. */
static int ui, ready = 1;
static int line_waits; /* Enter was pressed while busy: send on READY */
/* The server's last status line, and whether the More prompt covers it. */
static char server_status[CJP_MAXPAYLOAD + 1];
static int more_shown;
static long more_lines = -1;
static void hang_up(void);
/* Voice: clips are played with SAY.EXE (the speech project's player),
 * run as a child program while CodeJr stays in memory and connected. */
static int voice_on;
static char play_name[24];   /* a clip to play when the reply is done */
static char say_opts[24];    /* SAY= line of CODEJR.CFG (/H in DOSBox) */
static void speak(const char *clip);
static void key(unsigned k);
static int loop_mode; /* /L: C:\CODEJR.BAT runs RUNNEXT.BAT, then redials */
static int menu; /* the last line was /run: a bare number picks a program */
#define RUNNEXT "C:\\CODEJR\\RUNNEXT.BAT"
#define REDIAL "C:\\CODEJR\\REDIAL.TXT" /* tells the launcher to start us again */
#define BOTTOM_ROWS 21 /* transcript rows (screen.h) */
/* After a /run: what ran, its exit status and screen, and the report put
 * together from them for the server (sent on the next call). */
#define LASTRUN "C:\\CODEJR\\LASTRUN.TXT"
#define LASTERR "C:\\CODEJR\\LASTERR.TXT"
#define LASTSCR "C:\\CODEJR\\LASTSCR.TXT"
#define LASTREP "C:\\CODEJR\\LASTREP.TXT"
static FILE *report;   /* being sent, a piece at a time */
/* Remote work for the server: a DOS command to run (its output goes to
 * CMDOUT and is uploaded), a file to upload, and the upload in progress. */
#define CMDOUT "C:\\CODEJR\\CMDOUT.TXT"
/* What a command we run gets as its keyboard: a file of Enter presses,
 * so a prompt that takes Enter ("Strike a key", "Enter new date") goes
 * on instead of hanging the PCjr. (NUL made DATE loop forever.) */
#define CMDKEYS "C:\\CODEJR\\CJKEYS.TXT"
static char pend_cmd[CJP_MAXPAYLOAD + 1];
static char pend_dir[10];     /* CJP_RUN: the project folder to run it in */
static int pend_run;          /* pend_cmd is a program to run (CJP_RUN) */
static char pend_get[24];
static FILE *up;
static const char *up_temp;   /* delete this file once it is uploaded */
static unsigned long up_sent;
static char up_name[24];
/* Keys to press and a screenshot to take while a program we run is up
 * (see tick_hook). */
#define KQ_MAX 30
#define CJSHOT "C:\\CODEJR\\CJSHOT.BIN"
#define CJSCR "C:\\CODEJR\\CJSCR.BIN" /* our own text screen, for a picture */
#define SCREEN_NAME "*SCREEN"         /* a fetch of this name asks for it */
static struct { unsigned char delay, scan, ascii; } kq[KQ_MAX]; /* delay: units of 2 ticks, before the key */
static volatile unsigned char kq_n, kq_i, kq_break, kq_hold;
#define KEY_HOLD 5 /* ticks a scripted key stays down (0.27 s) */
static volatile unsigned kq_wait;
static unsigned char shot_want;          /* seconds; 0 none, 255 keep the latest */
static unsigned char far *shot_buf;      /* 32 KB, allocated once */
static volatile unsigned shot_len;       /* bytes captured; 0 if none */
static volatile unsigned char shot_mode, shot_pag;
static volatile unsigned run_ticks;
static int shot_pending;                 /* CJSHOT.BIN waits to be uploaded */
static int key_method;                   /* /K:B stuff the BIOS buffer, /K:I call INT 9; 0 = by machine */
static void (__interrupt __far *old_tick)(void);
/* A command the server marked "ask first": 1 = show the question, 2 =
 * waiting for Y or N (until ask_until), 0 = none. */
static int ask;
static unsigned long ask_until;
static int auto_answer; /* /A:Y or /A:N answers by itself (tests) */
static int results_ok; /* the server takes run reports */
static void start_report(void);
static int cursor_dirty;
static unsigned long quit_at;

static unsigned long ticks(void)
{
	return *(unsigned long far *)MK_FP(0x40, 0x6C);
}

/* Leaving, by any road (/exit, Alt-X, the server's BYE, /run, a cancelled
 * transfer). From here no more input is taken, so the input line and its
 * cursor go away at once: a prompt that still looks live while CodeJr is
 * on its way out invites typing that goes nowhere. Keys pressed from now
 * on stay in the BIOS buffer, where DOS finds them. */
static void start_quit(void)
{
	if (!quitting) {
		quitting = 1;
		quit_at = ticks();
		input[0] = 0;
		input_len = 0;
		line_waits = 0;
		cursor_dirty = 0;
		scr_input_off();
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

static unsigned queue_drops; /* messages that could not be queued (Alt-S shows it) */

static void queue(unsigned char type, const void *p, unsigned n)
{
	int i;
	if (outq_n == OUTQ || n > CJP_MAXPAYLOAD) {
		queue_drops++; /* callers keep within both limits: this counts a bug */
		return;
	}
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

static char last_file[112]; /* details of the last transfer, for Alt-S */

/* Alt-S: two short lines, so nothing wraps on the 80-column screen. */
static void counters(char *s)
{
	sprintf(s, "Line: %u lost (%u late), %u framing errors, %u bad frames, %u resent, %u unsent",
	        ser_overruns(), ser_uart_overruns(), ser_framing(), link.bad, link.resent, queue_drops);
}

/* Client-side messages go into the chat history (dark gray), so a status
 * update can't wipe them out. */
static void note(const char *s)
{
	scr_puts(s, 0x08);
	scr_putc('\n', 0x08);
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

/* NAME.EXT, or FOLDER\NAME.EXT one level down (a project folder). */
static int valid_path(const char *s)
{
	char dir[10];
	const char *bs = strchr(s, '\\');
	unsigned n;

	/* ..\NAME.EXT: one of CodeJr's own files, kept one level up in
	 * C:\CODEJR so that PROGS holds only the user's programs. */
	if (s[0] == '.' && s[1] == '.' && s[2] == '\\')
		return valid_name(s + 3);
	if (!bs)
		return valid_name(s);
	n = (unsigned)(bs - s);
	if (n == 0 || n > 8)
		return 0;
	memcpy(dir, s, n);
	dir[n] = 0;
	return !strchr(dir, '.') && valid_name(dir) && valid_name(bs + 1);
}

/* Makes the project folder a path needs, if it has one. */
static void make_folder(const char *path)
{
	char dir[10];
	const char *bs = strchr(path, '\\');
	if (path[0] == '.')
		return; /* ..\NAME: the folder above is already there */
	if (bs && bs - path < (int)sizeof dir) {
		memcpy(dir, path, (size_t)(bs - path));
		dir[bs - path] = 0;
		mkdir(dir); /* fails harmlessly if it exists */
	}
}

static void io_send(const unsigned char *p, unsigned n, void *ctx)
{
	(void)ctx;
	while (n--)
		ser_putc(*p++);
}

static unsigned long pull_t0;   /* tick count when the current file started */
static unsigned long pull_wire; /* bytes of it that came over the line */
/* Where the current file's time goes, in ticks: waiting for a burst to
 * start, receiving it, the silence that ends it, and writing to disk. The
 * rest is the CPU: unescaping, checks and unpacking. */
static unsigned long t_wait, t_recv, t_tail, t_disk;

static unsigned io_burst(unsigned char *buf, unsigned max, unsigned fends,
                         unsigned first, unsigned gap, void *ctx)
{
	unsigned n, t0 = (unsigned)ticks();
	(void)ctx;
	n = ser_burst(buf, max, fends, first, gap);
	pull_wire += n;
	t_wait += ser_burst_first - t0;
	t_recv += ser_burst_last - ser_burst_first;
	t_tail += (unsigned)ticks() - ser_burst_last;
	return n;
}

static void progress(unsigned long got)
{
	char s[112], bar[21]; /* worst case 98: a 21-char name and three 10-digit numbers */
	unsigned long t = ticks() - pull_t0;
	/* The line's speed: bytes that crossed it (compressed), 10 bits each
	 * with start and stop bits, over t/18.2 seconds, so it compares
	 * directly with the baud rate and can't exceed it. */
	unsigned long bps = t ? pull_wire * 182UL / t : 0;
	int i, n = offer.size ? (int)(got * 20 / offer.size) : 20;

	for (i = 0; i < 20; i++)
		bar[i] = i < n ? '#' : '.';
	bar[20] = 0;
	sprintf(s, "Receiving %s [%s] %lu/%lu  line %lu bps", offer.name, bar, got, offer.size, bps);
	scr_status(s);
}

/* Ticks as seconds with one decimal: "12.3". */
static void tenths(char *s, unsigned long t)
{
	unsigned long x = t * 100 / 182;
	sprintf(s, "%lu.%lu", x / 10, x % 10);
}

/* ---- edit scripts (see server/internal/cjp/delta.go) ------------------- */

static struct {
	int st;                 /* 0 op, 1 copy header, 2 data header, 3 data, 4 done */
	unsigned char hdr[6];
	int hn;
	unsigned long left;     /* data bytes still to come */
	unsigned long made;     /* bytes of the new file written */
	unsigned crc;
	FILE *old, *out;
} ds;

static int ds_write(const unsigned char *p, unsigned n)
{
	if (fwrite(p, 1, n, ds.out) != n)
		return -1;
	ds.crc = cjp_crc16_more(ds.crc, p, n);
	ds.made += n;
	return 0;
}

/* Copies n bytes from offset off of the old file into the new one. */
static int ds_copy(unsigned long off, unsigned long n)
{
	static unsigned char buf[512];
	if (fseek(ds.old, (long)off, SEEK_SET) != 0)
		return -1;
	while (n) {
		unsigned k = n > sizeof buf ? sizeof buf : (unsigned)n;
		if (fread(buf, 1, k, ds.old) != k || ds_write(buf, k) != 0)
			return -1;
		n -= k;
	}
	return 0;
}

/* Replays more of the script. Returns -1 if it is broken. */
static int ds_feed(const unsigned char *p, unsigned n)
{
	unsigned i = 0;
	while (i < n) {
		switch (ds.st) {
		case 0:
			switch (p[i++]) {
			case 0: ds.st = 4; break;
			case 1: ds.st = 1; ds.hn = 0; break;
			case 2: ds.st = 2; ds.hn = 0; break;
			default: return -1;
			}
			break;
		case 1:
			ds.hdr[ds.hn++] = p[i++];
			if (ds.hn == 6) {
				unsigned long off = ds.hdr[0] | (unsigned long)ds.hdr[1] << 8 |
				                    (unsigned long)ds.hdr[2] << 16 | (unsigned long)ds.hdr[3] << 24;
				if (ds_copy(off, ds.hdr[4] | (unsigned)ds.hdr[5] << 8) != 0) {
					ds.st = 5; /* broken: a further byte must not land in hdr */
					return -1;
				}
				ds.st = 0;
			}
			break;
		case 2:
			ds.hdr[ds.hn++] = p[i++];
			if (ds.hn == 2) {
				ds.left = ds.hdr[0] | (unsigned)ds.hdr[1] << 8;
				ds.st = ds.left ? 3 : 0;
			}
			break;
		case 3: {
			unsigned k = n - i;
			if (k > ds.left)
				k = (unsigned)ds.left;
			if (ds_write(p + i, k) != 0)
				return -1;
			i += k;
			ds.left -= k;
			if (!ds.left)
				ds.st = 0;
			break;
		}
		default:
			return -1; /* bytes after the end */
		}
	}
	return 0;
}

/* Does our copy of the file match what the server's script expects? */
static int base_matches(FILE *f)
{
	static unsigned char buf[512];
	unsigned long n = 0;
	unsigned crc = 0xFFFF, k;
	while ((k = (unsigned)fread(buf, 1, sizeof buf, f)) > 0) {
		crc = cjp_crc16_more(crc, buf, k);
		n += k;
	}
	return n == offer.base_size && crc == offer.base_crc;
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
	unsigned char r[1 + 64];
	char err[64], line[160];
	unsigned long got = 0, t0 = ticks(), secs;
	unsigned k, rounds = 0, n;
	FILE *f = NULL;

	err[0] = 0;
	pc.bad = 0;
	pull_t0 = t0;
	pull_wire = 0;
	t_wait = t_recv = t_tail = t_disk = 0;
	ds.old = NULL;
	if (!valid_path(offer.name))
		strcpy(err, "not a DOS file name");
	else if (offer.delta && ((ds.old = fopen(offer.name, "rb")) == NULL || !base_matches(ds.old)))
		strcpy(err, "old copy differs"); /* the server then sends it whole */
	else if ((f = fopen(PULL_TMP, "wb")) == NULL)
		strcpy(err, "can't create file");
	ds.st = 0;
	ds.made = 0;
	ds.crc = 0xFFFF;
	ds.out = f;
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
		{
			unsigned long d = ticks();
			if (offer.delta) {
				if (ds_feed(out, (unsigned)len) != 0)
					strcpy(err, "bad change list (or disk full)");
			} else if (fwrite(out, 1, (size_t)len, f) != (size_t)len) {
				strcpy(err, "disk full?");
			}
			t_disk += ticks() - d;
		}
		got = offer.delta ? ds.made : got + (unsigned long)len;
		progress(got);
	}
	if (f && fclose(f) != 0 && !err[0])
		strcpy(err, "can't close file");
	if (ds.old)
		fclose(ds.old);
	if (!err[0] && got != offer.size)
		strcpy(err, "size mismatch");
	if (!err[0] && offer.delta && (ds.st != 4 || ds.crc != offer.new_crc))
		strcpy(err, "changes didn't check out");
	/* The old copy is replaced only once the new one is whole, so a
	 * failed pull of CODEJR.EXE leaves the working client in place. */
	if (!err[0]) {
		/* Swap it in without ever being left with neither copy: move the
		 * old one aside, move the new one in, and only then delete the
		 * old one. If the new one can't be moved in, the old one goes
		 * back. (A plain delete-then-rename once failed on the real PCjr
		 * and could have left no CODEJR.EXE at all.) */
		int had_old = access(offer.name, 0) == 0;
		make_folder(offer.name);
		remove(PULL_OLD);
		if (had_old && rename(offer.name, PULL_OLD) != 0) {
			sprintf(err, "can't move the old copy (error %d)", errno);
		} else if (rename(PULL_TMP, offer.name) != 0) {
			sprintf(err, "can't move the new copy in (error %d)", errno);
			if (had_old)
				rename(PULL_OLD, offer.name);
		} else {
			remove(PULL_OLD);
		}
	}
	if (err[0]) {
		if (f)
			remove(PULL_TMP); /* don't leave half a file */
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
	if (err[0] && offer.delta && strcmp(err, "old copy differs") == 0)
		sprintf(line, "Getting all of %s (this PCjr's copy is different).", offer.name);
	else if (err[0])
		sprintf(line, "Couldn't save %s: %s", offer.name, err);
	else {
		unsigned long t = ticks() - t0, bps = t ? pull_wire * 182UL / t : 0;
		unsigned long x10 = pull_wire ? got * 10 / pull_wire : 10;
		unsigned long spent = t_wait + t_recv + t_tail + t_disk;
		char sz[16], tw[14], tr[14], tg[14], tu[14], td[14]; /* "429496729.5" if the clock wraps at midnight */

		if (got < 1024)
			sprintf(sz, "%lu bytes", got);
		else
			sprintf(sz, "%lu.%lu KB", got / 1024, got % 1024 * 10 / 1024);
		if (bps > (unsigned long)line_baud)
			bps = (unsigned long)line_baud; /* the clock ticks 18 times a second */
		if (offer.delta)
			sprintf(line, "Saved %s (%s; sent %lu bytes of changes) in %lu s.",
			        offer.name, sz, offer.script, secs);
		else
			sprintf(line, "Saved %s (%s) in %lu s, line %lu of %ld bps.",
			        offer.name, sz, secs, bps, line_baud);
		note(line);
		/* Where the time went, so the slow part can be found. */
		tenths(tw, t_wait);
		tenths(tr, t_recv);
		tenths(tg, t_tail);
		tenths(tu, t > spent ? t - spent : 0);
		tenths(td, t_disk);
		sprintf(line, "  %s s waiting, %s receiving, %s gap, %s unpacking, %s disk; %lu.%lux",
		        tw, tr, tg, tu, td, x10 / 10, x10 % 10);
		if (offer.chunks <= 1)
			line[0] = 0; /* one chunk: nothing to learn */
		sprintf(last_file, "Last file: %lu.%lux compressed, %u damaged, %u requests for %u chunks",
		        x10 / 10, x10 % 10, pc.bad, rounds, offer.chunks);
	}
	if (line[0])
		note(line);
	scr_status(err[0] ? err : "Saved.");
}

/* Sets the PCjr's clock from the server's greeting (YYYYMMDDhhmmss, the
 * laptop's local time), so files written here get a real date. Says so
 * only if the clock was off by more than a minute. */
static void set_clock(const char *t)
{
	struct dosdate_t d, od;
	struct dostime_t tm, ot;
	static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	char s[96];
	int i, v[14];
	long was, now;

	for (i = 0; i < 14; i++) {
		if (t[i] < '0' || t[i] > '9')
			return;
		v[i] = t[i] - '0';
	}
	d.year = (unsigned)(v[0] * 1000 + v[1] * 100 + v[2] * 10 + v[3]);
	d.month = (unsigned char)(v[4] * 10 + v[5]);
	d.day = (unsigned char)(v[6] * 10 + v[7]);
	tm.hour = (unsigned char)(v[8] * 10 + v[9]);
	tm.minute = (unsigned char)(v[10] * 10 + v[11]);
	tm.second = (unsigned char)(v[12] * 10 + v[13]);
	tm.hsecond = 0;
	if (d.year < 1980 || d.year > 2099 || d.month < 1 || d.month > 12 || d.day < 1 ||
	    d.day > 31 || tm.hour > 23 || tm.minute > 59 || tm.second > 59)
		return;
	_dos_getdate(&od);
	_dos_gettime(&ot);
	if (_dos_setdate(&d) != 0 || _dos_settime(&tm) != 0)
		return;
	was = ot.hour * 3600L + ot.minute * 60L + ot.second;
	now = tm.hour * 3600L + tm.minute * 60L + tm.second;
	if (od.year == d.year && od.month == d.month && od.day == d.day &&
	    was - now < 60 && now - was < 60)
		return;
	sprintf(s, "PCjr clock set: %s %u, %u:%02u %s", mon[d.month - 1], d.day,
	        tm.hour % 12 ? tm.hour % 12 : 12, tm.minute, tm.hour < 12 ? "am" : "pm");
	note(s);
}

static void file_offer(const cjp_frame *f)
{
	unsigned n;

	if (f->len < 7 || f->len - 6 > sizeof offer.name - 1)
		return;
	n = f->len - 6;
	offer.size = f->payload[0] | (unsigned long)f->payload[1] << 8 |
	             (unsigned long)f->payload[2] << 16 | (unsigned long)f->payload[3] << 24;
	offer.chunks = f->payload[4] | ((unsigned)f->payload[5] << 8);
	memcpy(offer.name, f->payload + 6, n);
	offer.name[n] = 0;
	offer.delta = 0;
	offer.pending = 1; /* pulled from the main loop, once this frame is acked */
}

static unsigned long le32(const unsigned char *p)
{
	return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static void delta_offer(const cjp_frame *f)
{
	unsigned n;

	if (f->len < 19 || f->len - 18 > sizeof offer.name - 1)
		return;
	n = f->len - 18;
	offer.size = le32(f->payload);
	offer.chunks = f->payload[4] | ((unsigned)f->payload[5] << 8);
	offer.script = le32(f->payload + 6);
	offer.base_size = le32(f->payload + 10);
	offer.base_crc = f->payload[14] | ((unsigned)f->payload[15] << 8);
	offer.new_crc = f->payload[16] | ((unsigned)f->payload[17] << 8);
	memcpy(offer.name, f->payload + 18, n);
	offer.name[n] = 0;
	offer.delta = 1;
	offer.pending = 1;
}

static void handle(const cjp_frame *f)
{
	char s[CJP_MAXPAYLOAD + 1];
	unsigned i;

	switch (f->type) {
	case CJP_HELLO:
		connected = 1;
		memcpy(s, f->payload, f->len);
		s[f->len] = 0;
		if (strstr(s, " ui")) {
			ui = 1;
			ready = 0; /* until the server's READY */
		}
		if (strstr(s, " results")) {
			results_ok = 1;
			start_report();
		}
		{
			const char *t = strstr(s, " t=");
			if (t)
				set_clock(t + 3);
		}
		scr_status("Connected.");
		break;
	case CJP_READY:
		if (play_name[0]) {
			/* The reply and its files are all here and the line is
			 * quiet: say it before handing the prompt back. */
			speak(play_name);
			play_name[0] = 0;
		}
		ready = 1;
		if (line_waits) {
			line_waits = 0;
			key('\r'); /* the line typed while CodeJr was busy */
		}
		break;
	case CJP_PLAY:
		if (f->len && f->len < sizeof play_name) {
			memcpy(play_name, f->payload, f->len);
			play_name[f->len] = 0;
		}
		break;
	case CJP_EXEC:
		pend_run = 0;
		if (f->len && f->payload[0] == 1) {
			/* Not a look-only command: the user decides. */
			memcpy(pend_cmd, f->payload + 1, f->len - 1);
			pend_cmd[f->len - 1] = 0;
			ask = 1;
		} else {
			memcpy(pend_cmd, f->payload, f->len);
			pend_cmd[f->len] = 0;
			ask = 0;
		}
		break;
	case CJP_REDIAL: {
		/* The server is restarting. Leave a note for the launcher, which
		 * starts us again at once, and hang up. */
		FILE *rf;
		memcpy(s, f->payload, f->len);
		s[f->len] = 0;
		note(s[0] ? s : "CodeJr's server is restarting. Calling back...");
		scr_status("Calling back in a moment...");
		if (loop_mode && (rf = fopen(REDIAL, "w")) != NULL) {
			fputs("redial\n", rf);
			fclose(rf);
		} else {
			note("Type CODEJR to call again.");
		}
		start_quit();
		break;
	}
	case CJP_RUN: {
		/* [1 = ask] FOLDER "\n" COMMAND */
		const unsigned char *p = f->payload;
		unsigned n = f->len, i;
		ask = 0;
		if (n && p[0] == 1) {
			ask = 1;
			p++;
			n--;
		}
		for (i = 0; i < n && p[i] != '\n'; i++)
			;
		if (i < n && i < sizeof pend_dir) {
			unsigned c, k;
			memcpy(pend_dir, p, i);
			pend_dir[i] = 0;
			/* The command ends at a NUL; after it: when to take a
			 * screenshot (seconds; 0 none, 255 the last one), a key
			 * count, and the keys (delay, scan code, ASCII each). */
			for (c = i + 1; c < n && p[c]; c++)
				;
			memcpy(pend_cmd, p + i + 1, c - i - 1);
			pend_cmd[c - i - 1] = 0;
			shot_want = 0;
			kq_n = 0;
			if (c + 2 < n) {
				shot_want = p[c + 1];
				k = p[c + 2];
				if (k > KQ_MAX)
					k = KQ_MAX;
				if (c + 3 + k * 3 <= n) {
					memcpy(kq, p + c + 3, k * 3);
					kq_n = (unsigned char)k;
				}
			}
			pend_run = 1;
		}
		break;
	}
	case CJP_FETCH:
		if (f->len && f->len < sizeof pend_get) {
			memcpy(pend_get, f->payload, f->len);
			pend_get[f->len] = 0;
		}
		break;
	case CJP_VOICE:
		voice_on = f->len && f->payload[0];
		break;
	case CJP_TEMP:
		memcpy(s, f->payload, f->len);
		s[f->len] = 0;
		scr_temp(s, 0x03);
		break;
	case CJP_TEXT:
		if (f->len) {
			unsigned char attr = style_attr[f->payload[0] < 5 ? f->payload[0] : 0];
			for (i = 1; i < f->len; i++)
				scr_putc(f->payload[i], attr);
		}
		break;
	case CJP_STATUS:
		memcpy(server_status, f->payload, f->len);
		server_status[f->len] = 0;
		if (!more_shown)
			scr_status(server_status);
		break;
	case CJP_CLEAR:
		scr_clear_transcript();
		break;
	case CJP_INFO: /* the right end of the header: "3274 chars" */
		memcpy(s, f->payload, f->len);
		s[f->len] = 0;
		scr_info(s);
		break;
	case CJP_FILEOFFER:
		file_offer(f);
		break;
	case CJP_DELTAOFFER:
		delta_offer(f);
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

/* ---- /run: the programs in this directory, numbered ------------------- */

#define MAXPROGS 60
static char progs[MAXPROGS][22];
static int nprogs;

static int runnable(const char *name)
{
	static const char *skip[] = { "CODEJR.EXE", "RUN.BAT", "NATIVE.BAT", "FAST.BAT",
	                              "SETUP.BAT", NULL };
	const char *dot = strchr(name, '.');
	int i;

	if (!dot || (strcmp(dot, ".BAS") && strcmp(dot, ".EXE") && strcmp(dot, ".COM") &&
	             strcmp(dot, ".BAT")))
		return 0;
	for (i = 0; skip[i]; i++)
		if (strcmp(name, skip[i]) == 0)
			return 0;
	return 1;
}

static int by_name(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

/* Adds the programs matching pattern; dir is put in front of their names. */
static void find_progs(const char *pattern, const char *dir)
{
	struct find_t ff;

	if (_dos_findfirst(pattern, _A_NORMAL, &ff) != 0)
		return;
	do {
		if (nprogs < MAXPROGS && runnable(ff.name))
			sprintf(progs[nprogs++], "%s%s%s", dir, *dir ? "\\" : "", ff.name);
	} while (_dos_findnext(&ff) == 0);
}

/* Fills progs: the programs here, then those in each project folder. */
static void scan_progs(void)
{
	static char dirs[20][13];
	struct find_t ff;
	char pat[20];
	int nd = 0, i;

	nprogs = 0;
	find_progs("*.*", "");
	/* Collect the folders first: a search inside one would end this one. */
	if (_dos_findfirst("*.*", _A_SUBDIR, &ff) == 0) {
		do {
			/* A project folder has no extension (8 characters at most):
			 * BACKUP.OLD would overflow the 22-byte program names. */
			if ((ff.attrib & _A_SUBDIR) && !strchr(ff.name, '.') && strlen(ff.name) <= 8 && nd < 20)
				strcpy(dirs[nd++], ff.name);
		} while (_dos_findnext(&ff) == 0);
	}
	for (i = 0; i < nd; i++) {
		sprintf(pat, "%s\\*.*", dirs[i]);
		find_progs(pat, dirs[i]);
	}
	qsort(progs, nprogs, sizeof progs[0], by_name);
}

static void list_progs(void)
{
	char line[81], cell[30];
	int i;

	scan_progs();
	if (nprogs == 0) {
		note("No programs here yet. Ask for one!");
		return;
	}
	line[0] = 0;
	for (i = 0; i < nprogs; i++) {
		sprintf(cell, "%2d %-22s ", i + 1, progs[i]);
		strcat(line, cell);
		if (i % 3 == 2 || i == nprogs - 1) {
			scr_puts(line, 0x03);
			scr_putc('\n', 0x03);
			line[0] = 0;
		}
	}
	note("Type a number and Enter to run it.");
	menu = 1;
}

/* Runs a program: C:\CODEJR.BAT runs RUNNEXT.BAT once we hang up, then
 * starts CodeJr again. */
static void run_prog(const char *name)
{
	char s[90], dir[10];
	const char *file = name, *bs = strchr(name, '\\');
	FILE *f;
	int bas = strstr(name, ".BAS") != NULL;

	dir[0] = 0;
	if (bs) {
		memcpy(dir, name, (size_t)(bs - name));
		dir[bs - name] = 0;
		file = bs + 1;
	}
	if (!loop_mode) {
		if (dir[0])
			sprintf(s, "Alt-X, then CD %s and type %s%s.", dir, bas ? "RUN " : "", file);
		else
			sprintf(s, "Alt-X, then type %s%s.", bas ? "RUN " : "", file);
		note(s);
		note("(Start CodeJr with CODEJR to run programs from here.)");
		return;
	}
	if ((f = fopen(RUNNEXT, "w")) == NULL) {
		note("Can't write C:\\CODEJR\\RUNNEXT.BAT.");
		return;
	}
	/* Run it, note a nonzero exit, and save the text screen before
	 * anything clears it; the next call sends all that to CodeJr. A .BAT
	 * goes through COMMAND /C so control comes back here. BASIC runs
	 * GW-BASIC directly (RUN.BAT would clear the screen first), then
	 * resets the palette GW-BASIC scrambles. */
	fprintf(f, "@ECHO OFF\nCD \\CODEJR\\PROGS%s%s\n", dir[0] ? "\\" : "", dir);
	if (bas)
		fprintf(f, "GWBASIC %s\n", file);
	else if (strstr(file, ".BAT"))
		fprintf(f, "COMMAND /C %s\n", file);
	else
		fprintf(f, "%s\n", file);
	fprintf(f, "IF ERRORLEVEL 1 ECHO 1 > " LASTERR "\n");
	/* The screen grabber is in C:\CODEJR (in PROGS before 1.0.22). */
	fprintf(f, "%s > " LASTSCR "\n", access("C:\\CODEJR\\SCRDUMP.EXE", 0) == 0 ?
	        "C:\\CODEJR\\SCRDUMP" : "C:\\CODEJR\\PROGS\\SCRDUMP");
	if (bas)
		fprintf(f, "MODE CO80\n");
	fclose(f);
	remove(LASTERR);
	remove(LASTSCR);
	if ((f = fopen(LASTRUN, "w")) != NULL) {
		fprintf(f, "%s\n", name);
		fclose(f);
	}
	sprintf(s, "Running %s. CodeJr calls back when it ends.", name);
	note(s);
	queue(CJP_BYE, NULL, 0);
	start_quit();
}

/* ---- run reports ------------------------------------------------------- */

/* Copies up to max bytes of a text file into out, without trailing blank
 * lines or the spaces at line ends. */
static void copy_text(FILE *out, const char *path, long max)
{
	FILE *in = fopen(path, "r");
	char line[100];
	int blanks = 0, i;
	long n = 0;

	if (!in)
		return;
	while (n < max && fgets(line, sizeof line, in)) {
		size_t len = strlen(line);
		while (len && (line[len - 1] == '\n' || line[len - 1] == ' ' || line[len - 1] == '\r'))
			line[--len] = 0;
		if (len == 0) {
			blanks++; /* written only if more text follows */
			continue;
		}
		for (i = 0; i < blanks; i++)
			fputc('\n', out);
		blanks = 0;
		fprintf(out, "%s\n", line);
		n += (long)len + 1;
	}
	if (n >= max)
		fprintf(out, "... (cut)\n");
	fclose(in);
}

/* A file's DOS date and time as one comparable number. */
static int file_time(const char *path, unsigned long *t)
{
	int h;
	unsigned d, tm;
	if (_dos_open(path, 0, &h) != 0)
		return 0;
	_dos_getftime(h, &d, &tm);
	_dos_close(h);
	*t = (unsigned long)d << 16 | tm;
	return 1;
}

/* If a /run left results, puts the report together and starts sending
 * it: 'B' + the program's name now, the text from the main loop. */
static void start_report(void)
{
	FILE *f = fopen(LASTRUN, "r");
	char name[24], log[40], *nl;
	unsigned long t_run, t_log;

	if (!f)
		return;
	name[0] = 0;
	fgets(name, sizeof name, f);
	fclose(f);
	if ((nl = strchr(name, '\n')) != NULL)
		*nl = 0;
	if ((f = fopen(LASTREP, "w")) == NULL)
		return;
	fprintf(f, "Exit status: %s\n", access(LASTERR, 0) == 0 ? "errorlevel 1 or more (a failure?)" : "0");
	fprintf(f, "Text screen when it ended:\n");
	copy_text(f, LASTSCR, 2400);
	/* Its LOG.TXT, if it wrote one during this run. */
	strcpy(log, name);
	nl = strrchr(log, '\\');
	strcpy(nl ? nl + 1 : log, "LOG.TXT");
	if (file_time(LASTRUN, &t_run) && file_time(log, &t_log) && t_log >= t_run) {
		fprintf(f, "%s:\n", log);
		copy_text(f, log, 3000);
	}
	fclose(f);
	if ((report = fopen(LASTREP, "rb")) == NULL)
		return;
	{
		unsigned char b[32];
		b[0] = 'B';
		memcpy(b + 1, name, strlen(name));
		queue(CJP_RESULT, b, (unsigned)strlen(name) + 1);
	}
	note("Sending CodeJr the results of your last run...");
}

/* Sends the next piece of the report while the queue has room. */
static void pump_report(void)
{
	unsigned char b[1 + 120];
	size_t n;

	while (report && outq_n < OUTQ - 2) {
		n = fread(b + 1, 1, 120, report);
		if (n == 0) {
			b[0] = 'E';
			queue(CJP_RESULT, b, 1);
			fclose(report);
			report = NULL;
			remove(LASTREP);
			remove(LASTRUN);
			remove(LASTERR);
			remove(LASTSCR);
			break;
		}
		b[0] = 'D';
		queue(CJP_RESULT, b, (unsigned)n + 1);
	}
}

/* ---- remote work: commands and uploads for the server ---------------- */

static void upload_fail(const char *why);

/* ---- keys and screenshots for a program we run ------------------------
 * While the program runs we are still in memory. A hook on the timer
 * tick (INT 1Ch, 18.2 a second) presses scripted keys and copies the
 * video memory, so a test can drive a program and see its screen. */

/* The PCjr's keyboard path ends in a software INT 9 with the scan code in
 * AL; issuing one is a key press to the BIOS and to any program that
 * hooked INT 9 (JRGFX's key_init does). */
static void int9(unsigned char sc);
#pragma aux int9 = "int 9" parm [al] modify [ax];

/* On other machines INT 9 reads the keyboard port, so put the key
 * straight into the BIOS buffer (40:1E to 40:3D, head 40:1A, tail 40:1C). */
static void stuff_key(unsigned char scan, unsigned char ascii)
{
	unsigned far *head = (unsigned far *)MK_FP(0x40, 0x1A);
	unsigned far *tail = (unsigned far *)MK_FP(0x40, 0x1C);
	unsigned t = *tail, nx = t + 2;
	if (nx >= 0x3E)
		nx = 0x1E;
	if (nx != *head) {
		*(unsigned far *)MK_FP(0x40, t) = (unsigned)scan << 8 | ascii;
		*tail = nx;
	}
}

static int use_int9(void)
{
	if (key_method)
		return key_method == 'I';
	return *(unsigned char far *)MK_FP(0xF000, 0xFFFE) == 0xFD; /* a PCjr */
}

/* Copies the screen's memory if a graphics mode is up. The mode is the
 * BIOS's (40:49); on the PCjr the page on screen is in the low 128K at
 * (page << 10):0, the page number in the BIOS's copy of port 3DFh (40:8A).
 * 16 KB, or 32 KB for the 16-color 320x200 and 4-color 640x200 modes. */
static void grab_screen(void)
{
	unsigned char mode = *(unsigned char far *)MK_FP(0x40, 0x49);
	unsigned char pag = *(unsigned char far *)MK_FP(0x40, 0x8A);
	unsigned seg = 0xB800, len = 16384u;

	if (mode < 4 || mode == 7 || mode > 10)
		return; /* text: the text dump at the end covers it */
	if (*(unsigned char far *)MK_FP(0xF000, 0xFFFE) == 0xFD && (pag & 7)) {
		seg = (unsigned)(pag & 7) << 10;
		if (mode >= 9)
			len = 32768u;
	}
	_fmemcpy(shot_buf, MK_FP(seg, 0), len);
	shot_mode = mode;
	shot_pag = pag;
	shot_len = len;
}

static void __interrupt __far tick_hook(void)
{
	run_ticks++;
	if (shot_buf && shot_want) {
		if (shot_want == 255 ? run_ticks % 18 == 0 : run_ticks == (unsigned)shot_want * 18u)
			grab_screen();
	}
	if (kq_break) {
		/* The key stays down a quarter second before we let go: a game
		 * that looks at the keyboard once a frame, at 6 frames a second,
		 * would miss a press that lasted one tick. */
		if (kq_hold) {
			kq_hold--;
		} else {
			if (use_int9())
				int9((unsigned char)(kq_break | 0x80));
			kq_break = 0;
		}
	} else if (kq_i < kq_n) {
		if (kq_wait) {
			kq_wait--;
		} else {
			unsigned char sc = kq[kq_i].scan, as = kq[kq_i].ascii;
			kq_i++;
			if (sc) {
				if (use_int9()) {
					int9(sc);
					kq_break = sc;
					kq_hold = KEY_HOLD - 1;
				} else {
					stuff_key(sc, as);
				}
			}
			if (kq_i < kq_n)
				kq_wait = (unsigned)kq[kq_i].delay * 2u;
		}
	}
	_chain_intr(old_tick);
}

static void hook_run(void)
{
	if (shot_want && !shot_buf)
		shot_buf = (unsigned char far *)_fmalloc(32768u);
	shot_len = 0;
	run_ticks = 0;
	kq_i = 0;
	kq_break = 0;
	kq_wait = kq_n ? (unsigned)kq[0].delay * 2u : 0;
	old_tick = _dos_getvect(0x1C);
	_dos_setvect(0x1C, tick_hook);
}

/* Unhooks, and writes the screenshot (if one was taken) to CJSHOT.BIN:
 * "JS", the mode, the page byte, the length (2 bytes), then the memory. */
static void unhook_run(void)
{
	FILE *f;
	unsigned char h[6];
	unsigned left, k;
	unsigned char far *p = shot_buf;
	static unsigned char buf[512];

	_dos_setvect(0x1C, old_tick);
	kq_n = 0;
	shot_pending = 0;
	if (!shot_want)
		return;
	shot_pending = 1; /* an answer is owed even if there is no picture */
	remove(CJSHOT);
	if (!shot_len || (f = fopen(CJSHOT, "wb")) == NULL)
		return;
	h[0] = 'J'; h[1] = 'S'; h[2] = shot_mode; h[3] = shot_pag;
	h[4] = (unsigned char)shot_len; h[5] = (unsigned char)(shot_len >> 8);
	fwrite(h, 1, 6, f);
	for (left = shot_len; left; left -= k, p += k) {
		k = left > sizeof buf ? sizeof buf : left;
		_fmemcpy(buf, p, k);
		fwrite(buf, 1, k, f);
	}
	fclose(f);
}

/* The user's answer to "CodeJr wants to run ...": 1 yes, 0 no. */
static void answer(int yes)
{
	ask = 0;
	if (yes) {
		note("Allowed.");
	} else {
		note("Not run.");
		pend_cmd[0] = 0;
		upload_fail("the user said no (or didn't answer)");
	}
	scr_status(server_status);
	more_shown = 0;
	more_lines = -1;
}

/* Writes the file of Enter presses, once. */
static void make_keys(void)
{
	FILE *f;
	int i;
	if (access(CMDKEYS, 0) == 0 || (f = fopen(CMDKEYS, "w")) == NULL)
		return;
	for (i = 0; i < 100; i++)
		fputc('\n', f);
	fclose(f);
}

static void upload_fail(const char *why)
{
	unsigned char b[1 + 60];
	size_t n = strlen(why);
	b[0] = 'F';
	memcpy(b + 1, why, n);
	queue(CJP_UPLOAD, b, (unsigned)n + 1);
}

/* Starts sending a file to the server: kind 'B' (a file, with its name)
 * or 'X' (the output of a command). The main loop sends the pieces. */
static void start_upload(const char *path, const char *name, int kind, int temp)
{
	unsigned char b[1 + 24];
	size_t n = kind == 'B' ? strlen(name) : 0;

	if ((up = fopen(path, "rb")) == NULL) {
		upload_fail(kind == 'B' ? "no such file on the PCjr" : "the command made no output file");
		return;
	}
	b[0] = (unsigned char)kind;
	memcpy(b + 1, name, n);
	queue(CJP_UPLOAD, b, (unsigned)n + 1);
	up_temp = temp ? path : NULL;
	up_sent = 0;
	strncpy(up_name, name, sizeof up_name - 1);
	up_name[sizeof up_name - 1] = 0;
}

/* Our own screen as the user sees it now, for a picture made on the
 * laptop (tools/textshot.py): 4000 bytes of video memory, then the ROM's
 * 8x8 characters 0-127 (F000:FA6E) and 128-255 (the INT 1Fh table, zeros
 * if there is none). Nothing is written on the screen for it. */
static void upload_screen(void)
{
	static unsigned char b[250];
	unsigned char far *src[3];
	unsigned len[3], k, i, n;
	FILE *f = fopen(CJSCR, "wb");

	if (!f) {
		upload_fail("can't write the screen file on the PCjr");
		return;
	}
	src[0] = (unsigned char far *)MK_FP(0xB800, 0);
	len[0] = 4000;
	src[1] = (unsigned char far *)MK_FP(0xF000, 0xFA6E);
	src[2] = *(unsigned char far * far *)MK_FP(0, 0x1F * 4);
	len[1] = len[2] = 1024;
	for (k = 0; k < 3; k++)
		for (i = 0; i < len[k]; i += n) {
			n = len[k] - i < sizeof b ? len[k] - i : sizeof b;
			if (src[k])
				_fmemcpy(b, src[k] + i, n);
			else
				memset(b, 0, n);
			fwrite(b, 1, n, f);
		}
	fclose(f);
	start_upload(CJSCR, "SCREEN.BIN", 'B', 1);
}

static void pump_upload(void)
{
	unsigned char b[1 + 120];
	char s[96];
	size_t n;

	while (up && outq_n < OUTQ - 2) {
		n = fread(b + 1, 1, 120, up);
		if (n == 0) {
			b[0] = 'E';
			queue(CJP_UPLOAD, b, 1);
			fclose(up);
			up = NULL;
			if (up_temp)
				remove(up_temp);
			if (!more_shown)
				scr_status(server_status);
			break;
		}
		b[0] = 'D';
		queue(CJP_UPLOAD, b, (unsigned)n + 1);
		up_sent += n;
		if (!more_shown) {
			sprintf(s, "Sending %s to CodeJr: %lu bytes", up_name, up_sent);
			scr_status(s);
		}
	}
}

/* Runs a DOS command for the server. Its output goes to a file, which is
 * then uploaded; the keyboard is NUL, so nothing can sit waiting for a
 * key. The screen is repainted afterwards: the command (or DOS's own
 * error messages) may have written on it. The current folder is put
 * back: in DOS a CD in the child changes ours too, and files we receive
 * land in the current folder. */
static void run_command(void)
{
	static char line[CJP_MAXPAYLOAD + 64]; /* the command, two redirections (44), NUL */
	char s[100], home[80];

	sprintf(s, "Running on this PCjr: %.60s", pend_cmd);
	note(s);
	scr_status("Running a DOS command for CodeJr...");
	remove(CMDOUT);
	getcwd(home, sizeof home);
	make_keys();
	sprintf(line, "%s <" CMDKEYS " >" CMDOUT, pend_cmd);
	pend_cmd[0] = 0;
	if (system(line) == -1) {
		chdir(home);
		scr_refresh();
		upload_fail("couldn't start COMMAND.COM (memory?)");
	} else {
		chdir(home);
		scr_refresh();
		start_upload(CMDOUT, "the output", 'X', 1);
	}
	scr_status(server_status);
	more_shown = 0;
	more_lines = -1;
	cursor_dirty = 1;
}

/* Runs a program in its project folder for the server and uploads the
 * whole story: what it printed, its exit code, the text screen it left
 * and its LOG.TXT if it wrote one. A .EXE or .COM is started directly,
 * which gives the real exit code; anything else goes through COMMAND. */
static void run_program(void)
{
	static char line[CJP_MAXPAYLOAD + 64], cmd[CJP_MAXPAYLOAD + 1]; /* as in run_command */
	char s[128], home[80], prog[24], *args[10], *p;
	unsigned long t_out = 0, t_log = 0;
	int rc = -1, direct = 0, n = 0, i;
	FILE *f;

	strcpy(cmd, pend_cmd);
	pend_cmd[0] = 0;
	pend_run = 0;
	sprintf(s, "Running %s%s%.50s on this PCjr for CodeJr...", pend_dir, pend_dir[0] ? "\\" : "", cmd);
	note(s);
	getcwd(home, sizeof home);
	if (pend_dir[0] && chdir(pend_dir) != 0) {
		upload_fail("no such folder on the PCjr");
		return;
	}
	remove(CMDOUT);
	if ((f = fopen(CMDOUT, "w")) != NULL)
		fclose(f); /* its time marks the start, to tell a new LOG.TXT */
	/* Read it now: by the end the file holds the output and is newer than
	 * a LOG.TXT closed moments earlier (seen on the real PCjr). */
	file_time(CMDOUT, &t_out);

	/* The first word, with .EXE or .COM added if that file is here. */
	strcpy(line, cmd);
	for (p = strtok(line, " "); p && n < 9; p = strtok(NULL, " "))
		args[n++] = p;
	args[n] = NULL;
	if (n) {
		static const char *ext[] = { "", ".EXE", ".COM" };
		strupr(args[0]);
		for (i = 0; i < 3 && !direct; i++) {
			if (strlen(args[0]) + 4 >= sizeof prog)
				break;
			sprintf(prog, "%s%s", args[0], ext[i]);
			if ((strstr(prog, ".EXE") || strstr(prog, ".COM")) && access(prog, 0) == 0)
				direct = 1;
		}
	}
	make_keys();
	scr_blank(); /* the program gets a clean screen; we dump what it leaves */
	hook_run();
	if (direct) {
		/* With scripted keys the program reads the real keyboard (where
		 * they arrive); otherwise a file of Enter presses. */
		int nul = kq_n ? -1 : open(CMDKEYS, O_RDONLY), out = open(CMDOUT, O_WRONLY | O_TRUNC | O_TEXT);
		int in0 = dup(0), out0 = dup(1);
		if (nul >= 0)
			dup2(nul, 0);
		if (out >= 0)
			dup2(out, 1);
		args[0] = prog;
		rc = spawnv(P_WAIT, prog, (const char **)args);
		dup2(in0, 0);
		dup2(out0, 1);
		close(in0);
		close(out0);
		if (nul >= 0)
			close(nul);
		if (out >= 0)
			close(out);
	} else {
		if (kq_n)
			sprintf(line, "%s >" CMDOUT, cmd);
		else
			sprintf(line, "%s <" CMDKEYS " >" CMDOUT, cmd);
		if (system(line) == -1)
			rc = -2;
	}
	unhook_run();
	if ((f = fopen(CMDOUT, "a")) != NULL) {
		if (direct)
			fprintf(f, "\n--- exit code %d\n", rc);
		else if (rc == -2)
			fprintf(f, "\n--- couldn't start COMMAND.COM\n");
		else
			fprintf(f, "\n--- ended (no exit code: it ran through COMMAND)\n");
		fprintf(f, "--- text screen it left\n");
		scr_dump(f);
		if (t_out && file_time("LOG.TXT", &t_log) && t_log >= t_out) {
			fprintf(f, "--- LOG.TXT\n");
			copy_text(f, "LOG.TXT", 6000);
		}
		fclose(f);
	}
	chdir(home);
	scr_refresh();
	start_upload(CMDOUT, "the output", 'X', 1);
	scr_status(server_status);
	more_shown = 0;
	more_lines = -1;
	cursor_dirty = 1;
}

/* Handles a line meant for the client. Returns 1 if it was. */
static int local_line(const char *s)
{
	int n;

	if (menu && (n = atoi(s)) > 0 && strspn(s, "0123456789") == strlen(s)) {
		menu = 0;
		if (n > nprogs) {
			note("No program with that number. /run lists them.");
			return 1;
		}
		run_prog(progs[n - 1]);
		return 1;
	}
	menu = 0;
	if (!stricmp(s, "/exit") || !stricmp(s, "/quit") || !stricmp(s, "/bye")) {
		hang_up();
		return 1;
	}
	if (strnicmp(s, "/run", 4) != 0 || (s[4] && s[4] != ' '))
		return 0;
	s += 4;
	while (*s == ' ')
		s++;
	if (!*s) {
		list_progs();
	} else if ((n = atoi(s)) > 0) {
		if (nprogs == 0)
			scan_progs();
		if (n <= nprogs)
			run_prog(progs[n - 1]);
		else
			note("No program with that number. /run lists them.");
	} else {
		/* SNAKE finds SNAKE.BAS, SNAKE\SNAKE.EXE, or the first program in
		 * a SNAKE folder. */
		char want[22];
		int i;

		strncpy(want, s, sizeof want - 1);
		want[sizeof want - 1] = 0;
		strupr(want);
		scan_progs();
		for (i = 0; i < nprogs; i++) {
			const char *p = progs[i], *bs = strchr(p, '\\');
			const char *file = bs ? bs + 1 : p;
			size_t wl = strlen(want);
			if (strcmp(p, want) == 0 || strcmp(file, want) == 0 ||
			    (strncmp(file, want, wl) == 0 && file[wl] == '.') ||
			    (bs && (size_t)(bs - p) == wl && strncmp(p, want, wl) == 0)) {
				run_prog(p);
				return 1;
			}
		}
		note("No such program here. /run lists them.");
	}
	return 1;
}

/* Shows a typed line in the transcript at once, as the server would. */
static void echo_line(const char *s)
{
	if (scr_col() > 0)
		scr_putc('\n', 0x0E);
	if (s[0] != '/')
		scr_puts("> ", 0x0E);
	scr_puts(s, 0x0E);
	scr_putc('\n', 0x0E);
}

/* Keeps the status bar honest about paging: "-- More --" while new lines
 * wait below the view, the server's status otherwise. */
static void show_more(void)
{
	long n = scr_following() ? 0 : scr_unread();
	char s[112];

	if (n == more_lines)
		return;
	more_lines = n;
	if (n > 0) {
		sprintf(s, "-- More -- %ld new line%s.  Space: next page   PgUp/PgDn, arrows: scroll",
		        n, n == 1 ? "" : "s");
		scr_status(s);
		more_shown = 1;
	} else if (!scr_following()) {
		scr_status("Scrolled back.  PgDn or End returns to the latest.");
		more_shown = 1;
	} else if (more_shown) {
		scr_status(server_status);
		more_shown = 0;
	}
}

/* Plays a clip with SAY.EXE. SAY's own messages go to NUL: CodeJr owns
 * the screen. A key stops the speech. The serial line is quiet by then
 * (the server sends PLAY just before READY, or we are hanging up). */
static void speak(const char *clip)
{
	int nul, out, err;
	/* SAY lives in its own folder; older installs have it beside us. */
	const char *say = access("SAY\\SAY.EXE", 0) == 0 ? "SAY\\SAY.EXE" : "SAY.EXE";

	if (access(clip, 0) != 0 || access(say, 0) != 0)
		return;
	scr_status(quitting ? "Speaking...  (a key stops it)" : "Speaking...  (just type to stop it)");
	more_shown = 0;
	nul = open("NUL", O_WRONLY);
	out = dup(1);
	err = dup(2);
	if (nul >= 0) {
		dup2(nul, 1);
		dup2(nul, 2);
	}
	{
		/* Up to two options from SAY= (e.g. /H in DOSBox). */
		char o[sizeof say_opts], *o2;
		const char *args[10];
		int n = 0;
		strcpy(o, say_opts);
		args[n++] = say;
		args[n++] = "/Q";
		/* /K: the key that stops the speech stays in the keyboard buffer,
		 * so it lands in the input line: typing over the voice just works,
		 * and no key is lost. Not while leaving: nothing is being typed to. */
		if (!quitting)
			args[n++] = "/K";
		if (o[0]) {
			args[n++] = o;
			if ((o2 = strchr(o, ' ')) != NULL) {
				*o2++ = 0;
				args[n++] = o2;
			}
		}
		args[n++] = clip;
		args[n] = NULL;
		spawnv(P_WAIT, say, args);
	}
	if (nul >= 0) {
		dup2(out, 1);
		dup2(err, 2);
		close(nul);
	}
	close(out);
	close(err);
	if (!quitting) {
		scr_status(server_status);
		cursor_dirty = 1;
	}
}

/* Hangs up at once, saying so. */
static void hang_up(void)
{
	if (quitting)
		return;
	queue(CJP_BYE, NULL, 0);
	start_quit(); /* first: the input line goes before the goodbye plays */
	scr_status("Hanging up...");
	more_shown = 0;
	if (voice_on)
		speak("VOICE\\BYE.SND");
	scr_status("Hanging up...");
}

static void key(unsigned k)
{
	unsigned char ch = (unsigned char)k;
	unsigned scan = k >> 8;

	if (ask == 2) {
		/* Only Y or N count while the question is up; Alt-X still leaves. */
		if ((ch | 0x20) == 'y') {
			answer(1);
			return;
		}
		if ((ch | 0x20) == 'n' || ch == 27) {
			answer(0);
			return;
		}
		if (k != 0x2D00)
			return;
	}
	/* Scrolling and paging keys first. */
	if (ch == 0 || ch == 0xE0) {
		switch (scan) {
		case 0x49: scr_scroll(-(BOTTOM_ROWS - 1)); show_more(); return; /* PgUp */
		case 0x51: scr_scroll(BOTTOM_ROWS - 1); show_more(); return;    /* PgDn */
		case 0x48: scr_scroll(-1); show_more(); return;                 /* Up */
		case 0x50: scr_scroll(1); show_more(); return;                  /* Down */
		case 0x4F: scr_scroll(100000L); show_more(); return;            /* End */
		}
	}
	if (!scr_following() && scr_unread() > 0 && k != 0x2D00) {
		/* -- More --: Space (or Enter on an empty line) shows the next
		 * page; any other key also shows it, then does its own job. */
		scr_page();
		show_more();
		if (ch == ' ' && input_len == 0)
			return;
		if (ch == '\r' && input_len == 0)
			return;
	}

	if (k == 0x1F00) { /* Alt-S: connection counters, into the history */
		char c[128];
		counters(c);
		note(c);
		if (last_file[0])
			note(last_file);
	} else if (k == 0x2D00) { /* Alt-X */
		hang_up();
	} else if (ch == '\r') {
		if (input_len && !ready) {
			/* Never drop it: it goes as soon as CodeJr is ready. */
			line_waits = 1;
			scr_status("Your line goes as soon as CodeJr is ready.");
		} else if (input_len) {
			scr_mark_input();
			show_more();
			if (ui)
				echo_line(input);
			if (!local_line(input)) {
				/* A frame carries 128 bytes: a longer line goes as
				 * INPUT_MORE pieces, then INPUT with the last one.
				 * (1.3 and earlier queued the whole line as one frame,
				 * which was dropped silently when it was too long.) */
				int off = 0;
				while (input_len - off > CJP_MAXPAYLOAD) {
					queue(CJP_INPUT_MORE, input + off, CJP_MAXPAYLOAD);
					off += CJP_MAXPAYLOAD;
				}
				queue(CJP_INPUT, input + off, (unsigned)(input_len - off));
				if (ui)
					ready = 0;
			}
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
	if (!quitting) /* gone for good once CodeJr is leaving */
		scr_input(input, ready);
}

static int read_cfg(char *dial, int size)
{
	/* /setup delivers the file to C:\ and CODEJR.BAT moves it, but only
	 * on its next run, so look in both places. */
	FILE *f = fopen("C:\\CODEJR\\CODEJR.CFG", "r");
	char line[100];
	int found = 0;
	if (!f)
		f = fopen("C:\\CODEJR.CFG", "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof line, f)) {
		if (strncmp(line, "SAY=", 4) == 0) {
			char *e = line + strlen(line);
			while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
				*--e = 0;
			strncpy(say_opts, line + 4, sizeof say_opts - 1);
			say_opts[sizeof say_opts - 1] = 0;
		}
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

/* Ends a call the modem is still on: a second of silence, "+++", a second
 * of silence (the modem's rule), then AT must answer. Returns 1 if it
 * does, at the speed the port is set to. */
static int escape_call(void)
{
	settle(22);
	ser_puts("+++");
	settle(24);
	ser_puts("AT\r");
	if (expect("OK", TICKS_PER_SEC))
		return 1;
	ser_puts("AT\r");
	return expect("OK", TICKS_PER_SEC);
}

/* Dials and waits for the server's READY banner. Returns 0 on success. */
static int dial(const char *host)
{
	char seen[16] = "";
	unsigned long start = ticks(), last = 0, asked = ticks();
	char s[80];

	sprintf(s, "Dialing %.40s ...   (Alt-X gives up)", host);
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
		/* No banner after 8 seconds: the server isn't answering yet (it
		 * may be restarting). Ask again; the server's banner repeats
		 * every 2 seconds once we are through. */
		if (ticks() - asked > 8UL * TICKS_PER_SEC) {
			ser_puts("\rATD");
			ser_puts(host);
			ser_putc('\r');
			asked = ticks();
		}
		if (ticks() - last > TICKS_PER_SEC) {
			sprintf(s, "Dialing %.40s ... %lus   (Alt-X gives up)", host,
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
	unsigned long spun = 0;
	long baud = 9600, fast = 37287;
	int at_fast = 0, restore_only = 0;
	unsigned long start;
	char hello[CJP_MAXPAYLOAD + 1];

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (a[0] != '/' || !a[1] || a[2] != ':') {
			printf("usage: CODEJR [/D:host:port] [/P:1|2] [/B:9600] [/F:37287] [/L:1] [/R:1] [/T:text] [/X:secs]\n");
			return 1;
		}
		switch (a[1] | 0x20) {
		case 'd': strncpy(host, a + 3, sizeof host - 1); host[sizeof host - 1] = 0; break;
		case 'l': loop_mode = 1; break;
		case 'a': auto_answer = a[3] & 0xDF; break;
		case 'k': key_method = a[3] & 0xDF; break;
		case 'p': port = atoi(a + 3) - 1; break;
		case 'b': baud = atol(a + 3); break;
		case 'r': restore_only = 1; break;
		case 'f': fast = atol(a + 3); break;
		case 't': auto_text = a + 3; break;
		case 'x': exit_after = atol(a + 3); break;
		case 's': shot = a + 3; break;
		}
	}
	{
		/* CODEJR.CFG: the address to dial (unless /D gave one) and SAY=. */
		char dial[64];
		int have = read_cfg(dial, sizeof dial);
		if (!host[0] && have)
			strcpy(host, dial);
	}
	if (!host[0]) {
		printf("CODEJR: no server to dial. Use /D:host:port or put DIAL=host:port\n"
		       "in C:\\CODEJR\\CODEJR.CFG\n");
		return 1;
	}

	/* The modem stays at the fast speed between calls (this is the only
	 * program that uses it; /R:1 puts it back, for MS-Kermit). So it is
	 * probably there already: ask at that speed first. */
	if (fast && fast != baud && !restore_only) {
		ser_open(port, fast); /* RTS stays off: see docs/JOURNAL.md */
		settle(1);
		ser_puts("AT\r");
		if (expect("OK", TICKS_PER_SEC))
			at_fast = 1;
		else {
			/* once more: the last call may only just have dropped */
			ser_puts("AT\r");
			if (expect("OK", TICKS_PER_SEC))
				at_fast = 1;
		}
		if (!at_fast) {
			ser_set_baud(baud);
			ser_puts("\r"); /* end whatever the modem made of that */
			settle(4);
			ser_puts("AT\r");
			if (!expect("OK", TICKS_PER_SEC)) {
				/* Silent at both speeds: the last run probably ended
				 * without hanging up (a crash, Ctrl-Break), and the
				 * modem is still on that call, passing what we type to
				 * the server. Zimodem's README: "+++" followed by a
				 * second of silence disconnects. Try it at each speed. */
				printf("CODEJR: waking the modem (it may still be on a call)...\n");
				ser_set_baud(fast);
				at_fast = escape_call();
				if (!at_fast) {
					ser_set_baud(baud);
					escape_call();
				}
			}
		}
	} else
		ser_open(port, baud);
	/* Whatever ends this program, the serial interrupt goes back first: a
	 * vector left pointing into freed memory crashes the next program. */
	atexit(ser_close);
	/* Ctrl-C and Ctrl-Break must not end CodeJr: it would leave the modem
	 * on the call. They still stop a DOS command we started, which is how
	 * a stuck one gets unstuck. (Ctrl-C at the prompt is read as a key.) */
	signal(SIGINT, SIG_IGN);
	signal(SIGBREAK, SIG_IGN);
	if (restore_only) {
		/* CODEJR /R:1: the modem back to its saved speed, then out. It may
		 * be at either speed. */
		ser_puts("AT\r");
		if (!expect("OK", TICKS_PER_SEC)) {
			ser_set_baud(fast);
			settle(2);
			change_speed(fast, baud);
		}
		ser_close();
		printf("CODEJR: the modem is at %ld baud.\n", baud);
		return 0;
	}
	scr_init("CodeJr " VERSION);
	start = ticks();
	line_baud = at_fast ? fast : baud;
	if (fast && fast != baud && !at_fast) {
		char s[80];
		sprintf(s, "Switching the modem to %ld baud...", fast);
		scr_status(s);
		if (change_speed(baud, fast)) {
			line_baud = fast;
		} else {
			sprintf(s, "The modem didn't answer at %ld baud; staying at %ld.", fast, baud);
			note(s);
		}
	}
	if (dial(host) != 0) {
		ser_close();
		scr_done();
		printf("CODEJR: no answer from %s\n", host);
		return 1;
	}

	cjp_init(&link, link_write, link_now, NULL, 2 * TICKS_PER_SEC);
	sprintf(hello, "CODEJR/2 client " VERSION " baud=%ld ui=1 dirs=1 delta=1 voice=1 remote=1 run=1 sys=1 keys=1", line_baud);
	queue(CJP_HELLO, hello, (unsigned)strlen(hello));
	scr_status("Connecting...");

	while (!(quitting && outq_n == 0 && cjp_idle(&link))) {
		poll_serial();
		if (offer.pending)
			pull_file();
		if (key_waiting() && !quitting)
			key(_bios_keybrd(_KEYBRD_READ));
		if (cursor_dirty && !quitting && ser_rx_pending() == 0) {
			scr_input(input, ready);
			cursor_dirty = 0;
		}
		/* Busy: keep something moving, so the PCjr never looks stuck. */
		if (connected && !ready && ticks() - spun >= 2) {
			spun = ticks();
			scr_spin();
		}
		if (auto_text && connected && !sent_auto) {
			/* Typed as if at the keyboard, so tests cover the same path
			 * (including a line typed before CodeJr is ready). */
			strncpy(input, auto_text, sizeof input - 1);
			input[sizeof input - 1] = 0;
			input_len = (int)strlen(input);
			key('\r');
			sent_auto = 1;
		}
		show_more();
		pump_report();
		/* Remote work waits until nothing else is going out. */
		if (ask == 1 && pend_cmd[0]) {
			char q[100];
			sprintf(q, "CodeJr wants to run: %.70s", pend_cmd);
			note(q);
			scr_status("Allow it?   Y = yes, run it     N = no");
			more_shown = 1; /* keep the question on the status bar */
			ask = 2;
			ask_until = ticks() + 180UL * TICKS_PER_SEC;
			if (auto_answer)
				answer(auto_answer == 'Y');
		} else if (ask == 2 && ticks() > ask_until) {
			answer(0);
		}
		if (!ask && !up && !report && !offer.pending && outq_n == 0) {
			if (pend_cmd[0] && pend_run) {
				run_program();
			} else if (pend_cmd[0]) {
				run_command();
			} else if (pend_get[0]) {
				if (strcmp(pend_get, SCREEN_NAME) == 0)
					upload_screen();
				else
					start_upload(pend_get, pend_get, 'B', 0);
				pend_get[0] = 0;
			}
		}
		pump_upload();
		if (!up && shot_pending && outq_n == 0) {
			/* The text went; now the picture, or why there is none. */
			shot_pending = 0;
			if (access(CJSHOT, 0) == 0)
				start_upload(CJSHOT, "the screen", 'S', 1);
			else
				upload_fail("no graphics screen was up when the screenshot was due");
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

	/* The modem keeps the fast speed: the next start finds it there and
	 * skips the switch. (A power cycle, or CODEJR /R:1, puts it back.) */
	ser_close();
	if (shot)
		scr_save(shot);
	scr_done();
	return 0;
}
