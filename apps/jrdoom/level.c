/* JRDOOM levels: 32x32 ASCII maps.
 *  walls: # stone  T tech  B brick  G green marble  W wood  R rock
 *         E exit switch   D door   K door that needs the red key
 *  things: P player  z zombieman  i imp  m medikit  t stimpack  c clip
 *          x shells  g shotgun  k red key  l lamp  b barrel  o column
 */
#include <string.h>
#include "jrdoom.h"

typedef struct {
	const char *rows[MAPH];
	int start_ang;
	int nway;
	const unsigned char *way;  /* demo autopilot path, tile x,y pairs */
} Level;

static const unsigned char way1[] = {
	5,3, 9,3, 17,3, 18,4, 21,4, 24,7, 24,12, 23,14, 23,17, 25,20, 23,21,
	23,25, 28,26, 28,27, 30,27
};

static const Level levels[] = {
	{ {
	"#######TTTTTTTTTTTTGGGGGGGGGGGGG",
	"#......T...........G...........G",
	"#......T.l.......l.G.b.......b.G",
	"#..P...D......z....G.....z.....G",
	"#......T...T...T...D...........G",
	"#....c.T...........G...G...G...G",
	"#......T...T...T...G...........G",
	"###D####........z..G..z........G",
	"#......#...........G.....c...z.G",
	"#......#BBBDBBBBBTTG...........G",
	"#....b.#........B##G...G...G...G",
	"#......#......x.B##G...........G",
	"#.z....D........B##G......i....G",
	"#......#....g...B##G.b.......m.G",
	"#......#.i......B##G...........G",
	"#......#........B#WWWWWDWWWWWGGG",
	"#......#BBBBBBBBB#W.........W###",
	"#....z.###########W.......x.W###",
	"#......###########W..i......W###",
	"#......###########W.........W###",
	"#...c..###########W......k..W###",
	"#.m....###########W.t.......W###",
	"#......###########W.........W###",
	"###############RRRRRRRRKRRRRRRRR",
	"###############R...............R",
	"###############R.l...i.......m.R",
	"###############R..............lR",
	"###############R....R.....R....E",
	"###############R........o.....lR",
	"###############R..z.........i..R",
	"###############R...............R",
	"###############RRRRRRRRRRRRRRRRR",
	}, 0, sizeof way1 / 2, way1 },
};

int num_levels(void) { return sizeof levels / sizeof levels[0]; }

const unsigned char *level_waypoints(int n, int *count)
{
	*count = levels[n].nway;
	return levels[n].way;
}

void load_level(int n)
{
	int x, y;
	const Level *lv = &levels[n];
	memset(map, 0, MAPW * MAPH);
	memset(blockmap, 0, MAPW * MAPH);
	nobjs = 0; ndoors = 0;
	kills = total_kills = items = total_items = 0;
	level_done = 0;
	pl.has_key = 0;
	pl.ang = lv->start_ang;
	for (y = 0; y < MAPH; y++) {
		for (x = 0; x < MAPW; x++) {
			unsigned char c = 0, ch = (unsigned char)lv->rows[y][x];
			int t = 0;
			switch (ch) {
			case '#': c = M_STONE; break;
			case 'T': c = M_TECH; break;
			case 'B': c = M_BRICK; break;
			case 'G': c = M_MARBLE; break;
			case 'W': c = M_WOOD; break;
			case 'R': c = M_ROCK; break;
			case 'E': c = M_EXIT; break;
			case 'D': case 'K':
				doors[ndoors].x = (unsigned char)x;
				doors[ndoors].y = (unsigned char)y;
				doors[ndoors].locked = ch == 'K';
				doors[ndoors].state = 0;
				doors[ndoors].open = 0;
				c = (unsigned char)(CELL_DOOR | ndoors++);
				break;
			case 'P': pl.x = x * TILE + TILE / 2; pl.y = y * TILE + TILE / 2; break;
			case 'z': t = OT_ZOMBIE; total_kills++; break;
			case 'i': t = OT_IMP; total_kills++; break;
			case 'm': t = OT_MEDIKIT; total_items++; break;
			case 't': t = OT_STIM; total_items++; break;
			case 'c': t = OT_CLIP; total_items++; break;
			case 'x': t = OT_SHELLS; total_items++; break;
			case 'g': t = OT_SHOTGUN; total_items++; break;
			case 'k': t = OT_KEY; total_items++; break;
			case 'l': t = OT_LAMP; break;
			case 'b': t = OT_BARREL; break;
			case 'o': t = OT_COLUMN; break;
			}
			map[y * MAPW + x] = c;
			if (t) spawn_obj(t, x * TILE + TILE / 2, y * TILE + TILE / 2);
		}
	}
	build_grid();
}
