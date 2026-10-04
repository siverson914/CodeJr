#include <string.h>
#include "cjp.h"
#include "lz.h"
#include "pull.h"

void pull_begin(pull_chunk *pc, unsigned index)
{
	pc->index = index;
	pc->have = 0;
	memset(pc->lens, 0, sizeof pc->lens);
}

/* Number of blocks in the chunk, from its header; 0 if not known yet or
 * the header makes no sense. */
static unsigned total_blocks(const pull_chunk *pc)
{
	unsigned paylen;

	if (!(pc->have & 1) || pc->lens[0] < PULL_HDR)
		return 0;
	paylen = PULL_HDR + (pc->data[3] | ((unsigned)pc->data[4] << 8));
	if (paylen > PULL_BLOCKS * PULL_BLOCK)
		return 0;
	return (paylen + PULL_BLOCK - 1) / PULL_BLOCK;
}

unsigned long pull_wanted(const pull_chunk *pc)
{
	unsigned total = total_blocks(pc);
	unsigned long mask = 0xFFFFFFFFUL;

	if (total && total < 32)
		mask = (1UL << total) - 1;
	return mask & ~pc->have & 0xFFFFFFFFUL;
}

unsigned pull_request(const pull_chunk *pc, unsigned char *out)
{
	cjp_frame f;
	unsigned long w = pull_wanted(pc);

	f.type = CJP_PULL;
	f.seq = 0;
	f.ack = 0;
	f.len = 6;
	f.payload[0] = (unsigned char)pc->index;
	f.payload[1] = (unsigned char)(pc->index >> 8);
	f.payload[2] = (unsigned char)w;
	f.payload[3] = (unsigned char)(w >> 8);
	f.payload[4] = (unsigned char)(w >> 16);
	f.payload[5] = (unsigned char)(w >> 24);
	return cjp_encode(&f, out);
}

/* One frame body (between FENDs, already unescaped): type seq ack len
 * payload crc-hi crc-lo. Stores it if it is a good block of this chunk. */
static unsigned take(pull_chunk *pc, const unsigned char *b, unsigned n)
{
	unsigned seq, len;

	if (n < 6)
		return 0; /* a stray byte between frames (line noise), not a block */
	if (b[3] != n - 6 ||
	    cjp_crc16(b, n - 2) != (((unsigned)b[n - 2] << 8) | b[n - 1])) {
		pc->bad++;
		return 0;
	}
	seq = b[1];
	len = n - 6;
	if (b[0] != CJP_BLOCK || b[2] != (pc->index & 0xFF) || seq >= PULL_BLOCKS ||
	    len == 0 || len > PULL_BLOCK)
		return 0; /* intact, but not ours: a late answer to an older pull */
	if (pc->have & (1UL << seq))
		return 0;
	memcpy(pc->data + seq * PULL_BLOCK, b + 4, len);
	pc->lens[seq] = (unsigned char)len;
	pc->have |= 1UL << seq;
	return 1;
}

unsigned pull_feed(pull_chunk *pc, const unsigned char *raw, unsigned n)
{
	unsigned char body[PULL_BLOCK + 8];
	unsigned len = 0, got = 0, i;
	int esc = 0, over = 0;

	for (i = 0; i < n; i++) {
		unsigned char c = raw[i];

		if (c == CJP_FEND) {
			if (over)
				pc->bad++;
			else if (len)
				got += take(pc, body, len);
			len = 0;
			esc = over = 0;
			continue;
		}
		if (esc) {
			c ^= 0x20;
			esc = 0;
		} else if (c == CJP_FESC) {
			esc = 1;
			continue;
		}
		if (len < sizeof body)
			body[len++] = c;
		else
			over = 1; /* a lost FEND ran two frames together */
	}
	return got;
}

int pull_finish(const pull_chunk *pc, unsigned char *out, unsigned cap)
{
	unsigned total = total_blocks(pc);
	unsigned method, rawlen, datalen, crc, paylen, i;

	if (!total || pull_wanted(pc))
		return -1;
	method = pc->data[0];
	rawlen = pc->data[1] | ((unsigned)pc->data[2] << 8);
	datalen = pc->data[3] | ((unsigned)pc->data[4] << 8);
	crc = pc->data[5] | ((unsigned)pc->data[6] << 8);
	paylen = PULL_HDR + datalen;
	if (rawlen > PULL_RAW || rawlen > cap)
		return -1;
	for (i = 0; i < total; i++) {
		unsigned want = (i + 1 < total) ? PULL_BLOCK : paylen - PULL_BLOCK * (total - 1);
		if (pc->lens[i] != want)
			return -1;
	}
	if (method == 0) {
		if (datalen != rawlen)
			return -1;
		memcpy(out, pc->data + PULL_HDR, rawlen);
	} else if (method == 1) {
		if (lz_decode(pc->data + PULL_HDR, datalen, out, cap) != (int)rawlen)
			return -1;
	} else {
		return -1;
	}
	if (cjp_crc16(out, rawlen) != crc)
		return -1;
	return (int)rawlen;
}

static unsigned popcount(unsigned long w)
{
	unsigned n = 0;
	for (; w; w &= w - 1)
		n++;
	return n;
}

int pull_fetch(pull_chunk *pc, unsigned index, const pull_io *io,
               unsigned char *raw, unsigned rawmax,
               unsigned char *out, unsigned cap, unsigned *rounds)
{
	int tries = 0, unclean = 0;

	pull_begin(pc, index);
	for (;;) {
		unsigned long want;
		unsigned char req[PULL_REQMAX];
		unsigned n;

		if (unclean) {
			/* The last burst didn't bring everything. The rest of it, or
			 * of an older answer, may still be on its way (the WiFi link
			 * stalls for a moment now and then): take whatever comes
			 * until the line is quiet, instead of asking on top of it.
			 * Asking early is what makes answers pile up. */
			do {
				n = io->burst(raw, rawmax, 0, PULL_QUIET, PULL_QUIET, io->ctx);
				pull_feed(pc, raw, n);
			} while (n == rawmax);
			unclean = 0;
		}
		want = pull_wanted(pc);
		if (want == 0) {
			int len = pull_finish(pc, out, cap);
			if (len >= 0)
				return len;
			pull_begin(pc, index); /* failed its checks: fetch it again */
			if (++tries > PULL_TRIES)
				return -1;
			continue;
		}
		if (++tries > PULL_TRIES)
			return -1;
		io->send(req, pull_request(pc, req), io->ctx);
		n = io->burst(raw, rawmax, 2 * popcount(want), PULL_FIRST, PULL_GAP, io->ctx);
		if (pull_feed(pc, raw, n))
			tries = 0;
		if (pull_wanted(pc))
			unclean = 1;
		++*rounds;
	}
}
