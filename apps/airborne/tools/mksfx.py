#!/usr/bin/env python3
"""The game's 11 sound effects for the PCjr's SN76496, worked out from the
original recordings (signed 8-bit at 3969 Hz, 66 samples per 60 Hz tick:
see ~/.local/share/codejr/airborne/re/sound/REPORT.md).

The Mac mixes the two lowest-numbered active effects. AIRBORNE.EXE does the
same with the chip: the first gets the noise voice (its shift rate set by
tone 3, which stays silent), the second gets tone 1. So every tick of every
effect has two versions, both from that tick's 66 samples:
  noise: loudness; white noise shifting at 4x the zero-crossing rate, or,
         for the hums (helicopters, transport), periodic noise at the hum's
         own pitch (tone 3's rate / 15), the only way under 110 Hz;
  tone:  loudness; a square wave at the zero-crossing rate, or the hum's
         pitch raised by octaves to the chip's 110 Hz floor.

Chunk SFX (little-endian): u8 effects (12), then per effect u16 first
record, u16 ticks, u8 loops, u8 0; then 6-byte records: u16 tone-3
divider for the noise (bit 15: periodic), u8 noise attenuation (0 loudest,
15 off), u16 tone divider, u8 tone attenuation.

  tools/mksfx.py   writes build/sfxNN-chip.wav previews (chip model, noise
                   version) next to build/sfxNN-orig.wav
"""
import os, struct, wave
import numpy as np

SRC = os.path.expanduser('~/.local/share/codejr/airborne/re/sound')
HERE = os.path.dirname(os.path.abspath(__file__))
CLOCK = 3579545
RATE = 3969.3
TICK = 66
LOOPS = {6, 9, 10, 11}
REF = 0.40  # the loudest tick in the set plays at attenuation 0


def load(i):
    names = [f for f in os.listdir(SRC) if f.startswith('fx%02d_' % i) and f.endswith('.wav')]
    if not names:
        return None
    w = wave.open(os.path.join(SRC, names[0]))
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float) / 32768
    return x - x.mean()


def hum_pitch(x):
    """The fundamental of a hum, by autocorrelation over the whole loop."""
    a = np.correlate(x, x, 'full')[len(x) - 1:]
    lo, hi = int(RATE / 200), int(RATE / 30)
    lag = lo + int(np.argmax(a[lo:hi]))
    return RATE / lag


def att(r):
    if r < 1e-4:
        return 15
    return int(np.clip(round(20 * np.log10(REF / r) / 2), 0, 15))


def div(hz):
    return int(np.clip(round(CLOCK / 32 / hz), 1, 1023))


def effect(x):
    n = len(x) // TICK
    flat = []
    for t in range(n):
        s = x[t * TICK:(t + 1) * TICK]
        sp = np.abs(np.fft.rfft(s * np.hanning(TICK), 256)) ** 2 + 1e-12
        flat.append(np.exp(np.log(sp).mean()) / sp.mean())
    hum = np.median(flat) < 0.03
    f0 = hum_pitch(x) if hum else 0
    recs = []
    for t in range(n):
        s = x[t * TICK:(t + 1) * TICK]
        r = np.sqrt((s * s).mean())
        z = max(((s[:-1] * s[1:]) < 0).sum() / TICK * RATE / 2, 60.0)
        if hum:
            nd = div(f0 * 15) | 0x8000
            tf = f0
            while tf < 110:
                tf *= 2
        else:
            nd = div(min(max(4 * z, 500.0), 8000.0))
            tf = max(z, 110.0)
        a = att(r)
        recs.append((nd, a, div(tf), a))
    return recs, hum, f0


def build():
    head, body, k = b'', b'', 0
    info = []
    for i in range(12):
        x = load(i)
        recs, hum, f0 = effect(x) if x is not None else ([], False, 0)
        head += struct.pack('<HHBB', k, len(recs), 1 if i in LOOPS else 0, 0)
        for r in recs:
            body += struct.pack('<HBHB', *r)
        k += len(recs)
        info.append((i, len(recs), hum, f0, x))
    return bytes([12]) + head + body, info


def lfsr_noise(shift_hz, periodic, n, rate):
    """SN76496 noise: a 15-bit shift register clocked at shift_hz."""
    out = np.empty(n)
    reg, acc, step = 0x4000, 0.0, shift_hz / rate
    for j in range(n):
        acc += step
        while acc >= 1:
            acc -= 1
            fb = (reg & 1) if periodic else ((reg ^ (reg >> 1)) & 1)
            reg = (reg >> 1) | (fb << 14)
        out[j] = 1.0 if reg & 1 else -1.0
    return out


def write_wav(path, x, rate):
    x = 0.9 * x / max(np.abs(x).max(), 1e-9)
    w = wave.open(path, 'wb')
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(int(rate))
    w.writeframes((x * 32767).astype('<i2').tobytes())


def previews(info, out):
    rate = 22050
    per = rate // 60
    blob, _ = build()
    for i, n, hum, f0, x in info:
        if not n:
            continue
        o = 1 + 6 * 12 + 6 * struct.unpack_from('<H', blob, 1 + 6 * i)[0]
        y = []
        for t in range(n):
            nd, na, td, ta = struct.unpack_from('<HBHB', blob, o + 6 * t)
            per_hz = CLOCK / 32 / (nd & 0x3FF)
            v = 0 if na == 15 else 10 ** (-na / 10)
            y.append(v * lfsr_noise(per_hz, bool(nd & 0x8000), per, rate))
        write_wav(os.path.join(out, 'sfx%02d-chip.wav' % i), np.concatenate(y), rate)
        write_wav(os.path.join(out, 'sfx%02d-orig.wav' % i), x, RATE)
        print('fx %2d: %3d ticks%s' % (i, n, ', hum %.0f Hz' % f0 if hum else ''))


if __name__ == '__main__':
    blob, info = build()
    out = os.path.join(HERE, '..', 'build')
    os.makedirs(out, exist_ok=True)
    previews(info, out)
    print('SFX chunk: %d bytes' % len(blob))
