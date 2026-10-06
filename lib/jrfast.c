/* JRGFX fast path (fx_*): draws in the PCjr's native packed format, straight
 * into a hidden video page, and flips pages at vertical retrace.
 * See jrgfx.h for the interface and STATUS.md "Fast path" for design notes,
 * measurements and sources. 8088 only, large model.
 */
#include <dos.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include "jrgfx.h"

extern const unsigned char jr_font[96 * 8];
extern unsigned jr_rowoff[200];
extern void (*jr_done_hook)(void);
extern void (*jr_present_hook)(void);
unsigned jr_claim(unsigned want, unsigned paras);
int jr_reserved(unsigned seg, unsigned paras);
void jr_dos_free(unsigned seg);
void jr_set_vseg(unsigned seg);
unsigned jr_alloc_high(unsigned paras);

unsigned fx_seg;
int fx_pages;
int fx_irq;
volatile unsigned fx_vbl;

static int on, flags;
static unsigned char page[2];   /* CRT page numbers (16 KB units)        */
static int drawn;               /* index into page[] of the hidden page  */
static unsigned char modebits;  /* port 3DFh bits 7:6                    */
static unsigned own_page;       /* DOS block we claimed, or 0            */
static unsigned buf_seg;        /* FX_BUFFER: RAM buffer                 */
static unsigned user_seg;       /* fx_target(seg)                        */
static int slot;                /* wall cache / dirty state: 0,1 pages, 2 buffer */
static int cy0 = 0, cy1 = 199;  /* fx_clip                               */
static struct { int xa, xb; unsigned off; } tab[200]; /* span table */
/* Parameter block for fx_wallcol_asm; must be in DGROUP (= SS). */
static struct {
	unsigned col, ce, t, b1, fs, v, dv2, dv, texoff, texseg, rpoff, rpseg, rowtab, lo, seg;
} __near W;

static volatile unsigned char pend, pend_val, pal_pend;
static unsigned char pal_q[16];
/* fx_pal_anim: palette ranges rotated by the IRQ every `period` retraces */
#define NANIM 4
static struct { unsigned char a, b, period, cnt; signed char dir; } anim[NANIM];
static unsigned char pal_live[16];   /* the palette with the animation applied */
static volatile unsigned char anim_on;
/* fx_pal_flash: up to 4 entries overridden for a number of retraces */
#define NFLASH 4
static volatile struct { unsigned char idx, col, cnt; } flash[NFLASH];
static volatile unsigned char flash_new;
/* what the IRQ writes: pal_live plus flashes (below the split line) */
static unsigned char pal_out[16];
static volatile unsigned char out_valid;
/* fx_split: rows above split_row use pal_top */
static unsigned char pal_top[16];
static volatile unsigned char split_row;
static void (*irq_hook)(void);  /* fx_irq_hook */
int fx_split_state;              /* 0 off, 1 running, -1 gave up (no timing found) */
unsigned fx_split_guard;         /* polls left at the split, last frame (diagnostics) */
static void (__interrupt __far *old_irq5)(void);
static unsigned char old_mask;

#define PSEG(i) ((unsigned)page[i] << 10)

/* ---- Small asm helpers ------------------------------------------------- */

extern void fx_fillw(unsigned seg, unsigned off, unsigned n, unsigned v);
#pragma aux fx_fillw = "rep stosw" parm [es] [di] [cx] [ax] modify [cx di];

/* rows x w bytes at the same offset in two 80-byte-stride buffers */
extern void fx_copy2d(unsigned dseg, unsigned off, unsigned sseg, unsigned rows, unsigned w);
#pragma aux fx_copy2d = \
	"push ds" "mov ds,dx" "mov si,di" "mov dx,80" "sub dx,ax" \
	"FC1:" "mov cx,ax" "shr cx,1" "rep movsw" "adc cx,cx" "rep movsb" \
	"add si,dx" "add di,dx" "dec bx" "jnz FC1" \
	"pop ds" \
	parm [es] [di] [dx] [bx] [ax] modify [bx cx dx si di];

/* Span table walker. Each row entry: int xa, int xb, unsigned rowoffset.
 * Fills xa..xb (either order, clipped to 0..159) with the row's pattern
 * byte: BL for this row, BH for the next (swapped every row). Whole bytes
 * with REP STOSW, nibble read-modify-write only at the two ends. */
extern void fx_spans_asm(unsigned seg, const void far *tab, unsigned n, unsigned pat);
#pragma aux fx_spans_asm = \
	"push ds" "push bp" "mov ds,dx" "mov bp,cx" "mov bx,ax" \
	"SP0:" "lodsw" "mov dx,ax" "lodsw" "mov cx,ax" "lodsw" "mov di,ax" \
	"cmp dx,cx" "jle SP1" "xchg dx,cx" \
	"SP1:" "or dx,dx" "jns SP2" "xor dx,dx" \
	"SP2:" "cmp cx,159" "jle SP3" "mov cx,159" \
	"SP3:" "cmp dx,cx" "jg SP9" \
	"mov ax,dx" "shr ax,1" "add di,ax" \
	"test dl,1" "jz SP4" \
	"mov al,es:[di]" "and al,0F0h" "mov ah,bl" "and ah,0Fh" "or al,ah" "stosb" \
	"inc dx" "cmp dx,cx" "jg SP9" \
	"SP4:" "sub cx,dx" "inc cx" "shr cx,1" "sbb dx,dx" "mov al,bl" "mov ah,bl" \
	"shr cx,1" "rep stosw" "adc cx,cx" "rep stosb" \
	"or dx,dx" "jz SP9" \
	"mov al,es:[di]" "and al,0Fh" "and ah,0F0h" "or al,ah" "mov es:[di],al" \
	"SP9:" "xchg bl,bh" "dec bp" "jnz SP0" \
	"pop bp" "pop ds" \
	parm [es] [dx si] [cx] [ax] modify [ax bx cx dx si di];

/* Same walk, but each row's pattern byte comes from SS:BP (one byte per
 * row, DGROUP), so every row can have its own shade or dither. It also
 * widens the fx_track extents of each row: e0 at [bp-400], e1 at
 * [bp-200] (the pattern copy sits after them, see ext[]). */
extern void fx_spans_pat_asm(unsigned seg, const void far *tab, unsigned n, unsigned pats);
#pragma aux fx_spans_pat_asm = \
	"push ds" "push bp" "mov ds,dx" "mov bp,ax" \
	"SQ0:" "push cx" "lodsw" "mov dx,ax" "lodsw" "mov cx,ax" "lodsw" "mov di,ax" \
	"cmp dx,cx" "jle SQ1" "xchg dx,cx" \
	"SQ1:" "or dx,dx" "jns SQ2" "xor dx,dx" \
	"SQ2:" "cmp cx,159" "jle SQ3" "mov cx,159" \
	"SQ3:" "cmp dx,cx" "jg SQ9" \
	"mov ax,dx" "shr ax,1" "add di,ax" "cmp al,[bp-400]" "jae SQ5" "mov [bp-400],al" \
	"SQ5:" "mov ax,cx" "shr ax,1" "cmp al,[bp-200]" "jbe SQ6" "mov [bp-200],al" \
	"SQ6:" "mov bl,[bp+0]" \
	"test dl,1" "jz SQ4" \
	"mov al,es:[di]" "and al,0F0h" "mov ah,bl" "and ah,0Fh" "or al,ah" "stosb" \
	"inc dx" "cmp dx,cx" "jg SQ9" \
	"SQ4:" "sub cx,dx" "inc cx" "shr cx,1" "sbb dx,dx" "mov al,bl" "mov ah,bl" \
	"shr cx,1" "rep stosw" "adc cx,cx" "rep stosb" \
	"or dx,dx" "jz SQ9" \
	"mov al,es:[di]" "and al,0Fh" "and ah,0F0h" "or al,ah" "mov es:[di],al" \
	"SQ9:" "inc bp" "pop cx" "dec cx" "jz SQE" "jmp SQ0" "SQE:" \
	"pop bp" "pop ds" \
	parm [es] [dx si] [cx] [ax] modify [ax bx cx dx si di];

/* Same walk again, but the pixels are copied from another buffer in the
 * same layout (segment AX, same offsets): shadows that darken whatever is
 * under them, or restoring a background. The table is read through SS:BP
 * (it is in DGROUP) so DS:SI can point at the source; extents as above
 * through SS:DX (e0; e1 200 bytes on). */
extern void fx_spans_src_asm(unsigned seg, unsigned tab, unsigned n, unsigned src, unsigned e);
#pragma aux fx_spans_src_asm = \
	"push ds" "push bp" "mov bp,bx" "mov ds,ax" "mov bx,dx" \
	"SR0:" "push cx" "mov dx,[bp+0]" "mov cx,[bp+2]" "mov di,[bp+4]" \
	"cmp dx,cx" "jle SR1" "xchg dx,cx" \
	"SR1:" "or dx,dx" "jns SR2" "xor dx,dx" \
	"SR2:" "cmp cx,159" "jle SR3" "mov cx,159" \
	"SR3:" "cmp dx,cx" "jg SR9" \
	"mov ax,dx" "shr ax,1" "add di,ax" "cmp al,ss:[bx]" "jae SR5" "mov ss:[bx],al" \
	"SR5:" "mov ax,cx" "shr ax,1" "cmp al,ss:[bx+200]" "jbe SR6" "mov ss:[bx+200],al" \
	"SR6:" "test dl,1" "jz SR4" \
	"mov al,es:[di]" "and al,0F0h" "mov ah,[di]" "and ah,0Fh" "or al,ah" "stosb" \
	"inc dx" "cmp dx,cx" "jg SR9" \
	"SR4:" "mov si,di" "sub cx,dx" "inc cx" "shr cx,1" "sbb dx,dx" \
	"shr cx,1" "rep movsw" "adc cx,cx" "rep movsb" \
	"or dx,dx" "jz SR9" \
	"mov al,es:[di]" "and al,0Fh" "mov ah,[si]" "and ah,0F0h" "or al,ah" "mov es:[di],al" \
	"SR9:" "add bp,6" "inc bx" "pop cx" "dec cx" "jz SRE" "jmp SR0" "SRE:" \
	"pop bp" "pop ds" \
	parm [es] [bx] [cx] [ax] [dx] modify [ax bx cx dx si di];

