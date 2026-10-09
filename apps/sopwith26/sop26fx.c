/* Sopwith26's flash: the background goes red for one frame when a
 * building blows up.
 * New file, written 2026-10-08 for the CodeJr PCjr port. Not part of the
 * 1984 game. sopwith2.c includes it when SOPWITH26 is defined.
 *
 * It is a palette register, not anything drawn: the screen's contents and
 * the game are untouched. -x on the command line turns it off.
 * (Tried with it and dropped: debris shown as small blocks with sparks
 * flying out. It cost the same as the original debris on the real PCjr
 * and Steve preferred the original's look. See apps/sopwith/SPEED.md.) */

static int cj_flash,cj_flashon,cj_flashrest;

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

/* The screen has been wiped (a new plane, a new game). */
static void cj_fxreset(void)
{
  cj_flashoff();
}

/* Something has blown up. */
static void cj_boom(object *obj)
{
  if (cj_classic)
    return;
  if (obj->type==OBJ_BUILDING && cj_flashrest==0) {
    cj_flash=1;
    cj_flashrest=6;     /* never two flashes close together */
  }
}

/* Once for every frame that is drawn. */
static void cj_fxframe(void)
{
  if (cj_flashrest>0)
    cj_flashrest--;
  if (cj_flash>0) {
    if (!cj_flashon) {
      cj_background(4);   /* red */
      cj_flashon=1;
    }
    cj_flash=0;
  }
  else if (cj_flashon)
    cj_flashoff();
}
