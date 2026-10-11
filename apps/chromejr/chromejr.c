/* CHROMEJR.EXE: ChromeJr's PCjr half. A browser on the laptop reads the
 * web; this program dials it, shows each page as the laptop laid it out
 * (Junior markup: server/internal/web/markup.go) and sends back what the
 * person picks.
 *
 *   CHROMEJR /D:host:port [/P:1|2] [/B:9600] [/F:37287] [/T:line] [/X:secs] [/S:file]
 *
 *   /D  the laptop to dial (ChromeJr's port is 4850)
 *   /P  BIOS serial port (default 1)
 *   /B  the modem's saved speed (default 9600)
 *   /F  the speed to switch the modem and the PCjr to (default 37287)
 *   /T  type this line once connected; several /T go one after another,
 *       each when the last page is in. /T:~DDE presses keys instead:
 *       D down, U up, E Enter, B Backspace, N PgDn, P PgUp, W Alt-W
 *   /X  exit after N seconds     /S  save the screen to a file at the end
 *   (/T, /X and /S are for automated tests)
 *
 * Screen: row 0 the page's title, row 1 the address bar, rows 2-23 the
 * page, row 24 the status line. Keys: type an address or words and
 * Enter; the arrow keys move between the numbered things on the page and
 * Enter opens one; a number and Enter works too. Backspace on an empty
 * line goes back, F1 is help, F5 loads again, Alt-W shows the page as it
 * is, Alt-X hangs up.
 *
 * The serial port and the link are CodeJr's (client/serial.c, cjp.c).
 * Dialing is copied from client/codejr.c; keep the two alike.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <bios.h>
#include <conio.h>
#include <signal.h>
#include <malloc.h>
#include "serial.h"
#include "cjp.h"

#define VERSION "0.1"
#define TICKS_PER_SEC 18
#define COLS 80
#define ROW_TITLE 0
#define ROW_ADDR 1
#define TOP 2        /* first row of the page */
#define VIEW 22      /* rows of the page on screen */
#define ROW_STATUS 24

/* Colors */
#define A_TITLE 0x1F   /* white on blue */
#define A_ADDR 0x70    /* black on light gray */
#define A_ADDRL 0x71   /* blue on light gray: "Go to:" */
#define A_HEAD 0x0F    /* bright white */
#define A_TEXT 0x07
#define A_NUM 0x0B     /* light cyan */
#define A_ITEM 0x0E    /* yellow */
#define A_PRICE 0x0A   /* light green */
#define A_DETAIL 0x03  /* cyan */
#define A_BUTTON 0x70  /* black on light gray */
#define A_BOX 0x70
#define A_RULE 0x08    /* dark gray */
#define A_SEL 0x1F     /* the picked thing: white on blue */
#define A_STATUS 0x30  /* black on cyan */
#define A_NOTE 0x08

static const unsigned char style_attr[] = { 0x07, 0x0E, 0x03, 0x0C, 0x0F };

static unsigned short far *vid;

/* ---- the page ------------------------------------------------------ */

/* The page as drawn: DOC_ROWS rows of 80 cells (character and color), in
 * far memory (48 KB). Rows past the last are not kept: a page longer than
 * that is cut, and says so. */
#define DOC_ROWS 300
static unsigned short far *doc;
static int nrows;      /* rows in use */
static int tcol;       /* column of plain text being added to the last row */
static int cut;        /* the page went past DOC_ROWS */
static int top;        /* first page row on screen */
static int fresh;      /* a new page: stay at its top while it arrives */
static int mark;       /* the first row added since the last line sent */

/* The numbered things on the page, in order. */
#define MAXCH 80
static struct {
	int n;               /* its number, which goes back to the laptop */
	char kind;           /* I A F S */
	int r1, r2;          /* rows it covers */
	unsigned char c1, c2; /* and columns */
	char label[33];
} ch[MAXCH];
static int nch;
static int sel = -1;   /* the picked one, or -1 */

/* Actions sit side by side on one row. */
static int act_row = -1, act_col;

