/* LZ4 block format decoder (the compressor is server/internal/lz). */
#ifndef LZ_H
#define LZ_H

/* Decodes srclen bytes into dst (at most cap bytes). Returns the decoded
 * length, or -1 if the data is damaged. Never reads or writes out of
 * bounds. cap must be under 32768. */
int lz_decode(const unsigned char *src, unsigned srclen, unsigned char *dst, unsigned cap);

#endif