/* Edge DDA: n rows, stores the integer part of x (16.16, in dx:ax) every
 * 6 bytes, x += slope (si:bx). Two 16-bit adds per row, no 32-bit math. */
extern void fx_edge_asm(void far *dst, unsigned n, long x, long dx);
#pragma aux fx_edge_asm = \
	"ED1:" "mov es:[di],dx" "add di,6" "add ax,bx" "adc dx,si" "loop ED1" \
	parm [es di] [cx] [dx ax] [si bx] modify [ax cx dx di];

/* (adx << 16) / dy, unsigned, two DIVs */
extern long fx_slope(unsigned adx, unsigned dy);
#pragma aux fx_slope = \
	"xor dx,dx" "div bx" "mov cx,ax" "xor ax,ax" "div bx" "mov dx,cx" \
	parm [ax] [bx] value [dx ax] modify [cx];

/* n rows starting at ES:DI (row a, parity in bl), one byte per row from
 * DS:SI. Row stepping alternates +2000h and +80-2000h. */
extern void fx_rowrun_asm(unsigned char far *p, const unsigned char far *pats, unsigned n, unsigned odd);
#pragma aux fx_rowrun_asm = \
	"push ds" "mov ds,dx" \
	"test bl,1" "jz RR1" "movsb" "add di,0E04Fh" "dec cx" \
	"RR1:" "mov bx,cx" "shr cx,1" "jcxz RR3" \
	"RR2:" "movsb" "add di,1FFFh" "movsb" "add di,0E04Fh" "loop RR2" \
	"RR3:" "test bl,1" "jz RR4" "movsb" \
	"RR4:" "pop ds" \
	parm [es di] [dx si] [cx] [bx] modify [bx cx si di];

/* n bytes down one bank (stride 80): tex[v >> 8], v += dv. Unrolled x4.
 * Per byte: MOV AL,DH / XLAT / STOSB / ADD DI,79 / ADD DX,SI (9 code
 * bytes + 1 read + 1 write: ~44 clocks on an 8088, bus bound). */
extern void fx_tex_asm(unsigned char far *p, const unsigned char far *tex, unsigned n, unsigned v, unsigned dv);
#pragma aux fx_tex_asm = \
	"push ds" "mov ds,bx" "mov bx,ax" \
	"test cl,1" "jz TX1" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"TX1:" "test cl,2" "jz TX2" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"TX2:" "shr cx,1" "shr cx,1" "jcxz TX4" \
	"TX3:" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"loop TX3" \
	"TX4:" "pop ds" \
	parm [es di] [bx ax] [cx] [dx] [si] modify [ax bx cx dx di];

/* Low detail: n words down one bank (stride 80), both bytes = tex[v >> 8].
 * 11 code bytes + 1 read + 2 writes per 2 output bytes. */
extern void fx_tex2_asm(unsigned char far *p, const unsigned char far *tex, unsigned n, unsigned v, unsigned dv);
#pragma aux fx_tex2_asm = \
	"push ds" "mov ds,bx" "mov bx,ax" \
	"test cl,1" "jz TW1" \
	"mov al,dh" "xlat" "mov ah,al" "stosw" "add di,78" "add dx,si" \
	"TW1:" "shr cx,1" "jcxz TW3" \
	"TW2:" \
	"mov al,dh" "xlat" "mov ah,al" "stosw" "add di,78" "add dx,si" \
	"mov al,dh" "xlat" "mov ah,al" "stosw" "add di,78" "add dx,si" \
	"loop TW2" \
	"TW3:" "pop ds" \
	parm [es di] [bx ax] [cx] [dx] [si] modify [ax bx cx dx di];

/* Low detail row run: like fx_rowrun_asm but 2 bytes per row. */
extern void fx_rowrun2_asm(unsigned char far *p, const unsigned char far *pats, unsigned n, unsigned odd);
#pragma aux fx_rowrun2_asm = \
	"push ds" "mov ds,dx" \
	"test bl,1" "jz RW1" "lodsb" "mov ah,al" "stosw" "add di,0E04Eh" "dec cx" \
	"RW1:" "mov bx,cx" "shr cx,1" "jcxz RW3" \
	"RW2:" "lodsb" "mov ah,al" "stosw" "add di,1FFEh" \
	"lodsb" "mov ah,al" "stosw" "add di,0E04Eh" "loop RW2" \
	"RW3:" "test bl,1" "jz RW4" "lodsb" "mov ah,al" "stosw" \
	"RW4:" "pop ds" \
	parm [es di] [dx si] [cx] [bx] modify [ax bx cx si di];

/* One whole raycaster column from the parameter block at SS:AX (see W
 * below): ceiling rows ce..t-1 and floor rows b1..fs-1 from rowpat, then
 * the textured rows t..b1-1 as an even-row and an odd-row pass. lo = 1
 * writes 2 bytes per row (fx_wall2). Generated by a script: same loops as
 * fx_rowrun_asm / fx_rowrun2_asm / fx_tex_asm / fx_tex2_asm above. */
extern void fx_wallcol_asm(unsigned w);
#pragma aux fx_wallcol_asm = \
	"push bp" "push ds" "mov bp,ax" "mov es,[bp+28]" "mov cx,[bp+4]" "sub cx,[bp+2]" \
	"jg WC9g" "jmp WC9" "WC9g:" "mov bx,[bp+2]" "shl bx,1" "add bx,[bp+24]" "mov di,ss:[bx]" "add di,[bp+0]" \
	"mov si,[bp+20]" "add si,[bp+2]" "mov ds,[bp+22]" "mov bl,[bp+2]" "test byte ptr [bp+26],1" "jnz WCL" \
	"test bl,1" "jz WCha" "movsb" "add di,0E04Fh" "dec cx" "WCha:" \
	"mov bx,cx" "shr cx,1" "jcxz WChc" "WChb:" "movsb" "add di,1FFFh" \
	"movsb" "add di,0E04Fh" "loop WChb" "WChc:" "test bl,1" "jz WChd" \
	"movsb" "WChd:" "jmp WCE" "WCL:" "test bl,1" "jz WCla" \
	"lodsb" "mov ah,al" "stosw" "add di,0E04Eh" "dec cx" "WCla:" \
	"mov bx,cx" "shr cx,1" "jcxz WClc" "WClb:" "lodsb" "mov ah,al" \
	"stosw" "add di,1FFEh" "lodsb" "mov ah,al" "stosw" "add di,0E04Eh" \
	"loop WClb" "WClc:" "test bl,1" "jz WCld" "lodsb" "mov ah,al" \
	"stosw" "WCld:" "WCE:" "WC9:" "mov cx,[bp+8]" "sub cx,[bp+6]" \
	"jg WF9g" "jmp WF9" "WF9g:" "mov bx,[bp+6]" "shl bx,1" "add bx,[bp+24]" "mov di,ss:[bx]" "add di,[bp+0]" \
	"mov si,[bp+20]" "add si,[bp+6]" "mov ds,[bp+22]" "mov bl,[bp+6]" "test byte ptr [bp+26],1" "jnz WFL" \
	"test bl,1" "jz WFha" "movsb" "add di,0E04Fh" "dec cx" "WFha:" \
	"mov bx,cx" "shr cx,1" "jcxz WFhc" "WFhb:" "movsb" "add di,1FFFh" \
	"movsb" "add di,0E04Fh" "loop WFhb" "WFhc:" "test bl,1" "jz WFhd" \
	"movsb" "WFhd:" "jmp WFE" "WFL:" "test bl,1" "jz WFla" \
	"lodsb" "mov ah,al" "stosw" "add di,0E04Eh" "dec cx" "WFla:" \
	"mov bx,cx" "shr cx,1" "jcxz WFlc" "WFlb:" "lodsb" "mov ah,al" \
	"stosw" "add di,1FFEh" "lodsb" "mov ah,al" "stosw" "add di,0E04Eh" \
	"loop WFlb" "WFlc:" "test bl,1" "jz WFld" "lodsb" "mov ah,al" \
	"stosw" "WFld:" "WFE:" "WF9:" "mov cx,[bp+6]" "sub cx,[bp+4]" \
	"jg WT9g" "jmp WT9" "WT9g:" "mov bx,[bp+4]" "shl bx,1" "add bx,[bp+24]" "mov di,ss:[bx]" "mov ax,ss:[bx+2]" \
	"add di,[bp+0]" "add ax,[bp+0]" "push ax" "push cx" "inc cx" "shr cx,1" \
	"mov dx,[bp+10]" "mov si,[bp+12]" "mov bx,[bp+16]" "mov ds,[bp+18]" "test byte ptr [bp+26],1" "jnz WAL" \
	"test cl,1" "jz WAha" "mov al,dh" "xlat" "stosb" "add di,79" \
	"add dx,si" "WAha:" "test cl,2" "jz WAhb" "mov al,dh" "xlat" \
	"stosb" "add di,79" "add dx,si" "mov al,dh" "xlat" "stosb" \
	"add di,79" "add dx,si" "WAhb:" "shr cx,1" "shr cx,1" "jcxz WAhd" \
	"WAhc:" "mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" "mov al,dh" \
	"xlat" "stosb" "add di,79" "add dx,si" "mov al,dh" "xlat" \
	"stosb" "add di,79" "add dx,si" "loop WAhc" "WAhd:" "jmp WAE" \
	"WAL:" "test cl,1" "jz WAla" "mov al,dh" "xlat" "mov ah,al" \
	"stosw" "add di,78" "add dx,si" "WAla:" "shr cx,1" "jcxz WAlc" \
	"WAlb:" "mov al,dh" "xlat" "mov ah,al" "stosw" "add di,78" \
	"add dx,si" "mov al,dh" "xlat" "mov ah,al" "stosw" "add di,78" \
	"add dx,si" "loop WAlb" "WAlc:" "WAE:" "pop cx" "pop di" \
	"shr cx,1" "jcxz WT8" "jmp WT7" "WT8:" "jmp WT9" "WT7:" "mov dx,[bp+10]" "add dx,[bp+14]" "test byte ptr [bp+26],1" "jnz WBL" \
	"test cl,1" "jz WBha" "mov al,dh" "xlat" "stosb" "add di,79" \
	"add dx,si" "WBha:" "test cl,2" "jz WBhb" "mov al,dh" "xlat" \
	"stosb" "add di,79" "add dx,si" "mov al,dh" "xlat" "stosb" \
	"add di,79" "add dx,si" "WBhb:" "shr cx,1" "shr cx,1" "jcxz WBhd" \
	"WBhc:" "mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" \
	"mov al,dh" "xlat" "stosb" "add di,79" "add dx,si" "mov al,dh" \
	"xlat" "stosb" "add di,79" "add dx,si" "mov al,dh" "xlat" \
	"stosb" "add di,79" "add dx,si" "loop WBhc" "WBhd:" "jmp WBE" \
	"WBL:" "test cl,1" "jz WBla" "mov al,dh" "xlat" "mov ah,al" \
	"stosw" "add di,78" "add dx,si" "WBla:" "shr cx,1" "jcxz WBlc" \
	"WBlb:" "mov al,dh" "xlat" "mov ah,al" "stosw" "add di,78" \
	"add dx,si" "mov al,dh" "xlat" "mov ah,al" "stosw" "add di,78" \
	"add dx,si" "loop WBlb" "WBlc:" "WBE:" "WT9:" "pop ds" \
	"pop bp" \
	parm [ax] modify [ax bx cx dx si di es];

