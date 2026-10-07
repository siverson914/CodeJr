/* JRDOOM game logic: player, doors, monsters, pickups. Runs at 35 tics/s. */
#include <string.h>
#include <stdlib.h>
#include "jrdoom.h"

unsigned char map[MAPH * MAPW];
Door doors[32];
int ndoors;
Obj objs[MAX_OBJS];
int nobjs;
Player pl;
unsigned long gametic;
int kills, total_kills, items, total_items;
int level_done, level_num;
int god;
int profiling;
char msg[24];
int msg_t;
int hud_dirty = 1;
int flash_red, flash_bonus, muzzle_light;

extern const unsigned char rnd_tab[256];
static unsigned char rnd_i;
int rnd(void) { return rnd_tab[rnd_i++]; }

#define PL_R     64      /* player radius (quarter tile) */
#define MON_R    80
#define WALK     20      /* player speed per tic */
#define TURN     14      /* player turn per tic (1024 = full circle) */

void set_msg(const char *s)
{
	strncpy(msg, s, sizeof msg - 1);
	msg_t = 105;
	hud_dirty = 1;
}

Obj *spawn_obj(int type, int x, int y)
{
	Obj *o = 0;
	int i;
	for (i = 0; i < nobjs; i++)          /* reuse a free slot */
		if (objs[i].type == OT_NONE) { o = &objs[i]; break; }
	if (!o) {
		if (nobjs >= MAX_OBJS) return 0;
		o = &objs[nobjs++];
	}
	memset(o, 0, sizeof *o);
	o->type = (unsigned char)type;
	o->x = x; o->y = y;
	o->state = S_IDLE;
	o->hp = type == OT_ZOMBIE ? 20 : type == OT_IMP ? 60 : 0;
	o->cool = 20 + (rnd() & 31);
	o->timer = rnd() & 7;
	if (type == OT_LAMP || type == OT_BARREL || type == OT_COLUMN)
		blockmap[(y >> 8) * MAPW + (x >> 8)] = 1;
	return o;
}

/* ---- Geometry helpers --------------------------------------------------- */

extern const unsigned char atan_tab[65];

int ang_to(int dx, int dy)
{
	unsigned ax = (unsigned)abs(dx), ay = (unsigned)abs(dy);
	int a;
	if (!ax && !ay) return 0;
	while (ax > 1023 || ay > 1023) { ax >>= 1; ay >>= 1; }
	if (ax >= ay) a = atan_tab[(ay << 6) / ax];
	else a = 256 - atan_tab[(ax << 6) / ay];
	if (dx < 0) a = 512 - a;
	if (dy < 0) a = -a;
	return a & 1023;
}

int approx_dist(int dx, int dy)
{
	unsigned ax = (unsigned)abs(dx), ay = (unsigned)abs(dy);
	return (int)(ax > ay ? ax + (ay >> 1) - (ay >> 3) : ay + (ax >> 1) - (ax >> 3));
}

/* A map cell that stops movement (and sight). Open doors don't. */
static unsigned char mons[MAX_OBJS];   /* indexes of the monsters */
static int nmons;
static int nfire;                      /* live fireballs */

/* grid: bit 0 = wall or closed door (stops movement and sight),
 *       bit 1 = a lamp, barrel or column stands here (stops monsters). */
unsigned char grid[MAPW * MAPH];

void build_grid(void)
{
	int i;
	nmons = 0;
	nfire = 0;
	for (i = 0; i < nobjs; i++)
		if (objs[i].type == OT_ZOMBIE || objs[i].type == OT_IMP) mons[nmons++] = (unsigned char)i;
	for (i = 0; i < MAPW * MAPH; i++)
		grid[i] = (unsigned char)((map[i] ? 1 : 0) | (blockmap[i] ? 2 : 0));
}

int blocked_cell(int mx, int my)
{
	return grid[my * MAPW + mx] & 1;
}

/* Any of the four corners of a box of radius r touching grid bits `bits`. */
static int box_hits(int x, int y, int r, unsigned char bits)
{
	int t = ((y - r) >> 8) * MAPW, b = ((y + r) >> 8) * MAPW;
	int l = (x - r) >> 8, rr = (x + r) >> 8;
	return (grid[t + l] | grid[t + rr] | grid[b + l] | grid[b + rr]) & bits;
}
#define solid_box(x, y, r) box_hits(x, y, r, 1)

