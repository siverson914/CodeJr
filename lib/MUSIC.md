# JRMUSIC: music and sound effects for JRGFX games

`lib/jrmusic.c`, `lib/jrmusic.h` (prefix `mus_`). A small tracker for the PCjr's sound chip (TI SN76496: three tone voices and one noise voice) and a sound-effect player that shares the chip with it.

**Nothing here has run on a real PCjr.** Every result below is from DOSBox (`machine=pcjr`). What that leaves open is listed under "Not verified" at the end.

Why it exists: JRCUBE, JRAI and JUNRIS each wrote their own player. JUNRIS's first one ticked from the game loop, so a slow frame froze a note; its fix (tick from the 60 Hz retrace interrupt, never let effects and music interleave their writes to the chip) is what this module gives every game.

## Use

```c
#include "jrgfx.h"
#include "jrmusic.h"

/* Korobeiniki, first half: 2 patterns of 32 sixteenths, 6 ticks a row (150 BPM) */
static const char *const lead[] = {
	"E5:4 B4:2 C5:2 D5:4 C5:2 B4:2 | A4:4 A4:2 C5:2 E5:4 D5:2 C5:2",
	"B4:6 C5:2 D5:4 E5:4 | C5:4 A4:4 A4:4 -:4",
	0
};
static const char *const bass[] = {
	"E3 E4 E3 E4 E3 E4 E3 E4 E3 E4 E3 E4 E3 E4 E3 E4 | A2 A3 A2 A3 A2 A3 A2 A3 A2 A3 A2 A3 A2 A3 A2 A3",
	"G#3 G#4 G#3 G#4 G#3 G#4 G#3 G#4 E3 E4 E3 E4 E3 E4 E3 E4 | A2 A3 A2 A3 A2 A3 A2 A3 A2:4 -:4",
	0
};
static const char *const chords[] = {
	"E4M:16 | A3m:16",
	"E4M:16 | A3m:12 -:4",
	0
};
static const char *const drums[] = {
	"k.h.s.h.k.h.s.h.k.h.s.h.k.hks.ss",
	0
};
static const mus_inst inst[] = {
	{ 13, 10, 2, 0, 2, 0 },   /* 0 lead: plucked, settles at 10, vibrato */
	{ 14,  9, 1, 4, 0, 0 },   /* 1 bass: plucked, then dies away         */
	{ 10,  8, 3, 0, 0, 2 },   /* 2 chords: a chord note every 2 ticks    */
};
static const mus_step order[] = {
	/* patterns: lead bass chords drums, instruments, transpose, flags */
	{ { 0, 0, 0, 0 }, { 0, 1, 2 }, { 0, 0, 0 }, 0 },
	{ { 1, 1, 1, 0 }, { 0, 1, 2 }, { 0, 0, 0 }, 0 },
};
static const mus_song song = {
	6, 32,                              /* 6 ticks a row, 32 rows a pattern */
	{ lead, bass, chords, drums },
	order, MUS_COUNT(order), 0,         /* loop to step 0 */
	inst, MUS_COUNT(inst),
	0                                   /* the built-in drum kit */
};

/* a rising blip for voice 2, a thud for the noise channel */
static const mus_fx fx_blip[] = { { MUS_HZ(880), 12, 3, 0 }, { MUS_HZ(1320), 12, 5, -2 }, { 0, 0, 0, 0 } };
static const mus_fx fx_thud[] = { { 6, 12, 2, 0 }, { 6, 8, 2, 0 }, { 6, 4, 2, 0 }, { 0, 0, 0, 0 } };

int main(void)
{
	int e;
	key_init();
	if (fx_init(0)) return 1;
	e = mus_start(&song, 0);            /* after fx_init */
	if (e) dbg_log("song error %d: voice %d pattern %d char %d", e, mus_err.voice, mus_err.pat, mus_err.pos);
	while (!key_down[KEY_ESC]) {
		mus_poll();                     /* once a frame, always */
		if (key_down[KEY_SPACE] && !mus_sfx_busy(2)) mus_sfx(2, fx_blip);
		if (key_down[KEY_ENTER] && !mus_sfx_busy(MUS_NOISE)) mus_sfx(MUS_NOISE, fx_thud);
		/* ... draw a frame ... */
		fx_flip(); fx_begin();
	}
	return 0;                           /* mus_stop runs at exit */
}
```