/* n bytes of al down one bank (stride 80), unrolled x4 */
extern void fx_vfill_asm(unsigned char far *p, unsigned n, unsigned char c);
#pragma aux fx_vfill_asm = \
	"test cl,1" "jz VF1" "stosb" "add di,79" \
	"VF1:" "test cl,2" "jz VF2" "stosb" "add di,79" "stosb" "add di,79" \
	"VF2:" "shr cx,1" "shr cx,1" "jcxz VF4" \
	"VF3:" "stosb" "add di,79" "stosb" "add di,79" \
	"stosb" "add di,79" "stosb" "add di,79" "loop VF3" \
	"VF4:" \
	parm [es di] [cx] [al] modify [cx di];

/* Calls generated sprite code at cx:bx with DS = screen segment, DI = base. */
extern void fx_call(unsigned seg, unsigned off, unsigned cseg, unsigned coff);
#pragma aux fx_call = \
	"push ds" "push bp" "mov ds,ax" "push cx" "push bx" "mov bp,sp" \
	"call dword ptr [bp]" "add sp,4" "pop bp" "pop ds" \
	parm [ax] [di] [cx] [bx] modify [ax bx cx dx si di es];

/* Copies only the words of src that differ from dst (same offsets), with
 * REPE CMPSW to skip equal runs and REP MOVSW for differing runs. Returns
 * the number of words written. */
extern unsigned fx_delta_asm(unsigned dseg, unsigned off, unsigned sseg, unsigned words);
#pragma aux fx_delta_asm = \
	"push ds" "push bp" "mov ds,dx" "mov si,di" "xor bp,bp" "cld" \
	"DL1:" "jcxz DL9" "repe cmpsw" "je DL9" \
	"mov bx,si" "sub bx,2" "mov ax,si" "jcxz DL4" \
	"repne cmpsw" "mov ax,si" "jne DL4" "sub ax,2" \
	"DL4:" "push cx" "push si" "mov cx,ax" "sub cx,bx" "shr cx,1" "add bp,cx" \
	"mov si,bx" "mov di,bx" "rep movsw" "pop si" "mov di,si" "pop cx" "jmp DL1" \
	"DL9:" "mov ax,bp" "pop bp" "pop ds" \
	parm [es] [di] [dx] [cx] value [ax] modify [bx cx dx si di];

/* Line inner loops (fully visible lines). BX points at the LN block in
 * DGROUP (read through SS: DS is not DGROUP here): error decrement, error increment, the bank-step constant
 * (+-80), the starting error, the first row step and mask/color. A pixel
 * is old ^ ((old ^ color) & mask); the mask toggles F0h/0Fh, and the
 * byte pointer moves when it wraps. Rows alternate between the banks
 * with step = K - step. x = x-major, y = y-major; r/l = direction. */
static struct { int esub, eadd, k, err, step; unsigned mc; } __near LN;
#define LN_HEAD "push bp" "mov bp,ss:[bx+6]" "mov si,ss:[bx+8]" "mov dx,ss:[bx+10]" "mov ah,dh"
#define LN_PLOT "mov al,es:[di]" "xor al,ah" "and al,dl" "xor es:[di],al"
extern void fx_line_xr(unsigned seg, unsigned off, unsigned n, unsigned ln);
#pragma aux fx_line_xr = LN_HEAD \
	"LXR:" LN_PLOT "xor dl,0FFh" "jns LXR1" "inc di" \
	"LXR1:" "sub bp,ss:[bx]" "jge LXR2" "add bp,ss:[bx+2]" "add di,si" "neg si" "add si,ss:[bx+4]" \
	"LXR2:" "loop LXR" "pop bp" \
	parm [es] [di] [cx] [bx] modify [ax cx dx si di];
extern void fx_line_xl(unsigned seg, unsigned off, unsigned n, unsigned ln);
#pragma aux fx_line_xl = LN_HEAD \
	"LXL:" LN_PLOT "xor dl,0FFh" "js LXL1" "dec di" \
	"LXL1:" "sub bp,ss:[bx]" "jge LXL2" "add bp,ss:[bx+2]" "add di,si" "neg si" "add si,ss:[bx+4]" \
	"LXL2:" "loop LXL" "pop bp" \
	parm [es] [di] [cx] [bx] modify [ax cx dx si di];
extern void fx_line_yr(unsigned seg, unsigned off, unsigned n, unsigned ln);
#pragma aux fx_line_yr = LN_HEAD \
	"LYR:" LN_PLOT "add di,si" "neg si" "add si,ss:[bx+4]" \
	"sub bp,ss:[bx]" "jge LYR2" "add bp,ss:[bx+2]" "xor dl,0FFh" "jns LYR2" "inc di" \
	"LYR2:" "loop LYR" "pop bp" \
	parm [es] [di] [cx] [bx] modify [ax cx dx si di];
extern void fx_line_yl(unsigned seg, unsigned off, unsigned n, unsigned ln);
#pragma aux fx_line_yl = LN_HEAD \
	"LYL:" LN_PLOT "add di,si" "neg si" "add si,ss:[bx+4]" \
	"sub bp,ss:[bx]" "jge LYL2" "add bp,ss:[bx+2]" "xor dl,0FFh" "js LYL2" "dec di" \
	"LYL2:" "loop LYL" "pop bp" \
	parm [es] [di] [cx] [bx] modify [ax cx dx si di];

/* ---- Retrace interrupt (IRQ5) ------------------------------------------ */

static void pal_write(const unsigned char *p)
{
	int i;
	inp(0x3DA);
	for (i = 0; i < 16; i++) {
		outp(0x3DA, 0x10 + i);
		outp(0x3DA, p[i]);
	}
	inp(0x3DA);
	outp(0x3DA, 0); /* index < 10h re-enables video */
	inp(0x3DA);     /* leave the latch expecting an index */
}

/* Advances the fx_pal_anim ranges by one retrace; 1 if pal_live changed. */
static int anim_step(void)
{
	int i, k, ch = 0;
	for (i = 0; i < NANIM; i++) {
		unsigned char a = anim[i].a, b = anim[i].b, t;
		if (!anim[i].period || --anim[i].cnt) continue;
		anim[i].cnt = anim[i].period;
		if (anim[i].dir > 0) { t = pal_live[b]; for (k = b; k > a; k--) pal_live[k] = pal_live[k - 1]; pal_live[a] = t; }
		else { t = pal_live[a]; for (k = a; k < b; k++) pal_live[k] = pal_live[k + 1]; pal_live[b] = t; }
		ch = 1;
	}
	return ch;
}

/* Waits from the start of vertical retrace until the display has drawn
 * n lines: first the end of retrace (3DAh bit 3), then 2n changes of
 * bit 0, which toggles at the start and end of every displayed line and
 * holds still in the borders. Returns the polls left out of `guard`; 0
 * means the timing never came (then the split is given up, not hung). */
