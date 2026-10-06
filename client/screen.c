#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "screen.h"

#define COLS 80
#define TOP 1
#define BOTTOM 21
#define ROW_DIV 22
#define ROW_INPUT 23
#define ROW_STATUS 24

#define A_HEADER 0x1F /* bright white on blue */
#define A_DIV    0x01 /* blue on black */
#define A_PROMPT 0x0A /* bright green */
#define A_INPUT  0x0F /* bright white */
#define A_STATUS 0x30 /* black on cyan */

static unsigned short far *vid;
static int spin;  /* spinner frame */

/* The transcript is a view of a scrollback buffer: RING lines of 80
 * cells (character and color) in far memory. Lines are numbered from 0;
 * "last" is the line being written. The screen shows ROWS lines from
 * view_top. Following, the view keeps the last line at the bottom; paused
 * (a page was filled, or the user scrolled back) new text goes only into
 * the buffer, ready to show at once. */
#define RING 200
#define ROWS (BOTTOM - TOP + 1)
#define PAGE (ROWS - 1) /* keep one line of the last page in view */
static unsigned short far *ring;
static long last, first, view_top;
static int col;        /* column in the last line */
static int temp;       /* the last line holds a transient line */
static int follow = 1; /* the view tracks the end */
static int since;      /* lines added since the user's line or a page */

static unsigned short far *line_at(long k) { return ring + (unsigned)(k % RING) * COLS; }

/* Moves the hardware cursor by programming the CRTC (6845) directly. The
 * BIOS call (INT 10h, AH=2) can hold interrupts off long enough on the
 * PCjr for the serial port to lose incoming bytes. */
static void set_cursor(int r, int c)
{
	unsigned pos = (unsigned)(r * COLS + c);
	outp(0x3D4, 0x0E);
	outp(0x3D5, pos >> 8);
	outp(0x3D4, 0x0F);
	outp(0x3D5, pos & 0xFF);
}

static void fill_row(int r, unsigned char ch, unsigned char attr)
{
	unsigned short cell = (unsigned short)(attr << 8 | ch);
	unsigned short far *p = vid + r * COLS;
	int i;
	for (i = 0; i < COLS; i++)
		p[i] = cell;
}

static void put_text(int r, int c, const char *s, unsigned char attr, int width)
{
	unsigned short far *p = vid + r * COLS + c;
	int i;
	for (i = 0; i < width && c + i < COLS; i++)
		p[i] = (unsigned short)(attr << 8 | (unsigned char)(*s ? *s++ : ' '));
}

void scr_init(const char *title)
{
	union REGS x;
	int r;

	x.h.ah = 0; x.h.al = 3; /* 80x25 color text; also clears the screen */
	int86(0x10, &x, &x);
	vid = (unsigned short far *)MK_FP(0xB800, 0);
	fill_row(0, ' ', A_HEADER);
	put_text(0, 1, title, A_HEADER, COLS - 1);
	for (r = TOP; r <= BOTTOM; r++)
		fill_row(r, ' ', 0x07);
	fill_row(ROW_DIV, 0xC4, A_DIV); /* CP437 horizontal line */
	scr_input("", 0);
	scr_status("");
	if (!ring)
		ring = (unsigned short far *)_fmalloc((size_t)RING * COLS * 2);
	scr_clear_transcript();
}

void scr_done(void)
{
	union REGS x;
	x.h.ah = 0; x.h.al = 3;
	int86(0x10, &x, &x);
}

/* Shows buffer line k on screen row r (blank if there is none). */
static void draw_row(int r, long k)
{
	unsigned short far *p = vid + r * COLS;
	if (k < first || k > last || !ring) {
		fill_row(r, ' ', 0x07);
		return;
	}
	_fmemcpy(p, line_at(k), COLS * 2);
}

static void redraw(void)
{
	int r;
	for (r = 0; r < ROWS; r++)
		draw_row(TOP + r, view_top + r);
}

static void clear_line(long k)
{
	unsigned short far *p = line_at(k);
	int i;
	for (i = 0; i < COLS; i++)
		p[i] = 0x0720;
}

/* The row the last line is on, or -1 if it is out of view. */
static int last_row(void)
{
	long r = last - view_top;
	return r >= 0 && r < ROWS ? TOP + (int)r : -1;
}

static void newline(void)
{
	last++;
	if (last - first >= RING)
		first++; /* the oldest line falls out of the buffer */
	clear_line(last);
	col = 0;
	since++;
	if (!follow) {
		if (view_top < first) {
			view_top = first;
			redraw();
		}
		return;
	}
	if (since > PAGE) {
		/* A page filled since the user last read: hold here. */
		follow = 0;
		return;
	}
	/* Scroll the transcript up one row. */
	view_top++;
	_fmemmove(vid + TOP * COLS, vid + (TOP + 1) * COLS, (BOTTOM - TOP) * COLS * 2);
	fill_row(BOTTOM, ' ', 0x07);
}