static char title[COLS + 1];
static char url[COLS + 1];

static unsigned short far *row_at(int r)
{
	return doc + (unsigned)r * COLS;
}

/* A new empty row at the bottom; -1 when the page is full. */
static int new_row(void)
{
	unsigned short far *p;
	int i;
	if (nrows >= DOC_ROWS) {
		cut = 1;
		return -1;
	}
	p = row_at(nrows);
	for (i = 0; i < COLS; i++)
		p[i] = 0x0720;
	tcol = 0;
	act_row = -1;
	return nrows++;
}

static int row_blank(int r)
{
	unsigned short far *p = row_at(r);
	int i;
	for (i = 0; i < COLS; i++)
		if ((unsigned char)p[i] != ' ')
			return 0;
	return 1;
}

/* One empty row between blocks, never two, none at the top. */
static void gap(void)
{
	if (nrows > 0 && !row_blank(nrows - 1))
		new_row();
}

static void put(int r, int c, const char *s, unsigned char attr, int width)
{
	unsigned short far *p;
	int i;
	if (r < 0)
		return;
	p = row_at(r);
	for (i = 0; i < width && c + i < COLS && s[i]; i++)
		p[c + i] = (unsigned short)(attr << 8 | (unsigned char)s[i]);
}

static void put_ch(int r, int c, unsigned char chr, unsigned char attr, int n)
{
	unsigned short far *p;
	int i;
	if (r < 0)
		return;
	p = row_at(r);
	for (i = 0; i < n && c + i < COLS; i++)
		p[c + i] = (unsigned short)(attr << 8 | chr);
}

/* Writes text from column `indent`, `width` columns wide, breaking at
 * spaces; at most `maxrows` rows (0: no limit). Returns the last row. */
static int wrap(const char *s, unsigned char attr, int indent, int width, int maxrows)
{
	int r = -1, rows = 0;
	while (*s == ' ')
		s++;
	while (*s) {
		int n = (int)strlen(s), cutat;
		if (maxrows && rows == maxrows)
			break;
		cutat = n;
		if (n > width) {
			cutat = width;
			while (cutat > width / 3 && s[cutat] != ' ')
				cutat--;
			if (cutat <= width / 3)
				cutat = width;
		}
		if ((r = new_row()) < 0)
			return nrows - 1;
		put(r, indent, s, attr, cutat);
		rows++;
		s += cutat;
		while (*s == ' ')
			s++;
	}
	return r;
}

static void add_choice(int n, char kind, int r1, int r2, int c1, int c2, const char *label)
{
	if (nch >= MAXCH || r1 < 0)
		return;
	ch[nch].n = n;
	ch[nch].kind = kind;
	ch[nch].r1 = r1;
	ch[nch].r2 = r2 < r1 ? r1 : r2;
	ch[nch].c1 = (unsigned char)c1;
	ch[nch].c2 = (unsigned char)(c2 > COLS - 1 ? COLS - 1 : c2);
	strncpy(ch[nch].label, label, sizeof ch[nch].label - 1);
	ch[nch].label[sizeof ch[nch].label - 1] = 0;
	/* The first thing that opens something is picked to begin with (not
	 * a box: typed words go to the address bar unless a box is picked). */
	if (sel < 0 && kind != 'F')
		sel = nch;
	nch++;
}

/* Splits a markup line's fields at tabs, in place. Returns how many. */
static int fields(char *s, char **f, int max)
{
	int n = 0;
	f[n++] = s;
	while (*s && n < max) {
		if (*s == '\t') {
			*s = 0;
			f[n++] = s + 1;
		}
		s++;
	}
	return n;
}

static void draw_title(void);
static void draw_addr(void);