extern unsigned split_wait(unsigned n2, unsigned guard);
#pragma aux split_wait = \
	"mov dx,3DAh" \
	"SW1:" "in al,dx" "test al,8" "jz SW2" "dec bx" "jnz SW1" "jmp SWF" \
	"SW2:" "in al,dx" "and al,1" "mov ah,al" \
	"SW3:" "in al,dx" "and al,1" "cmp al,ah" "jne SW4" "dec bx" "jnz SW3" "jmp SWF" \
	"SW4:" "mov ah,al" "loop SW3" "mov ax,bx" "jmp SWE" \
	"SWF:" "xor ax,ax" \
	"SWE:" \
	parm [cx] [bx] value [ax] modify [ax bx cx dx];

/* palette entries that differ from what is showing, mid-screen: as few
 * writes as possible, since the PCjr blanks the picture while the
 * gate array's index points at a palette register */
static void pal_write_diff(const unsigned char *now, const unsigned char *want)
{
	int i;
	inp(0x3DA);
	for (i = 0; i < 16; i++)
		if (now[i] != want[i]) { outp(0x3DA, 0x10 + i); outp(0x3DA, want[i]); }
	outp(0x3DA, 0);
}

static void irq_body(void)
{
	int ch = 0, i;
	fx_vbl++;
	if (pend) {
		outp(0x3DF, pend_val);
		*(unsigned char far *)MK_FP(0x40, 0x8A) = pend_val;
		pend = 0;
	}
	if (pal_pend) { for (i = 0; i < 16; i++) pal_live[i] = pal_q[i]; pal_pend = 0; ch = 1; }
	if (anim_on && anim_step()) ch = 1;
	if (flash_new) { flash_new = 0; ch = 1; }
	for (i = 0; i < NFLASH; i++)
		if (flash[i].cnt && !--flash[i].cnt) ch = 1;
	if (ch || !out_valid) {
		for (i = 0; i < 16; i++) pal_out[i] = pal_live[i];
		for (i = 0; i < NFLASH; i++)
			if (flash[i].cnt) pal_out[flash[i].idx] = flash[i].col;
		if (anim_on) for (i = 0; i < 16; i++) gfx_pal[i] = pal_live[i]; /* screenshots see it */
		out_valid = 1;
		if (fx_split_state <= 0) pal_write(pal_out);
	}
	if (fx_split_state > 0) {
		/* top palette in the retrace, the normal one from split_row down */
		pal_write(pal_top);
		fx_split_guard = split_wait(split_row * 2, 12000);
		if (fx_split_guard) pal_write_diff(pal_top, pal_out);
		else { fx_split_state = -1; pal_write(pal_out); }
	}
	if (irq_hook) irq_hook();
	outp(0x20, 0x20);
}

/* The handler proper runs on a private stack in DGROUP. Two reasons, both
 * invisible in DOSBox (its DOS never leaves the program's stack):
 * - In the large model the compiler reaches DGROUP through SS in ordinary
 *   functions. An interrupt that lands while DOS or the BIOS is on its own
 *   stack would read and write that stack's segment instead.
 * - DOS's stacks are small.
 * irq5 itself touches statics only through DS (an __interrupt function
 * reloads it) and has no locals, so nothing here depends on SS. */
static unsigned char __near irq_stack[1024];
static unsigned long irq_old;
extern unsigned long stk_enter(unsigned seg, unsigned sp);
#pragma aux stk_enter = "mov dx,ss" "mov ax,sp" "mov ss,cx" "mov sp,bx" parm [cx] [bx] value [dx ax];
extern void stk_leave(unsigned long old);
#pragma aux stk_leave = "mov ss,dx" "mov sp,ax" parm [dx ax];

static void __interrupt __far irq5(void)
{
	irq_old = stk_enter(FP_SEG((void far *)irq_stack), FP_OFF(irq_stack) + sizeof irq_stack);
	irq_body();
	stk_leave(irq_old);
}

static void wait_retrace(void)
{
	while (inp(0x3DA) & 8) ;
	while (!(inp(0x3DA) & 8)) ;
}

static void set_3df(unsigned char v)
{
	outp(0x3DF, v);
	*(unsigned char far *)MK_FP(0x40, 0x8A) = v;
}

/* ---- Dirty rectangles (FX_BUFFER) and wall cache ----------------------- */

typedef struct { unsigned char x0, x1, y0, y1; } rect; /* bytes 0-79, rows */
#define NR 8
static rect dr[2][NR];
static int ndr[2], cur_dr;
static int view_dirty;

static void dirty_add(int bx0, int y0, int bx1, int y1)
{
	rect *r = dr[cur_dr];
	int n = ndr[cur_dr], i, best = 0;
	unsigned bestg = 0xFFFF;
	for (i = 0; i < n; i++) {
		/* overlaps or touches: merge */
		if (bx0 <= r[i].x1 + 1 && bx1 + 1 >= r[i].x0 && y0 <= r[i].y1 + 1 && y1 + 1 >= r[i].y0)
			goto merge;
	}
	if (n < NR) {
		r[n].x0 = (unsigned char)bx0; r[n].x1 = (unsigned char)bx1;
		r[n].y0 = (unsigned char)y0;  r[n].y1 = (unsigned char)y1;
		ndr[cur_dr] = n + 1;
		return;
	}
	for (i = 0; i < n; i++) { /* full: grow the rect that grows least */
		unsigned a0 = (unsigned)(r[i].x1 - r[i].x0 + 1) * (r[i].y1 - r[i].y0 + 1);
		int ux0 = bx0 < r[i].x0 ? bx0 : r[i].x0, ux1 = bx1 > r[i].x1 ? bx1 : r[i].x1;
		int uy0 = y0 < r[i].y0 ? y0 : r[i].y0, uy1 = y1 > r[i].y1 ? y1 : r[i].y1;
		unsigned g = (unsigned)(ux1 - ux0 + 1) * (uy1 - uy0 + 1) - a0;
		if (g < bestg) { bestg = g; best = i; }
	}
	i = best;
merge:
	if (bx0 < r[i].x0) r[i].x0 = (unsigned char)bx0;
	if (bx1 > r[i].x1) r[i].x1 = (unsigned char)bx1;
	if (y0 < r[i].y0) r[i].y0 = (unsigned char)y0;
	if (y1 > r[i].y1) r[i].y1 = (unsigned char)y1;
}

static int vy0 = 0, vy1 = 199;
static unsigned char wc[3][80], wf[3][80]; /* ceiling rows end, floor rows start */
static const unsigned char *rowpat;

static void wall_reset(int s)
{
	memset(wc[s], vy0, 80);
	memset(wf[s], vy1 + 1, 80);
}

/* fx_track: per target slot, per row, the byte columns drawn since that
 * slot's last fx_restore (e0 > e1: nothing), and the rows that have any. */
static int track;
/* [slot]: e0 at 0, e1 at 200, row patterns for fx_spans_pat_asm at 400.
 * Slot 3 takes the extents when tracking is off. */
static unsigned char __near ext[4][600];
static int ey0[3], ey1[3];

/* Updates the extents (BX: e0 of the first row, e1 200 bytes on, both in
 * DGROUP) from n span table rows at SI (DGROUP), clipped like the fills. */
extern void fx_ext_asm(unsigned tab, unsigned n, unsigned e);
#pragma aux fx_ext_asm = \
	"push ds" "push ss" "pop ds" \
	"EX0:" "lodsw" "mov dx,ax" "lodsw" "add si,2" \
	"cmp dx,ax" "jle EX1" "xchg dx,ax" \
	"EX1:" "or dx,dx" "jns EX2" "xor dx,dx" \
	"EX2:" "cmp ax,159" "jle EX3" "mov ax,159" \
	"EX3:" "cmp dx,ax" "jg EX9" \
	"shr dx,1" "shr ax,1" \
	"cmp dl,[bx]" "jae EX4" "mov [bx],dl" \
	"EX4:" "cmp al,[bx+200]" "jbe EX9" "mov [bx+200],al" \
	"EX9:" "inc bx" "loop EX0" \
	"pop ds" \
	parm [si] [cx] [bx] modify [ax bx cx dx si];

/* n rows: copies bytes e0..e1 of each row from DS (src) to ES (dst) at
 * the row offsets (BP: jr_rowoff, DGROUP), then empties the extents. */
extern void fx_restore_asm(unsigned dst, unsigned src, unsigned n, unsigned e, unsigned rows);
#pragma aux fx_restore_asm = \
	"push ds" "push bp" "mov ds,ax" "mov bp,dx" \
	"RS0:" "push cx" \
	"mov al,ss:[bx]" "mov cl,ss:[bx+200]" "mov byte ptr ss:[bx],0FFh" "mov byte ptr ss:[bx+200],0" \
	"xor ah,ah" "xor ch,ch" "sub cx,ax" "jb RS9" "inc cx" \
	"mov di,[bp+0]" "add di,ax" "mov si,di" \
	"shr cx,1" "rep movsw" "adc cx,cx" "rep movsb" \
	"RS9:" "add bp,2" "inc bx" "pop cx" "loop RS0" \
	"pop bp" "pop ds" \
	parm [es] [ax] [cx] [bx] [dx] modify [ax bx cx dx si di];

static void ext_reset(int s)
{
	memset(ext[s], 255, 200);
	memset(ext[s] + 200, 0, 200);
	ey0[s] = 200; ey1[s] = -1;
}

