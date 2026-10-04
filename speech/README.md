# SAY: speech on the PCjr

A modern neural voice (ElevenLabs through gipity), reshaped on the laptop
and played by the PCjr's own sound hardware. No add-on card is needed.

```
just clip READY "Ready when you are."     # gipity speech -> speech/clips/READY.mp3 + .txt
just speech                               # every clip -> speech/out/NAME.SND + preview WAV
just deploy-speech HELLO READY            # queue SAY.EXE and clips for the PCjr
```

On the PCjr: `SAY HELLO`, or `SAY HELLO READY FRIENDS`. A key stops it.

| Option | What it does |
|---|---|
| `/I` | show what's in a clip without playing it |
| `/V` | afterwards, report the rate reached and the late samples |
| `/T` | speed test: play as fast as the machine can (chipmunk) and report that rate |
| `/H` | time chip clips with the timer interrupt, as SAY 1.0 did (needed in DOSBox) |
| `/Q` | don't print the clip's text |

`speech/out/NAME-chip.wav` is a preview of how the clip should sound on the
PCjr, through a rough small-speaker filter, assuming ideal hardware.

## Two ways to make a PCjr talk

The SN76496 has three square-wave tone voices and a noise voice, each with
a 4-bit volume (16 steps of 2 dB). It has no DAC. The PIT's channel 2 can
drive the same audio output. SAY plays clips either way. The clip's header
says which.

**Method 1, `.SND`, the chip (9,322 Hz).** A tone voice set to period 1
switches at about 111 kHz, far above hearing, so the speaker just sees a
steady level, and the volume register sets that level. That is the trick
JRAI already uses with one voice: 4 bits, 16 log-spaced levels, at 6 kHz.
Applied to the HELLO clip, Whisper hears that as "Hello to you! I'm Cor,
and I'm talking to you from an IBM TV union". SAY uses all three voices: their outputs add, and the 4,096
volume combinations give about 800 distinct levels. The converter picks
up to 256 of them that cover the range evenly, so each sample is one byte,
looked up in three tables, and written as three volume changes.

**Method 2, `.SNP`, PWM (15,909 Hz).** PIT channel 2 in mode 0: each write
pulls the line low, and it rises after that many PIT counts, so the byte
is a pulse width. The period is 75 counts, which gives 72 usable levels
(about 6 bits), with noise shaping that pushes the hiss above 4 kHz. The
carrier is the sample rate, 15.9 kHz. Some people (and some speakers) will
hear it as a faint whine.

