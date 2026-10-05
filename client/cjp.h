/* CodeJr protocol, client side. Plain C with no DOS calls, so the same file
 * builds for the PCjr (Open Watcom) and for host tests (gcc). The server's
 * side is server/internal/cjp; keep the two in step. */
#ifndef CJP_H
#define CJP_H

#define CJP_FEND 0xC0
#define CJP_FESC 0xDB
#define CJP_MAXPAYLOAD 128
#define CJP_WINDOW 4
#define CJP_MAXWIRE (2 * (CJP_MAXPAYLOAD + 6) + 2)

/* Message types. */
#define CJP_ACK         0x00
#define CJP_HELLO       0x01
#define CJP_TEXT        0x02
#define CJP_STATUS      0x03
#define CJP_INPUT       0x04
#define CJP_KEY         0x05
#define CJP_FILEBEGIN   0x06
#define CJP_FILEDATA    0x07
#define CJP_FILEEND     0x08
#define CJP_FILERESULT  0x09
#define CJP_BYE         0x0A
#define CJP_CLEAR       0x0B
#define CJP_NAK         0x0C
#define CJP_FILEOFFER   0x0D /* size (4 LE), chunk count (2 LE), 8.3 name */
#define CJP_READY       0x0E /* server: the user can type now */
#define CJP_TEMP        0x0F /* server: a line erased by the next text */
#define CJP_RESULT      0x10 /* client: a /run report: 'B' name, 'D' text, 'E' */
#define CJP_DELTAOFFER  0x11 /* like FILEOFFER; the chunks are an edit script */

/* Types from CJP_DATAGRAM up carry no sequence number and are never
 * acknowledged. The file pull protocol uses them (pull.h). */
#define CJP_DATAGRAM    0x40
#define CJP_PULL        0x40 /* chunk index (2 LE), bitmap of wanted blocks (4 LE) */
#define CJP_BLOCK       0x41 /* seq = block index, ack = chunk index mod 256 */

/* Text styles (first byte of a TEXT payload). */
#define CJP_STYLE_PLAIN  0
#define CJP_STYLE_USER   1
#define CJP_STYLE_NOTE   2
#define CJP_STYLE_WARN   3
#define CJP_STYLE_BANNER 4

#define CJP_KEY_CTRLC 3

typedef struct {
	unsigned char type, seq, ack;
	unsigned len;
	unsigned char payload[CJP_MAXPAYLOAD];
} cjp_frame;

typedef struct {
	unsigned char buf[CJP_MAXPAYLOAD + 9];
	unsigned len;
	char in, esc;
} cjp_decoder;

unsigned cjp_crc16(const unsigned char *p, unsigned n);
/* Continues a CRC over more bytes (start with 0xFFFF). */
unsigned cjp_crc16_more(unsigned crc, const unsigned char *p, unsigned n);

/* Writes f to out (at least CJP_MAXWIRE bytes); returns the length. */
unsigned cjp_encode(const cjp_frame *f, unsigned char *out);

/* Feeds one byte: returns 1 when *f holds a complete frame, -1 for a frame
 * that failed its check, 0 otherwise. */
int cjp_feed(cjp_decoder *d, unsigned char c, cjp_frame *f);

/* The reliable link. The caller supplies the clock and the byte writer. */
typedef struct {
	void (*write)(const unsigned char *p, unsigned n, void *ctx);
	unsigned long (*now)(void *ctx);
	void *ctx;
	unsigned long timeout;           /* in the units now() returns */

	unsigned char base, next, expected;
	unsigned char npend;
	unsigned char pend[CJP_WINDOW][CJP_MAXWIRE];
	unsigned pendlen[CJP_WINDOW];
	unsigned long sent_at;
	cjp_decoder dec;
	cjp_frame rx;

	unsigned resent, bad;
} cjp_link;

void cjp_init(cjp_link *l, void (*write)(const unsigned char *, unsigned, void *),
              unsigned long (*now)(void *), void *ctx, unsigned long timeout);

/* Room for another message? */
int cjp_can_send(const cjp_link *l);

/* Sends a message; returns 0, or -1 if the window is full or n is too long. */
int cjp_send(cjp_link *l, unsigned char type, const unsigned char *p, unsigned n);

/* Feeds one received byte. Returns 1 when a new in-order message is ready
 * in l->rx: handle it, then call cjp_ack. The PCjr acknowledges only after
 * handling, so a slow disk write naturally slows the server down. */
int cjp_input(cjp_link *l, unsigned char c);
void cjp_ack(cjp_link *l);

/* Call regularly: resends unacknowledged frames after the timeout. */
void cjp_tick(cjp_link *l);

/* Everything sent has been acknowledged? */
int cjp_idle(const cjp_link *l);

/* Sends an unacknowledged datagram (type >= CJP_DATAGRAM). */
void cjp_send_datagram(cjp_link *l, unsigned char type, const unsigned char *p, unsigned n);

#endif