/* widen n rows of extents at BX (DGROUP) to bytes AL..AH */
extern void fx_extr_asm(unsigned e, unsigned n, unsigned b01);
#pragma aux fx_extr_asm = \
	"ER0:" "cmp al,ss:[bx]" "jae ER1" "mov ss:[bx],al" \
	"ER1:" "cmp ah,ss:[bx+200]" "jbe ER2" "mov ss:[bx+200],ah" \
	"ER2:" "inc bx" "loop ER0" \
	parm [bx] [cx] [ax] modify [bx cx];

static void ext_rect(int b0, int y0, int b1, int y1)
{
	if (y0 < ey0[slot]) ey0[slot] = y0;
	if (y1 > ey1[slot]) ey1[slot] = y1;
	fx_extr_asm(FP_OFF(&ext[slot][y0]), y1 - y0 + 1, (unsigned)b1 << 8 | b0);
}

/* exact extents from the span table rows y0..y1 */
static void ext_spans(int y0, int y1)
{
	if (y0 < ey0[slot]) ey0[slot] = y0;
	if (y1 > ey1[slot]) ey1[slot] = y1;
	fx_ext_asm(FP_OFF(&tab[y0]), y1 - y0 + 1, FP_OFF(&ext[slot][y0]));
}

/* Something other than fx_wall drew pixels x0..x1, y0..y1 (clipped). */
static void touch(int x0, int y0, int x1, int y1)
{
	if (track) ext_rect(x0 >> 1, y0, x1 >> 1, y1);
	if (flags & FX_BUFFER || user_seg) dirty_add(x0 >> 1, y0, x1 >> 1, y1);
	if (rowpat && y1 >= vy0 && y0 <= vy1) {
		int b;
		unsigned char *c = wc[slot], *f = wf[slot];
		for (b = x0 >> 1; b <= x1 >> 1; b++) {
			if (y0 < c[b]) c[b] = (unsigned char)(y0 < vy0 ? vy0 : y0);
			if (y1 >= f[b]) f[b] = (unsigned char)(y1 > vy1 ? vy1 + 1 : y1 + 1);
		}
	}
}

static void retarget(void)
{
	if (user_seg) { fx_seg = user_seg; slot = 2; }
	else if (flags & FX_BUFFER) { fx_seg = buf_seg; slot = 2; }
	else { fx_seg = PSEG(drawn); slot = drawn; }
}

/* ---- Init, flip -------------------------------------------------------- */

static void present_hook(void) { fx_begin(); }

int fx_init(int fl)
{
	unsigned char sh;
	unsigned w = 0;
	static const unsigned cand[5] = { 0x1800, 0x1400, 0x1000, 0x0C00, 0x0800 };
	int i;
	if (gfx_init(GFX_160x200)) return -1;
	flags = fl;
	sh = *(unsigned char far *)MK_FP(0x40, 0x8A);
	modebits = (unsigned char)(sh & 0xC0);
	page[0] = (unsigned char)(sh & 7);
	/* a second 16 KB page below 128K: page 6 if DOS keeps it out of its
	 * pool (DOSBox), else claim a free 16K-aligned block */
	if (page[0] == 7 && !(fl & 24) && jr_reserved(0x1800, 0x400)) w = 0x1800;
	for (i = 0; !w && !(fl & 8) && i < 5; i++)
		if (cand[i] != ((unsigned)page[0] << 10) && (w = jr_claim(cand[i], 0x400)) != 0)
			own_page = w;
	if (w) {
		fx_pages = 2;
		page[1] = (unsigned char)(w >> 10);
		_fmemset(MK_FP(w, 0), 0, 16384u);
	} else {
		fx_pages = 1;
		flags |= FX_BUFFER;
		page[1] = page[0];
	}
	if (flags & FX_BUFFER) {
		if (!(buf_seg = jr_alloc_high(0x400))) { fx_done(); gfx_done(); gfx_err = 2; return -1; }
		_fmemset(MK_FP(buf_seg, 0), 0, 16384u);
	}
	on = 1;
	drawn = fx_pages == 2 ? 1 : 0;
	set_3df((unsigned char)(modebits | (page[drawn] << 3) | page[0]));
	jr_set_vseg(PSEG(drawn));
	ndr[0] = ndr[1] = 0; cur_dr = 0;
	user_seg = 0;
	retarget();
	cy0 = 0; cy1 = 199;
	rowpat = 0;
	for (i = 0; i < 200; i++) tab[i].off = jr_rowoff[i];
	track = 0;
	anim_on = 0;
	fx_split_state = 0;
	irq_hook = 0;
	out_valid = 0;
	memset((void *)flash, 0, sizeof flash);
	memset(anim, 0, sizeof anim);
	memcpy(pal_live, gfx_pal, 16);
	fx_irq = 0;
	if (!(flags & FX_NOIRQ)) {
		unsigned long t;
		unsigned v0;
		old_irq5 = _dos_getvect(0x0D);
		_dos_setvect(0x0D, irq5);
		old_mask = (unsigned char)inp(0x21);
		outp(0x21, old_mask & ~0x20);
		v0 = fx_vbl;
		t = tmr_ms();
		while (fx_vbl == v0 && tmr_ms() - t < 100) ;
		if (fx_vbl != v0) fx_irq = 1;
		else {
			outp(0x21, inp(0x21) | (old_mask & 0x20));
			_dos_setvect(0x0D, old_irq5);
		}
	}
	jr_done_hook = fx_done;
	jr_present_hook = present_hook;
	return 0;
}

void fx_done(void)
{
	if (!on) return;
	if (fx_irq) {
		outp(0x21, (inp(0x21) & ~0x20) | (old_mask & 0x20));
		_dos_setvect(0x0D, old_irq5);
		fx_irq = 0;
	}
	pend = pal_pend = 0;
	set_3df((unsigned char)(modebits | (page[0] << 3) | page[0]));
	jr_set_vseg(PSEG(0));
	if (own_page) { jr_dos_free(own_page); own_page = 0; }
	if (buf_seg) { jr_dos_free(buf_seg); buf_seg = 0; }
	jr_done_hook = 0;
	jr_present_hook = 0;
	on = 0;
	fx_seg = 0;
}

void fx_begin(void)
{
	while (pend) ;
}

static void copy_rects(unsigned dst, unsigned src, const rect *r, int n)
{
	int i;
	for (i = 0; i < n; i++)
		fx_copy_rect(dst, src, r[i].x0 * 2, r[i].y0, r[i].x1 * 2 + 1, r[i].y1);
}

void fx_flip(void)
{
	unsigned char v;
	if (!on) return;
	fx_begin();
	if (view_dirty) { dirty_add(0, vy0, 79, vy1); view_dirty = 0; }
	if (flags & FX_BUFFER) {
		/* the hidden page shows the frame before last: copy what changed
		 * in either of the last two frames */
		copy_rects(PSEG(drawn), buf_seg, dr[cur_dr], ndr[cur_dr]);
		if (fx_pages == 2) copy_rects(PSEG(drawn), buf_seg, dr[cur_dr ^ 1], ndr[cur_dr ^ 1]);
	}
	cur_dr ^= 1;
	ndr[cur_dr] = 0;
	if (anim_on && !fx_irq && anim_step()) fx_pal_set(pal_live);
	if (fx_pages == 2) {
		int shown = drawn;
		drawn ^= 1;
		v = (unsigned char)(modebits | (page[drawn] << 3) | page[shown]);
		if (flags & FX_TEAR) set_3df(v);
		else if (fx_irq) { pend_val = v; pend = 1; }
		else { wait_retrace(); set_3df(v); }
		jr_set_vseg(PSEG(drawn));
	}
	retarget();
}

void fx_sync_rows(int y0, int y1)
{
	int s;
	if (!on || fx_pages < 2) return;
	if (y0 < 0) y0 = 0;
	if (y1 > 199) y1 = 199;
	if (y0 > y1) return;
	fx_copy_rect(PSEG(drawn ^ 1), fx_seg, 0, y0, 159, y1);
	s = drawn ^ 1;
	if (rowpat && y1 >= vy0 && y0 <= vy1) wall_reset(s);
}

void fx_clip(int y0, int y1)
{
	cy0 = y0 < 0 ? 0 : y0;
	cy1 = y1 > 199 ? 199 : y1;
}

unsigned fx_alloc_buffer(void)
{
	unsigned s = jr_alloc_high(0x400);
	if (s) _fmemset(MK_FP(s, 0), 0, 16384u);
	return s;
}

void fx_target(unsigned seg)
{
	if (seg != user_seg && rowpat) wall_reset(2);
	user_seg = seg;
	retarget();
}

void fx_dirty(int x0, int y0, int x1, int y1)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > 159) x1 = 159;
	if (y1 > 199) y1 = 199;
	if (x0 <= x1 && y0 <= y1) dirty_add(x0 >> 1, y0, x1 >> 1, y1);
}

/* Rect in pixels (rounded out to whole bytes), both banks. */
void fx_copy_rect(unsigned dst, unsigned src, int x0, int y0, int x1, int y1)
{
	int b0, w, ya, yb;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > 159) x1 = 159;
	if (y1 > 199) y1 = 199;
	if (x0 > x1 || y0 > y1) return;
	b0 = x0 >> 1; w = (x1 >> 1) - b0 + 1;
	/* even rows (bank 0) then odd rows (bank 1) */
	ya = (y0 + 1) & ~1; yb = y1 & ~1;
	if (ya <= yb) {
		if (w == 80) fx_copy2d(dst, jr_rowoff[ya], src, 1, (unsigned)((yb - ya) / 2 + 1) * 80);
		else fx_copy2d(dst, jr_rowoff[ya] + b0, src, (yb - ya) / 2 + 1, w);
	}
	ya = y0 | 1; yb = (y1 - 1) | 1;
	if (yb > y1) yb -= 2;
	if (ya <= yb) {
		if (w == 80) fx_copy2d(dst, jr_rowoff[ya], src, 1, (unsigned)((yb - ya) / 2 + 1) * 80);
		else fx_copy2d(dst, jr_rowoff[ya] + b0, src, (yb - ya) / 2 + 1, w);
	}
}

