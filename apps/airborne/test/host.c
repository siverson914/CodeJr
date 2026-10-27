/* Run the C port on the laptop with the reference trace's scripted input and
 * compare the hidden Mac screen (rows 0-299) tick by tick with the original
 * running in the emulator (Airborne repo: node tools/trace.mjs).
 *   cc -O2 -o host test/host.c game.c && ./host CODE4.bin ref.bin [outdir]
 * On the first difference it writes both screens as PBM files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../game.h"

static u8 code[40000], fb[MAC_FB_BYTES];

static u32 fnv(const u8 *p, size_t n)
{
	u32 h = 0x811c9dc5u;
	size_t i;
	for (i = 0; i < n; i++) { h ^= p[i]; h *= 0x01000193u; }
	return h & 0xffffffffu;
}

static void pbm(const char *name, const u8 *rows)
{
	FILE *f = fopen(name, "wb");
	fprintf(f, "P4 512 300\n");
	fwrite(rows, 1, 300 * 64, f);
	fclose(f);
}

static void lists(void)
{
	static const struct { const char *name; u16 head; } L[] = {
		{ "heli", 0x1e }, { "air", 0x28 }, { "ground", 0x24 }, { "debris", 0x3c }, { "aa", 0x34 }, { "mortar", 0x68 } };
	unsigned i;
	for (i = 0; i < sizeof L / sizeof L[0]; i++) {
		s32 p = gl(L[i].head);
		printf("%s", L[i].name);
		while (p) {
			u16 o = (u16)(p - 0x40000);
			printf(" %x:%d,%d s%d t%d", o, gw(o + 4), gw(o + 6), gw(o + 0x20), gw(o + 0x22));
			if (L[i].head == 0x3c) printf(" d%d,%d L%d", gw(o + 0x28), gw(o + 0x2a), gw(o + 0x2c));
			printf(" |");
			p = gl(o);
		}
		printf("\n");
	}
	printf("score %ld gun %d cc %d\n", (long)game_score(), gw(0x32), gw(0xcc));
}

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	size_t n = fread(code, 1, sizeof code, f);
	u32 count, dumpEvery, t, *ref, mtime;
	u16 level;
	u8 guided;
	u8 *dumps;
	long size;
	const char *out = argc > 3 ? argv[3] : ".";
	fclose(f);
	(void)n;
	f = fopen(argv[2], "rb");
	fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
	{
		u8 *buf = malloc(size);
		if (fread(buf, 1, size, f) != (size_t)size) return 2;
		count = buf[0] | buf[1] << 8 | buf[2] << 16 | (u32)buf[3] << 24;
		dumpEvery = buf[4] | buf[5] << 8 | buf[6] << 16 | (u32)buf[7] << 24;
		level = buf[8] | buf[9] << 8;
		mtime = buf[12] | buf[13] << 8 | buf[14] << 16 | (u32)buf[15] << 24;
		guided = buf[16];
		ref = malloc(4 * count);
		for (t = 0; t < count; t++) { u8 *p = buf + 20 + 4 * t; ref[t] = p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
		dumps = buf + 20 + 4 * count;
	}
	fclose(f);
	g_code = code;
	g_fb = fb;
	g_mouse_h = 56; g_mouse_v = 150;
	game_start(level, guided, 0, mtime);
	game_tick(0);
	for (t = 1; t <= count; t++) {
		u32 h;
		g_mouse_h = (s16)(56 + (t * 7) % 400);
		g_button = t % 40 < 30;
		if (t % 600 == 300) game_key(49, 32);
		game_tick(t);
		h = fnv(fb, 300 * 64);
		if (h != ref[t - 1]) {
			if (game_tick(t) == 1) { printf("match until the game ended at tick %lu (score %ld, level %u)\n", (unsigned long)t, (long)game_score(), game_level()); return 0; }
			char a[512], b[512];
			u32 d = (t / dumpEvery) * dumpEvery;
			printf("first difference at tick %lu (score %ld level %u)\n", (unsigned long)t, (long)game_score(), game_level());
			lists();
			snprintf(a, sizeof a, "%s/port_t%lu.pbm", out, (unsigned long)t);
			pbm(a, fb);
			if (d >= dumpEvery) {
				snprintf(b, sizeof b, "%s/ref_t%lu.pbm", out, (unsigned long)d);
				pbm(b, dumps + (size_t)(d / dumpEvery - 1) * 300 * 64);
				printf("nearest reference dump: tick %lu\n", (unsigned long)d);
			}
			return 1;
		}
	}
	printf("all %lu ticks match (score %ld, level %u)\n", (unsigned long)count, (long)game_score(), game_level());
	return 0;
}
