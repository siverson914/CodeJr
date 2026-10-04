# JRGFX fast path: design notes and measurements

`fx_*` in `jrgfx.h`, implemented in `jrfast.c`. This file covers why it is built
this way, every trick that was tried, and what each one buys. The short
"how to use it" guide is in STATUS.md.

## The idea in one paragraph

The old pipeline does three passes over the screen every frame:
1. clear a 64 KB one-byte-per-pixel buffer;
2. draw into it;
3. convert it to the PCjr's 4-bit format through a lookup table (gfx_present).

The fast path removes passes 1 and 3:
- It draws directly in the PCjr's own format (2 pixels per byte, the two-bank interleave) into the **hidden video page**.
- The PCjr's page register (port 3DFh) then shows that page at the next vertical retrace, from the retrace interrupt (IRQ5).
- A frame costs no copy and no conversion. The raycaster column renderer also writes each pixel at most once per frame, and skips ceiling and floor pixels that are already correct on that page.

## Memory layout and page flipping

- **Page layout:** same as the screen. Row y is at `(y&1)*0x2000 + (y>>1)*80`, and the high nibble is the left pixel. `jr_rowoff[y]` holds the offsets.
- **Two 16 KB pages in the low 128K:**
  - The BIOS puts mode 8 at page 7 (1C00:0). The second page is page 6 (1800:0) when DOS keeps 1800-1BFF out of its pool (DOSBox does).
  - Otherwise fx_init claims a free 16K-aligned block below 128K with the same DOS-chain technique that gfx_init(9) uses. It tries 1800, 1400, 1000, 0C00 and 0800.
  - If no block is free, it falls back to one page plus a RAM buffer and dirty-rectangle copies (`fx_pages == 1`).
  - Test flag 16 forces the claim path, which got 1000:0 in DOSBox. Test flag 8 forces the fallback. Both pass T3.