`lib/build.sh` puts every `jr*.c` into `jrgfx.lib`, so a game that links the library has it. Rules:

- Call `mus_start` after `fx_init`.
- Call `mus_poll()` once per frame whatever the mode. It is what plays the music when there is no retrace interrupt, and it costs a few instructions when there is.
- While the player runs, do not call `snd_tone` or `snd_noise`: they would cut into the player's writes. Use `mus_sfx`.
- A game with its own `fx_irq_hook` function passes `MUS_NOHOOK` to `mus_start` and calls `mus_irq()` from that function. The library has one hook slot; without `MUS_NOHOOK` the player takes it.
- Never call `mus_irq()` from an interrupt handler of your own. It needs the library's interrupt stack (see `fx_irq_hook` in `jrgfx.h`).

## How it keeps time

One tick is 1/60 s. Three ways to get it, chosen by `mus_start`:

| Mode | When | Who ticks |
|---|---|---|
| retrace interrupt | `fx_irq == 1`, flags 0 | the library's IRQ5 handler calls the player through `fx_irq_hook`. A slow frame changes nothing. |
| the game's hook | `fx_irq == 1`, `MUS_NOHOOK` | the game's hook function calls `mus_irq()` |
| polling | `fx_irq == 0` (`FX_NOIRQ`, plain `gfx_` mode, text mode) | `mus_poll()` reads `tmr_ticks()` and plays every tick that has come due since the last call, up to 240 at once (4 s; beyond that it skips ahead). A note still hangs for as long as the game stays away from `mus_poll`, so call it inside long loops (loading, a slow redraw). |

`mus_poll` also repairs two things: if `fx_done` takes the interrupt away under a playing song, it carries on from the timer; if `fx_init` is called again (it clears the hook), it puts the hook back after 8 retraces without a tick.

Polling is 60.0 Hz from the PIT; the PCjr's retrace is 59.92 Hz, so the same song is 0.1% slower with the interrupt. To step the player yourself (a video renderer, a test), use `MUS_NOHOOK`, leave `mus_poll` out and call `mus_irq()` once per tick.

## The song

A song is a `mus_song`: tempo, pattern length, up to four lists of patterns (voices 0, 1, 2 and drums), and optionally an order, instruments and a drum kit. Everything left out at the end of the initializer is 0 and means a default, so the smallest song is

```c
static const char *const lead[] = { "C4:4 E4:4 G4:4 C5:4", 0 };
static const mus_song song = { 8, 16, { lead } };
```

`mus_start` compiles the strings into a block from `malloc` and reports the first mistake: the return value is a `MUS_E_` code and `mus_err` says which voice, pattern and character. Log it; a song with a typo plays nothing.

### Patterns

A pattern is a string of `rows` tokens. `tempo` is the number of 60 Hz ticks per row: with rows as sixteenth notes, BPM = 900 / tempo (7: 128.6, 6: 150, 5: 180). Every list of patterns ends with `0`.

Tone voices, one token per row; spaces and `|` are ignored, so bars can be marked:

| Token | Meaning |
|---|---|
| `C4` `F#3` `Bb2` | a note, A2 (110 Hz) to B7. Upper-case letter, `#` or `b`, octave 0-7. It sounds until the next note or `-`. |
| `.` | hold: nothing new on this row |
| `-` | note off |
| `A3m` `C4M` | a chord for an arpeggio instrument: `m` minor, `M` (or nothing) major |
| `E5:4` `-:8` `.:2` | `:n` makes a token last n rows (the note, then n-1 holds) |

A pattern with fewer tokens than `rows` is filled up with holds. One with more is an error (`MUS_E_LONG`), a note below A2 or above B7 is `MUS_E_RANGE`. The chip's divisor has 10 bits: nothing lower than A2 exists.

Drums are one character per row from the kit, `.` for nothing. The built-in kit:

