# JRDOOM working notes

## Requests for lib

All of these were answered by the library's fast path (fx_*), which JRDOOM
now uses. Kept here as a record.

- **Page flipping in mode 8** (done: `fx_init`, `fx_begin`, `fx_flip`).
  JRDOOM draws its view straight into the hidden page and never copies it.
- **The video row layout** (done: `jr_rowoff[]` in the header). JRDOOM's
  assembly assumes 80-byte rows with odd rows 0x2000 after even rows.
- **A cheap timer** (done: `tmr_ticks()`). `tmr_ms()` costs about 500
  instructions per call (two 32-bit divisions); calling it a dozen times a
  frame for profiling cost several ms per frame.
- **HUD on both pages** (done: `fx_sync_rows`).
- Palette changes without waiting for retrace (done: `fx_pal_set`), used
  for the red damage flash and the pickup flash.

Still open, nice to have:

- `fx_sprite_draw` clipped to a row range, so a bobbing weapon could slide
  below the view's bottom edge without touching the status bar.

## No TEMP code

The library existed (and worked) before JRDOOM needed it, so nothing in
apps/jrdoom is a stand-in for library code.

## Cost model (DOSBox, cycles=fixed 400)

DOSBox's normal core charges one cycle per instruction, including each
iteration of `rep movsw` and `MUL`/`DIV`. So 400 cycles/ms is about 400
instructions/ms, and instruction count is what to optimize. Measured
(LOG.TXT with /PROF): see README.md for the table.
