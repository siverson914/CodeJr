# JRFLIGHT notes

## What JRFLIGHT uses from JRGFX

- `fx_init(0)`, `fx_begin`, `fx_flip`, `fx_seg`, `fx_pages`, `fx_sync_rows`,
  `jr_rowoff[]`: the 3D view is drawn straight into the hidden page and
  page-flipped at retrace. `/LD` uses plain `gfx_init(8)` instead.
- `gfx_vseg` (panel rectangles, `/COPY`), `gfx_back` + `gfx_screenshot`
  and `fx_screenshot` (screenshots), `key_*`, `tmr_ms`, `snd_*`,
  `fix_mul`/`fix_div` (flight model), `dbg_log`.
- Linked against the prebuilt `lib/jrgfx.lib` (`./build.sh src` compiles
  the lib sources instead). No TEMP code.

The drawing itself stays in `raster.c` (spans, polygons, sky bands) rather
than `fx_poly`/`fx_hspan`: it already writes the same packed format with a
`REP STOSW` scanline loop in assembly, and it also handles the line-doubled
low-detail layout (the view in bank 0 only), the small windows and the
2-pixel-wide mode, which `fx_poly` doesn't.

## Fast path, measured

DOSBox 400 fps went from 2.7-5.4 (copy, no flipping) to 2.7-5.2 with page
flipping at the same settings, and up to 3.7-6.0 with `/FAST`. The frame
here is CPU-bound (culling, transforms, the attitude ball), so removing the
15-23 ms copy mostly buys tear-free frames; the bigger wins came from the
smaller windows. Panel rectangles are written to both pages directly
(the shown page is last frame's `fx_seg`), which is cheaper than
`fx_sync_rows` of whole rows.

## Requests for lib

1. ~~Packed drawing~~: done (`fx_*`).
2. ~~`gfx_vseg` in the header~~: done.
3. ~~`jr_rowoff[]`~~: done.
4. ~~Cheaper timer~~: done (`tmr_ticks`).
5. **The shown page's segment** (e.g. `fx_shown_seg`): JRFLIGHT infers it
   as last frame's `fx_seg` to write small panel updates to both pages.
6. **A linear-row variant for the 160x100 mode**, if it gets verified on a
   real PCjr, so `/LD` could page-flip too (it currently draws in RAM and
   copies, single page).
