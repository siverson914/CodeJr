/* Sopwith26's own parts: the pause, the starting level and the top ten.
 * New file, written 2026-10-08 for the CodeJr PCjr port. Not part of the
 * 1984 game. sopwith2.c includes it when SOPWITH26 is defined, so it can
 * use the game's own text and sound routines. */

#define CJ_NHI 10
#define CJ_HIFILE "SOP26.HI"

typedef struct {
  int score,level;      /* level 1 to 7, as shown */
  char mode;            /* S: alone, C: against the computer */
  char name[4],date[9]; /* initials, MM-DD-YY */
} cj_hient;

static cj_hient cj_hi[CJ_NHI];
static int cj_nhi;
static int cj_startlevel;                  /* 0 to 6 */
static volatile int cj_pausereq,cj_pkey;   /* set by the keyboard interrupt */
static volatile int cj_paused;             /* the timer leaves the sound alone */
static char *cj_script;                    /* test mode: the keys "typed" */

/* A key for the screens here. In test mode the keys come from a script,
   and when it runs out the screen is saved and the program ends: that is
   how these screens get looked at without anyone at the keyboard. */
static int cj_key(void)
{
  int k;
  if (cj_limit!=0) {
    if (cj_script==NULL || *cj_script==0) {
      cj_shot();
      finish(NULL,FALSE);
    }
    return *(cj_script++);
  }
  do {
    while (!kbhit());
    k=getch();
    if (k==0 && kbhit()) { /* a function or arrow key: not for us */
      getch();
      k=0;
    }
  } while (k==0);
  return k;
}

static void cj_at(int x,int y,int colour,char *s)
{
  poscurs(x,y);
  setcolour(colour);
  printstr(s);
}

static void cj_hisort(void)
{
  int i,j;
  for (i=1;i<cj_nhi;i++) {
    cj_hient e=cj_hi[i];
    for (j=i;j>0 && cj_hi[j-1].score<e.score;j--)
      cj_hi[j]=cj_hi[j-1];
    cj_hi[j]=e;
  }
}

/* The file is text, one score a line: score level mode initials date.
   Anything that doesn't read as that is skipped: it may have been edited. */
static void cj_hiload(void)
{
  FILE *f=fopen(CJ_HIFILE,"r");
  char line[64],nm[8],dt[12],md;
  int sc,lv,i,ok;
  cj_nhi=0;
  if (f==NULL)
    return;
  while (cj_nhi<CJ_NHI && fgets(line,sizeof(line),f)!=NULL) {
    if (sscanf(line,"%d %d %c %3s %8s",&sc,&lv,&md,nm,dt)!=5)
      continue;
    ok=lv>=1 && lv<=7 && (md=='S' || md=='C') && sc>0;
    for (i=0;nm[i]!=0;i++)
      if (!isalnum((unsigned char)nm[i]))
        ok=0;
    for (i=0;dt[i]!=0;i++)
      if (!isdigit((unsigned char)dt[i]) && dt[i]!='-')
        ok=0;
    if (!ok)
      continue;
    cj_hi[cj_nhi].score=sc;
    cj_hi[cj_nhi].level=lv;
    cj_hi[cj_nhi].mode=md;
    strcpy(cj_hi[cj_nhi].name,nm);   /* at most 3 letters and the 0 */
    strcpy(cj_hi[cj_nhi].date,dt);   /* at most 8 and the 0 */
    cj_nhi++;
  }
  fclose(f);
  cj_hisort();
}

static void cj_hisave(void)
{
  FILE *f=fopen(CJ_HIFILE,"w");
  int i;
  if (f==NULL)
    return;
  for (i=0;i<cj_nhi;i++)
    fprintf(f,"%d %d %c %s %s\n",cj_hi[i].score,cj_hi[i].level,cj_hi[i].mode,
            cj_hi[i].name,cj_hi[i].date);
  fclose(f);
}

static int cj_hiqualifies(int sc)
{
  return sc>0 && (cj_nhi<CJ_NHI || sc>cj_hi[CJ_NHI-1].score);
}

/* Puts a score in its place and returns the place (0 is the best). */
static int cj_hiinsert(int sc,int lv,char md,char *name)
{
  struct dosdate_t d;
  int i,j;
  for (i=0;i<cj_nhi && cj_hi[i].score>=sc;i++);
  if (cj_nhi<CJ_NHI)
    cj_nhi++;
  for (j=cj_nhi-1;j>i;j--)
    cj_hi[j]=cj_hi[j-1];
  cj_hi[i].score=sc;
  cj_hi[i].level=lv;
  cj_hi[i].mode=md;
  strcpy(cj_hi[i].name,name);
  _dos_getdate(&d);
  sprintf(cj_hi[i].date,"%02u-%02u-%02u",d.month%100u,d.day%100u,d.year%100u);
  return i;
}