static int is_monster(const Obj *o)
{
	return (o->type == OT_ZOMBIE || o->type == OT_IMP) && o->state < S_DIE;
}

/* Lamps, barrels and columns sit in the middle of their tile: blockmap
 * marks those tiles, and they block a box 0.3 tiles around the center. */
unsigned char blockmap[MAPW * MAPH];

static int decor_hit(int x, int y, int r)
{
	int tx, ty;
	for (ty = (y - r) >> 8; ty <= (y + r) >> 8; ty++)
		for (tx = (x - r) >> 8; tx <= (x + r) >> 8; tx++)
			if (blockmap[ty * MAPW + tx] &&
			    abs(x - (tx * TILE + 128)) < r + 77 && abs(y - (ty * TILE + 128)) < r + 77)
				return 1;
	return 0;
}

/* Solid for the player: walls, closed doors, decorations (by their size). */
static int solid_at(int x, int y, int r)
{
	return box_hits(x, y, r, 1) || (box_hits(x, y, r, 2) && decor_hit(x, y, r));
}

/* The player bumping into a live monster (only if the move gets closer). */

static int monster_blocks(int ox, int oy, int x, int y)
{
	int i;
	for (i = 0; i < nmons; i++) {
		const Obj *o = &objs[mons[i]];
		if (o->state >= S_DIE) continue;
		if (abs(o->x - x) < PL_R + 72 && abs(o->y - y) < PL_R + 72 &&
		    approx_dist(o->x - x, o->y - y) < approx_dist(o->x - ox, o->y - oy))
			return 1;
	}
	return 0;
}

int line_of_sight(int x0, int y0, int x1, int y1)
{
	int dx = x1 - x0, dy = y1 - y0, n, sx, sy, i;
	n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) / 64 + 1;
	sx = dx / n; sy = dy / n;
	for (i = 0; i < n; i++) {
		x0 += sx; y0 += sy;
		if (blocked_cell(x0 >> 8, y0 >> 8)) return 0;
	}
	return 1;
}

/* ---- Player ------------------------------------------------------------- */

static void hurt_player(int dmg)
{
	if (pl.dead) return;
	pl.health -= dmg;
	if (god && pl.health < 10) pl.health = 10;
	if (pl.health <= 0) { pl.health = 0; pl.dead = 1; }
	pl.ouch = 20;
	flash_red = dmg > 20 ? 10 : dmg / 2 + 2;
	hud_dirty = 1;
	sfx(SFX_HURT);
}

static void damage_obj(Obj *o, int dmg)
{
	o->hp -= dmg;
	if (o->hp <= 0) {
		o->state = S_DIE;
		o->timer = 6;
		kills++;
		sfx(o->type == OT_ZOMBIE ? SFX_ZDIE : SFX_IDIE);
		if (o->type == OT_ZOMBIE) {
			Obj *c = spawn_obj(OT_CLIP, o->x + 40, o->y + 30);
			if (c) c->hp = 1; /* dropped: not counted as an item */
		}
		return;
	}
	if (o->state == S_IDLE) o->state = S_CHASE;
	if (rnd() < 140) { o->state = S_PAIN; o->timer = 3; sfx(SFX_PAIN); }
}

/* Hitscan through screen column `col` using last frame's projection. */
static void hitscan(int col, int dmg)
{
	int i, best = -1, bd = 32767;
	for (i = 0; i < nobjs; i++) {
		Obj *o = &objs[i];
		if (!is_monster(o) || !o->seen) continue;
		if (abs(col - o->scol) > o->shw) continue;
		if (o->sh <= wallh_p[col]) continue;     /* behind the wall */
		if (o->sdepth < bd) { bd = o->sdepth; best = i; }
	}
	if (best >= 0) damage_obj(&objs[best], dmg);
}

static void alert_monsters(void)
{
	int i;
	for (i = 0; i < nobjs; i++) {
		Obj *o = &objs[i];
		if (is_monster(o) && o->state == S_IDLE &&
		    approx_dist(o->x - pl.x, o->y - pl.y) < 7 * TILE) {
			o->state = S_CHASE;
			o->cool = 15 + (rnd() & 15);
		}
	}
}

