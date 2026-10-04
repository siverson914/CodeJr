# SAY: speech on the PCjr

A modern neural voice (ElevenLabs through gipity), reshaped on the laptop
and played by the PCjr's own sound hardware. No add-on card is needed.

```
just clip READY "Ready when you are."     # gipity speech -> speech/clips/READY.mp3 + .txt
just speech                               # every clip -> speech/out/NAME.SND, .SNP, previews
just deploy-speech HELLO HELLO.SNP        # queue SAY.EXE and clips for the PCjr
```

On the PCjr: `SAY HELLO`, `SAY HELLO.SNP`, `SAY HELLO READY FRIENDS`. A key
stops it. `SAY /I HELLO` shows what's in a clip, and `SAY /V HELLO` reports
afterwards how many samples were late.

`speech/out/NAME-chip.wav` and `NAME-pwm.wav` are previews: what each
method should sound like on the PCjr, through a rough small-speaker filter.
They assume ideal hardware.

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

**Which is better is an open question for the real machine.** Two reasons:

- The chip method's fine levels rely on the chip's 2 dB steps being exact.
  In simulation the HELLO clip's speech-band SNR is 37 dB with exact steps,
  30 dB if each step is off by 0.25 dB, and 23 dB at 0.5 dB. (Old JRAI
  method on the same clip: 8.5 dB.) The level table already prefers
  combinations that tolerate step error. Picking for exact levels alone
  gave 39.5 dB ideal but 27 dB at 0.25 dB.
- Whether period 1 really reads as a steady level on this PCjr's chip and
  audio path is untested (JRAI has the same open question).

PWM doesn't depend on the chip at all, only on timing.

## How the player keeps time

- `player.asm` is copied into a DOS block allocated "last fit", so on this
  640 KB PCjr it lands above 128 KB, away from the video wait states (6
  clocks per byte below 128 KB, 4 above). Its code, stack, tables and samples
  are all up there. It has its own linker group, so its internal offsets
  are relative to its first byte and survive the copy.
- PIT channel 0 runs at the sample rate (mode 2), and only IRQ0 is
  unmasked. The loop does `STI; HLT; CLI`: it wakes on the tick and writes
  the sample a fixed number of clocks later, so the timing jitter is a few
  clocks. That matters most for PWM, where a late write is a wrong pulse.
- Before each `HLT` the loop reads the 8259's request register. If IRQ0 is
  already waiting, the last sample took more than one period. `SAY /V`
  prints that count.
- Afterwards it restores channel 0 (mode 3, 65536), the IRQ0 vector, the
  interrupt mask and port 61h, and adds the elapsed ticks to the BIOS clock.

**Clock budget (estimates, not measured).** The 8088 is bus-bound: 4 clocks
per code byte. Each SN76496 write also holds the CPU in READY wait states
for 32 of the chip's clocks (about 9 us, 43 CPU clocks). Taking the
interrupt and the 5-instruction handler costs about 150 clocks.

| Method | Loop bytes | Estimate per sample | Budget | Margin |
|---|---|---|---|---|
| chip at 9,322 Hz | 40 | ~466 | 512 | ~9% |
| pwm at 15,909 Hz | 22 | ~254 | 300 | ~15% |

The chip method was planned at 11 kHz. That budget is 432 clocks, so it
dropped to 9.3 kHz. If `SAY /V` reports late samples on the real PCjr,
lower the rate in `METHODS` in `mksnd.py` (the divisor is in the header,
and SAY doesn't care).

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
