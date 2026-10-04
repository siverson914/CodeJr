#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>
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
static int col; /* transcript column on the bottom row */

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
	scr_input("");
	scr_status("");
	col = 0;
}

void scr_done(void)
{
	union REGS x;
	x.h.ah = 0; x.h.al = 3;
	int86(0x10, &x, &x);
}

static void newline(void)
{
	/* Scroll the transcript up one row. */
	_fmemmove(vid + TOP * COLS, vid + (TOP + 1) * COLS, (BOTTOM - TOP) * COLS * 2);
	fill_row(BOTTOM, ' ', 0x07);
	col = 0;
}

void scr_putc(unsigned char c, unsigned char attr)
{
	if (c == '\n') {
		newline();
		return;
	}
	if (c == '\r' || c < 32)
		return;
	if (col >= COLS)
		newline();
	vid[BOTTOM * COLS + col] = (unsigned short)(attr << 8 | c);
	col++;
}

void scr_puts(const char *s, unsigned char attr)
{
	while (*s)
		scr_putc((unsigned char)*s++, attr);
}

void scr_clear_transcript(void)
{
	int r;
	for (r = TOP; r <= BOTTOM; r++)
		fill_row(r, ' ', 0x07);
	col = 0;
}

void scr_status(const char *s)
{
	fill_row(ROW_STATUS, ' ', A_STATUS);
	put_text(ROW_STATUS, 1, s, A_STATUS, COLS - 2);
}

void scr_input(const char *s)
{
	int len = (int)strlen(s), room = COLS - 3;
	const char *shown = len > room ? s + (len - room) : s;

	fill_row(ROW_INPUT, ' ', A_INPUT);
	put_text(ROW_INPUT, 0, "> ", A_PROMPT, 2);
	put_text(ROW_INPUT, 2, shown, A_INPUT, room);
	set_cursor(ROW_INPUT, 2 + (int)strlen(shown));
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