static void fire_weapon(void)
{
	int i;
	if (pl.weapon == 1 && pl.shells <= 0) pl.weapon = 0;
	if (pl.weapon == 0 && pl.bullets <= 0) {
		if (pl.has_shotgun && pl.shells > 0) pl.weapon = 1;
		else { pl.cool = 20; sfx(SFX_NOWAY); return; }
	}
	if (pl.weapon == 0) {
		pl.bullets--;
		pl.fire_t = 5; pl.cool = 11;
		sfx(SFX_PISTOL);
		hitscan(vcols / 2 + (rnd() % 3) - 1, 5 * (rnd() % 3 + 1));
	} else {
		pl.shells--;
		pl.fire_t = 7; pl.cool = 30;
		sfx(SFX_SHOTGUN);
		for (i = 0; i < 7; i++)
			hitscan(vcols / 2 + (rnd() % 11) - 5, 5 * (rnd() % 3 + 1));
	}
	muzzle_light = 3;
	hud_dirty = 1;
	alert_monsters();
}

static void use_action(void)
{
	int fx = pl.x + (int)(((long)cos_tab(pl.ang) * 220) >> 14);
	int fy = pl.y + (int)(((long)sin_tab(pl.ang) * 220) >> 14);
	unsigned char c = map[(fy >> 8) * MAPW + (fx >> 8)];
	if (c & CELL_DOOR) {
		Door *d = &doors[c & 0x3F];
		if (d->locked && !pl.has_key) { set_msg("NEED THE RED KEY"); sfx(SFX_NOWAY); return; }
		if (d->state == 0 || d->state == 3) { d->state = 1; sfx(SFX_DOOR); }
	} else if (c == M_EXIT) {
		level_done = 1;
		sfx(SFX_EXIT);
	}
}

static void try_move(int mx, int my)
{
	int nx = pl.x + mx, ny = pl.y + my;
	if (mx && !solid_at(nx, pl.y, PL_R) && !monster_blocks(pl.x, pl.y, nx, pl.y))
		pl.x = nx;
	if (my && !solid_at(pl.x, ny, PL_R) && !monster_blocks(pl.x, pl.y, pl.x, ny))
		pl.y = ny;
}

static void player_tic(const Input *in)
{
	int c, s, mx = 0, my = 0;
	if (pl.dead) return;
	if (!in->strafe) {
		if (in->left) pl.ang -= TURN;
		if (in->right) pl.ang += TURN;
		pl.ang &= 1023;
	}
	c = cos_tab(pl.ang); s = sin_tab(pl.ang);
	if (in->fwd)  { mx += (int)(((long)c * WALK) >> 14); my += (int)(((long)s * WALK) >> 14); }
	if (in->back) { mx -= (int)(((long)c * WALK) >> 14); my -= (int)(((long)s * WALK) >> 14); }
	if (in->strafe) {
		if (in->left)  { mx += (int)(((long)s * WALK) >> 14); my -= (int)(((long)c * WALK) >> 14); }
		if (in->right) { mx -= (int)(((long)s * WALK) >> 14); my += (int)(((long)c * WALK) >> 14); }
	}
	pl.moving = mx || my;
	if (pl.moving) { try_move(mx, my); pl.bob = (pl.bob + 40) & 1023; }

	if (in->w1 && pl.weapon != 0) { pl.weapon = 0; hud_dirty = 1; }
	if (in->w2 && pl.has_shotgun && pl.weapon != 1) { pl.weapon = 1; hud_dirty = 1; }
	if (pl.fire_t) pl.fire_t--;
	if (pl.cool) pl.cool--;
	else if (in->fire) fire_weapon();
	if (in->use) use_action();
	if (pl.ouch && !--pl.ouch) hud_dirty = 1;
}

/* ---- Doors -------------------------------------------------------------- */

static int door_occupied(const Door *d)
{
	int i;
	if ((pl.x + PL_R) >> 8 >= d->x && (pl.x - PL_R) >> 8 <= d->x &&
	    (pl.y + PL_R) >> 8 >= d->y && (pl.y - PL_R) >> 8 <= d->y) return 1;
	for (i = 0; i < nobjs; i++)
		if (is_monster(&objs[i]) && objs[i].x >> 8 == d->x && objs[i].y >> 8 == d->y) return 1;
	return 0;
}