- **Port 3DFh:** `modebits | cpu_page << 3 | crt_page`.
  - The CRT page is the one shown. The CPU page is the one B800 maps to.
  - fx_init and every flip set the CPU page to the hidden page. Code that writes B800 (games/doom's `view_to_screen`) or `gfx_vseg` (games/flight) therefore draws into the hidden page with no change. T3 verifies this through B800 and through the physical segment.
  - The BIOS shadow at 40:8Ah is kept in step.
  - Sources: PCjr Tech Ref p. 2-79; PCJR.TXT (https://pdos.csail.mit.edu/6.828/2014/readings/hardware/vgadoc/PCJR.TXT).
- **IRQ5 flips:**
  - The PCjr raises IRQ5 at vertical retrace. It was the first PC with a retrace interrupt, meant for page flipping (Tech Ref pp. 2-15, 2-82; https://en.wikipedia.org/wiki/Raster_interrupt).
  - fx_init installs an INT 0Dh handler and unmasks IRQ5. It checks that the interrupt arrives within 100 ms, and falls back to polling port 3DAh bit 3 if it does not.
  - fx_flip only queues the new 3DFh value, so the game's logic overlaps the wait for retrace. fx_begin (and gfx_present) wait for the flip only if it hasn't happened yet.
  - The handler also counts frames (`fx_vbl`, 60 Hz) and applies a queued palette (fx_pal_set).
- **The page register is not latched:** it drives the gate array's address lines directly, unlike the 6845 start address. So a flip must happen in retrace, which IRQ5 guarantees. One caveat: the PCjr's keyboard NMI runs with interrupts off for 8-9 ms per key. A keystroke that lands just before retrace can delay a flip into the visible frame, so expect an occasional torn frame while keys are typed.

## Measurements

DOSBox 0.74, `machine=pcjr`, `cycles=fixed 400`, tmr_ms, `DEMO F`. DOSBox charges one cycle per instruction and one per REP iteration, with no wait states. So it **flatters REP string copies and fills** (a REP MOVSW word costs about 25 clocks on a real 8088) and is roughly right for ordinary code. The real-PCjr columns are estimates (see the model below), not measurements.

Before is the old API (byte-per-pixel buffer + gfx_present). After is the fast path. Both are mode 8 (160x200), DOSBox cycles=400.

| Operation | Before (old API) | After (fast path) | Gain |
|---|---|---|---|
| Get a frame on screen | gfx_present: 183 ms | fx_flip: 0 ms (queued to IRQ5). FX_BUFFER full copy: 21 ms. HUD-only dirty copy: 4.6 ms | the whole present |
| Clear | gfx_clear: 41 ms | fx_clear: 21.5 ms; not needed at all with fx_wall | |
| Full-screen filled rectangle | 48 ms | 41 ms | 1.2x |
| Solid columns, whole screen | gfx_vline x160: 212 ms | fx_col x80: 118 ms | 1.8x |
| Textured columns, ~150 px | gfx_vtex x160: 359 ms | fx_texcol x80: 195 ms | 1.8x, and no present |
| Raycaster columns, 160-row view | n/a | fx_wall x80 (heights change every frame): 193 ms. Same heights: 104 ms. fx_wall2 x40: 117 ms | |
| Triangle ~30 px / large | 6.2 / 63 ms | 4.6 / 34 ms | 1.3x / 1.9x |
| Sprite 16x16 with transparency | gfx_blit: 4.9 ms | compiled fx_sprite: 0.62 ms | 8x |
| Palette change | gfx_palette: waits for a retrace each call (~16 ms) | fx_pal_set / fx_pal_cycle: queued, 0 ms | |
| Timer read | tmr_ms: 1.2 ms | tmr_ticks: 0.16 ms | 7.5x |
| Text, 10 chars | 10.4 ms | fx_text: 14.2 ms | slower (C nibble code; fine for HUDs drawn on change) |
| Line, random | 3.3 ms | fx_line: 8.2 ms | slower (nibble read-modify-write per pixel) |
| **Demo scene** (old: sky, floor, 160 textured columns, polygon, sprite, text) | **1.2-1.6 fps** | raycaster scene (80 textured columns, ceiling/floor, 2 sprites, HUD, palette cycling): **5.0 fps** with page flipping, 4.1 fps with FX_BUFFER. Low detail (fx_wall2): **7.9 fps**. Polygon scene (sky, ground, 17 polygons, dithering): **4.7 fps** | 3-5x |

FX_NOIRQ (polled flips) measured the same as IRQ flips here (4.9 / 7.9 / 4.8 fps), because the demo has no game logic to overlap with the wait. In a real game, the queued IRQ flip lets the logic run during the wait.

### Real-PCjr cost model (estimates)

- **Bus speed:** the 8088 spends 4 clocks per bus byte. On the PCjr, the low 128 KB (system board RAM, shared with the video gate array) takes about 6 clocks per access, and sidecar expansion RAM takes 4. Brutman: "four cycle memory reads, instead of six", 1.2 us against 840 ns per access (https://www.brutman.com/PCjr/pcjr_hardware.html). Code running in the low 128K is "up to 2.6 times slower" than on a 5150 (riq, https://retro.moe/2018/09/26/64k-ram-ought-to-be-enough-a-demo-for-the-pcjr/).
- **Two configurations:**
  - **A** = 128K PCjr. Everything (code, tables, buffers, video) is in slow low RAM: 6 clocks per byte.
  - **B** = expanded PCjr. Code, tables and RAM buffers are above 128K: 4 clocks per byte for them, 6 for video RAM.
- **Per-loop cost:** max(execution-unit clocks, bus bytes x clocks per byte). The bus bytes are opcode bytes plus data bytes. Clock counts are from Intel's 8088 tables (https://www2.math.uni-wuppertal.de/~fpf/Uebungen/GdR-SS02/opcode_i.html); REP MOVSW is 25 clocks per word (https://www.vogons.org/viewtopic.php?p=1370080).

| Inner loop | Code bytes + data | A: 128K jr | B: expanded jr |
|---|---|---|---|
| old gfx_present, per video byte (LODSW XLAT OR STOSB) | 5 + 3 reads + 1 write | 56 clk | 41 clk |
| old gfx_vtex, per pixel (2 per screen byte) | 9 + 1 + 1 | 66 clk | 44 clk |
| old gfx_clear, per word (REP STOSW, 64000-byte buffer) | 2 writes | 14 clk | 14 clk |
| fx_wall / fx_texcol, per screen byte (MOV AL,DH / XLAT / STOSB / ADD DI,79 / ADD DX,SI) | 9 + 1 + 1 | 66 clk | 46 clk |
| fx_wall2 (low detail), per screen byte (... MOV AH,AL / STOSW / ADD DI,78 ...) | 11 + 1 + 2 per 2 bytes | 42 clk | 30 clk |
| ceiling/floor delta, per byte (MOVSB / ADD DI,imm16) | 5 + 1 + 1 | 42 clk | 30 clk |
| fx_col, per byte (STOSB / ADD DI,79) | 4 + 1 write | 30 clk | 22 clk |
| span fill (REP STOSW), per byte | | 7 clk | 7 clk |
| full 16 KB copy RAM to page (REP MOVSW), per word | 2 reads + 2 writes | ~25-30 clk | 25 clk |
| compiled sprite, per 2 opaque bytes (MOV [DI+d8],imm16) | 5 + 2 writes | 42 clk | 32 clk |
| old keyed blit, per pixel (LODSB CMP JE MOV INC LOOP) | 11 + 1 + 1 | 78 clk | 52 clk |
| per span row in fx_poly (clip, nibble ends, setup) | ~80 code bytes | ~480 clk | ~320 clk |
| per fx_wall call (C clip + parameter block + asm setup, ~110 instructions) | | ~2000 clk | ~1300 clk |

Frame estimates at 4.77 MHz, rendering only (the game's own ray casting or 3D transforms come on top):

| Scene | A: 128K jr | B: expanded jr |
|---|---|---|
| Old API, same content as the raycaster scene (64000-byte clear, 160 textured columns ~100 px, gfx_present) | ~500 ms (2 fps) | ~380 ms (2.6 fps) |
| fx_wall, 80 columns, 160-row view, average wall 100 rows | ~150 ms (6.5 fps) | ~105 ms (9.5 fps) |
| fx_wall2 (40 columns), same view | ~90 ms (11 fps) | ~64 ms (15.5 fps) |
| fx_wall2, 160x100 view (rows 0-99), average wall 60 rows | ~60 ms (16 fps) | ~43 ms (23 fps) |
| Polygon scene as in the demo (160x160: sky, ground, 16 quads, pentagon) | ~190 ms (5.3 fps) | ~136 ms (7.3 fps) |
| Flight-style 160x120 view: sky/ground spans + 30 small polygons (~20 rows each) | ~115 ms (8.7 fps) | ~85 ms (12 fps) |

These are estimates from the cost model above, not measurements: there is no real PCjr in the loop.

## What each trick buys (and what didn't pay)

| Trick | Status | Measured (DOSBox 400) | Real-PCjr estimate |
|---|---|---|---|
| Native packed drawing (no gfx_present conversion) | **used** | gfx_present 184 ms becomes 0 | saves 131-187 ms per frame |
| Page flipping through 3DFh, flipped from IRQ5 | **used** | flip 0 ms (queued); polled flip waits up to a frame | saves the 16 KB copy (~42-50 ms) and gives tear-free frames |
| B800 and gfx_vseg follow the hidden page | **used** | n/a | lets the games' own copy code double-buffer with one call |
| Each pixel written once: wall columns paint ceiling/floor only where they changed on this page (per-page, per-column cache) | **used** | fx_wall with unchanged heights: 104 ms vs 193 ms when heights change | ceiling/floor cost is near zero when walking straight |
| No clear needed (walls cover the view) | **used** | gfx_clear 41 ms becomes 0 | saves ~94 ms (64000-byte clear) |
| Whole column in one asm routine driven by a parameter block in DGROUP | **used** | 2-row wall call: 0.58 ms becomes 0.44 ms | ~25% less per-column overhead |
| Low-detail columns (fx_wall2: 4-pixel columns, one STOSW per row) | **used, optional** | raycaster scene 5.0 fps becomes 7.9 fps | ~1.5x on walls |
| Compiled sprites (sprite becomes MOV [DI+d],imm code; 4 parity variants; word stores; AND/OR only on half-covered bytes) | **used** | 0.61 ms vs 5.1 ms keyed blit (8x) | ~6x per pixel, and no present |
| Dither patterns as pattern bytes (even/odd row bytes) | **used** | free | free |
| Edge stepping with two 16-bit adds (16.16 in DX:AX, slope in SI:BX), one pair of DIVs per edge | **used** | fx_tri big 34 ms vs 63 ms | |
| Palette writes queued to the retrace IRQ (fx_pal_set, fx_pal_cycle) | **used** | 0 ms per call (gfx_palette waits a whole frame per register) | color cycling for sky and water and damage flashes at no draw cost |
| Dirty rectangles (FX_BUFFER mode: up to 8 per frame, union of the last two frames copied) | **used, optional** | HUD 160x40: 4.6 ms vs full 21 ms | copy cost scales with the changed area |
| Delta present (REPE CMPSW against the page, REP MOVSW only the differing runs) | **implemented, not recommended** | identical: 2.1 ms; 1/16 changed: 22 ms; all changed: 43 ms (full copy: 21 ms) | **worse than a plain copy on real hardware.** A compare reads 4 bytes per word, just as a copy moves 4, and REPE CMPSW is ~30 clocks per word against 25 for MOVSW. It only wins in DOSBox, which charges REP compares like one instruction each |
| Raw timer (tmr_ticks) | **used** | 0.16 ms vs 1.2 ms for tmr_ms | no 32-bit division |
| Reciprocal table for texture steps (`recip[h]` in fastdemo.c) | **used in the demo** | | one DIV (~150 clk) per column becomes one table load |
| Quarter-square multiply tables | **evaluated, not used** | | an 8x8 lookup multiply is ~50 clk against 70-77 for MUL r8. A 16x16 multiply needs 3-4 lookups (more than IMUL's 128-154), so the gain is too small for 3D transforms |
| Compiled texture scalers (Wolf3D style: one routine per wall height) | **evaluated, not used** | | ~8 code bytes per pixel, so no faster than the DDA loop on an 8-bit bus, and 80+ KB of code |
| 160x100 line-doubled mode (Trixter, 2013): BIOS mode 8, then port 3DFh with address-mode bits 7:6 = 00; rows become linear, each shown twice | **documented, not implemented** | cannot be tested: DOSBox 0.74 forces graphics row addressing in `TandyCheckLineMask` | halves every fill and wall cost and makes rows linear. See "Next steps" |
| Racing the beam / retrace-aware partial copies | **not needed** | | with page flipping nothing is ever copied to the visible page. Only the one-page fallback copies to the visible page, and a 16 KB copy (~45 ms) is slower than one frame (16.7 ms) anyway |

## Demoscene sources used

- **Trixter, 8088 Domination (2014):** encodes each frame as code that writes only changed bytes. "Minimizing code size typically yields the fastest results", because each byte costs 4 cycles. That rule guided every inner loop here. https://trixter.oldskool.org/2014/06/19/8088-domination-post-mortem-part-1/
- **Scali, 8088 MPH sprites:** compiled sprites, separate code for even and odd start lines because of the CGA-style interleave, all even lines then all odd lines. The fx_sprite variants follow this. https://scalibq.wordpress.com/2015/04/19/8088-mph-sprites-where-were-going-we-dont-need-sprites/
- **Scali, 8088 MPH polygons:** a span-delta renderer that draws only the span differences from the previous frame. Not implemented (see "Next steps"). The ceiling/floor cache in fx_wall applies the same idea to raycaster columns. https://scalibq.wordpress.com/2015/04/23/8088-mph-the-polygons/
- **Trixter's 160x100x16 PCjr mode,** used in INTROjr/SHAKEjr (2013) and in riq's PCjr demo (2018). Its setup is in `part3/part3.asm`, `set_vid_160_100_16`. https://gitlab.com/ricardoquesada/pcjr-flashparty-2018 ; http://nerdlypleasures.blogspot.com/2016/01/milestones-in-ibm-pc-compatible.html

## Next steps (not done)

1. **160x100 mode (FX_LORES).**
   - Mode 8, then OUT 3DFh with bits 7:6 = 00.
   - Rows are linear (80 bytes each, 100 of them = 8000 bytes), so two frames fit in one 16 KB page. A second option is flipping with the 6845 start address (R12/R13), which the 6845 latches per frame and so needs no retrace timing.
   - Every fx loop would need a linear-stride variant.
   - It needs a real PCjr (or an emulator that models the gate array) to verify. DOSBox 0.74 cannot show it.
2. **Data-driven compiled sprites (Scali):** MOVSW from the sprite data for opaque runs costs 5 bus bytes per word against 7 for MOV [DI+d8],imm16.
3. **Span-delta polygons (Scali)** for the flight sim: per-row span lists compared with the previous frame of the same page.
4. **Batched wall columns** (one call for all 80) to cut the ~110-instruction per-column overhead further.