**On the real PCjr the chip method won** (2026-10-03): Steve heard both, and
PWM had "a ton of high-pitched noise", the 15.9 kHz carrier, made worse by
late writes. So the period-1 trick works on this PCjr's chip and audio path
(JRAI's voice depends on it too), and the converter now makes chip clips
only, unless asked with `--method pwm` or `both`.

The chip method's fine levels rely on the chip's 2 dB steps being exact. In
simulation the HELLO clip's speech-band SNR is 37 dB with exact steps, 30 dB
if each step is off by 0.25 dB, and 23 dB at 0.5 dB. (Old JRAI method on the
same clip: 8.5 dB.) The level table already prefers combinations that
tolerate step error. Picking for exact levels alone gave 39.5 dB ideal but
27 dB at 0.25 dB. How exact this chip's steps are is still unmeasured.

## How the player keeps time

- `player.asm` is copied into a DOS block allocated "last fit", so it lands
  at the top of memory, away from the video wait states (6 clocks per byte
  below 128 KB, 4 above). This PCjr has memory up to 736 KB, and the
  blocks landed at A98F and 9FCB. Its code, stack, tables and samples are
  all up there. It has its own linker group, so its internal offsets are
  relative to its first byte and survive the copy.
- PIT channel 0 runs at the sample rate (mode 2), and only IRQ0 is unmasked.
  - **Chip (default): polling.** Interrupts stay off. The loop sends the
    8259 its poll command (OCW3 0Ch) and reads port 20h: bit 7 says the tick
    has come, and the read itself acknowledges it, so an EOI follows. That
    avoids taking an interrupt every sample. The cost is up to one poll of
    timing jitter (about 10 us), which a level DAC tolerates.
  - **Chip with `/H`, and PWM: the interrupt.** The loop does `STI; HLT;
    CLI`: it wakes on the tick, and the handler sends EOI. The jitter is a
    few clocks, which PWM needs. DOSBox doesn't implement the poll command
    (it exits with "PIC: poll command not handled"), so the emulator test
    plays chip clips with `/H`.
- **Late count.** If the tick is already waiting when the loop gets back to
  it (first poll, or the request register before `HLT`), the last sample
  took more than one period, and the clip is playing slow. `SAY /V` prints
  the count.
- **Stopwatch** (chip only). PIT channel 2 free-runs with the speaker data
  bit off, and is read every 256 samples. `SAY /V` prints the rate actually
  reached and the CPU clocks per sample. `SAY /T` plays a clip with a
  50 kHz tick, so every sample is late and the loop runs flat out, and
  reports the fastest rate this machine can play.
- **Cleanup.** Afterwards it restores channel 0 (mode 3, 65536), the IRQ0
  vector, the interrupt mask and port 61h, and adds the elapsed time to the
  BIOS clock.

**Clock budget.** The 8088 is bus-bound: 4 clocks per code byte. Each
SN76496 write also holds the CPU in READY wait states for 32 of the chip's
clocks (about 9 us, 43 CPU clocks).

What SAY 1.0 (interrupt timing for both methods) measured on the real
PCjr, against my estimates:

| Method | Loop bytes | Estimated | Budget | Real PCjr |
|---|---|---|---|---|
| chip at 9,322 Hz | 40 | ~466 clocks | 512 | late on 72% of samples |
| pwm at 15,909 Hz | 22 | ~254 clocks | 300 | late on 18% of samples |

So the estimates were short by roughly 10-20%, and the interrupt is the
biggest fixed cost. The PWM loop's own code is under 100 clocks, so taking
the interrupt plus the handler costs about 200. The chip method now polls
instead. That should save about 100 clocks per sample, but it's not
measured yet: `SAY /T` will say. Then set the rate with
`mksnd.py --hz` (the divisor is in each clip's header, and SAY doesn't
care).

The other idea tried on paper, skipping writes to a voice whose volume
didn't change, doesn't pay. The voices already change only 1.7 times per
sample on average, and the compare-and-branch code costs about what the
skipped writes save on this bus.

## The converter (`mksnd.py`)

It runs with `uv` (numpy, scipy) and needs `ffmpeg`, plus `gipity` for
`--text`. The steps:

1. Trim silence.
2. Cut everything below 200 Hz (the speaker can't play it, and it wastes
   range).
3. Add +6 dB of presence above about 2 kHz, which also offsets the hold
   droop.
4. Compress hard (5:1 from -26 dB), so quiet syllables stay above the
   quantization floor.
5. Resample with an anti-alias filter, then soft-limit.
6. Fade in and out over 80 ms from silence to the resting mid level, so
   playback starts and ends without a click.
7. Quantize with error feedback (first order at half strength for the
   chip, which measured best in the speech band; second order for PWM).

Whisper (small.en) transcribes every preview in `clips/` word for word.

## File format

All fields are little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 bytes | `JRSN` |
| 4 | u8 | version 1 |
| 5 | u8 | method: 1 chip, 2 pwm |
| 6 | u16 | PIT divisor |
| 8 | u32 | samples |
| 12 | u16 | text length |
| 14 | u16 | table length |

After the header come the text (printed while playing), then the table
(method 1: 768 bytes, ready-made OUT values for voices 0, 1 and 2), then
one byte per sample.

## Sizes and the link

At 413 bytes/s the cost is about 2.3 minutes per 6-second chip clip (56 KB)
and 4 minutes for the PWM version (95 KB). `deploy-speech` prints the
total. The 37287 baud link, if it holds, would make that about 20 seconds.
