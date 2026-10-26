/* KEYSIM: does the JRKEYS layer (JRGFX/JRKEYS.H) do the right thing with
 * what the real PCjr keyboard sends? KEYTEST (2026-10-09/10, two runs,
 * 731 codes, one run under game conditions) showed the keyboard loses
 * nothing: every press has its release, and only the newest key pressed
 * repeats (after 0.6 s, then every 96 ms). So a game must see a held key
 * as held for as long as it is held, whatever else is tapped meanwhile.
 * This feeds the layer such sequences through key_feed() and reports
 * every tick where jk_down disagrees with the finger. Runs in text mode,
 * no keys needed: KEYSIM, result on the screen and in LOG.TXT.
 */
#include <stdio.h>
#include <string.h>
#include "jrgfx.h"
#include "jrkeys.h"

static FILE *f;
static int kl, kr, ku, fails, checks;
static int fl, fr, fu;        /* what the fingers hold */
static int tickno;
static int rep_l;             /* ticks until the next simulated repeat of LEFT */

static void say(const char *s)
{
    printf("%s\n", s);
    fprintf(f, "%s\n", s);
}

/* One game tick: the keyboard may send a repeat of the newest key first. */
static void tick(int newest_is_left_repeating)
{
    if (newest_is_left_repeating && fl && --rep_l <= 0) {
        key_feed(KEY_LEFT, 1);
        rep_l = 3;              /* 96 ms at 30 ticks a second */
    }
    jk_poll();
    jk_tick();
    tickno++;
}

static const char *lastwhat = "";
static int lastbad;

/* Reports the first bad tick of a run with the same label, then counts. */
static void check(const char *what)
{
    int dl = jk_down(kl) != 0, dr = jk_down(kr) != 0;
    char b[120];
    checks++;
    if (dl != fl || dr != fr) {
        if (strcmp(what, lastwhat) != 0 || !lastbad) {
            sprintf(b, "FAIL tick %4d %-34s layer: L=%d R=%d  fingers: L=%d R=%d",
                    tickno, what, dl, dr, fl, fr);
            say(b);
        }
        lastbad++;
        fails++;
    } else if (lastbad) {
        sprintf(b, "     ... %d bad ticks, right again at tick %d", lastbad, tickno);
        say(b);
        lastbad = 0;
    }
    lastwhat = what;
}

static void press(int sc, int *fp)   { key_feed(sc, 1); *fp = 1; if (sc == KEY_LEFT) rep_l = 18; }
static void release(int sc, int *fp) { key_feed(sc, 0); *fp = 0; }

int main(void)
{
    int i;
    char b[80];

    f = fopen("LOG.TXT", "w");
    if (!f) return 1;
    key_init();
    jk_init();
    kl = jk_add(KEY_LEFT, 0, 1, 90);
    kr = jk_add(KEY_RIGHT, 0, 1, 90);
    ku = jk_add(KEY_UP, 0, 0, 30);
    say("KEYSIM: the JRKEYS layer against what the PCjr keyboard really does");

    /* A: hold LEFT for 150 ticks (5 s at 30 Hz), no repeats seen yet. */
    say("A: hold LEFT 150 ticks");
    press(KEY_LEFT, &fl);
    for (i = 0; i < 150; i++) { tick(0); check("A hold LEFT"); }
    release(KEY_LEFT, &fl);
    tick(0); check("A after release");

    /* B: hold LEFT, with the keyboard repeating it; tap UP (rotate); keep
     * holding LEFT: the keyboard stops repeating LEFT, as seen in KEYTEST. */
    say("B: hold LEFT with repeats, tap UP, keep holding LEFT 100 ticks");
    press(KEY_LEFT, &fl);
    for (i = 0; i < 40; i++) { tick(1); check("B hold LEFT, repeating"); }
    press(KEY_UP, &fu); tick(0); check("B UP down");
    release(KEY_UP, &fu); tick(0); check("B UP up");
    for (i = 0; i < 100; i++) { tick(0); check("B hold LEFT, no more repeats"); }
    release(KEY_LEFT, &fl);
    tick(0); check("B after release");

    /* C: hold RIGHT, tap LEFT to nudge back, keep holding RIGHT. */
    say("C: hold RIGHT, tap LEFT, keep holding RIGHT 30 ticks");
    press(KEY_RIGHT, &fr);
    for (i = 0; i < 10; i++) { tick(0); check("C hold RIGHT"); }
    press(KEY_LEFT, &fl); tick(0); check("C LEFT down too");
    release(KEY_LEFT, &fl); tick(0); check("C LEFT up again");
    for (i = 0; i < 30; i++) { tick(0); check("C still holding RIGHT"); }
    release(KEY_RIGHT, &fr);
    tick(0); check("C after release");

    /* D: a tap between two ticks must show for one tick, then be gone. */
    say("D: tap LEFT between ticks");
    press(KEY_LEFT, &fl); release(KEY_LEFT, &fl);
    jk_poll(); jk_tick(); tickno++;
    checks++;
    if (!jk_down(kl)) { say("FAIL D: the tap was not seen"); fails++; }
    tick(0); check("D tick after the tap");

    /* E: roll LEFT to RIGHT: L down, R down, L up, R up. */
    say("E: roll from LEFT to RIGHT");
    press(KEY_LEFT, &fl); tick(0); check("E L down");
    press(KEY_RIGHT, &fr); tick(0); check("E L+R down");
    checks++;
    if (jk_best(1) != kr) { say("FAIL E: newest is not RIGHT"); fails++; }
    release(KEY_LEFT, &fl); tick(0); check("E L up, R held");
    for (i = 0; i < 20; i++) { tick(0); check("E R held on"); }
    release(KEY_RIGHT, &fr); tick(0); check("E all up");

    /* F: the mash from KEYTEST: R down, L down 16 ms later, R up, L up. */
    say("F: mash: R down, L down, R up, L up, 40 times");
    for (i = 0; i < 40; i++) {
        press(KEY_RIGHT, &fr); press(KEY_LEFT, &fl); tick(0); check("F both down");
        tick(0); check("F both held");
        release(KEY_RIGHT, &fr); tick(0); check("F R up, L held");
        release(KEY_LEFT, &fl); tick(0); check("F all up");
        tick(0); check("F idle");
    }

    sprintf(b, "KEYSIM done: %d checks, %d failed, repeats seen by the layer: %d",
            checks, fails, jk_repeats);
    say(b);
    fclose(f);
    key_done();
    return fails ? 1 : 0;
}