static void doors_tic(void)
{
	int i;
	for (i = 0; i < ndoors; i++) {
		Door *d = &doors[i];
		if (d->state == 0) continue;
		grid[d->y * MAPW + d->x] = (unsigned char)(d->open < 200);
		switch (d->state) {
		case 1:
			d->open += 12;
			if (d->open >= 256) { d->open = 256; d->state = 2; d->timer = 160; }
			break;
		case 2:
			if (--d->timer <= 0) {
				if (door_occupied(d)) d->timer = 35;
				else { d->state = 3; sfx(SFX_DOOR); }
			}
			break;
		case 3:
			if (door_occupied(d)) { d->state = 1; break; }
			d->open -= 12;
			if (d->open <= 0) { d->open = 0; d->state = 0; }
			break;
		}
	}
}

/* ---- Monsters ----------------------------------------------------------- */

/* Chase: head for the player, re-aiming every few tics; when stuck, open
 * a door or wander in a random direction for a while. */
static void monster_move(Obj *o, int speed)
{
	int a, nx, ny, moved = 0;
	if (--o->timer <= 0) {
		a = ang_to(pl.x - o->x, pl.y - o->y);
		o->dx = (int)(((long)cos_tab(a) * speed) >> 14);
		o->dy = (int)(((long)sin_tab(a) * speed) >> 14);
		o->timer = 3;
	}
	nx = o->x + o->dx; ny = o->y + o->dy;
	if (approx_dist(pl.x - nx, pl.y - ny) < 150) return;  /* close enough */
	if (!box_hits(nx, o->y, MON_R, 3)) { o->x = nx; moved++; }
	if (!box_hits(o->x, ny, MON_R, 3)) { o->y = ny; moved++; }
	if (!moved) {
		unsigned char c = map[((o->y + o->dy * 4) >> 8) * MAPW + ((o->x + o->dx * 4) >> 8)];
		if ((c & CELL_DOOR) && !doors[c & 0x3F].locked && doors[c & 0x3F].state == 0) {
			doors[c & 0x3F].state = 1;
			sfx(SFX_DOOR);
		} else {
			a = (rnd() & 3) * 256;
			o->dx = (int)(((long)cos_tab(a) * speed) >> 14);
			o->dy = (int)(((long)sin_tab(a) * speed) >> 14);
			o->timer = 5 + (rnd() & 7);
		}
	}
}

static void monster_attack(Obj *o)
{
	int d = approx_dist(pl.x - o->x, pl.y - o->y);
	if (o->type == OT_ZOMBIE) {
		sfx(SFX_ZFIRE);
		if (rnd() < 200 - (d >> 5) && line_of_sight(o->x, o->y, pl.x, pl.y))
			hurt_player(3 * (rnd() % 5 + 1));
	} else if (d < TILE + 96) {
		sfx(SFX_CLAW);
		hurt_player(3 * (rnd() % 8 + 1));
	} else {
		int a = ang_to(pl.x - o->x, pl.y - o->y);
		Obj *f = spawn_obj(OT_FIREBALL, o->x, o->y);
		if (f) {
			f->dx = (int)(((long)cos_tab(a) * 10) >> 14);
			f->dy = (int)(((long)sin_tab(a) * 10) >> 14);
			f->state = S_CHASE;
			f->timer = 300;
			nfire++;
		}
		sfx(SFX_FIREBALL);
	}
}

static void monster_tic(Obj *o, int idx)
{
	int d;
	switch (o->state) {
	case S_IDLE:
		if (((int)gametic + idx) & 14) break;
		d = approx_dist(pl.x - o->x, pl.y - o->y);
		if (d < 16 * TILE && line_of_sight(o->x, o->y, pl.x, pl.y)) {
			o->state = S_CHASE;
			o->timer = 0;
			o->cool = 10 + (rnd() & 15);
			sfx(SFX_SIGHT);
		}
		break;
	case S_CHASE:
		monster_move(o, o->type == OT_IMP ? 14 : 10);
		if (--o->cool <= 0) {
			o->cool = (o->type == OT_IMP ? 22 : 17) + (rnd() & 31);
			if (!pl.dead && line_of_sight(o->x, o->y, pl.x, pl.y)) {
				o->state = S_ATTACK;
				o->timer = 8;
			}
		}
		break;
	case S_ATTACK:
		if (--o->timer == 3) monster_attack(o);
		if (o->timer <= 0) o->state = S_CHASE;
		break;
	case S_PAIN:
		if (--o->timer <= 0) { o->state = S_CHASE; o->timer = 0; }
		break;
	case S_DIE:
		if (--o->timer <= 0) o->state = S_DEAD;
		break;
	}
}