/* One line of Junior markup. */
static void markup(char *m)
{
	char *f[4];
	int nf, n = 0, r, r2, len;
	char kind = m[0];
	char num[8];

	if (!kind)
		return;
	m++;
	if (kind == 'I' || kind == 'A' || kind == 'F' || kind == 'S') {
		nf = fields(m, f, 4);
		if (nf < 2)
			return;
		n = atoi(f[0]);
		sprintf(num, "%2d", n % 1000); /* at most 3 digits, 8 bytes */
	} else {
		nf = 0;
	}
	if (kind != 'A')
		act_row = -1;
	switch (kind) {
	case 'T':
		strncpy(title, m, COLS);
		title[COLS] = 0;
		draw_title();
		break;
	case 'U':
		strncpy(url, m, COLS);
		url[COLS] = 0;
		draw_addr();
		break;
	case 'H':
		gap();
		wrap(m, A_HEAD, 0, COLS - 1, 0);
		break;
	case 'P':
		gap();
		wrap(m, A_TEXT, 0, COLS - 1, 0);
		break;
	case '-':
		gap();
		r = new_row();
		put_ch(r, 1, 0xC4, A_RULE, COLS - 2);
		break;
	case 'I': {
		/* " 3 Title of the thing                       $19.99"
		 *     one line of detail                              */
		const char *name = f[1], *detail = nf > 2 ? f[2] : "", *price = nf > 3 ? f[3] : "";
		int plen = (int)strlen(price), room;
		if (plen > 16)
			plen = 16;
		room = COLS - 4 - 1 - (plen ? plen + 2 : 0);
		r = nrows; /* the item's first row: the number and the price go there */
		r2 = wrap(name, A_ITEM, 4, room, 2);
		if (r2 < r)
			break; /* the page is full */
		put(r, 0, num, A_NUM, 3);
		if (plen)
			put(r, COLS - 1 - plen, price, A_PRICE, plen);
		if (detail[0]) {
			int d = new_row();
			put(d, 4, detail, A_DETAIL, COLS - 5);
			if (d >= 0)
				r2 = d;
		}
		add_choice(n, 'I', r, r2, 0, COLS - 1, name);
		break;
	}
	case 'A': {
		/* side by side: " 3 Add to cart  4 Sort by price " */
		char b[44];
		len = sprintf(b, " %d %.32s ", n % 1000, f[1]); /* < 44 */
		if (act_row < 0 || act_row != nrows - 1 || act_col + len + 2 > COLS - 1) {
			gap();
			if ((act_row = new_row()) < 0)
				break;
			act_col = 1;
		}
		put(act_row, act_col, b, A_BUTTON, len);
		add_choice(n, 'A', act_row, act_row, act_col, act_col + len - 1, f[1]);
		act_col += len + 2;
		break;
	}
	case 'F': {
		/* " 3 Search: [_______________________________________]" */
		char b[40];
		len = sprintf(b, "%s %.30s:", num, f[1]); /* < 40 */
		gap();
		if ((r = new_row()) < 0)
			break;
		put(r, 0, b, A_HEAD, len);
		put_ch(r, len + 1, ' ', A_BOX, COLS - 2 - (len + 1));
		add_choice(n, 'F', r, r, 0, COLS - 1, f[1]);
		break;
	}
	case 'S':
		if ((r = new_row()) < 0)
			break;
		put(r, 0, num, A_NUM, 3);
		put_ch(r, 4, 0x10, A_NUM, 1); /* a small triangle */
		put(r, 6, f[1], A_HEAD, COLS - 7);
		add_choice(n, 'S', r, r, 0, COLS - 1, f[1]);
		break;
	}
}

/* Plain text (a raw page, a message): characters in a color, '\n' ends
 * the row, a long row goes on to the next. */
static void text_char(unsigned char c, unsigned char attr)
{
	if (nrows == 0 && c != '\n')
		new_row();
	if (c == '\n') {
		new_row();
		return;
	}
	if (c == '\r')
		return;
	if (tcol >= COLS - 1 && new_row() < 0)
		return; /* the page is full: new_row left tcol where it was */
	if (nrows > 0 && tcol < COLS)
		row_at(nrows - 1)[tcol++] = (unsigned short)(attr << 8 | c);
	act_row = -1;
}