/* The table, from text row y down; "mark" is the row to pick out. */
static void cj_hishow(int y,int mark)
{
  char buf[48];
  int i;
  cj_at(13,y,3,"T O P   T E N");
  if (cj_nhi==0)
    cj_at(10,y+3,1,"No scores yet. Be first.");
  for (i=0;i<cj_nhi;i++) {
    /* 2+2+3+1+6+8+1+2+1+8 = 34 characters at most */
    sprintf(buf,"%2d. %-3.3s %6d  level %d %c %.8s",i+1,cj_hi[i].name,cj_hi[i].score,
            cj_hi[i].level%10,cj_hi[i].mode=='C' ? 'C' : ' ',cj_hi[i].date);
    cj_at(3,y+2+i,i==mark ? 3 : 1,buf);
  }
}

/* Three initials, shown as they are typed. Enter takes them. */
static void cj_getname(char *name)
{
  int n=0,k;
  setcolour(3);
  writechar('_');
  writechar(8);
  for (;;) {
    k=cj_key();
    if (k==13 || k==27)
      break;
    if (k==8 && n>0) {
      n--;
      writechar(' ');
      writechar(8);
      writechar(8);
      writechar('_');
      writechar(8);
    }
    else if (isalnum(k) && n<3) {
      name[n++]=(char)toupper(k);
      writechar(name[n-1]);
      writechar(n<3 ? '_' : ' ');
      writechar(8);
    }
  }
  if (n==0)
    name[n++]='-';
  name[n]=0;
}

/* A game is over (the lives are gone, or the player left): the score, the
   initials if it earned a place, and the table. Returns when the player
   wants another game; leaves the program if not. */
static void cj_gameend(int quitting)
{
  int sc=objects[lastplane].score,rank=-1;
  char name[4],buf[40];
  if (cj_limit!=0 && cj_script==NULL)
    return;
  cj_paused=1;
  outportb(PORT_SPKR,inportb(PORT_SPKR)&0xfc);
  setgmode(4);
  while (cj_limit==0 && kbhit())
    getch();
  cj_at(15,1,3,"GAME  OVER");
  sprintf(buf,"Score %d   level %d",sc,level%10+1);   /* 6+6+10+1 at most */
  cj_at(11,3,1,buf);
  cj_hiload();
  if (cj_hiqualifies(sc)) {
    cj_at(3,5,2,"A top ten score! Your initials: ");
    cj_getname(name);
    rank=cj_hiinsert(sc,level%10+1,gamemode==GAME_COMPUTER ? 'C' : 'S',name);
    cj_hisave();
    poscurs(0,5);
    clearline();
  }
  cj_hishow(7,rank);
  if (quitting)
    cj_at(13,22,3,"Press any key");
  else {
    cj_at(6,21,3,"Press any key to play again");
    cj_at(15,23,1,"Esc - quit");
  }
  if (cj_key()==27 && !quitting)
    quitting=1;
  if (quitting)
    finish(NULL,FALSE);
  setgmode(4);
  keyspressed=keysnowpressed=keysacknowledged=0;
  cj_pausereq=0;
  cj_paused=0;
}

/* The pause notice is XORed onto the picture, and off again the same way. */
static void cj_pausetext(void)
{
  cj_at(14,9,131,"P A U S E D");
  cj_at(3,11,131,"Esc - quit    any other key - play");
}

static void cj_pause(void)
{
  unsigned char spk=inportb(PORT_SPKR);
  int k;
  cj_paused=1;
  outportb(PORT_SPKR,spk&0xfc);
  cj_pausetext();
  cj_pkey=0;
  if (cj_limit!=0) {
    cj_shot();
    finish(NULL,FALSE);
  }
  while ((k=cj_pkey)==0);
  cj_pausetext();
  if (k==1) /* Esc */
    cj_gameend(1);
  outportb(PORT_SPKR,spk);
  cj_paused=0;
  cj_pausereq=0;
  _disable();
  speedtick=0;
  _enable();
  cj_carry=0;
}

/* The title screen's own lines. */
static void cj_titlelevel(void)
{
  char buf[40];
  sprintf(buf,"1 to 7 - starting level: %d",cj_startlevel%10+1);
  cj_at(6,22,1,buf);
}

static void cj_titlebest(void)
{
  char buf[40];
  cj_hiload();
  if (cj_nhi==0)
    return;
  sprintf(buf,"Best: %d  %.3s",cj_hi[0].score,cj_hi[0].name); /* 6+6+2+3 */
  cj_at(12,11,3,buf);
}