| Char | Drum |
|---|---|
| `k` | kick (periodic noise dropping an octave) |
| `s` | snare |
| `h` `o` | closed and open hat |
| `c` | crash |
| `S` `C` | kick + snare, kick + crash (one noise voice: the kick sounds for two ticks, then the other takes over) |
| `x` | a long low boom |
| `1`-`9` | snare at volume 7 to 15, for rolls: `"1.2.3.4.5.6.7.8."` |

### Instruments

```c
typedef struct { unsigned char vol, sus, rate, fade, vib, arp; } mus_inst;
```

A note starts at volume `vol` (0-15) and moves one step towards `sus` every `rate` ticks: down is a pluck, up is a swell, `rate` 0 stays at `vol`. Then, if `fade` is not 0, it drops one step every `fade` ticks to silence; otherwise it holds until the next note or `-`.

- `vib`: vibrato depth 0-4 (2 is about 1.5% each way, a quarter of a semitone), 5 Hz, starting 13 ticks into the note.
- `arp`: arpeggio. `n` plays root, third, fifth, a new one every n ticks (1 is the classic chip chord); `n | MUS_UPDOWN` walks root, third, fifth, octave and back. The third is major, or minor when the note is written with `m`. An instrument with `arp` ignores `vib`.

The envelope, arpeggio and vibrato start counting on the tick after the note starts.

Without an instrument table there are three built in: 0 lead `{13,10,2,0,2,0}`, 1 bass `{15,9,1,0,0,0}`, 2 fast arpeggio `{11,9,2,0,0,1}`.

### Order

```c
typedef struct { unsigned char pat[4]; unsigned char inst[3]; signed char tr[3]; unsigned char flags; } mus_step;
```

