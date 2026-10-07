/* JRDOOM: a Doom-like for the IBM PCjr. Main loop, HUD, screens, sound,
 * demo autopilot and screenshots.
 *
 *   JRDOOM              play
 *   JRDOOM /DEMO:n      autopilot for n frames, log fps to LOG.TXT, exit
 *   JRDOOM /SHOT:a,b,.. write SHOTnn.PPM at those frame numbers
 *   JRDOOM /GOD         take damage but never die
 *   JRDOOM /NOSOUND
 *   JRDOOM /SIZE:n      start with view size n (0 full, 1, 2 smallest)
 *   JRDOOM /LOW /HIGH   low detail (4-pixel columns) or full detail
 *   JRDOOM /PROF        log where each frame's time goes (with /DEMO)
 *   JRDOOM /BENCH       time the renderer and game logic, then exit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "jrdoom.h"

#define KEY_1 0x02
#define KEY_2 0x03
#define KEY_F5 0x3F

static int demo_frames, no_sound, quit, bench, start_size;
static unsigned shot_at[16];
static int nshots, shot_n;
static unsigned long frame;
static unsigned long tp[5], ntics;
extern unsigned long tic_prof[3], rend_prof[5];   /* time per part (PIT ticks), for LOG.TXT */

/* ---- Sound: effect scripts stepped by the 18.2 Hz BIOS tick ------------ */

typedef struct { unsigned v; unsigned char vol; } Step;  /* hz or noise type */
#define END {0, 255}
static const Step s_pistol[] = {{4,15},{4,11},{5,7},{5,3},END};
static const Step s_shotgun[] = {{5,15},{5,14},{6,12},{6,9},{6,6},{6,3},END};
static const Step s_pickup[] = {{880,12},{1320,12},{1760,8},END};
static const Step s_weapon[] = {{440,12},{660,12},{880,12},{1320,12},{1320,6},END};
static const Step s_door[] = {{90,11},{85,11},{80,10},{75,10},{70,8},{65,6},END};
static const Step s_sight[] = {{180,12},{220,12},{260,10},{200,8},END};
static const Step s_zdie[] = {{400,13},{300,12},{220,11},{160,9},{120,7},{90,5},END};
static const Step s_idie[] = {{300,13},{250,13},{200,12},{150,10},{110,8},{80,6},{60,4},END};
static const Step s_hurt[] = {{160,14},{130,12},{110,10},END};
static const Step s_fireball[] = {{6,9},{6,11},{6,8},END};
static const Step s_boom[] = {{6,15},{6,12},{6,9},{6,6},{6,3},END};
static const Step s_claw[] = {{4,12},{4,6},END};
static const Step s_noway[] = {{100,10},{100,10},END};
static const Step s_exit[] = {{523,12},{659,12},{784,12},{1047,12},{1047,8},END};
static const Step s_zfire[] = {{4,12},{5,8},{5,4},END};
static const Step s_pain[] = {{250,11},{200,9},END};
/* channel: 0-2 tone voices, 3 noise */
static const struct { const Step *s; int ch; } sfx_tab[] = {
	{s_pistol,3},{s_shotgun,3},{s_pickup,2},{s_weapon,2},{s_door,2},{s_sight,1},
	{s_zdie,1},{s_idie,1},{s_hurt,0},{s_fireball,3},{s_boom,3},{s_claw,3},
	{s_noway,0},{s_exit,2},{s_zfire,3},{s_pain,1}
};
static const Step *volatile chan[4];
static void (__interrupt __far *old1c)(void);

static void __interrupt __far snd_isr(void)
{
	int c;
	for (c = 0; c < 4; c++) {
		const Step *s = chan[c];
		if (!s) continue;
		if (s->vol == 255) {
			if (c == 3) snd_noise(0, 0); else snd_tone(c, 0, 0);
			chan[c] = 0;
		} else {
			if (c == 3) snd_noise(s->v, s->vol); else snd_tone(c, s->v, s->vol);
			chan[c] = s + 1;
		}
	}
	_chain_intr(old1c);
}

