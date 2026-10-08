/* Sopwith26's explosions: debris shown as small bright blocks, sparks that
 * fly out and fall, and a flash of the background.
 * New file, written 2026-10-08 for the CodeJr PCjr port. Not part of the
 * 1984 game. sopwith2.c includes it when SOPWITH26 is defined.
 *
 * This is only what is shown. The game's own debris (up to fifteen 8 by 8
 * pieces an explosion, which can hit things and score) moves and collides
 * exactly as it always did; nothing here writes to the game's state or
 * uses its random number. -x on the command line gives the original look.
 *
 * Everything is XORed onto the screen and taken off by the same XOR, as
 * the game's own sprites are, so it doesn't matter what overlaps what. */

#define CJ_NSPK 40

typedef struct {
  int wx,wy;              /* place in the world, in quarter pixels */
  signed char vx,vy;      /* quarter pixels a frame */
  unsigned char life;     /* frames left; 0: free */
  unsigned char drawn;
  int sx,sy;              /* where on the screen it was drawn */
} cj_spk;

static cj_spk cj_sp[CJ_NSPK];
static int cj_live;                 /* sparks alive */
static int cj_steps;                /* frames whose drawing was left out since the last one drawn */
static unsigned cj_fxseed=0x26a5;   /* the sparks' own dice, never the game's */
static int cj_flash,cj_flashon,cj_flashrest;

static unsigned cj_fxrand(void)
{
  cj_fxseed=cj_fxseed*25173u+13849u;
  return cj_fxseed>>8;
}

/* One pixel, flipped. sx 0 to 319, sy 0 to 199 (0 is the bottom line). */
static void cj_pxor(int sx,int sy)
{
  static const unsigned char bit[4]={0xc0,0x30,0x0c,0x03}; /* an 8088 shifts a bit at a time */
  unsigned char far *d=(unsigned char far *)MK_FP(0xb800,rowtab[0][sy]+((unsigned)sx>>2));
  *d^=bit[sx&3];
}

/* A piece of the game's debris: 4 pixels wide and 3 high, at the middle of
   the 8 by 8 piece it stands for. Whether it is drawn depends only on
   where it is, so the same call takes it off again. */
static void cj_block(int sx,int sy,unsigned char colour)
{
  static const unsigned run[4]={0xff00u,0x3fc0u,0x0ff0u,0x03fcu};
  unsigned m,off;
  int r;
  sx+=2;
  sy-=2;
  if (sx<0 || sx>316 || sy<2 || sy>199)
    return;
  m=run[sx&3]&(colour*0x5555u);
  off=(unsigned)sx>>2;
  for (r=0;r<3;r++) {
    unsigned char far *d=(unsigned char far *)MK_FP(0xb800,rowtab[0][sy-r]+off);
    d[0]^=(unsigned char)(m>>8);
    if ((unsigned char)m!=0)
      d[1]^=(unsigned char)m;
  }
}

/* The background colour, through the PCjr's palette register 0. Only on a
   PCjr: a PC's CGA has no such register. */
static void cj_background(int colour)
{
  if (!on_pcjr)
    return;
  _disable();
  inportb(0x3da);                 /* the next write is a register number */
  outportb(0x3da,0x10);
  outportb(0x3da,colour&15);
  inportb(0x3da);
  outportb(0x3da,0);              /* off the palette registers, or the picture stays blank */
  inportb(0x3da);
  _enable();
}

static void cj_flashoff(void)
{
  if (cj_flashon)
    cj_background(0);
  cj_flashon=0;
  cj_flash=0;
}

/* The screen has been wiped (a new plane, a new game): nothing of ours is
   on it any more. */
static void cj_fxreset(void)
{
  int i;
  for (i=0;i<CJ_NSPK;i++)
    cj_sp[i].life=cj_sp[i].drawn=0;
  cj_live=0;
  cj_steps=0;
  cj_flashoff();
}

/* Something has blown up. */
static void cj_boom(object *obj)
{
  int n=(obj->type==OBJ_BUILDING) ? 20 : 12,i;
  int cx=(obj->x+(obj->width>>1))<<2,cy=(obj->y-(obj->height>>1))<<2;
  if (cj_classic)
    return;
  if (obj->type==OBJ_BUILDING && cj_flashrest==0) {
    cj_flash=1;
    cj_flashrest=6;     /* never two flashes close together */
  }
  for (i=0;i<CJ_NSPK && n>0;i++)
    if (cj_sp[i].life==0) {
      cj_spk *p=&cj_sp[i];
      unsigned r=cj_fxrand();
      p->wx=cx;
      p->wy=cy;
      p->vx=(signed char)((int)(r%29)-14+(obj->xv<<1));   /* up to 3.5 pixels a frame each way */
      p->vy=(signed char)((int)((r>>5)%22)-4);            /* mostly upwards */
      p->life=(unsigned char)(9+(r>>10)%10);
      p->drawn=0;
      cj_live++;
      n--;
    }
}

/* Once for every frame that is drawn: the flash, and the sparks moved on
   by as many frames as have passed. */
static void cj_fxframe(void)
{
  int i,k,steps=cj_steps+1,x,y;
  cj_steps=0;
  if (cj_flashrest>0)
    cj_flashrest=cj_flashrest>steps ? cj_flashrest-steps : 0;
  if (cj_flash>0) {
    if (!cj_flashon) {
      cj_background(4);   /* red */
      cj_flashon=1;
    }
    cj_flash=0;
  }
  else if (cj_flashon)
    cj_flashoff();
  if (cj_live==0)
    return;
  for (i=0;i<CJ_NSPK;i++) {
    cj_spk *p=&cj_sp[i];
    if (p->life==0)
      continue;
    if (p->drawn) {
      cj_pxor(p->sx,p->sy);
      p->drawn=0;
    }
    for (k=0;k<steps && p->life!=0;k++) {
      p->wx+=p->vx;
      p->wy+=p->vy;
      if (p->vy>-100)
        p->vy-=2;       /* gravity */
      p->life--;
    }
    x=p->wx>>2;
    y=p->wy>>2;
    if (p->life==0 || x<0 || x>=3000 || y>199 || y<=(int)workingground[x]) {
      p->life=0;
      cj_live--;
      continue;
    }
    x-=scnleft;
    if (x<0 || x>319)
      continue;         /* off the side: still flying, not shown */
    cj_pxor(x,y);
    p->sx=x;
    p->sy=y;
    p->drawn=1;
  }
}

/* The screen update's sprite call: debris as a block, the rest as ever. */
static void cj_show(int x,int y,unsigned char *p,object *obj)
{
  if (!cj_classic && obj->type==OBJ_SHRAPNEL) {
    /* white, or the colour of what blew up, by the piece's shape */
    cj_block(x,y,(unsigned char)((obj->kind&1) ? 3 : (obj->colour&3 ? obj->colour&3 : 3)));
    return;
  }
  putimage(x,y,p,obj);
}