Each step plays one pattern per voice for `rows` rows: `pat` are the pattern numbers (voices 0-2, drums; `MUS_NONE` for silence), `inst` the instrument of each tone voice, `tr` semitones to move it by (a note pushed below A2 plays at the chip's lowest pitch, about 109 Hz). `flags`: `MUS_FADE` fades the whole song to silence over this step (for the last one).

`loop` is the step to go back to after the last, or `MUS_ONCE` to stop there (`mus_playing()` then returns 0). Without an order the song is one step: pattern 0 of every voice with instruments 0, 1 and 2.

### Your own drums

```c
typedef struct { char key; unsigned char n0, at, n1, vol, vol1, dec; } mus_drum;
```

`n0` is the SN76496 noise type at the hit (0-2 periodic at high, middle, low rate; 4-6 white at the same rates; 3 and 7 take their rate from voice 2's pitch). At tick `at` (0: never) the type changes to `n1` and, if `vol1` is not 0, the volume to `vol1`. Volumes are in quarter steps (60 is full) and fall by `dec` per tick. A kit is an array of up to 14, ended by `{ 0 }`; the built-in one is at the top of `jrmusic.c`.

## Sound effects

```c
typedef struct { unsigned div; unsigned char vol, ticks; signed char slide; } mus_fx;
void mus_sfx(int ch, const mus_fx *fx);     /* ch 0-2, or MUS_NOISE */
int  mus_sfx_busy(int ch);
```

An effect is an array of steps ended by one with `ticks` 0. Each step sets the divisor (`MUS_HZ(880)`, a compile-time constant, 110 Hz and up; on the noise channel the noise type 0-7) and the volume for `ticks` ticks; `slide` is added to the divisor every tick (negative: the pitch rises). Volume 0 is a pause. The array must stay valid while it plays: make it `static const`.

The effect borrows the channel. The song keeps running underneath, silent on that channel, and has it back on the tick after the effect's last. A second `mus_sfx` on the same channel replaces the first. Effects keep their own volume under `mus_volume` and keep playing under `mus_pause`.

`mus_sfx` with no player running starts one (as `mus_start(0, 0)`), so a game with effects and no music needs only `mus_sfx` and `mus_poll`.

Which channel: voice 2 if the song uses it for chords (they are missed least), the noise channel for thuds and explosions. For a noise whose pitch sweeps, run two effects: noise type 7 on `MUS_NOISE` and a silent sliding step on voice 2 (`{ 8, 0, 40, 16 }`), which steers the noise.

## The rest of the interface

| Call | What it does |
|---|---|
| `int mus_start(const mus_song *song, int flags)` | starts `song` from the top, replacing the one playing; 0 or a `MUS_E_` code. `song` 0: effects only. |
| `void mus_irq(void)` | one tick; for a game's own hook function |
| `void mus_poll(void)` | once per frame, see above |
| `void mus_stop(void)` | silences the chip, releases the hook, frees the song. Registered with `atexit` by the first `mus_start`, so a game that just exits leaves no note hanging. |
| `void mus_pause(void)`, `void mus_resume(void)` | the song falls silent at once and stands still; effects still play |
| `void mus_volume(int vol)` | song volume 0-15, taken off every voice (15: as written) |
| `void mus_tempo(int ticks)` | ticks per row from the next row on (JUNRIS speeds up with the level) |
| `int mus_playing(void)` | 1 while a song sounds (not paused, not ended) |
| `unsigned long mus_ticks(void)` | ticks played since the player started |
| `int mus_level(int ch)` | volume on the chip now, 0-15, channel 0-3 (meters) |
| `mus_beat` | set to the drum's character on every drum hit; clear it yourself (flashes on the kick) |
| `mus_row`, `mus_ord` | row and step being played |
| `mus_div[108]` | the divisor of every note, C0 = index 0 |

Errors from `mus_start`: `MUS_E_MEM` (no memory, or patterns over 64 KB), `MUS_E_SYNTAX`, `MUS_E_LONG`, `MUS_E_RANGE`, `MUS_E_SONG` (tempo or rows 0, a pattern or instrument number that does not exist, a bad loop, an instrument volume over 15, a kit over 14 drums).

## Costs

Memory: 6.2 KB of code, 1.1 KB of near data (the note table is 216 bytes of it). A song takes one byte per row of every pattern, 14 bytes per instrument and 18 per step, from `malloc`: 302 bytes for the example above, about 1.5 KB for a song the size of JRCUBE's (64 patterns of 16 rows, 17 steps).

Time per tick. The test counts the instructions of every tick with the processor's single-step trap (exact, and the same on any 8088) and adds up their length in bytes. Milliseconds are an estimate from that: an 8088 spends 5 to 6 clock cycles per byte of code like this, so about 1.2 microseconds a byte at 4.77 MHz. **No tick has been timed on the PCjr.**

| Test song (3 voices and drums, an arpeggio stepping every tick) | Instructions | Bytes of code | Estimate |
|---|---|---|---|
| average tick | 254 | 683 | 0.85 ms |
| first tick of a row (new notes start) | 284 | | 0.95 ms |
| a quiet tick (only the arpeggio moving) | 141 | | 0.45 ms |
| worst tick | 357 | 990 | 1.2 ms |
| the same average with the C version of the tick (`-dMUS_C_TICK`) | 290 | 855 | 1.05 ms |
| JRCUBE's own tracker, measured the same way | 570 | 1456 | 1.8 ms |
| no song, no effect | 15 | | 0.05 ms |

So about 5% of the processor for a busy song, half of what JRCUBE's player takes. It is **not** "well under a millisecond" on every tick: the aim was 1 ms, the average is below it and row starts with several new notes are at or a little over it. The worst case found, in the second test song, was 599 instructions (about 2 ms): a new step, two effects and three busy voices on one tick. What it costs is the number of things that change per tick; a song with held notes and no fast arpeggio is much cheaper than these.

What keeps it short:

- No multiply or divide, no library call, no DOS in the tick. Pitch comes from a table of divisors worked out ahead (no floating point anywhere, not at run time either).
- The song is compiled by the main program: one byte per row, instruments in the shape the player copies.
- The work of a row is spread out. The last four ticks of a row prepare the next one with no sound (the new step if there is one, then one voice per tick, each new note built completely in a spare slot); the first tick of the row only swaps slots and writes the chip.
- The chip is written only where something changed.
- The per-tick part for the voices and the drum is assembly (`mus_vtick` in `jrmusic.c`). The same thing in C is next to it (`vtick_c`): it is the specification, and `-dMUS_C_TICK` uses it in place of the assembly.

With interrupts: in polling mode each tick runs with interrupts off (the estimate above, per tick). `mus_start`, `mus_pause` and `mus_volume` hold them off for a few hundred instructions.

## How the chip writes stay whole

A tone's pitch is two bytes; if another write lands between them, the second byte goes to the wrong register. Here every write to the chip is made either inside the retrace interrupt or with interrupts off (`mus_poll` turns them off around each tick; `mus_pause`, `mus_volume`, `mus_stop` write with them off). `mus_sfx` writes nothing: it leaves the effect for the next tick. The test traces every byte sent to port C0h from both sides at once and checks that no pair was split.

That covers the player's own writes only. `snd_tone` and `snd_noise` from the game are outside it.

## Test

```
lib/test/tmusic.sh [outdir]
```

builds `lib/test/tmusic.c` twice (the assembly tick, and `-dMUS_C_TICK`), runs both headless in DOSBox through `lib/emu.sh` (no window), prints the log and exits 0 only if the log ends `TMUSIC done: 0 failures` and both builds sent the same bytes to the chip. About 40 seconds. With `outdir` it keeps `LOG.TXT`, `SND.BIN`, `SND2.BIN` and `song.wav` (the test song rendered by `apps/jrai/render.py`, to listen to).

`lib/build.sh` builds every `lib/test/*.c` against the library, and `tmusic.c` builds that way too (it includes `jrmusic.c` itself), giving `lib/test/TMUSIC.EXE`: `lib/emu.sh <outdir> 3000 lib/test/TMUSIC.EXE`.

What it checks (the sound chip of the emulator can't be read back, so the test turns the single-step trap on and records the byte of every `OUT` to port C0h before it runs):

- the divisor table against a second integer formula (A4 = 254, 440 Hz);
- songs with mistakes are refused, with the place;
- retrace interrupt: one tick per retrace (90 in 1.5 s), notes started on every voice, the right divisors on the chip for the row being played;
- an effect borrows voice 2 and the noise channel and gives them back; a pitch slide; pause, resume, volume;
- the hook is put back after it is cleared; the song carries on from the timer after `fx_done`;
- the game's own hook with `MUS_NOHOOK`: one tick per call, first notes at the right pitch (E5 = divisor 170, A2 = 1017), and with the main program calling `mus_sfx`, `mus_volume` and `mus_pause` all the time, no two-byte write split;
- polling with a game that stalls 250 ms at a time: 179 ticks in 3 s;
- tick by tick: every note of the song started once, a `MUS_ONCE` song ends silent, `mus_tempo`, the instruction count per tick;
- a second song using everything else (own kit, swell, fade-out, slow arpeggio, transposes, a fading step, effects, pause, volume), 1120 ticks;
- a program that exits with a song and an effect sounding: the last byte sent to each channel is "silent".

## Not verified

- **Nothing has run on the real PCjr**: not the sound, not the timing, not the interrupt path on real PC DOS (where an interrupt can land on DOS's stack; the player relies on `fx_irq_hook` running it on the library's stack, which the library's own T3 tests in the emulator).
- Nobody has listened to it. Pitches are checked as divisors sent to the chip, volumes as numbers. Whether the built-in instruments and drums sound good on the PCjr's speaker is unknown; JRCUBE's kit was copied because it has at least been heard in the emulator.
- The millisecond figures are estimates from byte counts, not measurements.
- The chip needs about 9 microseconds per byte. The assembly sends the two bytes of a divisor 5 short instructions apart (about 8 microseconds by the same estimate) and some other writes closer. The library's `snd_tone` writes just as fast, so this is no worse than what the games do today, but whether the PCjr makes the processor wait while the chip is busy was not checked here.
- DOSBox's timer can read 55 ms early for an instant when the PIT wraps. `mus_poll` waits it out. `fx_init`'s search for the retrace interrupt does not: now and then (it depends on the exact program; one build of this test hit it every time) it gives `fx_irq` 0 in DOSBox, and then the music is polled. The test tries `fx_init` again when that happens.
- Polling mode across midnight (the BIOS tick count restarts) is handled in code and not tested.
