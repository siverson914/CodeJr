# JRGFX status

Graphics, keyboard, timer and sound library for the IBM PCjr (8088,
4.77 MHz). Interface: `lib/jrgfx.h`. Implementation: `lib/jrgfx.c` (C plus
Watcom `#pragma aux` inline assembly), `lib/jrfast.c` (the fast path, `fx_*`)
and `lib/jrtab.c` (8x8 font, sine table). Prebuilt library: `lib/jrgfx.lib`.

Everything in the header is implemented. Phase 1 and Phase 2 are done.

**New: the fast path (`fx_*`).**
- It draws straight into the hidden video page in the PCjr's own 4-bit format and flips pages at vertical retrace from IRQ5. There is no gfx_present and no copy.
- The demo scene runs **3-5x faster** (1.2-1.6 fps becomes 5.0, or 7.9 in low detail, at DOSBox cycles=400).
- Estimated real-PCjr rendering: **~10-15 fps** for a full-height 80-column raycaster view, and **~23 fps** for a 160x100 low-detail view on an expanded PCjr.
- See "Fast path" below. Design notes, every trick tried, the cost model and the sources are in `lib/FAST.md`.
- The old API is unchanged and still works, and you can mix the two.

**New (JRCUBE, `demos/cube`): moving objects over a static scene.**
- `fx_track` / `fx_restore`: no clears. Every fx_ call records the bytes it drew, per page and per row; `fx_restore(bg)` copies just those back from a background buffer.
- `fx_poly_rows`: a pattern byte per row, so gradients and 2x2 ordered dithers cost the same as a flat fill.
- `fx_poly_src`: a polygon filled by copying from another buffer (shadows that darken the floor's own pattern).
- `fx_pal_anim`: palette rotation run by the retrace IRQ at a steady 60 Hz pace (scrolling grids, water, twinkling stars) whatever the frame rate.
- `fx_line` now runs in asm (same pixels), clipped sprites plot directly, and `fx_compile` is about 2x faster.

## Build and link (game agents)

Use the large memory model with 8088 instructions only:

```sh
. /home/steve/Github/CodeJr/CodeJr/client/owenv.sh
cd games/doom
wcl -q -bt=dos -ml -0 -ox -wx -fe=DOOM.EXE doom.c ../../lib/jrgfx.lib
```

- In your source: `#include "../../lib/jrgfx.h"`.
- Link the prebuilt `lib/jrgfx.lib`. I only replace it after my tests pass.
- Don't run `lib/build.sh` yourself: it rebuilds the lib in place while I may be in the middle of an edit.
- If the lib looks stale or broken, compile the sources directly: `wcl ... game.c ../../lib/jrgfx.c ../../lib/jrtab.c`.
- The small or medium memory models are not supported. Watcom inline asm assumes far data pointers.

To run a program headless, use `lib/emu.sh <outdir> <cycles> <EXE> [args]`:
- It runs the program in DOSBox (`machine=pcjr`) with no window.
- It copies every file the program wrote (LOG.TXT, *.PPM) into `<outdir>`, and converts each PPM to a PNG there.
- The program's stdout goes to OUT.TXT.
- `cycles=400` is a rough stand-in for a real 4.77 MHz PCjr. Use 3000 for quick functional runs.

Library tests:
- `lib/build.sh` builds the lib, `test/T1.EXE`, `test/T2.EXE` and `DEMO.EXE`.
- T1 draws every primitive, writes SHOTn.PPM, checks VRAM against the back buffer, and tests the keyboard handler.
- T2 checks fix_mul and fix_div against `long long`.


## Fast path (fx_*): how games should use it

Mode 8 (160x200) only.

```c
if (fx_init(0)) { /* fails only like gfx_init(8) */ }
for (;;) {
    game_logic();      /* runs while the last flip waits for retrace */
    fx_begin();        /* wait until the hidden page is really hidden */
    draw();            /* fx_* calls, or your own code into fx_seg, B800 or gfx_vseg */
    fx_flip();         /* show it at the next retrace; returns at once */
}
gfx_done();            /* also undoes fx_init */
```

### What fx_init sets up

- **Two video pages.**
  - `fx_seg` is the segment of the hidden page. Its layout is the screen's: row y is at `jr_rowoff[y]` = `(y&1)*0x2000 + (y>>1)*80`, 2 pixels per byte, high nibble = left pixel.
  - B800 and `gfx_vseg` also point at the hidden page (the CPU page register follows it).
  - So code that already writes its own packed buffer to B800 or `gfx_vseg` becomes tear-free double-buffered by adding `fx_begin()` before that copy and `fx_flip()` after it.
- **IRQ5 retrace interrupt.**
  - fx_flip only queues the flip, so it costs nothing (`fx_irq` = 1).
  - If IRQ5 doesn't arrive, flips poll port 3DAh instead.
  - `fx_vbl` counts retraces (60 Hz), a free frame timer.
- **Fallback.** If no second 16 KB page is free below 128K, `fx_pages` = 1. fx_* then draws into a RAM buffer, and fx_flip copies the changed rectangles to the screen. Same code, just slower.
- **Flags:**
  - `FX_BUFFER`: always draw into a RAM buffer and copy dirty rectangles (no video wait states while drawing).
  - `FX_NOIRQ`: poll for retrace.
  - `FX_TEAR`: flip without waiting.
  - The default (0) is the fastest in every test.

**Patterns:**
- Drawing calls take pattern bytes, not colors: `FX_SOLID(c)`, or `FX_PAIR(l, r)` for two different pixels.
- Fills take a 16-bit pattern: low byte for even rows, high byte for odd rows. Use `FX_SOLID2(c)`, or `FX_DITH(a, b)` for a free checkerboard dither.

### Raycasters

1. Once, call `fx_view(y0, y1, rowpat)`. rowpat is 200 pattern bytes, one per row: the ceiling and floor colors (gradients and dithers are free).
2. Every frame, for each column 0-79, call `fx_wall(col, top, bot, tex, v, dv)`:
   - Wall rows are top..bot. They may extend past the view (clipped, with v adjusted).
   - `tex` is a column of packed texture bytes. `v` and `dv` are 8.8 texel steps, e.g. `dv = (64 << 8) / height` from a table.
   - `top > bot` means no wall, with `top` as the horizon.
3. fx_wall writes every view pixel at most once, so never clear the view. Ceiling and floor rows are written only where they changed since this page was last drawn.
4. Sprites and anything else drawn over the view with fx_* invalidate the columns they touch automatically. If you write the page yourself, call fx_view again to reset the cache.
5. **Low detail:** `fx_wall2(col2 0-39, ...)` draws 4-pixel-wide columns, about 1.5x faster. You can switch per frame.
6. Sprites: `fx_compile` once, then `fx_sprite_draw`. It is about 8x faster than `gfx_blit`.
7. Keep the view small: the cost is linear in wall pixels. A 160x100 view in low detail is the sweet spot (see FAST.md).

### Polygons and 3D

- Use `fx_poly(n, xy, pat2)` (convex), `fx_tri`, `fx_hspan`, `fx_fill_rect`. Whole bytes are written with REP STOSW, and dithering is free.
- `fx_clip(y0, y1)` limits polygons and spans to the 3D view rows.
- Draw back to front straight into the hidden page. Per-row setup dominates, so many small polygons cost more than a few big ones.
- If you keep your own packed buffer (like games/flight), you can also just copy it to `gfx_vseg` between fx_begin and fx_flip.
- Shaded faces: `fx_poly_rows(n, xy, rowpat200)` fills row y with `rowpat200[y]` (only the polygon's rows are read). Alternate the two bytes of a dither by row parity for a checkerboard or 2x2 ordered dither. `demos/cube` interpolates a light value down the rows and turns it into patterns with an XLAT loop.
- Shadows and reveals: `fx_poly_src(n, xy, seg)` copies the polygon's pixels from `seg` (page layout, e.g. `fx_alloc_buffer`). Pre-draw a darkened copy of the floor and the shadow keeps the floor's pattern.

### Moving objects over a static background (fx_track, fx_restore)

```c
bg = fx_alloc_buffer();
fx_target(bg); draw_background(); fx_target(0);
for (i = 0; i < 2; i++) { fx_copy_rect(fx_seg, bg, 0, 0, 159, 199); fx_flip(); fx_begin(); }
fx_track(1);
for (;;) {
    fx_begin();
    fx_restore(bg);   /* undo what this page drew last time */
    draw_objects();   /* any fx_ calls */
    fx_flip();
}
```

- Polygons record their exact row extents (in the fill loops themselves); other calls record their bounding box.
- Each page keeps its own record (it shows the frame before last), so the right bytes come back after every flip.
- Writes you make yourself into fx_seg are not recorded: redraw them every frame (JRCUBE's scroller does) or keep them out of the moving area.
- In FX_BUFFER mode the restored area is also marked dirty, so it reaches the screen.

### HUDs and cockpit panels

- Draw static parts once, then call `fx_sync_rows(y0, y1)`. It copies those rows to the other page too, so the HUD costs nothing per frame.
- When a number changes, redraw that part and call fx_sync_rows again for its rows.
- With page flipping, anything drawn on only one page flickers, so always sync static rows.
- `fx_text` and `fx_line` draw in the packed format. They are slower per call than the gfx_ versions, so use them for things that change rarely.
- Palette tricks with no redraw:
  - `fx_pal_set(pal16)` and `fx_pal_cycle(a, b, dir)` are queued and applied in the next retrace.
  - Use them for color cycling of water and sky, or a damage flash: e.g. set palette entry 0 to red for a few frames.
  - Avoid `gfx_palette` per frame: it waits a whole retrace on every call.
  - `fx_pal_anim(i, a, b, period, dir)`: up to 4 ranges rotated by the IRQ itself every `period` retraces. The speed doesn't depend on the frame rate. Ranges set together stay in step (JRCUBE rotates the lit and shadowed grid lines this way). A later `fx_pal_set` replaces the whole palette, phase included.

### Timing

- `tmr_ticks()`: raw 1.193182 MHz PIT counts, with no division. It is 7x cheaper than tmr_ms, so use it to profile parts of a frame.
- `TMR_TICKS_MS(t)` converts a difference to ms.
- `fx_vbl` counts 60 Hz retraces when `fx_irq` is set.

### For the two current games (no rewrite needed)

**games/doom:**
1. Replace `gfx_init(GFX_160x200)` with `fx_init(0)`.
2. Call `fx_begin()` before `view_to_screen()` and `fx_flip()` after it. B800 now maps to the hidden page, so that copy goes there.
3. After drawing the HUD with gfx_present_rows, call `fx_sync_rows(HUD_Y, 199)`. Otherwise the two pages show different HUDs and it flickers.
4. Bigger win: point draw_runs at `fx_seg` with BANK = 0x2000 (the page has the same 80-byte rows, odd rows 0x2000 later) and drop the 11.5 KB copy entirely (~45 ms per frame on a real PCjr).

**games/flight:**
1. Replace `gfx_init` with `fx_init(0)`.
2. Wrap `r_present_view` with `fx_begin()` and `fx_flip()`, and call `fx_sync_rows` after panel updates.
3. This gives tear-free frames for free. Rendering directly into `fx_seg` would save the 23 ms copy.
4. `jr_rowoff[]` and `tmr_ticks()` (your requests) are now in the header.

## Status

| Area | State |
|---|---|
| gfx_init(8) | verified (DOSBox) |
| gfx_init(9) | verified in DOSBox, on both the BIOS-pages path and the claimed-window path. Real hardware: see "Mode 9" below |
| gfx_done | restores 80x25 text |
| clear, pset, pget, hline, vline, line, rect, fill_rect | verified by screenshot. All are clipped |
| fill_tri, fill_poly (convex) | verified; 16.16 edge DDA, one division per edge. Includes vertex pixels (matches the gfx_line outline) |
| gfx_blit | verified; the keyed version uses asm |
| gfx_text | verified; 8x8 font from the IBM ROM layout, ASCII 32-127 |
| gfx_vtex (ADDED) | verified. Textured raycaster column in asm |
| gfx_present, gfx_present_rows | asm. VRAM read-back matches the back buffer byte for byte in both modes |
| gfx_vsync | polls port 3DAh bit 3 (vertical retrace). About 16 ms per call in DOSBox, i.e. 60 Hz |
| gfx_palette | follows the IBM BIOS sequence. The screenshot reflects it. Not checked on a real screen |
| gfx_screenshot | PPM P6 with the 16 PCjr default colors (CGA RGBI) after `gfx_pal`. Mode 8 is written 320x200 (pixels doubled) so the aspect ratio is right |
| fix_mul | verified exact: equals `(long long)a*b >> 16` (floor) on 2000 random pairs |
| fix_div | exact when \|b\| < 1.0. For larger b the relative error is < 2^-15. Truncates toward zero and saturates at +-7FFFFFFFh. Division by zero gives +-max |
| fix_sin, fix_cos | 1024-step table, exact to 1/65536 |
| fix_sqrt | correct, but slow (bit by bit). Avoid it per pixel |
| key_init, key_done, key_down[] | Verified in DOSBox by issuing INT 9 with AL = scan code (exactly what the PCjr's INT 48h does). Real keyboard not tested |
| tmr_init, tmr_ms, tmr_done | verified; about 1 us resolution, no ISR |
| snd_init, snd_tone, snd_noise, snd_done | writes follow the Tech Ref and run without errors. Not audible headless, so unverified by ear |
| fx_init / fx_flip / fx_begin (page flipping, IRQ5) | verified in DOSBox: IRQ5 fires (60 Hz), 3DFh and 40:8Ah follow each flip, B800 and gfx_vseg map to the hidden page. Tested in IRQ, polled, buffer, claimed-page and one-page modes (T3 flags 0, 1, 2, 3, 4, 8, 16, 18). Real PCjr: not tried |
| fx_clear, fx_fill_rect, fx_hspan, fx_poly, fx_tri, fx_pset, fx_text, fx_line, fx_col, fx_texcol | verified pixel for pixel against the gfx_* versions (T3) |
| fx_view, fx_wall, fx_wall2 | verified over 12 frames of random walls (full, low-detail and mixed) plus sprites, against a C model, on the page actually shown (T3) |
| fx_compile, fx_sprite_draw | verified against gfx_blit at random positions, all four parities, and clipped (T3). The compiler now generates each variant once (was twice, through a per-pixel accessor), and clipped sprites plot directly with one touch |
| fx_line (asm inner loops) | verified pixel for pixel against gfx_line (T3, 200 random lines) |
| fx_poly_rows, fx_poly_src | verified pixel for pixel against gfx_fill_poly coverage (T3, 160 polygons) |
| fx_track, fx_restore | verified: after 5 frames of polygons, row patterns, source copies, rects, text, lines and psets, restoring gives back the background byte for byte on both pages (T3) |
| fx_pal_anim | runs in DOSBox (JRCUBE); palette registers are write-only, so not checked on screen. Real PCjr: not tried |
| fx_copy_rect, fx_copy_delta, FX_BUFFER dirty rectangles | verified (T3) |
| fx_pal_set, fx_pal_cycle | run without errors; the IRQ applies them. Palette registers are write-only, so not checked on screen |
| tmr_ticks | verified against tmr_ms over 300 ms (T3) |
| dbg_log | verified. Opens and closes LOG.TXT on each call (a few ms), so don't call it every frame |

## API notes

**Back buffer:**
- `gfx_back` holds one byte per pixel, `gfx_w` bytes per row, values 0-15. You may write it directly: `gfx_back[(unsigned)y * gfx_w + x] = c`.
- Its segment is 64 KB, and offset 0 is pixel (0,0).
- Bytes 64000-64255 of that segment hold the gfx_present lookup table. Never write there.
- gfx_init allocates the buffer with DOS "last fit", so on the expanded PCjr it sits above 128 KB, away from video wait states.

**Pixel shape:** in mode 8, pixels are twice as wide as they are tall.

**Present cost:** gfx_present is the single biggest per-frame cost (see the benchmarks).
- Use `gfx_present_rows(y0, y1)` to copy only the rows that changed, for example just the 3D view and not the static status bar.
- The cost is linear in rows.

**gfx_vtex (raycasters):** `gfx_vtex(x, y0, y1, tex, v, dv)`:
- Draws column x, rows y0..y1, with `tex[v >> 16]`. v and dv are 16.16, and v advances by dv per row.
- Internally it is 8.8, so the texel index wraps at 256 and dv must be < 256.0.
- Clipping in y adjusts v correctly.
- `tex` is a far pointer to a column of texels (for example a 64-texel column; just keep v within 0..64).

**gfx_fill_poly:** takes convex polygons; coordinates must stay within +-16000.

**Extra globals (added to the header):**
- `gfx_mode`: 0 when not in graphics.
- `gfx_err`: why gfx_init failed. 1 = bad mode, 2 = no memory, 3 = no 32 KB video RAM for mode 9.
- `gfx_pal[16]`.

**Cleanup:** gfx_init, key_init, tmr_init and snd_init register their `*_done` with `atexit`, so `exit()` or returning from main restores text mode, INT 9 and the PIT. Calling the `*_done` functions yourself is still fine (they are idempotent). Ctrl-Break does not run atexit, so keep it out of your game loop.

**Keyboard:**
- `key_down[scancode]` is 1 while the key is held.
- The BIOS type-ahead buffer is emptied on every key, so don't mix in `getch()`/INT 16h after `key_init()`.
- The BIOS still handles Ctrl-Alt-Del, and the PCjr's key click still happens.
- The PCjr has no separate arrow keys. Its arrows send the classic 48h/4Bh/4Dh/50h codes, which is what the `KEY_UP`/`KEY_DOWN`/`KEY_LEFT`/`KEY_RIGHT` macros are.
- Each keystroke costs the PCjr about 8-9 ms with interrupts off, inside the BIOS NMI routine. Expect frame hitches while keys are typed. This is a hardware fact, not a library issue.

**Timer:**
- `tmr_ms()` works without `tmr_init()` (it calls it).
- `tmr_init` switches PIT channel 0 from mode 3 to mode 2 with the same 65536 divisor. The 18.2 Hz BIOS tick, the DOS clock and the disk BIOS are untouched, and no ISR is installed. The counter is then read as a 1.19 MHz sub-tick clock.
- The timer was deliberately NOT sped up. On the PCjr, a channel-0 reload of 300 or less hangs every INT 13h disk access (BIOS CLOCK_WAIT), and the disk BIOS bypasses INT 8 for missed ticks.
- Never touch PIT channel 1: the PCjr keyboard uses it.

**Sound:**
- `snd_tone(voice 0-2, hz, vol 0-15)` and `snd_noise(type 0-7, vol)`. Type bit 2 = white noise; bits 0-1 = rate N/512, N/1024, N/2048, or voice 2's frequency.
- vol 0 means off.
- `snd_init` sets port 61h bits 6:5 = 11 to route the SN76496 to the audio output. It changes only those bits; `snd_done` restores them.
- The SN76496 drives only the TV/monitor audio jack, never the internal beeper.

**Exit order if you do it by hand:** `gfx_done(); key_done(); snd_done(); tmr_done();`

## Mode 9 (320x200x16) and video memory

The PCjr's video RAM lives in the low 128 KB. The BIOS places 16 KB modes at page 7 (1C00:0) and 32 KB modes at pages 6-7 (1800:0). Three facts matter here:

- **B800 can't be used in mode 9.** The B800 window reaches only the first 16 KB of a 32 KB mode. Banks 2 and 3 alias banks 0 and 1. The library writes mode 9 through the physical segment (`gfx_vseg`) instead. Mode 8 uses B800:0.
- **POST reserves only 16 KB.** It sets 40:13 to 112K, and on this machine JRCONFIG reserves only 1C00-1FFF. So 1800-1BFF can hold DOS, COMMAND.COM or your own program, and BIOS mode 9 clears it.
- **DOSBox hides the problem.** It reserves all of 1800-1FFF.

What gfx_init(9) does:

1. If 1800-1FFF is reserved in the DOS memory chain (as in DOSBox), it uses BIOS mode 9 as is.
2. Otherwise it claims a free, 32K-aligned 32 KB block below 128 KB (tries 1000:0, then 0800:0) using DOS allocation.
3. It saves 1800-1BFF, sets mode 9 through INT 10h, restores 1800-1BFF, then points the CRT and CPU page registers at the claimed block (INT 10h AH=05h AL=83h).
4. If no such block is free, it returns -1 with `gfx_err` = 3. **Fall back to mode 8.**

This claim path runs correctly in DOSBox (forced with `T1 9 claim`, which got 0800:0). It has NOT been tried on the real PCjr. Whether a block is free there depends on where DOS loaded things. Mode 8 has no such issue.

Memory layout, verified against the Tech Ref and DOSBox:
- **Mode 8:** 80 bytes per row, 2 banks. Row y is at `0x2000*(y&1) + 80*(y>>1)`.
- **Mode 9:** 160 bytes per row, 4 banks. Row y is at `0x2000*(y&3) + 160*(y>>2)`.
- **Both modes:** 4 bits per pixel, and the high nibble is the left pixel.

## Benchmarks

The old API's numbers. For the fast path's numbers, the before/after table and real-PCjr estimates, see `lib/FAST.md`. Short version (DOSBox 400): present 183 ms becomes 0, demo 1.2-1.6 fps becomes 5.0 fps (7.9 fps low detail), and compiled sprites are 8x faster than gfx_blit.

Measured with DEMO.EXE in DOSBox `machine=pcjr`, `cycles=fixed 400`, using tmr_ms. DOSBox counts instructions, not clocks, so treat these as rough. A real 4.77 MHz PCjr has video-RAM wait states and an 8-bit bus, and may well be slower. The "each" times include a cheap random-number call.

| Operation | Mode 8 (160x200) | Mode 9 (320x200) |
|---|---|---|
| gfx_present (full screen) | 183 ms | 353 ms |
| gfx_present_rows(0, 99) | 92 ms | 177 ms |
| gfx_clear | 41 ms | 83 ms |
| gfx_fill_rect, full screen | 48 ms | 89 ms |
| gfx_vline, every column full height | 212 ms (6.6 us/px) | 423 ms |
| gfx_vtex, every column ~150 px | 359 ms (~11 us/px) | 718 ms |
| gfx_hline, 0-100 px random | 0.47 ms | 0.57 ms |
| gfx_line, random | 3.3 ms | 4.4 ms |
| gfx_fill_tri, ~30 px | 6.2 ms | 6.2 ms |
| gfx_fill_tri, large | 63 ms | 82 ms |
| gfx_blit 16x16, keyed / opaque | 4.9 / 1.9 ms | same |
| gfx_text, 10 chars | 10 ms | 9 ms |
| fix_mul / fix_div / fix_sin | 0.16 / 0.22 / 0.07 ms | same |
| fix_sqrt | 1.8 ms | same |
| Demo scene (sky, floor, 160 or 320 textured columns, polygon, sprite, text) + present | 1.2 fps | 0.7 fps |

Real-hardware estimate for gfx_present:
- The inner loop is LODSW/XLAT/OR/STOSB, about 40 clocks per video byte (bus bound).
- So mode 8 takes about 140 ms + video wait states. Full-screen mode 8 tops out near 6 fps before any drawing.

Advice:
- Present only the rows you need.
- Keep the 3D viewport small.
- Prefer mode 8.
- Draw columns with gfx_vline or gfx_vtex rather than pset.
- Use fix_mul (3x faster than a `long long` multiply).

## Hardware facts and sources

| Fact | Source |
|---|---|
| Mode 8/9 layout, banks, nibble order (above) | IBM PCjr Technical Reference (Nov 1983), "Graphics Storage Organization" pp. 2-56 to 2-61, https://archive.org/details/bitsavers_ibmpcpcjrPceNov83_24513363 ; DOSBox `vga_draw.cpp` (draws `byte>>4` first) |
| Port 3DFh, page register: bits 0-2 CRT page, bits 3-5 CPU page (16 KB units), bits 7:6 address mode (00 text, 01 16K graphics, 11 32K graphics). Write-only; the BIOS shadow is at 40:8A | Tech Ref p. 2-79; IBM PCjr BIOS listing SET_MODE, https://archive.org/details/pcjr-bios-transcription |
| BIOS mode set uses pages 6/6 for modes 9/A and 7/7 for 16K modes; B800 reaches only the first 16 KB in 32K modes | BIOS listing (SET_MODE, and the comment "ACCESS THROUGH ... B800H ... ONLY REACHES THE FIRST 16K"); DOSBox `int10_modes.cpp`, `vga_memory.cpp` |
| Mode 9 is accepted only with 128K (40:15). POST sets 40:13 = total - 16K | BIOS listing |
| INT 10h AH=05h: AL=80h read, 81h set CPU page, 82h set CRT page, 83h set both | BIOS listing |
| Port 3DAh read: bit 3 = vertical retrace, bit 0 = display enable (polarity differs from CGA, unused) | Tech Ref p. 2-73 |
| Video gate array: read 3DAh to reset the flip-flop, write the index then the data to 3DAh. Palette is regs 10h-1Fh, 4-bit IRGB. Video is blanked while the index is >= 10h, so write index 0 afterwards, during retrace | Tech Ref pp. 2-64, 2-71; BIOS SET_PALLETTE |
| Keyboard: NMI, then KBDNMI deserializes using PIT channel 1, INT 48h translates to an 83-key code, does OUT 60h,AL and INT 9 with AL = scan code. INT 9 is a software interrupt (no EOI needed or wanted). IRQ1 is not the keyboard on a PCjr | BIOS listing (KBDNMI, KEY62_INT); Tech Ref pp. 2-15, 2-30 |
| DOSBox pcjr: the keyboard raises IRQ6, and the stub reads port 60h, calls INT 9 with AL = scan code, and sends the EOI | DOSBox `keyboard.cpp`, `bios_keyboard.cpp` |
| SN76496 at port C0h (write-only; an IN hangs the system). 3.579545 MHz clock, n = 3579545/(32*hz). Hardware READY wait states, so no software delay is needed | Tech Ref p. 2-95 |
| Port 61h bits 6:5 audio mux: 00 8253 ch2, 01 cassette, 10 I/O channel, 11 SN76496 | Tech Ref; BIOS INT 1Ah AH=80h |
| PIT: channel 1 is used by the keyboard; disk BIOS disables NMI and counts missed ticks; a channel-0 reload <= 300 hangs INT 13h | BIOS listing (DISABLE/ENABLE, CLOCK_WAIT) |
| Vertical retrace interrupt is IRQ5 (INT 0Dh), masked after POST | Tech Ref pp. 2-15, 2-82 |
| Machine ID F000:FFFE = FDh | BIOS listing; DOSBox `bios.cpp` |

Verified in DOSBox: everything in the status table marked verified.

Assumed from the Tech Ref and BIOS listing, not tried on the real PCjr:
- palette on screen;
- sound on the speaker;
- mode 9 with only 16 KB reserved;
- real keyboard timing.

## Files

- `jrgfx.h`, `jrgfx.c`, `jrfast.c`, `jrtab.c`: the library. `jrgfx.lib`: built from them.
- `FAST.md`: the fast path's design, measurements, real-hardware cost model and sources.
- `build.sh`: builds the lib, the tests and the demo with `-wx`. It builds `jrgfx_n.lib`, runs T1 and T3 headless, and only then renames it to `jrgfx.lib` (atomic). If a test fails, the old library stays.
- `emu.sh`: headless DOSBox runner.
- `demo.c`, `fastdemo.c` / `DEMO.EXE`: `DEMO F [/F:frames] [/S] [/B] [/N]` runs the fast-path benchmarks (LOG.TXT, `fxbench` lines) and then three scenes with an on-screen fps counter: raycaster, low-detail raycaster, polygons. `/B` = FX_BUFFER, `/N` = FX_NOIRQ. `/S` writes `F*.PPM` from the page actually on screen.
- `DEMO [8|9] [/F:frames] [/S]`: the old-API demo. It runs the benchmarks, then an animated scene, logging to LOG.TXT. `/S` writes screenshots `D8*.PPM`. ESC quits early.
- `test/t1.c`, `test/t2.c`, `test/t3.c`: the tests. `T3 [flags]` checks the whole fast path against the old API and C models, and logs PASS/FAIL lines.
