# JRAI: PCjr MEETS AI

A demoscene-style demo for the IBM PCjr (8088 at 4.77 MHz, 16 colors, the
SN76496 sound chip), written by CodeJr. About two and a half minutes, with
a 3-voice chiptune and spoken dialog.

Run `JRAI` from DOS, in the folder with `SAY.EXE` (speech/) and its voice
clips (`J*.SND`, 13 files, 366 KB). Without them it runs silent where the
voices would be, and the dialog just shows as text. Esc quits, Space skips to the next part.

## The parts

| | Part | What it does |
|---|---|---|
| 0 | Boot | 40-column text. `C>CODEJR`, Steve asks for a demo, "CodeJr is thinking". The thinking is real: it covers the demo's precompute (plasma, tunnel texture, tunnel code). Then the screen dissolves into noise. |
| 1 | Plasma | The robot says "Hello... P.C. junior.", then the title over a full-resolution plasma. The plasma is drawn once. The retrace interrupt rotates 14 palette entries, so it moves at 60 Hz with no drawing at all. When "AI" lands, the music drops out for the announcer: "P.C. Junior meets A.I." The colors turn hot when the drums come in. |
| 2 | Fire | Classic fire with words burning in it: 1984, 2026, PCjr, + AI. Kick drums make it roar. |
| 3 | Tunnel | The announcer: "Entering the neural net." Then a flight down a tunnel of circuit traces with pulses of light running along them. |
| 4 | Meeting | A PCjr and a robot AI under the stars, talking. Every line is typed into a speech box, then the music drops out and the line is spoken (the robot in a deep, wise voice, the PCjr in a kid's voice), then the music picks up where it stopped. A heart rises at "Friends?" |
| 5 | Finale | Sine scroller in rotating rainbow colors, raster bars, a parallax starfield, a VU meter of the four voices, greetings, THE END, and the announcer: "Long live the P.C. Junior!" |

## How it's fast enough

- **Palette tricks.** The plasma, the tunnel's light pulses, the star twinkle and the scroller's rainbow are all palette entries rotated by the retrace interrupt (`fx_pal_anim`). That's free animation.
- **The tunnel writes its own code.** At startup it generates 8088 code with one `mov al,[bx+d1] / mov ah,[bx+d2] / stosw / mov es:[di+1FFEh],ax` group per pair of screen bytes, about 43 KB, with no loops and no address math. Each frame patches BX (the angle and depth offset) and calls it. The 64 KB texture segment wraps the offsets for free. Far cells read a darker copy of the texture, and the code switches DS between the two copies.
- **Fire in assembly** (`#pragma aux`). Heat is 0-63, so four neighbours sum to at most 252, and two cells add in one word with no carry between them. Cooling is an `XLAT`. Cells are 1 byte by 8 rows, drawn as a 2-color checkerboard (11 shades from 5 colors), and a cell pair is skipped when that page already shows the same shades.
- **The finale fills each row once.** Every row below the logo is either a bar color or black, written with one `rep stosw`. The scroller works from a precomputed column mask of the whole message.
- **Page flipping on the retrace IRQ**, from the library's fast path (`lib/FAST.md`).

## The music and the voices

`music.c` is a small tracker: lead (voice 0, with vibrato), bass (voice 1), chord arpeggios at 60 Hz (voice 2, the classic chip sound), and drums on the noise voice. It runs inside the retrace interrupt, chained after the library's handler, so it keeps time whatever the drawing does. Every part starts on its pattern's downbeat. The demo clock T comes from the same interrupt.

**The voices** are ElevenLabs voices through gipity, turned into PCjr clips by SAY's converter (`speech/mksnd.py`, see `speech/README.md`) at its default quality: SAY's `.SND` format, the three SN76496 tone voices held at period 1 as one 256-level DAC, the method that won on the real PCjr. `voice/lines.txt` lists every line (name, speaker, text). `mkvoice.sh` (`just jrai-voice`) makes `voice/NAME.mp3` once through gipity and keeps it in the repo, then makes `voice/out/NAME.SND`. The speakers:

| Speaker | ElevenLabs voice |
|---|---|
| the robot AI | Atlas (wise, deep) |
| the PCjr | Brittney (a child: the robot calls it "kid") |
| the announcer | a deep game announcer ("Gaming - Unreal Tonemanagement 2003") |

To change a line, edit `voice/lines.txt` and run `mkvoice.sh NAME`. Whisper (small.en) heard all 13 lines word for word in the preview video's soundtrack. "Meets" in the title needed a rewrite: as "P.C. junior... meets... A.I.!" it was heard as "miss".

**Playing them.** JRAI runs `SAY /Q NAME` for each line (`voice.c`), with the music paused, and picks the music up where it stopped. SAY is the player proven on the real PCjr: it turns off every interrupt but its timer while it plays, so the picture holds still (the robot and the PCjr show a talking pose), and T stops too, so the parts' clocks skip the line. Afterwards JRAI puts PIT channel 0 back in mode 2 (SAY leaves the BIOS's mode 3). The tunnel gives its 192 KB back to DOS when it ends, so SAY has room in the meeting. `/SAYH` passes SAY `/H` for DOSBox, which lacks the 8259 poll command SAY uses.