void sfx(int id)
{
	if (no_sound) return;
	_disable();
	chan[sfx_tab[id].ch] = sfx_tab[id].s;
	_enable();
}

static void sound_start(void)
{
	if (no_sound) return;
	snd_init();
	old1c = _dos_getvect(0x1C);
	_dos_setvect(0x1C, snd_isr);
}

static void sound_stop(void)
{
	if (no_sound) return;
	_dos_setvect(0x1C, old1c);
	snd_done();
}

/* ---- Big digits and letters (5x7, drawn 1 pixel wide x 2 rows tall) ---- */

static const char *big_chars = "0123456789%JRDOM";
static const unsigned char big_font[][7] = {
	{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,6,8,16,31},{31,2,4,2,1,17,14},
	{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},
	{14,17,17,14,17,17,14},{14,17,17,15,1,2,12},{24,25,2,4,8,19,3},
	{7,2,2,2,2,18,12},{30,17,17,30,20,18,17},{28,18,17,17,17,18,28},{14,17,17,17,17,17,14},
	{17,27,21,21,17,17,17}
};

static void big_text(int x, int y, const char *s, int pw, int ph, unsigned char c, unsigned char c2)
{
	for (; *s; s++, x += 6 * pw) {
		const char *p = strchr(big_chars, *s);
		int r, b;
		if (!p || *s == 0) continue;
		for (r = 0; r < 7; r++)
			for (b = 0; b < 5; b++)
				if (big_font[p - big_chars][r] & (16 >> b)) {
					if (pw == 1 && ph == 2) {   /* HUD digits: straight into the buffer */
						unsigned char far *q = gfx_back + (unsigned)(y + 2 * r) * 160 + x + b;
						q[0] = q[160] = r < 3 ? c : c2;
					} else
						gfx_fill_rect(x + b * pw, y + r * ph, pw, ph, r < 3 ? c : c2);
				}
	}
}

/* ---- HUD ---------------------------------------------------------------- */

static void panel(int x, int w)
{
	gfx_fill_rect(x, HUD_Y + 13, w, 43, 8);
	gfx_hline(x, x + w - 1, HUD_Y + 13, 7);
	gfx_vline(x, HUD_Y + 13, 199, 7);
	gfx_vline(x + w - 1, HUD_Y + 13, 199, 0);
}

/* The status bar. It is drawn into gfx_back and copied to both video
 * pages, and only the parts that changed since the last call are redrawn. */
static int hud_full = 1;

static void hud_rows(int *lo, int *hi, int y0, int y1)
{
	if (y0 < *lo) *lo = y0;
	if (y1 > *hi) *hi = y1;
}

