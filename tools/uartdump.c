/* UARTDUMP: prints the serial chip's registers, including the baud divisor
 * actually in effect, so we can copy the state MS-Kermit leaves behind.
 *   UARTDUMP [base-in-hex]   (default: the BIOS's COM1, else 2F8) */
#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <conio.h>

int main(int argc, char **argv)
{
	unsigned far *bda = (unsigned far *)MK_FP(0x40, 0);
	unsigned base = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 16) : bda[0];
	unsigned char lcr, dll, dlm;

	if (!base)
		base = 0x2F8;
	_disable();
	lcr = (unsigned char)inp(base + 3);
	outp(base + 3, lcr | 0x80);
	dll = (unsigned char)inp(base + 0);
	dlm = (unsigned char)inp(base + 1);
	outp(base + 3, lcr);
	_enable();
	printf("UART %03X: divisor %u (DLL %02X DLM %02X)  LCR %02X  MCR %02X  "
	       "IER %02X  LSR %02X  MSR %02X\n",
	       base, dll | (dlm << 8), dll, dlm, lcr, inp(base + 4), inp(base + 1),
	       inp(base + 5), inp(base + 6));
	printf("BIOS COM1=%04X COM2=%04X  machine ID %02X\n", bda[0], bda[1],
	       *(unsigned char far *)MK_FP(0xF000, 0xFFFE));
	return 0;
}