unsigned fx_copy_delta(unsigned dst, unsigned src)
{
	return fx_delta_asm(dst, 0, src, 4000) + fx_delta_asm(dst, 0x2000, src, 4000);
}

/* ---- Drawing ----------------------------------------------------------- */


void fx_clear(unsigned pat2)
{
	fx_fillw(fx_seg, 0, 4096, (pat2 & 0xFF) * 0x101u);
	fx_fillw(fx_seg, 0x2000, 4096, (pat2 >> 8) * 0x101u);
	if (flags & FX_BUFFER || user_seg) dirty_add(0, 0, 79, 199);
	if (rowpat) wall_reset(slot);
}

void fx_pset(int x, int y, int c)
{
	unsigned char far *p;
	if ((unsigned)x >= 160 || (unsigned)y >= 200) return;
	p = (unsigned char far *)MK_FP(fx_seg, jr_rowoff[y] + (x >> 1));
	if (x & 1) *p = (unsigned char)((*p & 0xF0) | (c & 15));
	else       *p = (unsigned char)((*p & 0x0F) | (c << 4));
	touch(x, y, x, y);
}

static void spans(int y0, int y1, unsigned pat2)
{
	if (y0 & 1) pat2 = (pat2 >> 8) | (pat2 << 8);
	fx_spans_asm(fx_seg, &tab[y0], y1 - y0 + 1, pat2);
}

void fx_hspan(int y, int x0, int x1, unsigned char pat)
{
	if (y < cy0 || y > cy1) return;
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (x1 < 0 || x0 > 159) return;
	tab[y].xa = x0; tab[y].xb = x1;
	spans(y, y, pat * 0x101u);
	touch(x0 < 0 ? 0 : x0, y, x1 > 159 ? 159 : x1, y);
}

void fx_fill_rect(int x, int y, int w, int h, unsigned pat2)
{
	int x1 = x + w - 1, y1 = y + h - 1, r;
	if (x < 0) x = 0;
	if (y < cy0) y = cy0;
	if (x1 > 159) x1 = 159;
	if (y1 > cy1) y1 = cy1;
	if (x > x1 || y > y1) return;
	for (r = y; r <= y1; r++) { tab[r].xa = x; tab[r].xb = x1; }
	spans(y, y1, pat2);
	touch(x, y, x1, y1);
}

/* Convex polygon. The top vertex splits it into two chains; each chain's
 * edges are stepped in 16.16 with two 16-bit adds per row into the span
 * table, then the spans are filled in one asm loop. poly_edges fills the
 * table and returns 0 if nothing is visible; the callers pick the fill. */
static int pymin, pymax, pxmin, pxmax;

static int poly_edges(int n, const int *xy)
{
	int i, it = 0, ib = 0, ymin, ymax, chain, xmin, xmax;
	if (n < 2) return 0;
	ymin = ymax = xy[1]; xmin = xmax = xy[0];
	for (i = 1; i < n; i++) {
		int y = xy[2 * i + 1], x = xy[2 * i];
		if (y < ymin) { ymin = y; it = i; }
		if (y > ymax) { ymax = y; ib = i; }
		if (x < xmin) xmin = x;
		if (x > xmax) xmax = x;
	}
	if (ymin > cy1 || ymax < cy0 || xmin > 159 || xmax < 0) return 0;
	for (chain = 0; chain < 2; chain++) {
		int a = it;
		while (a != ib) {
			int b = chain ? (a == 0 ? n - 1 : a - 1) : (a + 1 == n ? 0 : a + 1);
			int ax = xy[2 * a], ay = xy[2 * a + 1], bx = xy[2 * b], by = xy[2 * b + 1];
			int *dst;
			if (ay == by) {
				if (ay >= cy0 && ay <= cy1) {
					dst = chain ? &tab[ay].xb : &tab[ay].xa;
					*dst = ax;
				}
			} else {
				int y0 = ay, y1 = by, dy = by - ay;
				long x = ((long)ax << 16) + 0x8000L, sl;
				sl = bx >= ax ? fx_slope(bx - ax, dy) : -fx_slope(ax - bx, dy);
				if (y0 < cy0) { x += sl * (cy0 - y0); y0 = cy0; }
				if (y1 > cy1) y1 = cy1;
				if (y0 <= y1)
					fx_edge_asm(chain ? (void far *)&tab[y0].xb : (void far *)&tab[y0].xa,
					            y1 - y0 + 1, x, sl);
			}
			a = b;
		}
	}
	if (ymin < cy0) ymin = cy0;
	if (ymax > cy1) ymax = cy1;
	pymin = ymin; pymax = ymax;
	pxmin = xmin < 0 ? 0 : xmin; pxmax = xmax > 159 ? 159 : xmax;
	return 1;
}

/* bookkeeping after a polygon fill: exact row extents for fx_track */
static void poly_touch(void)
{
	int t = track;
	if (t) ext_spans(pymin, pymax);
	track = 0;
	touch(pxmin, pymin, pxmax, pymax);
	track = t;
}

void fx_poly(int n, const int *xy, unsigned pat2)
{
	if (!poly_edges(n, xy)) return;
	spans(pymin, pymax, pat2);
	poly_touch();
}

/* after a fill that tracked its own extents into slot es */
static void poly_touch_t(int es)
{
	int t = track;
	if (es != 3) {
		if (pymin < ey0[es]) ey0[es] = pymin;
		if (pymax > ey1[es]) ey1[es] = pymax;
	}
	track = 0;
	touch(pxmin, pymin, pxmax, pymax);
	track = t;
}

void fx_poly_rows(int n, const int *xy, const unsigned char *rowpat200)
{
	int es = track ? slot : 3;
	unsigned char *pr;
	if (!poly_edges(n, xy)) return;
	pr = ext[es] + 400 + pymin;
	_fmemcpy(pr, rowpat200 + pymin, pymax - pymin + 1);
	fx_spans_pat_asm(fx_seg, &tab[pymin], pymax - pymin + 1, FP_OFF(pr));
	poly_touch_t(es);
}

void fx_poly_src(int n, const int *xy, unsigned src)
{
	int es = track ? slot : 3;
	if (!poly_edges(n, xy)) return;
	fx_spans_src_asm(fx_seg, FP_OFF(&tab[pymin]), pymax - pymin + 1, src, FP_OFF(&ext[es][pymin]));
	poly_touch_t(es);
}

void fx_track(int on)
{
	track = on;
	ext_reset(0); ext_reset(1); ext_reset(2);
}

void fx_restore(unsigned bg)
{
	int s = slot, y, y0 = ey0[s], y1 = ey1[s];
	if (y0 > y1) return;
	if (flags & FX_BUFFER || user_seg) {   /* the copy to the screen must see it */
		int b0 = 79, b1 = 0;
		for (y = y0; y <= y1; y++)
			if (ext[s][y] <= ext[s][200 + y]) {
				if (ext[s][y] < b0) b0 = ext[s][y];
				if (ext[s][200 + y] > b1) b1 = ext[s][200 + y];
			}
		if (b0 <= b1) dirty_add(b0, y0, b1, y1);
	}
	fx_restore_asm(fx_seg, bg, y1 - y0 + 1, FP_OFF(&ext[s][y0]), FP_OFF(&jr_rowoff[y0]));
	ey0[s] = 200; ey1[s] = -1;
}

void fx_tri(int x0, int y0, int x1, int y1, int x2, int y2, unsigned pat2)
{
	int xy[6];
	xy[0] = x0; xy[1] = y0; xy[2] = x1; xy[3] = y1; xy[4] = x2; xy[5] = y2;
	fx_poly(3, xy, pat2);
}

void fx_text(int x, int y, const char *s, int c)
{
	for (; *s; s++, x += 8) {
		unsigned ch = (unsigned char)*s;
		const unsigned char *g;
		int r, k;
		if (ch < 32 || ch > 127) ch = '?';
		g = jr_font + (ch - 32) * 8;
		if (x >= 0 && y >= 0 && x + 8 <= 160 && y + 8 <= 200 && !(x & 1)) {
			unsigned char hi = (unsigned char)(c << 4), lo = (unsigned char)(c & 15);
			for (r = 0; r < 8; r++) {
				unsigned char bits = g[r];
				unsigned char far *p;
				if (!bits) continue;
				p = (unsigned char far *)MK_FP(fx_seg, jr_rowoff[y + r] + (x >> 1));
				for (k = 0; k < 4; k++, bits <<= 2) {
					switch (bits & 0xC0) {
					case 0xC0: p[k] = (unsigned char)(hi | lo); break;
					case 0x80: p[k] = (unsigned char)((p[k] & 0x0F) | hi); break;
					case 0x40: p[k] = (unsigned char)((p[k] & 0xF0) | lo); break;
					}
				}
			}
			touch(x, y, x + 7, y + 7);
		} else {
			for (r = 0; r < 8; r++)
				for (k = 0; k < 8; k++)
					if (g[r] & (0x80 >> k)) fx_pset(x + k, y + r, c);
		}
	}
}

