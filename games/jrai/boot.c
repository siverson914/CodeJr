/* The intro, in 40-column text mode: Steve asks CodeJr for a demo, and
 * CodeJr "thinks", which is when the demo really does its precomputing. */
#include <dos.h>
#include <string.h>
#include "jrai.h"

static unsigned short far *scr;
static int cx, cy;

static void bios_mode(unsigned char m)
{
	union REGS r;
	r.h.ah = 0; r.h.al = m;
	int86(0x10, &r, &r);
	r.h.ah = 1; r.x.cx = 0x2000; /* hide the BIOS cursor; we draw our own */
	int86(0x10, &r, &r);
}

static void wait_ms(unsigned ms)
{
	unsigned long t = tmr_ms();
	while (tmr_ms() - t < ms) ;
}

static void put(int x, int y, char c, unsigned char a)
{
	scr[y * 40 + x] = (unsigned short)((unsigned char)c | (a << 8));
}

static void scroll_up(void)
{
	int i;
	for (i = 2 * 40; i < 24 * 40; i++) scr[i] = scr[i + 40];
	for (i = 24 * 40; i < 25 * 40; i++) scr[i] = 0x0720;
}

static void newline(void)
{
	cx = 0;
	if (++cy > 24) { scroll_up(); cy = 24; }
}

static void cursor(int on)
{
	put(cx, cy, on ? '_' : ' ', 0x07);
}

static void click(void)
{
	snd_noise(5, 6);
	wait_ms(12);
	snd_noise(5, 0);
}

/* Types s, one key at a time, in attribute a (speed: ms per key). */
static void type(const char *s, unsigned char a, unsigned speed, int keys)
{
	for (; *s; s++) {
		if (*s == '\n') { cursor(0); newline(); continue; }
		put(cx, cy, *s, a);
		if (++cx == 40) newline();
		cursor(1);
		if (keys && *s != ' ') click();
		wait_ms(speed + (keys ? rnd() % (speed + 1) : 0));
	}
	cursor(0);
}

static void say_line(const char *s, unsigned char a)
{
	type(s, a, 0, 0);
}

static void blink(unsigned ms)
{
	unsigned long t = tmr_ms();
	while (tmr_ms() - t < ms) {
		cursor((int)((tmr_ms() - t) / 300) & 1 ? 0 : 1);
	}
	cursor(0);
}

static void header(void)
{
	static const char h[] = " CodeJr                      37287 bps  ";
	int i;
	for (i = 0; i < 40; i++) put(i, 0, h[i], 0x1F);
	for (i = 0; i < 40; i++) put(i, 1, (char)0xC4, 0x09);
}

void boot_run(void (*work)(int step), int nsteps)
{
	static const char *const think[] = {
		"  mixing 32000 pixels of plasma",
		"  lighting a fire",
		"  writing 45K of tunnel code",
		"  teaching the SN76496 to talk",
		"  reading 42 years of demoscene",
	};
	static const char spin[4] = { '|', '/', '-', '\\' };
	int i;
	bios_mode(1);
	scr = (unsigned short far *)MK_FP(0xB800, 0);
	cx = 0; cy = 0;
	snd_init();

	/* the DOS prompt */
	say_line("C>", 0x07);
	blink(1200);
	type("CODEJR", 0x07, 110, 1);
	blink(400);
	cursor(0);
	newline();
	wait_ms(300);

	/* the client */
	for (i = 0; i < 40 * 25; i++) scr[i] = 0x0720;
	header();
	cx = 0; cy = 3;
	say_line("Connected. Hello, Steve.", 0x0B);
	newline(); newline();
	wait_ms(700);
	say_line("> ", 0x0E);
	blink(900);
	type("make me a demo. 80s style.", 0x0E, 70, 1);
	blink(500);
	type(" make it\n  beyond epic.", 0x0E, 70, 1);
	blink(700);
	newline(); newline();

	say_line("CodeJr is thinking ", 0x0B);
	{
		int sx = cx, sl = cy;
		for (i = 0; i < nsteps; i++) {
			const char *t = i < 5 ? think[i] : "  ...";
			int k;
			cx = 0; cy = sl + 1 + i;
			say_line(t, 0x08);
			for (k = 0; k < 8; k++) { put(sx, sl, spin[k & 3], 0x0F); wait_ms(80); }
			put(sx, sl, '*', 0x0E);
			work(i);
			cx = 0;
			say_line(t, 0x07);
			say_line(" ok", 0x0A);
		}
		put(sx, sl, ' ', 0x07);
		cx = 0; cy = sl + 1 + nsteps;
		newline();
	}
	say_line("CodeJr: ", 0x0B);
	type("Done. Turn the volume up.\n", 0x0F, 45, 0);
	blink(1500);
	if (opt_bootdump) { /* the text screen, for headless tests */
		int y, x;
		for (y = 0; y < 25; y++) {
			char l[41];
			for (x = 0; x < 40; x++) l[x] = (char)scr[y * 40 + x];
			l[40] = 0;
			dbg_log("|%s|", l);
		}
	}
	boot_end();
}

/* The screen dissolves into noise while the sound chip hisses. */
void boot_end(void)
{
	int i, k;
	snd_noise(4, 12);
	for (k = 0; k < 24; k++) {
		for (i = 0; i < 260; i++) {
			unsigned r = rnd();
			scr[r % 1000] = (unsigned short)(((r >> 3) & 0xFF) | (((r >> 8) & 0x7F) << 8));
		}
		snd_noise(k & 1 ? 4 : 5, 12 - k / 2);
		wait_ms(30);
	}
	snd_noise(4, 0);
}