void scr_temp(const char *s, unsigned char attr)
{
	unsigned short far *p;
	int i, r;
	if (!ring)
		return;
	if (col > 0)
		newline();
	p = line_at(last);
	for (i = 0; i < COLS; i++)
		p[i] = (unsigned short)(attr << 8 | (unsigned char)(*s ? *s++ : ' '));
	if ((r = last_row()) >= 0)
		draw_row(r, last);
	temp = 1;
}

int scr_col(void) { return col; }

void scr_putc(unsigned char c, unsigned char attr)
{
	int r;
	if (!ring)
		return;
	if (temp) {
		clear_line(last);
		if ((r = last_row()) >= 0)
			draw_row(r, last);
		temp = 0;
		col = 0;
		if (c == '\n')
			return; /* the line it stood on is free again */
	}
	if (c == '\n') {
		newline();
		return;
	}
	if (c == '\r' || c < 32)
		return;
	if (col >= COLS)
		newline();
	line_at(last)[col] = (unsigned short)(attr << 8 | c);
	if ((r = last_row()) >= 0)
		vid[r * COLS + col] = (unsigned short)(attr << 8 | c);
	col++;
}

void scr_puts(const char *s, unsigned char attr)
{
	while (*s)
		scr_putc((unsigned char)*s++, attr);
}

void scr_clear_transcript(void)
{
	last = first = 0;
	view_top = last - (ROWS - 1);
	col = temp = since = 0;
	follow = 1;
	if (ring)
		clear_line(0);
	redraw();
}

long scr_unread(void)
{
	long bottom = view_top + ROWS - 1;
	return last > bottom ? last - bottom : 0;
}

int scr_following(void) { return follow; }

/* Moves the view by delta lines (negative: back), redrawing it. Reaching
 * the end resumes following. */
void scr_scroll(long delta)
{
	long end = last - (ROWS - 1), lo = first < end ? first : end;
	view_top += delta;
	if (view_top < lo)
		view_top = lo;
	if (view_top >= end) {
		view_top = end;
		follow = 1;
	} else {
		follow = 0;
	}
	since = 0;
	redraw();
}

void scr_page(void) { scr_scroll(PAGE); }

void scr_mark_input(void)
{
	since = 0;
	if (!follow || view_top != last - (ROWS - 1))
		scr_scroll(last); /* back to the end */
}

/* The right end of the header: how long the conversation is. */
void scr_info(const char *s)
{
	int n = (int)strlen(s);
	if (n > 30) n = 30;
	put_text(0, COLS - 31, "", A_HEADER, 30);
	put_text(0, COLS - 1 - n, s, A_HEADER, n);
}

void scr_status(const char *s)
{
	fill_row(ROW_STATUS, ' ', A_STATUS);
	put_text(ROW_STATUS, 1, s, A_STATUS, COLS - 2);
}

/* A twinkle like Claude Code's, in CP437: dot, bullet, plus, star, sun. */
static const unsigned char spin_chars[] = { 0xFA, 0xF9, '+', '*', 0x0F, '*', '+', 0xF9 };

void scr_spin(void)
{
	spin = (spin + 1) % (int)sizeof spin_chars;
	vid[ROW_INPUT * COLS] = (unsigned short)(0x0E << 8 | spin_chars[spin]);
}

void scr_input(const char *s, int prompt)
{
	int len = (int)strlen(s), room = COLS - 3;
	const char *shown = len > room ? s + (len - room) : s;

	fill_row(ROW_INPUT, ' ', A_INPUT);
	put_text(ROW_INPUT, 0, prompt ? "> " : "  ", A_PROMPT, 2);
	put_text(ROW_INPUT, 2, shown, A_INPUT, room);
	set_cursor(ROW_INPUT, 2 + (int)strlen(shown));
}

/* Blanks the input line and parks the cursor off the screen. For when
 * CodeJr has stopped taking input (hanging up, leaving for a program), so
 * it doesn't look as if it were still waiting for a line. */
void scr_input_off(void)
{
	fill_row(ROW_INPUT, ' ', A_INPUT);
	set_cursor(25, 0); /* row 25 is past the last one: not shown */
}

void scr_save(const char *path)
{
	FILE *f = fopen(path, "w");
	int r, c;
	if (!f)
		return;
	for (r = 0; r < 25; r++) {
		for (c = 0; c < COLS; c++) {
			unsigned char ch = (unsigned char)vid[r * COLS + c];
			fputc(ch >= 32 && ch < 127 ? ch : (ch == 0xC4 ? '-' : '.'), f);
		}
		fputc('\n', f);
	}
	fclose(f);
}