static void draw_hud(void)
{
	static int l_ammo = -1, l_health = -1, l_msg = -1, l_arms = -1;
	static const unsigned char *l_face;
	char buf[8];
	const unsigned char *face;
	int ammo = pl.weapon ? pl.shells : pl.bullets;
	int arms = pl.weapon | pl.has_shotgun << 1 | pl.has_key << 2;
	int shown_msg = msg_t ? 1 : pl.dead ? 2 : 0;
	int lo = 200, hi = -1;
	if (pl.dead) face = pic_face_dead;
	else if (pl.ouch) face = pic_face_ouch;
	else if (pl.health < 30) face = pic_face_bad;
	else if (pl.health < 65) face = pic_face_hurt;
	else face = pic_face_ok;

	if (hud_full) {
		gfx_fill_rect(0, HUD_Y, 160, 200 - HUD_Y, 8);
		gfx_hline(0, 159, HUD_Y, 7);
		panel(0, 36);
		gfx_text(2, HUD_Y + 40, "AMMO", 7);
		panel(36, 52);
		gfx_text(38, HUD_Y + 40, "HEALTH", 7);
		panel(88, 24);
		panel(112, 48);
		gfx_text(114, HUD_Y + 40, "ARMS", 7);
		l_ammo = l_health = l_msg = l_arms = -1; l_face = 0;
		hud_rows(&lo, &hi, HUD_Y, 199);
	}
	if (shown_msg != l_msg || shown_msg == 1) {
		gfx_fill_rect(0, HUD_Y + 1, 160, 11, 0);
		if (msg_t) gfx_text(0, HUD_Y + 3, msg, 14);
		else if (pl.dead) gfx_text(0, HUD_Y + 3, "DEAD. PRESS SPACE", 12);
		l_msg = shown_msg;
		hud_rows(&lo, &hi, HUD_Y + 1, HUD_Y + 11);
	}
	if (ammo != l_ammo) {
		gfx_fill_rect(1, HUD_Y + 18, 34, 14, 8);
		sprintf(buf, "%d", ammo);
		big_text(18 - (int)strlen(buf) * 3, HUD_Y + 18, buf, 1, 2, 12, 4);
		l_ammo = ammo;
		hud_rows(&lo, &hi, HUD_Y + 18, HUD_Y + 31);
	}
	if (pl.health != l_health) {
		gfx_fill_rect(37, HUD_Y + 18, 50, 14, 8);
		sprintf(buf, "%d%%", pl.health);
		big_text(62 - (int)strlen(buf) * 3, HUD_Y + 18, buf, 1, 2, 12, 4);
		l_health = pl.health;
		hud_rows(&lo, &hi, HUD_Y + 18, HUD_Y + 31);
	}
	if (face != l_face) {
		gfx_blit(93, HUD_Y + 20, PIC_FACE_OK_W, PIC_FACE_OK_H, face, 255);
		l_face = face;
		hud_rows(&lo, &hi, HUD_Y + 20, HUD_Y + 37);
	}
	if (arms != l_arms) {
		gfx_fill_rect(113, HUD_Y + 14, 46, 24, 8);
		gfx_text(116, HUD_Y + 17, "1", pl.weapon == 0 ? 14 : 15);
		gfx_text(126, HUD_Y + 17, "2", !pl.has_shotgun ? 0 : pl.weapon == 1 ? 14 : 15);
		if (pl.has_key) gfx_blit(148, HUD_Y + 30, PIC_KEYICON_W, PIC_KEYICON_H, pic_keyicon, 255);
		l_arms = arms;
		hud_rows(&lo, &hi, HUD_Y + 14, HUD_Y + 37);
	}
	if (hi >= 0) {
		gfx_present_rows(lo, hi);
		fx_sync_rows(lo, hi);   /* both video pages */
	}
	hud_full = 0;
	hud_dirty = 0;
}

/* ---- Screens ------------------------------------------------------------ */

static int wait_key(unsigned ms)
{
	unsigned long t = tmr_ms();
	int k;
	for (;;) {
		if (demo_frames && tmr_ms() - t > ms) return 0;
		for (k = 1; k < 128; k++)
			if (key_down[k] && k != KEY_CTRL && k != KEY_ALT && k != KEY_LSHIFT) {
				while (key_down[k]) ;
				return k;
			}
	}
}

static void shot(void);
static int guns_ready;

static void shot_title(void)
{
	gfx_screenshot("TITLE.PPM");
}

