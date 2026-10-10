/* JRKEYS: see jrkeys.h. */
#include <i86.h>
#include <string.h>
#include "jrkeys.h"

typedef struct {
	unsigned char sc, sc2, grp;
	unsigned char was;      /* key_down at the last poll */
	unsigned char on, hit;  /* this tick's answers */
	unsigned char latch;    /* pressed since the last tick */
	unsigned char mk;       /* press count last seen */
	unsigned stamp;         /* order of the last press: larger is newer */
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

int jk_add(int sc, int sc2, int grp, int unused)
{
	jk_key *k;

	(void)unused;
	if (jk_n >= JK_MAX || sc <= 0 || sc > 127 || sc2 < 0 || sc2 > 127)
		return -1;
	k = &jk_keys[jk_n];
	memset(k, 0, sizeof *k);
	k->sc = (unsigned char)sc;
	k->sc2 = (unsigned char)sc2;
	k->grp = (unsigned char)grp;
	k->mk = (unsigned char)(jk_mk[sc] + (sc2 ? jk_mk[sc2] : 0));
	return jk_n++;
}

void jk_poll(void)
{
	int fresh = key_seq != jk_seq, i, down, made;
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
		if ((down && !k->was) || (made && !down)) {
			/* a new press, or a tap that is already over */
			k->latch = 1;
			k->stamp = ++jk_stampc;
		} else if (made) {
			jk_repeats = 1; /* pressed again while held: the keyboard repeating */
		}
		k->was = (unsigned char)down;
	}
}

void jk_tick(void)
{
	int i;
	jk_key *k;

	jk_poll();
	for (i = 0, k = jk_keys; i < jk_n; i++, k++) {
		k->hit = k->latch;
		k->on = (unsigned char)(k->latch || k->was);
		k->latch = 0;
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