A first version had its own player inside JRAI, which kept the retrace interrupt running so the mouth could move. It played on time in DOSBox, but SAY was already proven and sounds great on the PCjr, so JRAI uses SAY. In `/VIDEO` mode JRAI still reads the clips itself, logs the writes SAY would make, and moves T on, so the preview video has the voices.

## Building and testing

```sh
games/jrai/build.sh          # or: just jrai
just jrai-video              # games/jrai/jrai-preview.mp4, with sound, no window
```

The clips must be next to `JRAI.EXE`: `just deploy-jrai` queues both. For DOSBox, `lib/emu.sh` takes `EMU_FILES="..."` to put them there.

Options, for testing: `/DIAG`, `/TUNC` and `/SAYH` (above), `/PART:n` start at part n (0-4), `/ONLY` just that part, `/NOBOOT`, `/QUIET`, `/LOCK[:n]` frame-locked (T advances n ticks a frame, so results don't depend on speed), `/SHOT:a,b,...` screenshots at those T values, `/BENCH` frame rates and precompute times to `LOG.TXT`, `/ONEPAGE` test the one-page fallback, `/BOOTDUMP` the boot screen to `LOG.TXT`, `/VIDEO` every frame as `Fnnnn.PPM` plus every sound chip write in `SND.BIN` (used by `video.sh`; `render.py` emulates the SN76496, with a rough small-speaker low-pass, to make the audio). `video.sh` also keeps the run's `LOG.TXT` next to the video, with when each line was spoken.

## Measurements (DOSBox only)

DOSBox `machine=pcjr`, `cycles=fixed 400`, which `lib/FAST.md` uses as a rough stand-in for a real PCjr. DOSBox charges one cycle per instruction and has no wait states, so the real machine will differ, probably slower in the memory-heavy parts.

| Part | Before tuning | Now |
|---|---|---|
| Plasma | 53 fps | (nothing to draw) |
| Fire | 1.5 fps | 8.8 fps |
| Tunnel | 17 fps | 17 fps |
| Meeting | 8.3 fps | 41 fps (12 fps counting the time spent talking) |
| Finale | 7.4 fps | 18.8 fps |
| Precompute at startup | 42 s | 8.7 s |

What made the difference: the fire called a 32-bit random function for every burning cell (now a list of cell offsets and a noise table, plus the assembly loops); the meeting redrew everything each frame (now only what changed on that page); the finale cleared the screen and then drew bars over it (now one fill per row).

## Not tested on the real PCjr

Nothing here has run on real hardware yet. Things to watch:

- **Memory.** About 350 KB beyond the program: two 64 KB tunnel textures, 64 KB of tunnel code, the library's 64 KB back buffer, a 64 KB clip buffer, the 16 KB plasma. Fine with this PCjr's 736 KB. If a texture can't be allocated, the tunnel uses one; with no room at all it says so.
- **The second video page.** The PCjr shows only 16 KB pages in the low 128K, and this PCjr reserves one. The 86 KB program probably loads right where a second page would go, so the library may fall back to one page plus a RAM buffer. JRAI then copies the whole frame each time (correct, slower, may tear). `/ONEPAGE` tests this path in DOSBox. `/BENCH` logs which mode it got (`fx_pages`) and where the program loaded (`psp`).
- **The tunnel was all gray** on the real PCjr (the first version). `JRAI /PART:2 /ONLY /DIAG` logs where its memory landed, checksums of its texture and generated code, and a checksum of the first frame (41FB in DOSBox). `/TUNC` draws the same tunnel in plain C instead of generated code; it gives the same checksum in DOSBox. Comparing the two on the PCjr says whether the generated code or the texture is at fault.
- **Running SAY from inside JRAI** works in DOSBox (all 13 lines, back to DOS cleanly). Not yet on the PCjr.