static void fireball_tic(Obj *o)
{
	if (o->state == S_BOOM) {
		if (--o->timer <= 0) { o->type = OT_NONE; nfire--; }
		return;
	}
	o->x += o->dx; o->y += o->dy;
	if (--o->timer <= 0 || blocked_cell(o->x >> 8, o->y >> 8)) {
		o->x -= o->dx; o->y -= o->dy;
		o->state = S_BOOM; o->timer = 8;
		sfx(SFX_BOOM);
	} else if (abs(o->x - pl.x) < 90 && abs(o->y - pl.y) < 90) {
		hurt_player(3 * (rnd() % 8 + 1));
		o->state = S_BOOM; o->timer = 8;
		sfx(SFX_BOOM);
	}
}

static int pickup(Obj *o)
{
	const char *m = 0;
	switch (o->type) {
	case OT_MEDIKIT:
		if (pl.health >= 100) return 0;
		pl.health += 25; m = "PICKED UP A MEDIKIT.";
		break;
	case OT_STIM:
		if (pl.health >= 100) return 0;
		pl.health += 10; m = "PICKED UP A STIMPAK";
		break;
	case OT_CLIP:
		if (pl.bullets >= 200) return 0;
		pl.bullets += o->hp ? 5 : 10; m = "PICKED UP A CLIP.";
		break;
	case OT_SHELLS:
		if (pl.shells >= 50) return 0;
		pl.shells += 8; m = "PICKED UP SHELLS.";
		break;
	case OT_SHOTGUN:
		pl.has_shotgun = 1; pl.shells += 8; pl.weapon = 1;
		m = "A SHOTGUN!";
		break;
	case OT_KEY:
		pl.has_key = 1; m = "GOT THE RED KEY.";
		break;
	default:
		return 0;
	}
	if (pl.health > 100) pl.health = 100;
	if (pl.bullets > 200) pl.bullets = 200;
	if (pl.shells > 50) pl.shells = 50;
	set_msg(m);
	sfx(o->type == OT_SHOTGUN ? SFX_WEAPON : SFX_PICKUP);
	if (!o->hp) items++;
	flash_bonus = 4;
	return 1;
}


static void objs_tic(void)
{
	int i;
	for (i = 0; i < nmons; i++)             /* monsters think at 17.5 Hz */
		if ((mons[i] ^ (int)gametic) & 1) monster_tic(&objs[mons[i]], mons[i]);
	if (nfire)
		for (i = 0; i < nobjs; i++)
			if (objs[i].type == OT_FIREBALL) fireball_tic(&objs[i]);
	if ((gametic & 1) || pl.dead) return;   /* pickups every other tic */
	for (i = 0; i < nobjs; i++) {
		Obj *o = &objs[i];
		if (o->type < OT_MEDIKIT || o->type > OT_KEY) continue;
		if (abs(o->x - pl.x) < 120 && abs(o->y - pl.y) < 120 && pickup(o))
			o->type = OT_NONE;
	}
}

unsigned long tic_prof[3];
void run_tic(const Input *in)
{
	unsigned long t0 = 0, t1 = 0, t2 = 0;
	if (profiling) t0 = tmr_ticks();
	gametic++;
	player_tic(in);
	if (profiling) { t1 = tmr_ticks(); tic_prof[0] += t1 - t0; }
	doors_tic();
	if (profiling) { t2 = tmr_ticks(); tic_prof[1] += t2 - t1; }
	objs_tic();
	if (profiling) tic_prof[2] += tmr_ticks() - t2;
	if (msg_t && !--msg_t) hud_dirty = 1;
	if (muzzle_light) muzzle_light--;
}