/* Same stepping as gfx_line (so the two agree pixel for pixel on screen);
 * off-screen pixels are skipped one by one, so keep lines mostly visible. */
void fx_line(int x0, int y0, int x1, int y1, int c)
{
	int dx = x1 - x0, dy = y1 - y0, sx = 1, sy = 1, err, n;
	unsigned char hi = (unsigned char)(c << 4), lo = (unsigned char)(c & 15);
	if (dx < 0) { dx = -dx; sx = -1; }
	if (dy < 0) { dy = -dy; sy = -1; }
	if ((unsigned)x0 < 160 && (unsigned)x1 < 160 && (unsigned)y0 < 200 && (unsigned)y1 < 200) {
		/* fully visible: one asm loop steps the address and the nibble */
		unsigned off = jr_rowoff[y0] + (x0 >> 1), lnp = FP_OFF(&LN);
		int yodd = y0 & 1;
		LN.mc = (unsigned)((c & 15) * 0x11) << 8 | (x0 & 1 ? 0x0F : 0xF0);
		if (sy > 0) { LN.step = yodd ? 80 - 0x2000 : 0x2000; LN.k = 80; }
		else { LN.step = yodd ? -0x2000 : 0x2000 - 80; LN.k = -80; }
		if (dx >= dy) {
			LN.esub = dy; LN.eadd = dx; LN.err = dx >> 1;
			if (sx > 0) fx_line_xr(fx_seg, off, dx + 1, lnp);
			else fx_line_xl(fx_seg, off, dx + 1, lnp);
		} else {
			LN.esub = dx; LN.eadd = dy; LN.err = dy >> 1;
			if (sx > 0) fx_line_yr(fx_seg, off, dy + 1, lnp);
			else fx_line_yl(fx_seg, off, dy + 1, lnp);
		}
		touch(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0);
		return;
	}
#define PLOT(x, y) if ((unsigned)(x) < 160 && (unsigned)(y) < 200) { \
		unsigned char far *p = (unsigned char far *)MK_FP(fx_seg, jr_rowoff[y] + ((x) >> 1)); \
		*p = (unsigned char)((x) & 1 ? (*p & 0xF0) | lo : (*p & 0x0F) | hi); }
	if (dx >= dy) {
		err = dx >> 1;
		for (n = dx + 1; n; n--) {
			PLOT(x0, y0);
			x0 += sx;
			if ((err -= dy) < 0) { err += dx; y0 += sy; }
		}
	} else {
		err = dy >> 1;
		for (n = dy + 1; n; n--) {
			PLOT(x0, y0);
			y0 += sy;
			if ((err -= dx) < 0) { err += dy; x0 += sx; }
		}
	}
#undef PLOT
	x0 = x1 - (sx > 0 ? dx : -dx); y0 = y1 - (sy > 0 ? dy : -dy);
	{
		int ax = x0 < x1 ? x0 : x1, bx = x0 < x1 ? x1 : x0, ay = y0 < y1 ? y0 : y1, by = y0 < y1 ? y1 : y0;
		if (ax < 0) ax = 0;
		if (ay < 0) ay = 0;
		if (bx > 159) bx = 159;
		if (by > 199) by = 199;
		if (ax <= bx && ay <= by) touch(ax, ay, bx, by);
	}
}

/* ---- Raycaster columns ------------------------------------------------- */

void fx_col(int bx, int y0, int y1, unsigned char pat)
{
	int n;
	if ((unsigned)bx >= 80) return;
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	if (y0 < 0) y0 = 0;
	if (y1 > 199) y1 = 199;
	if (y0 > y1) return;
	n = y1 - y0 + 1;
	fx_vfill_asm((unsigned char far *)MK_FP(fx_seg, jr_rowoff[y0] + bx), (n + 1) >> 1, pat);
	if (n > 1) fx_vfill_asm((unsigned char far *)MK_FP(fx_seg, jr_rowoff[y0 + 1] + bx), n >> 1, pat);
	touch(bx * 2, y0, bx * 2 + 1, y1);
}

static void texcol(int bx, int t, int b, const unsigned char far *tex, unsigned v, unsigned dv)
{
	int n = b - t + 1;
	fx_tex_asm((unsigned char far *)MK_FP(fx_seg, jr_rowoff[t] + bx), tex, (n + 1) >> 1, v, dv * 2);
	if (n > 1) fx_tex_asm((unsigned char far *)MK_FP(fx_seg, jr_rowoff[t + 1] + bx), tex, n >> 1, v + dv, dv * 2);
}

void fx_texcol(int bx, int y0, int y1, const unsigned char far *tex, unsigned v, unsigned dv)
{
	if ((unsigned)bx >= 80 || y0 > y1) return;
	if (y0 < 0) { v += dv * (unsigned)-y0; y0 = 0; }
	if (y1 > 199) y1 = 199;
	if (y0 > y1) return;
	texcol(bx, y0, y1, tex, v, dv);
	touch(bx * 2, y0, bx * 2 + 1, y1);
}

void fx_view(int y0, int y1, const unsigned char *rp)
{
	if (y0 < 0) y0 = 0;
	if (y1 > 199) y1 = 199;
	vy0 = y0; vy1 = y1;
	rowpat = rp;
	W.rpoff = FP_OFF(rp); W.rpseg = FP_SEG(rp);
	W.rowtab = (unsigned)(unsigned __near *)jr_rowoff;
	wall_reset(0); wall_reset(1); wall_reset(2);
}


/* The body of fx_wall and fx_wall2 (LO = 0 or 1, a constant, so each
 * copy keeps only its own branches; saves a call per column). */
#define WALL_BODY(LO) \
	int t, b; \
	unsigned char *c, *f; \
	if (!rowpat) return; \
	if (top > bot) { /* no wall: top is the horizon */ \
		t = top < vy0 ? vy0 : top > vy1 + 1 ? vy1 + 1 : top; \
		b = t - 1; \
	} else { \
		t = top; b = bot; \
		if (t < vy0) { v += dv * (unsigned)(vy0 - t); t = vy0; } \
		if (b > vy1) b = vy1; \
		if (t > vy1) { t = vy1 + 1; b = vy1; }      /* all below the view */ \
		else if (b < vy0) { t = vy0; b = vy0 - 1; } /* all above the view */ \
	} \
	c = &wc[slot][bx]; f = &wf[slot][bx]; \
	W.ce = *c; W.fs = *f; \
	LO_PAIR \
	*c = (unsigned char)t; *f = (unsigned char)(b + 1); \
	W.col = bx; W.t = t; W.b1 = b + 1; W.lo = LO; \
	W.v = v; W.dv = dv; W.dv2 = dv * 2; \
	W.texoff = FP_OFF(tex); W.texseg = FP_SEG(tex); \
	W.seg = fx_seg; \
	fx_wallcol_asm((unsigned)&W); \
	view_dirty = 1;

#define LO_PAIR
void fx_wall(int bx, int top, int bot, const unsigned char far *tex, unsigned v, unsigned dv)
{
	if ((unsigned)bx >= 80) return;
	{ WALL_BODY(0) }
}
#undef LO_PAIR

/* low detail: both bytes of the pair must agree */
#define LO_PAIR \
	if (c[1] < W.ce) W.ce = c[1]; \
	if (f[1] > W.fs) W.fs = f[1]; \
	c[1] = (unsigned char)t; f[1] = (unsigned char)(b + 1);

void fx_wall2(int bx2, int top, int bot, const unsigned char far *tex, unsigned v, unsigned dv)
{
	int bx = bx2 * 2;
	if ((unsigned)bx2 >= 40) return;
	{ WALL_BODY(1) }
}
#undef LO_PAIR

/* ---- Compiled sprites -------------------------------------------------- */

struct fx_sprite {
	int w, h, key;
	const unsigned char far *pix;
	unsigned char far *code[4]; /* [yodd * 2 + xodd] */
	unsigned len[4];
};

/* Emits [di+d] addressing: mod 01 (d8) or 10 (d16), r/m 101 (DI). */
static unsigned char far *emit_ea(unsigned char far *o, unsigned char op, unsigned char reg, int d)
{
	*o++ = op;
	if (d >= -128 && d <= 127) { *o++ = (unsigned char)(0x45 | (reg << 3)); *o++ = (unsigned char)d; }
	else { *o++ = (unsigned char)(0x85 | (reg << 3)); *o++ = (unsigned char)d; *o++ = (unsigned char)((unsigned)d >> 8); }
	return o;
}

/* Generates the code for one parity variant; returns bytes (or the size
 * needed when out == NULL). */
/* Generates the code for one parity variant into out (big enough: see
 * fx_compile); returns its length including the RETF. Each row is first
 * unpacked into px[] (color, or -1 where transparent or outside). */
