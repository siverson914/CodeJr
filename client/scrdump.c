/* SCRDUMP: writes the 80x25 text screen to stdout (for headless tests). */
#include <stdio.h>
#include <dos.h>

int main(void)
{
	unsigned char far *v = (unsigned char far *)MK_FP(0xB800, 0);
	int r, c;

	for (r = 0; r < 25; r++) {
		for (c = 0; c < 80; c++) {
			unsigned char ch = v[(r * 80 + c) * 2];
			putchar(ch >= 32 && ch < 127 ? ch : '.');
		}
		putchar('\n');
	}
	return 0;
}
