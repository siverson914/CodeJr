/* File transfer by pulling: the client's half. Plain C with no DOS calls,
 * so it builds for the PCjr and for host tests. The protocol is described
 * in server/internal/cjp/pull.go; keep the two in step.
 *
 * Per chunk: pull_begin, then repeat { send pull_request, listen to the
 * burst, pull_feed it } until pull_wanted is 0, then pull_finish. */
#ifndef PULL_H
#define PULL_H

#define PULL_BLOCK  128
#define PULL_BLOCKS 32
#define PULL_HDR    7
#define PULL_RAW    (PULL_BLOCKS * PULL_BLOCK - PULL_HDR) /* 4089 */
#define PULL_REQMAX 32 /* room for an encoded Pull frame */

typedef struct {
	unsigned index;                   /* chunk number */
	unsigned long have;               /* bitmap of blocks received */
	unsigned char lens[PULL_BLOCKS];  /* length of each received block */
	unsigned char data[PULL_BLOCKS * PULL_BLOCK];
	unsigned bad;                     /* damaged frames seen, for display */
} pull_chunk;

void pull_begin(pull_chunk *pc, unsigned index);

/* Bitmap of blocks still needed; 0 once the chunk is all here. */
unsigned long pull_wanted(const pull_chunk *pc);

/* Writes the Pull frame for what's still needed; returns its length. */
unsigned pull_request(const pull_chunk *pc, unsigned char *out);

/* Takes the raw bytes of a burst; returns how many new blocks it held. */
unsigned pull_feed(pull_chunk *pc, const unsigned char *raw, unsigned n);

/* Checks and unpacks a complete chunk into out. Returns its length, or -1
 * if it doesn't check out: start the chunk again with pull_begin. */
int pull_finish(const pull_chunk *pc, unsigned char *out, unsigned cap);

/* The line, as pull_fetch uses it. Times are in BIOS ticks (18.2 a second). */
typedef struct {
	void (*send)(const unsigned char *p, unsigned n, void *ctx);
	/* Receives raw bytes until `fends` bytes of value 0xC0 have arrived
	 * (0 = don't count), buf is full, or the line is quiet: `first` ticks
	 * with nothing at all, or `gap` ticks after the last byte. */
	unsigned (*burst)(unsigned char *buf, unsigned max, unsigned fends,
	                  unsigned first, unsigned gap, void *ctx);
	void *ctx;
} pull_io;

#define PULL_FIRST 54 /* wait 3 s for an answer to start */
#define PULL_GAP    9 /* a burst is over after 0.5 s of silence: WiFi stalls */
#define PULL_QUIET  4 /* the line counts as quiet after 0.2 s */
#define PULL_TRIES 20 /* rounds without progress before giving up */

/* Fetches chunk `index` and unpacks it into out. raw is scratch space for
 * one burst. Adds the number of requests made to *rounds. Returns the
 * chunk's length, or -1 if it couldn't be fetched. */
int pull_fetch(pull_chunk *pc, unsigned index, const pull_io *io,
               unsigned char *raw, unsigned rawmax,
               unsigned char *out, unsigned cap, unsigned *rounds);

#endif
