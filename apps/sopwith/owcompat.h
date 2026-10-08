/* Open Watcom stand-ins for the Turbo C 2.0 things sopwith2.c uses.
 * New file, written 2026-10-07 for the CodeJr PCjr port. */
#ifndef OWCOMPAT_H
#define OWCOMPAT_H

#include <conio.h>
#include <i86.h>
#include <malloc.h>
#include <stdarg.h>

#define inportb(p)     ((unsigned char)inp(p))
#define outportb(p, v) outp(p, v)
#define delay(ms)      ((void)0)  /* Turbo C calibrated its delay loop on the first call */

/* The history (record and playback) buffer. Turbo C took all far memory. */
#define farcoreleft()  60000L
#define farmalloc(n)   halloc((long)(n), 1)
#define farfree(p)     hfree(p)

/* Turbo C's pseudo-register for the data segment. */
unsigned ow_ds(void);
#pragma aux ow_ds = "mov ax,ds" value [ax];
#define _DS ow_ds()

#endif