static void clear_page(void)
{
	nrows = 0;
	tcol = 0;
	nch = 0;
	sel = -1;
	top = 0;
	cut = 0;
	act_row = -1;
	fresh = 1;
	title[0] = 0;
	url[0] = 0;
}

/* ---- the screen ---------------------------------------------------- */

static void set_cursor(int r, int c)
{
	/* the CRT controller directly: the BIOS call held interrupts off
	 * long enough to lose serial bytes (client/screen.c) */
	unsigned pos = (unsigned)(r * COLS + c);
	outp(0x3D4, 0x0E);
	outp(0x3D5, pos >> 8);
	outp(0x3D4, 0x0F);
	outp(0x3D5, pos & 0xFF);
}

static void fill(int r, int c, int n, unsigned char chr, unsigned char attr)
{
	unsigned short far *p = vid + r * COLS + c;
	while (n-- > 0 && c++ < COLS)
		*p++ = (unsigned short)(attr << 8 | chr);
}

static void text_at(int r, int c, const char *s, unsigned char attr, int width)
{
	unsigned short far *p = vid + r * COLS + c;
	int i;
	for (i = 0; i < width && c + i < COLS; i++)
		p[i] = (unsigned short)(attr << 8 | (unsigned char)(*s ? *s++ : ' '));
}

static void draw_title(void)
{
	fill(ROW_TITLE, 0, COLS, ' ', A_TITLE);
	text_at(ROW_TITLE, 1, title[0] ? title : "ChromeJr", A_TITLE, COLS - 12);
	text_at(ROW_TITLE, COLS - 10, "ChromeJr", A_TITLE, 9);
}

/* The page, rows top to top+VIEW, with the picked thing lit up. */
static void draw_page(void)
{
	int r, c;
	for (r = 0; r < VIEW; r++) {
		unsigned short far *s = vid + (TOP + r) * COLS;
		int k = top + r;
		if (k >= nrows) {
			for (c = 0; c < COLS; c++)
				s[c] = 0x0720;
			continue;
		}
		_fmemcpy(s, row_at(k), COLS * 2);
		if (sel >= 0 && k >= ch[sel].r1 && k <= ch[sel].r2)
			for (c = ch[sel].c1; c <= ch[sel].c2; c++)
				s[c] = (unsigned short)(A_SEL << 8 | (unsigned char)s[c]);
	}
	/* more below: a mark at the bottom right */
	if (top + VIEW < nrows)
		vid[(TOP + VIEW - 1) * COLS + COLS - 1] = (unsigned short)(0x0B << 8 | 0x19); /* down arrow */
}

/* ---- the link ------------------------------------------------------ */

static cjp_link link;
#define OUTQ 6
static struct {
	unsigned char type;
	unsigned len;
	unsigned char p[CJP_MAXPAYLOAD];
} outq[OUTQ];
static int outq_head, outq_n;
static unsigned queue_drops;

