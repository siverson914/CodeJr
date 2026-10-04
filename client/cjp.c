#include <string.h>
#include "cjp.h"

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection. Table
 * driven: the bitwise version cost an 8088 about 0.4 s per 4 KB, as much
 * as receiving the data. The table is built on first use. */
unsigned cjp_crc16(const unsigned char *p, unsigned n)
{
	static unsigned short tab[256];
	static char ready;
	unsigned crc = 0xFFFF;

	if (!ready) {
		unsigned i, j, c;
		for (i = 0; i < 256; i++) {
			c = i << 8;
			for (j = 0; j < 8; j++)
				c = (c & 0x8000) ? ((c << 1) ^ 0x1021) : (c << 1);
			tab[i] = (unsigned short)c;
		}
		ready = 1;
	}
	while (n--)
		crc = ((crc << 8) ^ tab[((crc >> 8) ^ *p++) & 0xFF]) & 0xFFFF;
	return crc;
}

static int needs_escape(unsigned char c)
{
	/* Also CR: Zimodem's telnet translation drops a NUL after a CR. */
	return c == CJP_FEND || c == CJP_FESC || c == 0x11 || c == 0x13 || c == 0xFF || c == 0x0D;
}

static unsigned put(unsigned char *out, unsigned n, unsigned char c)
{
	if (needs_escape(c)) {
		out[n++] = CJP_FESC;
		out[n++] = c ^ 0x20;
	} else {
		out[n++] = c;
	}
	return n;
}

unsigned cjp_encode(const cjp_frame *f, unsigned char *out)
{
	unsigned char hdr[4];
	unsigned crc, n = 0, i;

	hdr[0] = f->type;
	hdr[1] = f->seq;
	hdr[2] = f->ack;
	hdr[3] = (unsigned char)f->len; /* catches dropped or extra bytes */
	{
		unsigned char tmp[4 + CJP_MAXPAYLOAD];
		memcpy(tmp, hdr, 4);
		memcpy(tmp + 4, f->payload, f->len);
		crc = cjp_crc16(tmp, 4 + f->len);
	}
	out[n++] = CJP_FEND;
	for (i = 0; i < 4; i++)
		n = put(out, n, hdr[i]);
	for (i = 0; i < f->len; i++)
		n = put(out, n, f->payload[i]);
	n = put(out, n, (unsigned char)(crc >> 8));
	n = put(out, n, (unsigned char)crc);
	out[n++] = CJP_FEND;
	return n;
}

int cjp_feed(cjp_decoder *d, unsigned char c, cjp_frame *f)
{
	if (c == 0x11 || c == 0x13)
		return 0; /* raw XON/XOFF is flow control, never frame data */
	if (c == CJP_FEND) {
		unsigned n;

		if (!d->in || d->len == 0) {
			d->in = 1;
			d->esc = 0;
			d->len = 0;
			return 0;
		}
		n = d->len;
		d->len = 0;
		d->esc = 0;
		/* This FEND also opens the next frame. */
		if (n < 6 || n - 6 > CJP_MAXPAYLOAD || d->buf[3] != n - 6)
			return -1;
		if (cjp_crc16(d->buf, n - 2) != ((unsigned)d->buf[n - 2] << 8 | d->buf[n - 1]))
			return -1;
		f->type = d->buf[0];
		f->seq = d->buf[1];
		f->ack = d->buf[2];
		f->len = n - 6;
		memcpy(f->payload, d->buf + 4, f->len);
		return 1;
	}
	if (!d->in)
		return 0;
	if (d->esc) {
		d->esc = 0;
		c ^= 0x20;
	} else if (c == CJP_FESC) {
		d->esc = 1;
		return 0;
	}
	if (d->len >= sizeof(d->buf)) {
		d->in = 0; /* runaway: wait for the next FEND */
		d->len = 0;
		return -1;
	}
	d->buf[d->len++] = c;
	return 0;
}

void cjp_init(cjp_link *l, void (*write)(const unsigned char *, unsigned, void *),
              unsigned long (*now)(void *), void *ctx, unsigned long timeout)
{
	memset(l, 0, sizeof(*l));
	l->write = write;
	l->now = now;
	l->ctx = ctx;
	l->timeout = timeout;
}

int cjp_can_send(const cjp_link *l)
{
	return l->npend < CJP_WINDOW;
}

int cjp_idle(const cjp_link *l)
{
	return l->npend == 0;
}

int cjp_send(cjp_link *l, unsigned char type, const unsigned char *p, unsigned n)
{
	cjp_frame f;
	unsigned char slot;

	if (n > CJP_MAXPAYLOAD || l->npend >= CJP_WINDOW)
		return -1;
	f.type = type;
	f.seq = l->next;
	f.ack = l->expected;
	f.len = n;
	if (n)
		memcpy(f.payload, p, n);
	slot = (unsigned char)(l->next % CJP_WINDOW);
	l->pendlen[slot] = cjp_encode(&f, l->pend[slot]);
	if (l->npend == 0)
		l->sent_at = l->now(l->ctx);
	l->npend++;
	l->next++;
	l->write(l->pend[slot], l->pendlen[slot], l->ctx);
	return 0;
}

static void send_ack(cjp_link *l)
{
	cjp_frame f;
	unsigned char wire[16];

	f.type = CJP_ACK;
	f.seq = 0;
	f.ack = l->expected;
	f.len = 0;
	l->write(wire, cjp_encode(&f, wire), l->ctx);
}

static void handle_ack(cjp_link *l, unsigned char a)
{
	unsigned char acked = (unsigned char)(a - l->base);

	/* Valid acks are base+1 .. next. */
	if (acked == 0 || acked > l->npend)
		return;
	l->base = a;
	l->npend -= acked;
	l->sent_at = l->now(l->ctx);
}

int cjp_input(cjp_link *l, unsigned char c)
{
	int r = cjp_feed(&l->dec, c, &l->rx);

	if (r < 0) {
		l->bad++;
		return 0;
	}
	if (r == 0)
		return 0;
	if (l->rx.type >= CJP_DATAGRAM)
		return 0; /* a stray block from a file burst: not link traffic */
	handle_ack(l, l->rx.ack);
	if (l->rx.type == CJP_ACK)
		return 0;
	if (l->rx.seq != l->expected) {
		send_ack(l); /* duplicate or out of order: repeat where we are */
		return 0;
	}
	l->expected++;
	return 1; /* the caller handles it, then calls cjp_ack */
}

void cjp_ack(cjp_link *l)
{
	send_ack(l);
}

void cjp_tick(cjp_link *l)
{
	unsigned char i, seq;

	if (l->npend == 0 || l->now(l->ctx) - l->sent_at < l->timeout)
		return;
	/* Go back N: resend everything not yet acknowledged, oldest first. */
	for (i = 0, seq = l->base; i < l->npend; i++, seq++) {
		unsigned char slot = (unsigned char)(seq % CJP_WINDOW);
		l->write(l->pend[slot], l->pendlen[slot], l->ctx);
		l->resent++;
	}
	l->sent_at = l->now(l->ctx);
}

void cjp_send_datagram(cjp_link *l, unsigned char type, const unsigned char *p, unsigned n)
{
	cjp_frame f;
	unsigned char wire[CJP_MAXWIRE];

	if (n > CJP_MAXPAYLOAD)
		return;
	f.type = type;
	f.seq = 0;
	f.ack = 0;
	f.len = n;
	if (n)
		memcpy(f.payload, p, n);
	l->write(wire, cjp_encode(&f, wire), l->ctx);
}
