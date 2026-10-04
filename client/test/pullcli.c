/* Host test harness for pull.c: fetches a file with the pull protocol over
 * stdin/stdout, the way CODEJR.EXE does over the serial line.
 *   pullcli OUTFILE SIZE CHUNKS
 * Driven by server/internal/cjp/pull_test.go, which plays the server over
 * a line that drops and flips bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include "../cjp.h"
#include "../pull.h"

static void io_send(const unsigned char *p, unsigned n, void *ctx)
{
	(void)ctx;
	if (write(1, p, n) != (ssize_t)n)
		exit(3);
}

/* Like ser_burst on the PCjr; a tick is 55 ms. */
static unsigned io_burst(unsigned char *buf, unsigned max, unsigned fends,
                         unsigned first, unsigned gap, void *ctx)
{
	unsigned n = 0;

	(void)ctx;
	if (fends == 0)
		fends = 0xFFFF;
	while (n < max) {
		struct pollfd pfd = { 0, POLLIN, 0 };
		unsigned char c;

		if (poll(&pfd, 1, (int)((n ? gap : first) * 55)) <= 0)
			break;
		if (read(0, &c, 1) != 1)
			exit(3);
		buf[n++] = c;
		if (c == CJP_FEND && --fends == 0)
			break;
	}
	return n;
}

int main(int argc, char **argv)
{
	static pull_chunk pc;
	static unsigned char raw[9216], out[PULL_RAW];
	unsigned long size, written = 0;
	static const pull_io io = { io_send, io_burst, NULL };
	unsigned chunks, k, rounds = 0;
	FILE *f;

	if (argc != 4)
		return 2;
	size = strtoul(argv[2], NULL, 10);
	chunks = (unsigned)atoi(argv[3]);
	f = fopen(argv[1], "wb");
	if (!f)
		return 2;
	for (k = 0; k < chunks; k++) {
		int len = pull_fetch(&pc, k, &io, raw, sizeof raw, out, sizeof out, &rounds);

		if (len < 0)
			return 4;
		fwrite(out, 1, (size_t)len, f);
		written += (unsigned long)len;
	}
	fclose(f);
	return written == size ? 0 : 5;
}