static unsigned long ticks(void)
{
	return *(unsigned long far *)MK_FP(0x40, 0x6C);
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
	if (outq_n == OUTQ || n > CJP_MAXPAYLOAD) {
		queue_drops++;
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

/* ---- the address bar and the status line ---------------------------- */

#define INPUT_MAX 120 /* one frame: 128 bytes, with "123 " in front */
static char input[INPUT_MAX + 1];
static int input_len;
static int connected, ready, quitting, line_waits;
static unsigned long quit_at;
static char status[CJP_MAXPAYLOAD + 1];
static int spin;

static int field_mode(void)
{
	return sel >= 0 && ch[sel].kind == 'F';
}

static void draw_addr(void)
{
	char label[40];
	int lc, room;
	const char *shown;

	fill(ROW_ADDR, 0, COLS, ' ', A_ADDR);
	if (field_mode())
		sprintf(label, " %.24s: ", ch[sel].label); /* < 40 */
	else
		strcpy(label, " Go to: ");
	lc = (int)strlen(label);
	text_at(ROW_ADDR, 0, label, A_ADDRL, lc);
	room = COLS - lc - 1;
	if (input_len) {
		shown = input_len > room ? input + (input_len - room) : input;
		text_at(ROW_ADDR, lc, shown, A_ADDR, room);
		set_cursor(ROW_ADDR, lc + (int)strlen(shown));
	} else {
		/* where we are, in gray, until something is typed */
		text_at(ROW_ADDR, lc, field_mode() ? "" : url, 0x78, room);
		set_cursor(ROW_ADDR, lc);
	}
}

static void draw_status(void)
{
	const char *help = "Enter=open  Bksp=back  F1=help";
	int hl = (int)strlen(help);
	fill(ROW_STATUS, 0, COLS, ' ', A_STATUS);
	text_at(ROW_STATUS, 2, status, A_STATUS, COLS - hl - 5);
	text_at(ROW_STATUS, COLS - hl - 1, help, A_STATUS, hl);
}

static void set_status(const char *s)
{
	strncpy(status, s, sizeof status - 1);
	status[sizeof status - 1] = 0;
	draw_status();
}

/* A twinkle like CodeJr's, in CP437: dot, bullet, plus, star, sun. */
static const unsigned char spin_chars[] = { 0xFA, 0xF9, '+', '*', 0x0F, '*', '+', 0xF9 };

static void spinner(void)
{
	spin = (spin + 1) % (int)sizeof spin_chars;
	vid[ROW_STATUS * COLS] = (unsigned short)(0x3E << 8 | spin_chars[spin]);
}

/* ---- moving around ------------------------------------------------- */

static void show_row(int r)
{
	if (r < top)
		top = r;
	else if (r >= top + VIEW)
		top = r - VIEW + 1;
	if (top < 0)
		top = 0;
}

static void pick(int k)
{
	if (k < 0 || k >= nch)
		return;
	sel = k;
	/* the whole of it on screen if it fits, its first row anyway */
	show_row(ch[k].r2);
	show_row(ch[k].r1);
	fresh = 0;
}

static void scroll(int d)
{
	int k, maxtop = nrows - VIEW;
	top += d;
	if (top > maxtop)
		top = maxtop;
	if (top < 0)
		top = 0;
	fresh = 0;
	/* the picked thing stays on screen: else the first one that is */
	if (sel >= 0 && ch[sel].r1 >= top && ch[sel].r1 < top + VIEW)
		return;
	for (k = 0; k < nch; k++)
		if (ch[k].r1 >= top && ch[k].r1 < top + VIEW && ch[k].kind != 'F') {
			sel = k;
			return;
		}
}

/* Sends a typed line (or a pick) to the laptop. The page is busy until
 * its READY. */
static void send_line(const char *s)
{
	unsigned n = (unsigned)strlen(s);
	if (n > CJP_MAXPAYLOAD)
		n = CJP_MAXPAYLOAD;
	queue(CJP_INPUT, s, n);
	mark = nrows;
	ready = 0;
	set_status("Working...");
}

static void hang_up(void)
{
	if (quitting)
		return;
	queue(CJP_BYE, NULL, 0);
	quitting = 1;
	quit_at = ticks();
	set_status("Hanging up...");
}

static void enter(void)
{
	char line[INPUT_MAX + 8];
	if (!ready) {
		/* A typed line is never dropped: it goes as soon as this page
		 * is in. A pick isn't kept: by then the thing picked may be a
		 * different one, on the new page. */
		if (input_len) {
			line_waits = 1;
			set_status("That goes as soon as this page is in.");
		} else {
			set_status("Still loading: Enter again when the page is in.");
		}
		return;
	}
	if (input_len) {
		if (field_mode())
			sprintf(line, "%d %s", ch[sel].n % 1000, input); /* <= 4 + 120 */
		else
			strcpy(line, input);
		input_len = 0;
		input[0] = 0;
		send_line(line);
	} else if (field_mode()) {
		set_status("Type what goes in the box, then Enter. Esc: the address bar.");
	} else if (sel >= 0) {
		sprintf(line, "%d", ch[sel].n % 1000);
		send_line(line);
	}
}

static void key(unsigned k)
{
	unsigned char c = (unsigned char)k;
	unsigned scan = k >> 8;

	if (c == 0 || c == 0xE0) {
		switch (scan) {
		case 0x48: /* Up */
			if (nch)
				pick(sel <= 0 ? 0 : sel - 1);
			else
				scroll(-1);
			break;
		case 0x50: /* Down */
			if (nch)
				pick(sel < 0 ? 0 : (sel + 1 < nch ? sel + 1 : sel));
			else
				scroll(1);
			break;
		case 0x49: scroll(-(VIEW - 1)); break; /* PgUp */
		case 0x51: scroll(VIEW - 1); break;    /* PgDn */
		case 0x47: scroll(-DOC_ROWS); break;   /* Home */
		case 0x4F: scroll(DOC_ROWS); break;    /* End */
		case 0x3B: if (ready) send_line("?"); break; /* F1 */
		case 0x3F: if (ready) send_line("r"); break; /* F5 */
		case 0x11: if (ready) send_line("w"); break; /* Alt-W */
		case 0x2D: hang_up(); break;                  /* Alt-X */
		case 0x1F: {                                  /* Alt-S: link counters */
			char s[100];
			sprintf(s, "Line: %u lost, %u framing errors, %u bad frames, %u resent, %u unsent",
			        ser_overruns(), ser_framing(), link.bad, link.resent, queue_drops);
			set_status(s);
			break;
		}
		}
	} else if (c == '\r') {
		enter();
	} else if (c == 8) {
		if (input_len)
			input[--input_len] = 0;
		else if (ready)
			send_line("b");
	} else if (c == 27) {
		if (input_len) {
			input_len = 0;
			input[0] = 0;
		} else if (field_mode()) {
			sel = -1;
		}
	} else if (c == 9) {
		if (nch)
			pick(sel + 1 < nch ? sel + 1 : 0);
	} else if (c >= 32 && c < 127) {
		if (input_len < INPUT_MAX) {
			input[input_len++] = (char)c;
			input[input_len] = 0;
		} else {
			set_status("That is as long as a line goes here (120). Enter sends it.");
		}
	}
	draw_page();
	draw_addr();
}

static int key_waiting(void)
{
	unsigned far *head = (unsigned far *)MK_FP(0x40, 0x1A);
	unsigned far *tail = (unsigned far *)MK_FP(0x40, 0x1C);
	return *head != *tail;
}

/* ---- what the laptop sends ----------------------------------------- */

#define MLINE_MAX 1500
static char mline[MLINE_MAX + 1];
static int mlen;
static int dirty; /* the page changed: draw it when the line is quiet */

static FILE *dbg; /* /L:file: every frame received, for finding faults */

static void handle(const cjp_frame *f)
{
	unsigned i;
	char s[CJP_MAXPAYLOAD + 1];

	if (dbg) {
		fprintf(dbg, "%lu type %u len %u style %u\n", ticks(), f->type, f->len, f->len ? f->payload[0] : 0);
		fflush(dbg);
	}

	switch (f->type) {
	case CJP_HELLO:
		connected = 1;
		set_status("Connected.");
		break;
	case CJP_READY:
		ready = 1;
		fresh = 0;
		set_status(status[0] && strcmp(status, "Working...") ? status : "");
		if (line_waits) {
			line_waits = 0;
			enter();
		}
		break;
	case CJP_TEXT:
		if (!f->len)
			break;
		if (f->payload[0] == CJP_STYLE_MARKUP) {
			for (i = 1; i < f->len; i++) {
				char c = (char)f->payload[i];
				if (c == '\n') {
					mline[mlen] = 0;
					markup(mline);
					mlen = 0;
				} else if (mlen < MLINE_MAX) {
					mline[mlen++] = c; /* past the end: cut, never written */
				}
			}
		} else {
			unsigned char attr = style_attr[f->payload[0] < 5 ? f->payload[0] : 0];
			for (i = 1; i < f->len; i++)
				text_char(f->payload[i], attr);
			/* text after the page is in (a message, more of the page):
			 * show it from its start */
			if (!fresh && nrows - top > VIEW) {
				int want = nrows - VIEW;
				if (want > mark)
					want = mark;
				if (want > top)
					top = want;
			}
		}
		dirty = 1;
		break;
	case CJP_STATUS:
		memcpy(s, f->payload, f->len);
		s[f->len] = 0;
		set_status(s);
		break;
	case CJP_CLEAR:
		clear_page();
		mlen = 0;
		draw_title();
		dirty = 1;
		break;
	case CJP_BYE:
		quitting = 1;
		quit_at = ticks();
		break;
	}
}

static void poll_serial(void)
{
	int c;
	while ((c = ser_getc()) >= 0) {
		if (cjp_input(&link, (unsigned char)c)) {
			cjp_frame f = link.rx;
			handle(&f);
			cjp_ack(&link);
		}
	}
}

/* ---- dialing (as client/codejr.c) ---------------------------------- */

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

static int change_speed(long from, long to)
{
	char cmd[24];
	int i;

	sprintf(cmd, "ATB%ld\r", to);
	ser_puts(cmd);
	settle(6);
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

static int dial(const char *host)
{
	char seen[16] = "";
	unsigned long start = ticks(), last = 0, asked = ticks();
	char s[100]; /* 8 + 40 + 5 + 10 + 20: 83 at most */

	/* ATD, not ATDT: on Zimodem the T means "translate as TELNET". */
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
		if (ticks() - asked > 8UL * TICKS_PER_SEC) {
			ser_puts("\rATD");
			ser_puts(host);
			ser_putc('\r');
			asked = ticks();
		}
		if (ticks() - last > 4) {
			sprintf(s, "Dialing %.40s ... %lus   (Alt-X gives up)", host,
			        (ticks() - start) / TICKS_PER_SEC);
			set_status(s);
			spinner();
			last = ticks();
		}
	}
}

/* ---- tests: typed lines and keys, the screen saved ------------------- */

#define MAXAUTO 8
static const char *autos[MAXAUTO];
static int nautos, next_auto;

/* The next /T, once the last page is in: a line typed and Enter, or
 * "~" and keys. */
static void auto_step(void)
{
	const char *a = autos[next_auto++];
	if (a[0] == '~') {
		for (a++; *a; a++) {
			switch (*a | 0x20) {
			case 'd': key(0x5000); break;
			case 'u': key(0x4800); break;
			case 'n': key(0x5100); break;
			case 'p': key(0x4900); break;
			case 'e': key('\r'); break;
			case 'b': key(8); break;
			case 'w': key(0x1100); break; /* Alt-W */
			}
		}
		return;
	}
	strncpy(input, a, INPUT_MAX);
	input[INPUT_MAX] = 0;
	input_len = (int)strlen(input);
	key('\r');
}

static void save_screen(const char *path)
{
	FILE *f = fopen(path, "w");
	int r, c;
	if (!f)
		return;
	for (r = 0; r < 25; r++) {
		for (c = 0; c < COLS; c++) {
			unsigned short cell = vid[r * COLS + c];
			unsigned char chr = (unsigned char)cell;
			/* the picked thing is marked with > in column 0 of the copy */
			if (c == 0 && (cell >> 8) == A_SEL && r >= TOP)
				chr = '>';
			fputc(chr >= 32 && chr < 127 ? chr : (chr == 0xC4 ? '-' : '.'), f);
		}
		fputc('\n', f);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	char host[80] = "";
	const char *shot = NULL;
	long exit_after = -1;
	int port = 0, i, at_fast = 0;
	long baud = 9600, fast = 37287, line_baud;
	unsigned long start, spun = 0;
	char hello[CJP_MAXPAYLOAD + 1];
	union REGS x;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (a[0] != '/' || !a[1] || a[2] != ':') {
			printf("usage: CHROMEJR /D:host:port [/P:1|2] [/B:9600] [/F:37287] [/T:line] [/X:secs] [/S:file]\n");
			return 1;
		}
		switch (a[1] | 0x20) {
		case 'd': strncpy(host, a + 3, sizeof host - 1); host[sizeof host - 1] = 0; break;
		case 'p': port = atoi(a + 3) - 1; break;
		case 'b': baud = atol(a + 3); break;
		case 'f': fast = atol(a + 3); break;
		case 't': if (nautos < MAXAUTO) autos[nautos++] = a + 3; break;
		case 'x': exit_after = atol(a + 3); break;
		case 's': shot = a + 3; break;
		case 'l': dbg = fopen(a + 3, "w"); break;
		}
	}
	if (!host[0]) {
		printf("CHROMEJR: no laptop to dial. Use /D:host:4850\n");
		return 1;
	}
	doc = (unsigned short far *)_fmalloc((size_t)DOC_ROWS * COLS * 2);
	if (!doc) {
		printf("CHROMEJR: not enough memory\n");
		return 1;
	}

	/* The modem is probably at the fast speed already (CodeJr leaves it
	 * there): ask there first. */
	if (fast && fast != baud) {
		ser_open(port, fast); /* RTS stays off */
		settle(1);
		ser_puts("AT\r");
		if (expect("OK", TICKS_PER_SEC))
			at_fast = 1;
		else {
			ser_puts("AT\r");
			if (expect("OK", TICKS_PER_SEC))
				at_fast = 1;
		}
		if (!at_fast) {
			ser_set_baud(baud);
			ser_puts("\r");
			settle(4);
			ser_puts("AT\r");
			if (!expect("OK", TICKS_PER_SEC)) {
				printf("CHROMEJR: waking the modem (it may still be on a call)...\n");
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
	atexit(ser_close);
	signal(SIGINT, SIG_IGN);
	signal(SIGBREAK, SIG_IGN);

	x.h.ah = 0;
	x.h.al = 3;
	int86(0x10, &x, &x);
	vid = (unsigned short far *)MK_FP(0xB800, 0);
	clear_page();
	draw_title();
	draw_addr();
	draw_page();
	set_status("");
	start = ticks();
	line_baud = at_fast ? fast : baud;
	if (fast && fast != baud && !at_fast) {
		set_status("Switching the modem to the fast speed...");
		if (change_speed(baud, fast))
			line_baud = fast;
	}
	if (dial(host) != 0) {
		ser_close();
		x.h.ah = 0;
		x.h.al = 3;
		int86(0x10, &x, &x);
		printf("CHROMEJR: no answer from %s\n", host);
		return 1;
	}

	cjp_init(&link, link_write, link_now, NULL, 2 * TICKS_PER_SEC);
	sprintf(hello, "CODEJR/2 client CHROMEJR " VERSION " baud=%ld markup=1", line_baud);
	queue(CJP_HELLO, hello, (unsigned)strlen(hello));
	set_status("Connecting...");

	while (!(quitting && outq_n == 0 && cjp_idle(&link))) {
		poll_serial();
		if (key_waiting() && !quitting)
			key(_bios_keybrd(_KEYBRD_READ));
		/* Draw when the line is quiet: drawing while bytes arrive is
		 * fine (the port has an interrupt and a buffer), but there is
		 * no need to draw every frame of a page. */
		if (dirty && ser_rx_pending() == 0) {
			draw_page();
			draw_addr();
			dirty = 0;
		}
		if (connected && !ready && ticks() - spun >= 2) {
			spun = ticks();
			spinner();
		}
		if (connected && ready && !quitting && next_auto < nautos)
			auto_step();
		flush_queue();
		cjp_tick(&link);
		if (exit_after >= 0 && (long)(ticks() - start) > exit_after * 182 / 10) {
			hang_up();
			exit_after = -1;
		}
		if (quitting && ticks() - quit_at > 5UL * TICKS_PER_SEC)
			break;
	}
	ser_close();
	if (shot)
		save_screen(shot);
	x.h.ah = 0;
	x.h.al = 3;
	int86(0x10, &x, &x);
	return 0;
}
