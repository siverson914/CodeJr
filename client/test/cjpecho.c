/* Host test harness for cjp.c: speaks the CodeJr protocol on stdin/stdout
 * and echoes every message back with the same type, acknowledging only
 * after "handling" it, like the PCjr client. Exits on BYE.
 * Built and driven by server/internal/cjp/interop_test.go. */
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include "../cjp.h"

static void wr(const unsigned char *p, unsigned n, void *ctx)
{
	(void)ctx;
	while (n) {
		ssize_t w = write(1, p, n);
		if (w <= 0)
			return;
		p += w;
		n -= (unsigned)w;
	}
}

static unsigned long now_ms(void *ctx)
{
	struct timespec ts;
	(void)ctx;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Echoes waiting to go out because the window was full. */
static struct { unsigned char type; unsigned len; unsigned char p[CJP_MAXPAYLOAD]; } q[512];
static unsigned qh, qt;

int main(void)
{
	static cjp_link l;
	unsigned char buf[512];
	int bye = 0;

	cjp_init(&l, wr, now_ms, NULL, 120);
	for (;;) {
		struct pollfd pfd = { 0, POLLIN, 0 };
		int r = poll(&pfd, 1, 10);
		if (r > 0) {
			ssize_t n = read(0, buf, sizeof buf), i;
			if (n <= 0)
				return 0;
			for (i = 0; i < n; i++) {
				if (!cjp_input(&l, buf[i]))
					continue;
				if (l.rx.type == CJP_BYE) {
					bye = 1;
				} else if (qt - qh < 512) {
					q[qt % 512].type = l.rx.type;
					q[qt % 512].len = l.rx.len;
					memcpy(q[qt % 512].p, l.rx.payload, l.rx.len);
					qt++;
				}
				cjp_ack(&l);
			}
		}
		while (qh != qt && cjp_can_send(&l)) {
			cjp_send(&l, q[qh % 512].type, q[qh % 512].p, q[qh % 512].len);
			qh++;
		}
		cjp_tick(&l);
		if (bye && qh == qt && cjp_idle(&l))
			return 0;
	}
}
