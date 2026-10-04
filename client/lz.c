#include <string.h>
#include "lz.h"

int lz_decode(const unsigned char *src, unsigned srclen, unsigned char *dst, unsigned cap)
{
	const unsigned char *s = src, *send = src + srclen;
	unsigned char *d = dst, *dend = dst + cap;

	while (s < send) {
		unsigned tok = *s++;
		unsigned len = tok >> 4, off;
		unsigned char b;

		if (len == 15) {
			do {
				if (s >= send)
					return -1;
				b = *s++;
				len += b;
				if (len > cap)
					return -1;
			} while (b == 255);
		}
		if ((unsigned)(send - s) < len || (unsigned)(dend - d) < len)
			return -1;
		memcpy(d, s, len);
		d += len;
		s += len;
		if (s >= send)
			break; /* the last sequence has no match */
		if (send - s < 2)
			return -1;
		off = s[0] | ((unsigned)s[1] << 8);
		s += 2;
		len = tok & 15;
		if (len == 15) {
			do {
				if (s >= send)
					return -1;
				b = *s++;
				len += b;
				if (len > cap)
					return -1;
			} while (b == 255);
		}
		len += 4;
		if (off == 0 || off > (unsigned)(d - dst) || (unsigned)(dend - d) < len)
			return -1;
		{
			/* Byte by byte on purpose: matches may overlap their output. */
			const unsigned char *m = d - off;
			while (len--)
				*d++ = *m++;
		}
	}
	return (int)(d - dst);
}