static unsigned gen(const fx_sprite *s, int xo, int yo, unsigned char far *out)
{
	static int px[168];
	unsigned n = 0;
	int r, k, nb = (s->w + xo + 1) / 2;
	for (r = 0; r < s->h; r++) {
		int roff = yo ? ((r & 1) ? -0x2000 + ((r + 1) >> 1) * 80 : (r >> 1) * 80)
		              : (r & 1) * 0x2000 + (r >> 1) * 80;
		const unsigned char far *row = s->pix + (unsigned)r * s->w;
		for (k = 0; k < 2 * nb + 2; k++) {
			int x = k - xo;
			px[k] = x < 0 || x >= s->w || row[x] == s->key ? -1 : (row[x] & 15);
		}
		for (k = 0; k < nb; k++) {
			int l = px[2 * k], rr = px[2 * k + 1];
			int d = roff + k;
			unsigned char far *o = out + n;
			if (l >= 0 && rr >= 0) {
				int l2 = k + 1 < nb ? px[2 * k + 2] : -1;
				int r2 = k + 1 < nb ? px[2 * k + 3] : -1;
				if (l2 >= 0 && r2 >= 0) { /* two whole bytes: MOV word */
					o = emit_ea(o, 0xC7, 0, d);
					*o++ = (unsigned char)(l << 4 | rr);
					*o++ = (unsigned char)(l2 << 4 | r2);
					k++;
				} else {
					o = emit_ea(o, 0xC6, 0, d);
					*o++ = (unsigned char)(l << 4 | rr);
				}
			} else if (l >= 0 || rr >= 0) {
				/* one nibble: AND keeps the other, OR adds ours */
				unsigned char keep = (unsigned char)(l >= 0 ? 0x0F : 0xF0);
				unsigned char val = (unsigned char)(l >= 0 ? l << 4 : rr);
				if (val != (unsigned char)~keep) { o = emit_ea(o, 0x80, 4, d); *o++ = keep; }
				if (val) { o = emit_ea(o, 0x80, 1, d); *o++ = val; }
			}
			n += (unsigned)(o - (out + n));
		}
	}
	out[n] = 0xCB; /* RETF */
	return n + 1;
}

/* Each variant is generated once into a worst-case scratch buffer (10
 * code bytes per screen byte: AND and OR with 16-bit displacements),
 * then copied into a block of the exact size. */
fx_sprite *fx_compile(const unsigned char far *pix, int w, int h, int key)
{
	fx_sprite *s;
	unsigned char far *tmp;
	unsigned long worst = ((unsigned long)w / 2 + 2) * h * 10 + 1;
	int v;
	if (w < 1 || w > 160 || h < 1 || worst > 65000UL) return 0;
	if (!(s = (fx_sprite *)malloc(sizeof *s))) return 0;
	if (!(tmp = (unsigned char far *)_fmalloc((unsigned)worst))) { free(s); return 0; }
	s->w = w; s->h = h; s->key = key; s->pix = pix;
	for (v = 0; v < 4; v++) {
		unsigned len = gen(s, v & 1, v >> 1, tmp);
		s->code[v] = (unsigned char far *)_fmalloc(len);
		if (!s->code[v]) { while (v--) _ffree(s->code[v]); _ffree(tmp); free(s); return 0; }
		_fmemcpy(s->code[v], tmp, len);
		s->len[v] = len;
	}
	_ffree(tmp);
	return s;
}

void fx_sprite_free(fx_sprite *s)
{
	int v;
	if (!s) return;
	for (v = 0; v < 4; v++) _ffree(s->code[v]);
	free(s);
}

void fx_sprite_draw(const fx_sprite *s, int x, int y)
{
	if (x >= 0 && y >= 0 && x + s->w <= 160 && y + s->h <= 200) {
		unsigned char far *c = s->code[((y & 1) << 1) | (x & 1)];
		fx_call(fx_seg, jr_rowoff[y] + (x >> 1), FP_SEG(c), FP_OFF(c));
		touch(x, y, x + s->w - 1, y + s->h - 1);
	} else { /* clipped: per pixel over the visible part, one touch */
		int r0 = y < 0 ? -y : 0, r1 = y + s->h > 200 ? 200 - y : s->h;
		int k0 = x < 0 ? -x : 0, k1 = x + s->w > 160 ? 160 - x : s->w, r, k;
		if (r0 >= r1 || k0 >= k1) return;
		for (r = r0; r < r1; r++) {
			const unsigned char far *src = s->pix + (unsigned)r * s->w;
			unsigned char far *row = (unsigned char far *)MK_FP(fx_seg, jr_rowoff[y + r]);
			for (k = k0; k < k1; k++) {
				int c = src[k], X = x + k;
				unsigned char far *p;
				if (c == s->key) continue;
				p = row + (X >> 1);
				*p = (unsigned char)(X & 1 ? (*p & 0xF0) | (c & 15) : (*p & 0x0F) | (c << 4));
			}
		}
		touch(x + k0, y + r0, x + k1 - 1, y + r1 - 1);
	}
}

/* ---- Palette ----------------------------------------------------------- */

void fx_pal_set(const unsigned char *rgbi16)
{
	int i;
	for (i = 0; i < 16; i++) gfx_pal[i] = (unsigned char)(rgbi16[i] & 15);
	if (!gfx_mode) return;
	if (fx_irq) {
		while (pal_pend) ;
		memcpy(pal_q, gfx_pal, 16);
		pal_pend = 1;
	} else {
		wait_retrace();
		_disable();
		pal_write(gfx_pal);
		_enable();
	}
}

void fx_pal_cycle(int a, int b, int dir)
{
	unsigned char p[16];
	int i;
	memcpy(p, gfx_pal, 16);
	if (a < 0 || b > 15 || a >= b) return;
	for (i = a; i <= b; i++) {
		int j = dir > 0 ? (i == a ? b : i - 1) : (i == b ? a : i + 1);
		p[i] = gfx_pal[j];
	}
	fx_pal_set(p);
}

void fx_pal_anim(int i, int a, int b, int period, int dir)
{
	int k, any = 0;
	if (i < 0 || i >= NANIM) return;
	anim_on = 0; /* the IRQ must not step while the table changes */
	if (period > 0 && a >= 0 && b <= 15 && a < b) {
		anim[i].a = (unsigned char)a; anim[i].b = (unsigned char)b;
		anim[i].dir = (signed char)(dir < 0 ? -1 : 1);
		anim[i].period = (unsigned char)(period > 255 ? 255 : period);
	} else anim[i].period = 0;
	for (k = 0; k < NANIM; k++) {
		anim[k].cnt = anim[k].period; /* restart all in step */
		if (anim[k].period) any = 1;
	}
	if (any) { while (pal_pend) ; memcpy(pal_live, gfx_pal, 16); }
	anim_on = (unsigned char)any;
}

void fx_irq_hook(void (*fn)(void))
{
	_disable();
	irq_hook = fn;
	_enable();
}

void fx_pal_flash(int index, int rgbi, int retraces)
{
	int i, k = -1;
	if (index < 0 || index > 15 || !fx_irq) return;
	for (i = 0; i < NFLASH; i++)   /* same entry again, else a free slot */
		if (flash[i].cnt && flash[i].idx == index) { k = i; break; }
	for (i = 0; k < 0 && i < NFLASH; i++)
		if (!flash[i].cnt) k = i;
	if (k < 0) k = 0;
	_disable();
	flash[k].idx = (unsigned char)index;
	flash[k].col = (unsigned char)(rgbi & 15);
	flash[k].cnt = (unsigned char)(retraces < 1 ? 1 : retraces > 255 ? 255 : retraces);
	flash_new = 1;
	_enable();
}

int fx_split(int row, const unsigned char *top16)
{
	if (!fx_irq || row < 1 || row > 199 || !top16) {
		if (fx_split_state > 0) fx_split_state = 0;
		out_valid = 0;   /* rewrite the whole palette at the next retrace */
		return fx_split_state;
	}
	_disable();
	memcpy(pal_top, top16, 16);
	split_row = (unsigned char)row;
	if (fx_split_state == 0) fx_split_state = 1;
	_enable();
	return fx_split_state;
}

/* ---- Screenshot of the page on screen ---------------------------------- */

static const unsigned char rgb16[16][3] = {
	{0x00,0x00,0x00},{0x00,0x00,0xAA},{0x00,0xAA,0x00},{0x00,0xAA,0xAA},
	{0xAA,0x00,0x00},{0xAA,0x00,0xAA},{0xAA,0x55,0x00},{0xAA,0xAA,0xAA},
	{0x55,0x55,0x55},{0x55,0x55,0xFF},{0x55,0xFF,0x55},{0x55,0xFF,0xFF},
	{0xFF,0x55,0x55},{0xFF,0x55,0xFF},{0xFF,0xFF,0x55},{0xFF,0xFF,0xFF},
};

/* Reads the CRT page from port 3DFh's BIOS shadow (40:8Ah), i.e. what the
 * hardware is displaying, so a test sees the real result of the flips. */
int fx_screenshot(const char *path)
{
	FILE *f;
	int x, y;
	static unsigned char row[320 * 3];
	unsigned seg = (unsigned)(*(unsigned char far *)MK_FP(0x40, 0x8A) & 7) << 10;
	if (!(f = fopen(path, "wb"))) return -1;
	fprintf(f, "P6\n320 200\n255\n");
	for (y = 0; y < 200; y++) {
		unsigned char far *p = (unsigned char far *)MK_FP(seg, jr_rowoff[y]);
		unsigned char *o = row;
		/* the palette the hardware shows on this row: the split's top
		 * palette above its line, else the IRQ's (animation, flashes) */
		const unsigned char *pal = fx_split_state > 0 && y < split_row ? pal_top
		                         : fx_irq && out_valid ? pal_out : gfx_pal;
		for (x = 0; x < 160; x++) {
			int px = x & 1 ? p[x >> 1] & 15 : p[x >> 1] >> 4, k;
			const unsigned char *c = rgb16[pal[px] & 15];
			for (k = 0; k < 2; k++) { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o += 3; }
		}
		if (fwrite(row, 1, sizeof row, f) != sizeof row) { fclose(f); return -1; }
	}
	return fclose(f) ? -1 : 0;
}