static void title_screen(void)
{
	int y;
	gfx_clear(0);
	for (y = 0; y < 4; y++) gfx_hline(0, 159, 60 + y, y & 1 ? 4 : 12);
	big_text(10, 12, "JRDOOM", 4, 6, 4, 4);       /* shadow */
	big_text(8, 9, "JRDOOM", 4, 6, 14, 12);
	gfx_text(32, 68, "PCJR EDITION", 15);
	gfx_text(4, 86, "ARROWS  MOVE/TURN", 7);
	gfx_text(4, 96, "CTRL    FIRE", 7);
	gfx_text(4, 106, "SPACE   OPEN/USE", 7);
	gfx_text(4, 116, "ALT     STRAFE", 7);
	gfx_text(4, 126, "1 2     WEAPONS", 7);
	gfx_text(4, 136, "- =     VIEW SIZE", 7);
	gfx_text(4, 146, "F5      DETAIL", 7);
	gfx_text(4, 156, "ESC     QUIT", 7);
	gfx_present();
	fx_sync_rows(0, 199);
	if (!guns_ready) {   /* build the weapon sprites while the title is up */
		gfx_text(32, 176, "LOADING...", 8);
		gfx_present_rows(176, 183);
		compile_guns();
		guns_ready = 1;
	}
	gfx_fill_rect(0, 176, 160, 8, 0);
	gfx_text(20, 176, "PRESS ENTER", 12);
	gfx_present_rows(176, 183);
	fx_sync_rows(176, 183);
	{
		int i;
		for (i = 0; i < nshots; i++) if (shot_at[i] == 0) shot_title();
	}
	if (wait_key(1500) == KEY_ESC) quit = 1;
}

static void intermission(unsigned long ms)
{
	char buf[24];
	unsigned long s = ms / 1000;
	gfx_clear(0);
	gfx_fill_rect(0, 0, 160, 40, 4);
	gfx_text(24, 8, "LEVEL", 15);
	gfx_text(24, 22, "COMPLETE!", 14);
	sprintf(buf, "KILLS  %3d%%", total_kills ? kills * 100 / total_kills : 100);
	gfx_text(8, 60, buf, 12);
	sprintf(buf, "ITEMS  %3d%%", total_items ? items * 100 / total_items : 100);
	gfx_text(8, 76, buf, 12);
	sprintf(buf, "TIME  %2lu:%02lu", s / 60, s % 60);
	gfx_text(8, 92, buf, 12);
	gfx_text(8, 140, "YOU ESCAPED THE", 7);
	gfx_text(8, 152, "BASE. FOR NOW...", 7);
	gfx_text(20, 180, "PRESS ENTER", 14);
	gfx_present();
	fx_sync_rows(0, 199);
	dbg_log("level complete: kills %d/%d items %d/%d time %lus", kills, total_kills, items, total_items, s);
	if (nshots) { shot(); }
	wait_key(1500);
}

static void shot(void)
{
	char name[16];
	view_to_back();
	sprintf(name, "SHOT%02d.PPM", ++shot_n);
	gfx_screenshot(name);
	dbg_log("screenshot %s at frame %lu", name, frame);
}

/* ---- Demo autopilot ----------------------------------------------------- */

static int way_i, ap_target = -1;

static int ang_diff(int a, int b) { return ((a - b + 512) & 1023) - 512; }

