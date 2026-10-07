/* JRDOOM: shared declarations. */
#ifndef JRDOOM_H
#define JRDOOM_H

#include "../../lib/jrgfx.h"

/* ---- Screen layout (160x200, mode 8) ----------------------------------- */
#define VIEW_COLS 80          /* most rays; each is one video byte = 2 pixels */
#define VIEW_H    144         /* most 3D view rows; HUD below              */
#define BANK      0x2000      /* odd rows, 8K after even rows (video layout) */
#define HUD_Y     VIEW_H

/* ---- Map ---------------------------------------------------------------- */
#define MAPW 32
#define MAPH 32
#define TILE 256              /* positions are 8.8 fixed point tiles       */
#define CELL_DOOR 0x80        /* map cell: 0x80 | door index               */

/* wall materials (map cell values 1..) */
enum { M_NONE, M_STONE, M_BRICK, M_TECH, M_MARBLE, M_WOOD, M_ROCK, M_EXIT,
       M_DOOR, M_LOCKED, NUM_MATS };

typedef struct {
	unsigned char x, y;
	unsigned char locked;     /* needs the red key */
	unsigned char state;      /* 0 closed, 1 opening, 2 open, 3 closing */
	int open;                 /* 0..256 slid open */
	int timer;
} Door;

/* ---- Objects ------------------------------------------------------------ */
enum { OT_NONE, OT_ZOMBIE, OT_IMP, OT_FIREBALL, OT_MEDIKIT, OT_STIM, OT_CLIP,
       OT_SHELLS, OT_SHOTGUN, OT_KEY, OT_LAMP, OT_BARREL, OT_COLUMN };
enum { S_IDLE, S_CHASE, S_ATTACK, S_PAIN, S_DIE, S_DEAD, S_BOOM };

typedef struct {
	int x, y;                 /* 8.8 */
	int dx, dy;               /* fireball velocity per tic */
	unsigned char type, state;
	int hp, timer, cool;
	/* last frame's projection, for hit tests */
	int scol, shw, sdepth, sh;
	unsigned char seen;
} Obj;

#define MAX_OBJS 64

/* ---- Player ------------------------------------------------------------- */
typedef struct {
	int x, y, ang;
	int health, bullets, shells;
	int weapon;               /* 0 pistol, 1 shotgun */
	int has_shotgun, has_key;
	int fire_t, cool;         /* firing frame / refire delay, in tics */
	int bob;                  /* walking phase */
	int moving;
	int ouch;                 /* tics of the ouch face */
	int dead;
} Player;

typedef struct {
	unsigned char fwd, back, left, right, fire, use, strafe, w1, w2;
} Input;

/* ---- Art ---------------------------------------------------------------- */
typedef struct {
	unsigned char w, h;       /* columns (pixels for world sprites, bytes for weapons), rows */
	const unsigned short *col;
	const unsigned char *data; /* per column: (len, keepmask, even, odd)..., 0 */
} Sprite;
typedef struct {
	const unsigned short *col;
	const unsigned char *data; /* per column: (len, tone)..., 0 */
} Texture;
#include "artids.h"
extern const Sprite sprites[NUM_SPRITES];
extern const Texture textures[NUM_TEXTURES];

/* ---- Globals (game.c) --------------------------------------------------- */
extern unsigned char map[MAPH * MAPW];
extern unsigned char blockmap[MAPH * MAPW];
extern Door doors[];
extern int ndoors;
extern Obj objs[MAX_OBJS];
extern int nobjs;
extern Player pl;
extern unsigned long gametic;
extern int kills, total_kills, items, total_items, secrets;
extern int level_done, level_num;
extern int god;
extern int profiling;          /* /PROF: time the parts of each frame */
extern char msg[24];
extern int msg_t;
extern int hud_dirty;
extern int flash_red, flash_bonus, muzzle_light;
extern const int sin_t[1024];
#define sin_tab(a) sin_t[(a) & 1023]
#define cos_tab(a) sin_t[((a) + 256) & 1023]
int rnd(void);

void load_level(int n);
Obj *spawn_obj(int type, int x, int y);
int  num_levels(void);
void run_tic(const Input *in);
int  ang_to(int dx, int dy);
int  approx_dist(int dx, int dy);
int  blocked_cell(int mx, int my);
void build_grid(void);
int  line_of_sight(int x0, int y0, int x1, int y1);
const unsigned char *level_waypoints(int n, int *count);

/* ---- Render (render.c) -------------------------------------------------- */
extern int *wallh_p;             /* wall height per column: the z-buffer */
extern int vcols, vh, view_size, low_detail;  /* current view window */
void render_init(void);
void set_view_size(int n);       /* 0 full .. 2 smallest */
void draw_border(void);
void compile_guns(void);         /* slow: call once, behind the title */
void render_view(void);
void bench_render(int n);
void sprite_gallery(int first);
void view_target(void);    /* point the renderer at the hidden page */
void view_to_screen(void);
void view_to_back(void);   /* copy the view into gfx_back (screenshots) */

/* ---- Sound (main.c) ----------------------------------------------------- */
enum { SFX_PISTOL, SFX_SHOTGUN, SFX_PICKUP, SFX_WEAPON, SFX_DOOR, SFX_SIGHT,
       SFX_ZDIE, SFX_IDIE, SFX_HURT, SFX_FIREBALL, SFX_BOOM, SFX_CLAW,
       SFX_NOWAY, SFX_EXIT, SFX_ZFIRE, SFX_PAIN };
void sfx(int id);
void set_msg(const char *s);

#endif
