/* Checks fix_mul / fix_div against long long, and times them. */
#include <stdio.h>
#include <stdlib.h>
#include "../jrgfx.h"
static unsigned long seed = 12345;
static long rnd(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (long)(seed ^ (seed >> 13) ^ ((seed & 0xFF) << 24)) >> (int)(seed % 20);
}
int main(void)
{
	int i, bad = 0;
	unsigned long t;
	volatile fix acc = 0;
	for (i = 0; i < 2000; i++) {
		long a = rnd(), b = rnd();
		long long m = ((long long)a * b) >> 16;
		fix r = fix_mul(a, b);
		if ((long)m != r && m == (long)m) { if (bad++ < 5) printf("mul %ld %ld got %ld want %ld\n", a, b, r, (long)m); }
		if (b) {
			long long q = ((long long)a << 16) / b;
			long long e = (long long)fix_div(a, b) - q;
			long long tol = (b > 65535L || b < -65535L) ? (q < 0 ? -q : q) / 16384 + 1 : 0;
			if (q > -0x7FFFFFFFLL && q < 0x7FFFFFFFLL && (e > tol || e < -tol)) {
				if (bad++ < 10) printf("div %ld %ld got %ld want %ld\n", a, b, fix_div(a, b), (long)q);
			}
		}
	}
	printf("bad=%d\n", bad);
	t = tmr_ms();
	for (i = 0; i < 1000; i++) acc += fix_mul(acc + 70000L, -123456L);
	dbg_log("1000 fix_mul %lu ms", tmr_ms() - t);
	t = tmr_ms();
	for (i = 0; i < 1000; i++) acc += fix_div(acc + 70000L, 123456L);
	dbg_log("1000 fix_div (b>=1.0) %lu ms", tmr_ms() - t);
	t = tmr_ms();
	for (i = 0; i < 1000; i++) acc += fix_div(acc + 70000L, 12345L);
	dbg_log("1000 fix_div (b<1.0) %lu ms", tmr_ms() - t);
	t = tmr_ms();
	for (i = 0; i < 1000; i++) acc += (fix)(((long long)acc * 12345L) >> 16);
	dbg_log("1000 long long mul %lu ms", tmr_ms() - t);
	dbg_log("bad=%d", bad);
	tmr_done();
	return 0;
}