static void autopilot(Input *in)
{
	int i, n, best = -1, bd = 9 * TILE, d, a, fx, fy;
	const unsigned char *w = level_waypoints(level_num, &n);
	unsigned char c;
	static int lx, ly, unstick;
	memset(in, 0, sizeof *in);
	if (pl.dead) return;
	if ((gametic % 70) == 0) {   /* no progress in 2 s: back off and turn */
		if (approx_dist(pl.x - lx, pl.y - ly) < 40 && ap_target < 0) unstick = 25;
		lx = pl.x; ly = pl.y;
	}
	if (unstick) { unstick--; in->back = 1; in->right = 1; return; }
	/* pick a target every 8 tics (sight checks are not cheap) */
	if ((gametic & 7) == 0) {
		for (i = 0; i < nobjs; i++) {
			Obj *o = &objs[i];
			if ((o->type != OT_ZOMBIE && o->type != OT_IMP) || o->state >= S_DIE || o->state == S_IDLE)
				continue;
			d = approx_dist(o->x - pl.x, o->y - pl.y);
			if (d < bd && line_of_sight(pl.x, pl.y, o->x, o->y)) { bd = d; best = i; }
		}
		ap_target = best;
	}
	best = ap_target;
	if (best >= 0 && objs[best].state >= S_DIE) best = ap_target = -1;
	if (best >= 0) {
		a = ang_diff(ang_to(objs[best].x - pl.x, objs[best].y - pl.y), pl.ang);
		if (a < -10) in->left = 1;
		else if (a > 10) in->right = 1;
		if (a > -24 && a < 24) in->fire = 1;
		return;
	}
	if (way_i < n) {
		int wx = w[way_i * 2] * TILE + TILE / 2, wy = w[way_i * 2 + 1] * TILE + TILE / 2;
		if (approx_dist(wx - pl.x, wy - pl.y) < 90) { way_i++; return; }
		a = ang_diff(ang_to(wx - pl.x, wy - pl.y), pl.ang);
		if (a < -8) in->left = 1;
		else if (a > 8) in->right = 1;
		if (a > -80 && a < 80) in->fwd = 1;
	} else {
		a = ang_diff(0, pl.ang); /* face east: the exit switch */
		if (a < -8) in->left = 1;
		else if (a > 8) in->right = 1;
		else in->use = (int)(gametic & 1);
	}
	fx = pl.x + (int)(((long)cos_tab(pl.ang) * 200) >> 14);
	fy = pl.y + (int)(((long)sin_tab(pl.ang) * 200) >> 14);
	c = map[(fy >> 8) * MAPW + (fx >> 8)];
	if ((c & CELL_DOOR) && doors[c & 0x3F].state == 0 && (gametic & 3) == 0) in->use = 1;
}

/* ---- Main loop ---------------------------------------------------------- */

static void new_game(void)
{
	memset(&pl, 0, sizeof pl);
	pl.health = 100;
	pl.bullets = 50;
	level_num = 0;
}

static void read_keys(Input *in)
{
	in->fwd = key_down[KEY_UP];
	in->back = key_down[KEY_DOWN];
	in->left = key_down[KEY_LEFT];
	in->right = key_down[KEY_RIGHT];
	in->fire = key_down[KEY_CTRL];
	in->use = key_down[KEY_SPACE];
	in->strafe = key_down[KEY_ALT];
	in->w1 = key_down[KEY_1];
	in->w2 = key_down[KEY_2];
}

