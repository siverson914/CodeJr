/* JRKEYS: see jrkeys.h. */
#include <i86.h>
#include <string.h>
#include "jrkeys.h"

typedef struct {
	unsigned char sc, sc2, grp;
	unsigned char was;      /* key_down at the last poll */
	unsigned char on, hit;  /* this tick's answers */
	unsigned char latch;    /* press seen since the last tick */
	unsigned char alive;    /* down and not given up on */
	unsigned char mk;       /* press count last seen */
	unsigned char reps;     /* presses while already down (repeats?) */
	int held, max, quiet;
	unsigned stamp;
} jk_key;

static jk_key jk_keys[JK_MAX];
static int jk_n;
static unsigned jk_stampc, jk_seq;
static unsigned char jk_mk[128];
int jk_repeats;

/* The interrupt's press counts, copied whole. */
static void jk_read(void)
{
	_disable();
	jk_seq = key_seq;
	memcpy(jk_mk, (void *)key_makes, sizeof jk_mk);
	_enable();
}

void jk_init(void)
{
	jk_n = 0;
	jk_repeats = 0;
	jk_stampc = 0;
	jk_read();
}

void jk_done(void)
{
}

int jk_add(int sc, int sc2, int grp, int max)
{
	jk_key *k;

	if (jk_n >= JK_MAX || sc <= 0 || sc > 127 || sc2 < 0 || sc2 > 127)
		return -1;
	k = &jk_keys[jk_n];
	memset(k, 0, sizeof *k);
	k->sc = (unsigned char)sc;
	k->sc2 = (unsigned char)sc2;
	k->grp = (unsigned char)grp;
	k->mk = (unsigned char)(jk_mk[sc] + (sc2 ? jk_mk[sc2] : 0));
	k->max = max;
	return jk_n++;
}

static void jk_press(jk_key *k)
{
	k->latch = 1;
	k->alive = 1;
	k->held = 0;
	k->quiet = 0;
	k->reps = 0;
	k->stamp = ++jk_stampc;
}

void jk_poll(void)
{
	int fresh = key_seq != jk_seq, i, j, down, made;
	unsigned char m;
	jk_key *k;

	if (fresh)
		jk_read();
	for (i = 0, k = jk_keys; i < jk_n; i++, k++) {
		down = key_down[k->sc] || (k->sc2 && key_down[k->sc2]);
		made = 0;
		if (fresh) {
			m = (unsigned char)(jk_mk[k->sc] + (k->sc2 ? jk_mk[k->sc2] : 0));
			if (m != k->mk) {
				k->mk = m;
				made = 1;
			}
		}
		if (down && !k->was) {
			jk_press(k);                /* a plain new press */
		} else if (made && down) {
			k->quiet = 0;
			if (!k->alive)
				jk_press(k);            /* pressed again after a lost release */
			else if (++k->reps >= 3)
				jk_repeats = 1;         /* the keyboard repeats: use that */
		} else if (made) {
			jk_press(k);                /* a tap already over: it still counts */
		}
		if (!down && k->was) {
			k->alive = 0;
			if (k->grp)                 /* a release frees its whole group */
				for (j = 0; j < jk_n; j++)
					if (jk_keys[j].grp == k->grp)
						jk_keys[j].alive = 0;
		}
		k->was = (unsigned char)down;
	}
}

void jk_tick(void)
{
	int i, on;
	jk_key *k;

	jk_poll();
	for (i = 0, k = jk_keys; i < jk_n; i++, k++) {
		k->hit = k->latch;
		on = k->latch || (k->was && k->alive);
		/* Given up on: the next press (or repeat) of it counts as new. */
		if (on && k->max > 0 && k->held >= k->max)
			on = k->alive = 0;
		/* Only the newest key held repeats (measured on the PCjr): an
		 * older one goes quiet as soon as another key is pressed, and
		 * that says nothing about it. */
		if (on && jk_repeats && k->stamp == jk_stampc && k->quiet >= JK_QUIET)
			on = k->alive = 0;
		if (on && k->held < 32000)
			k->held++;
		if (k->quiet < 32000)
			k->quiet++;
		k->latch = 0;
		k->on = (unsigned char)on;
	}
}

int jk_down(int i)
{
	return i >= 0 && i < jk_n && jk_keys[i].on;
}

int jk_hit(int i)
{
	return i >= 0 && i < jk_n && jk_keys[i].hit;
}

int jk_best(int grp)
{
	int i, best = -1;
	unsigned bs = 0;

	for (i = 0; i < jk_n; i++)
		if (jk_keys[i].on && (grp < 0 || jk_keys[i].grp == grp) &&
		    jk_keys[i].stamp >= bs) {
			bs = jk_keys[i].stamp;
			best = i;
		}
	return best;
}
