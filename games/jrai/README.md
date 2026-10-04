# JRAI: PCjr MEETS AI

A demoscene-style demo for the IBM PCjr (8088 at 4.77 MHz, 16 colors, the
SN76496 sound chip), written by CodeJr. About two minutes, with a 3-voice
chiptune and a robot voice.

Run `JRAI` from DOS. Esc quits, Space skips to the next part.

## The parts

| | Part | What it does |
|---|---|---|
| 0 | Boot | 40-column text. `C>CODEJR`, Steve asks for a demo, "CodeJr is thinking". The thinking is real: it covers the demo's precompute (plasma, tunnel texture, tunnel code). Then the screen dissolves into noise. |
| 1 | Plasma | "HELLO, P C JUNIOR." in SAM's voice, then the title over a full-resolution plasma. The plasma is drawn once. The retrace interrupt rotates 14 palette entries, so it moves at 60 Hz with no drawing at all. The colors turn hot when the drums come in. |
| 2 | Fire | Classic fire with words burning in it: 1984, 2026, PCjr, + AI. Kick drums make it roar. |
| 3 | Tunnel | "Into the neural net": a flight down a tunnel of circuit traces with pulses of light running along them. |
| 4 | Meeting | A PCjr and a robot AI under the stars. The robot says its first line aloud with its LED mouth moving, then they talk in a speech box, and a heart rises. |
| 5 | Finale | Sine scroller in rotating rainbow colors, raster bars, a parallax starfield, a VU meter of the four voices, greetings, THE END. |

## How it's fast enough

- **Palette tricks.** The plasma, the tunnel's light pulses, the star twinkle and the scroller's rainbow are all palette entries rotated by the retrace interrupt (`fx_pal_anim`). That's free animation.
- **The tunnel writes its own code.** At startup it generates 8088 code with one `mov al,[bx+d1] / mov ah,[bx+d2] / stosw / mov es:[di+1FFEh],ax` group per pair of screen bytes, about 43 KB, with no loops and no address math. Each frame patches BX (the angle and depth offset) and calls it. The 64 KB texture segment wraps the offsets for free. Far cells read a darker copy of the texture, and the code switches DS between the two copies.
- **Fire in assembly** (`#pragma aux`). Heat is 0-63, so four neighbours sum to at most 252, and two cells add in one word with no carry between them. Cooling is an `XLAT`. Cells are 1 byte by 8 rows, drawn as a 2-color checkerboard (11 shades from 5 colors), and a cell pair is skipped when that page already shows the same shades.
- **The finale fills each row once.** Every row below the logo is either a bar color or black, written with one `rep stosw`. The scroller works from a precomputed column mask of the whole message.
- **Page flipping on the retrace IRQ**, from the library's fast path (`lib/FAST.md`).

## The music and the voice

`music.c` is a small tracker: lead (voice 0, with vibrato), bass (voice 1), chord arpeggios at 60 Hz (voice 2, the classic chip sound), and drums on the noise voice. It runs inside the retrace interrupt, chained after the library's handler, so it keeps time whatever the drawing does. Every part starts on its pattern's downbeat. The demo clock T comes from the same interrupt.

The voice is SAM (Software Automatic Mouth, 1982), rendered by `genspeech.py` through `say.cjs` (node and `npm i sam-js`), resampled to 6000 Hz and stored as 4-bit SN76496 attenuations (24 KB for three phrases). On the PCjr, tone voice 0 is set to an inaudible 111 kHz so it acts as a steady level, and each sample is a write to its volume register, timed from the PIT counter. Music pauses while it speaks. `speech.c` is generated and kept in the repo, so a normal build needs neither node nor SAM.

## Building and testing

```sh
games/jrai/build.sh          # or: just jrai
just jrai-video              # games/jrai/jrai-preview.mp4, with sound, no window
```

Options, for testing: `/PART:n` start at part n (0-4), `/ONLY` just that part, `/NOBOOT`, `/QUIET`, `/LOCK[:n]` frame-locked (T advances n ticks a frame, so results don't depend on speed), `/SHOT:a,b,...` screenshots at those T values, `/BENCH` frame rates and precompute times to `LOG.TXT`, `/ONEPAGE` test the one-page fallback, `/BOOTDUMP` the boot screen to `LOG.TXT`, `/VIDEO` every frame as `Fnnnn.PPM` plus every sound chip write in `SND.BIN` (used by `video.sh`; `render.py` emulates the SN76496 to make the audio).

## Measurements (DOSBox only)

DOSBox `machine=pcjr`, `cycles=fixed 400`, which `lib/FAST.md` uses as a rough stand-in for a real PCjr. DOSBox charges one cycle per instruction and has no wait states, so the real machine will differ, probably slower in the memory-heavy parts.

| Part | Before tuning | Now |
|---|---|---|
| Plasma | 53 fps | (nothing to draw) |
| Fire | 1.5 fps | 8.8 fps |
| Tunnel | 17 fps | 17 fps |
| Meeting | 8.3 fps | 41 fps |
| Finale | 7.4 fps | 18.8 fps |
| Precompute at startup | 42 s | 8.7 s |

What made the difference: the fire called a 32-bit random function for every burning cell (now a list of cell offsets and a noise table, plus the assembly loops); the meeting redrew everything each frame (now only what changed on that page); the finale cleared the screen and then drew bars over it (now one fill per row).

## Not tested on the real PCjr

Nothing here has run on real hardware yet. Things to watch:

- **Memory.** About 290 KB beyond the program: two 64 KB tunnel textures, 64 KB of tunnel code, the library's 64 KB back buffer, the 16 KB plasma. Fine with 640K. If a texture can't be allocated, the tunnel uses one; with no room at all it says so.
- **The second video page.** The PCjr shows only 16 KB pages in the low 128K, and this PCjr reserves one. The 86 KB program probably loads right where a second page would go, so the library may fall back to one page plus a RAM buffer. JRAI then copies the whole frame each time (correct, slower, may tear). `/ONEPAGE` tests this path in DOSBox. `/BENCH` logs which mode it got (`fx_pages`) and where the program loaded (`psp`).
- **The voice.** The volume-register trick is used by other PCjr and Tandy demos, but nobody has heard this one on the real chip yet.