static void set_flash(void)
{
	static int cur = 0;
	int want = flash_red ? 4 : flash_bonus ? 6 : pl.dead ? 4 : 0;
	if (want != cur) {
		static unsigned char pal[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
		pal[0] = (unsigned char)want;
		gfx_pal[0] = (unsigned char)want;   /* so screenshots show it */
		fx_pal_set(pal);                    /* applied at the next retrace */
		cur = want;
	}
}

static void play_level(void)
{
	Input in;
	unsigned long t_last, t_start, t_log, acc = 0, tk_last;
	unsigned long f_log = frame;
	int dead_t = 0;

	load_level(level_num);
	pl.dead = 0;
	if (pl.health <= 0) pl.health = 100;
	way_i = 0;
	gfx_clear(0);
	draw_border();
	hud_full = 1;
	draw_hud();
	t_last = t_start = t_log = tmr_ms();
	tk_last = tmr_ticks();
	while (!quit && !level_done) {
		unsigned long now = tmr_ticks();   /* 1.19 MHz PIT counts: cheap */
		int tics = 0;
		acc += now - tk_last;
		tk_last = now;
		while (acc >= 34091UL) { acc -= 34091UL; tics++; }   /* 35 Hz */
		if (tics > 6) { tics = 6; acc = 0; }
		if (tics < 1) tics = 1;   /* always advance */
		ntics += tics;
		while (tics--) {
			if (demo_frames) {
				unsigned long ta = profiling ? tmr_ticks() : 0;
				autopilot(&in);
				if (profiling) tp[4] += tmr_ticks() - ta;
			} else read_keys(&in);
			run_tic(&in);
			if (flash_red) flash_red--;
			if (flash_bonus) flash_bonus--;
			if (pl.dead) dead_t++;
		}
		if (profiling) { tp[0] += tmr_ticks() - now; now = tmr_ticks(); }
		fx_begin();             /* the page we draw on is now hidden */
		if (profiling) { tp[1] += tmr_ticks() - now; now = tmr_ticks(); }
		render_view();          /* straight into it */
		view_to_screen();
		if (profiling) { tp[2] += tmr_ticks() - now; now = tmr_ticks(); }
		set_flash();
		if (hud_dirty) draw_hud();
		if (profiling) tp[3] += tmr_ticks() - now;
		frame++;
		{
			int i;
			for (i = 0; i < nshots; i++)
				if (shot_at[i] == frame) { draw_hud(); shot(); }
		}
		fx_flip();              /* shown at the next retrace */
		if ((frame & 63) == 0) {
			unsigned long n = tmr_ms();
			unsigned long fps10 = (frame - f_log) * 10000UL / (n - t_log + 1);
			if (!profiling) dbg_log("frame %lu: %lu.%lu fps (last %lu frames)", frame, fps10 / 10, fps10 % 10, frame - f_log);
			else {
			dbg_log("frame %lu: %lu.%lu fps (last %lu frames); ms: logic %lu (autopilot %lu) wait %lu render %lu hud %lu; %lu tics",
				frame, fps10 / 10, fps10 % 10, frame - f_log,
				TMR_TICKS_MS(tp[0]), TMR_TICKS_MS(tp[4]), TMR_TICKS_MS(tp[1]), TMR_TICKS_MS(tp[2]), TMR_TICKS_MS(tp[3]), ntics);
			dbg_log("  tics: player %lu doors %lu objs %lu", TMR_TICKS_MS(tic_prof[0]), TMR_TICKS_MS(tic_prof[1]), TMR_TICKS_MS(tic_prof[2]));
			tic_prof[0] = tic_prof[1] = tic_prof[2] = 0;
			dbg_log("  render: cast %lu clear %lu walls %lu things %lu weapon %lu", TMR_TICKS_MS(rend_prof[0]), TMR_TICKS_MS(rend_prof[1]),
				TMR_TICKS_MS(rend_prof[2]), TMR_TICKS_MS(rend_prof[3]), TMR_TICKS_MS(rend_prof[4]));
			memset(rend_prof, 0, sizeof rend_prof);
			}
			tp[0] = tp[1] = tp[2] = tp[3] = tp[4] = 0; ntics = 0;

			f_log = frame; t_log = n;
		}
		if (key_down[KEY_ESC]) quit = 1;
		{   /* - and = change the view size, like Doom */
			static int held;
			int k = key_down[KEY_MINUS] ? 1 : key_down[KEY_PLUS] ? -1 : 0;
			if (k && !held && view_size + k >= 0 && view_size + k <= 2) {
				set_view_size(view_size + k);
				draw_border();
			}
			held = k;
		}
		{   /* F5 toggles low detail (4-pixel columns), like Doom */
			static int held5;
			if (key_down[KEY_F5] && !held5) { low_detail ^= 1; set_view_size(view_size); }
			held5 = key_down[KEY_F5];
		}
		if (demo_frames && frame >= (unsigned long)demo_frames) quit = 1;
		if (pl.dead && dead_t > 35) {
			if (demo_frames) quit = 1;
			else if (key_down[KEY_SPACE]) {
				while (key_down[KEY_SPACE]) ;
				pl.health = 100; pl.bullets = 50; pl.shells = 0;
				pl.weapon = 0; pl.has_shotgun = 0;
				load_level(level_num);
				pl.dead = 0; dead_t = 0;
				hud_dirty = 1;
			}
		}
	}
	flash_red = flash_bonus = 0;
	set_flash();
	if (level_done) {
		intermission(tmr_ms() - t_start);
		level_num++;
		if (level_num >= num_levels()) level_num = 0;
	}
}

static void parse_args(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (*a == '/' || *a == '-') a++;
		strupr(a);
		if (!strncmp(a, "DEMO", 4)) demo_frames = a[4] == ':' ? atoi(a + 5) : 300;
		else if (!strncmp(a, "SHOT:", 5)) {
			char *p = a + 5;
			while (*p && nshots < 16) {
				shot_at[nshots++] = (unsigned)atoi(p);
				while (*p && *p != ',') p++;
				if (*p) p++;
			}
		} else if (!strcmp(a, "GOD")) god = 1;
		else if (!strcmp(a, "BENCH")) bench = 1;
		else if (!strncmp(a, "SIZE:", 5)) start_size = atoi(a + 5) % 3;
		else if (!strcmp(a, "LOW")) low_detail = 1;
		else if (!strcmp(a, "PROF")) profiling = 1;
		else if (!strcmp(a, "HIGH")) low_detail = 0;
		else if (!strcmp(a, "NOSOUND")) no_sound = 1;
	}
}

