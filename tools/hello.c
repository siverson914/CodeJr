/* Toolchain check: prints the DOS version and the BIOS machine ID byte. */
#include <stdio.h>
#include <dos.h>

int main(void)
{
	union REGS r;
	unsigned char far *id = (unsigned char far *)MK_FP(0xF000, 0xFFFE);

	r.h.ah = 0x30;
	intdos(&r, &r);
	printf("CodeJr toolchain OK: DOS %u.%02u, machine ID %02X%s\n",
	       r.h.al, r.h.ah, *id, *id == 0xFD ? " (PCjr)" : "");
	return 0;
}