int main(int argc, char **argv)
{
	unsigned long t0;
	parse_args(argc, argv);
	if (fx_init(0)) {
		printf("JRDOOM: can't set 160x200x16 graphics\n");
		return 1;
	}
	key_init();
	tmr_init();
	sound_start();
	render_init();
	set_view_size(start_size);
	if (demo_frames) god = 1;
	dbg_log("JRDOOM start, demo %d", demo_frames);
	if (bench) {
		static const int views[][3] = { {3, 3, 0}, {3, 3, 512}, {12, 4, 0}, {25, 7, 256}, {23, 25, 0} };
		int v, sz;
		compile_guns();
		guns_ready = 1;
		new_game();
		load_level(0);
		dbg_log("loaded");
		for (sz = 0; sz < 6; sz++) {
			low_detail = sz >= 3;
			set_view_size(sz % 3);
			gfx_clear(0);
			draw_border();
			dbg_log("view size %d: %dx%d, low detail %d", sz % 3, vcols, vh, low_detail);
			for (v = 0; v < 5; v++) {
				pl.x = views[v][0] * TILE + 128; pl.y = views[v][1] * TILE + 128; pl.ang = views[v][2];
				bench_render(20);
				render_view(); view_to_screen(); draw_hud(); shot();
			}
		}
		set_view_size(0);
		for (v = 0; v < NUM_SPRITES; v += 5) { sprite_gallery(v); shot(); }
		{   /* game logic: everyone awake, 70 tics */
			Input in;
			unsigned long t;
			int i, awake = 0;
			load_level(0);
			pl.x = 24 * TILE + 128; pl.y = 7 * TILE + 128;
			for (i = 0; i < nobjs; i++)
				if (objs[i].type == OT_ZOMBIE || objs[i].type == OT_IMP) { objs[i].state = S_CHASE; awake++; }
			memset(&in, 0, sizeof in);
			t = tmr_ms();
			for (i = 0; i < 70; i++) run_tic(&in);
			dbg_log("logic: 70 tics, %d monsters chasing: %lu ms", awake, tmr_ms() - t);
			t = tmr_ms();
			for (i = 0; i < 70; i++) autopilot(&in);
			dbg_log("autopilot: 70 tics: %lu ms", tmr_ms() - t);
		}
		quit = 1;
	}
	if (!quit) title_screen();   /* the demo shows it for 1.5 s too */
	new_game();
	t0 = tmr_ms();
	while (!quit) {
		play_level();
		if (demo_frames && !quit) quit = 1;
		if (!demo_frames && !quit) title_screen();
	}
	{
		unsigned long ms = tmr_ms() - t0;
		unsigned long fps10 = frame * 10000UL / (ms + 1);
		dbg_log("JRDOOM end: %lu frames in %lu ms = %lu.%lu fps", frame, ms, fps10 / 10, fps10 % 10);
	}
	sound_stop();
	gfx_done();
	key_done();
	tmr_done();
	return 0;
}
