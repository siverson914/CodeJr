#!/usr/bin/env python3
"""Renders SND.BIN (written by JRAI /VIDEO) to a WAV, by emulating the
PCjr's SN76496: 3 square-wave voices and a noise voice, 2 dB volume steps,
unipolar output (which is what makes the speech trick work: a voice at an
inaudible pitch averages to half its volume, so the volume register acts
as a 4-bit DAC).
Usage: render.py SND.BIN out.wav
"""
import struct, sys, wave

CLOCK = 3579545
RATE = 22050
VOL = [10 ** (-2 * k / 20) for k in range(15)] + [0.0]


def main(src, dst):
    data = open(src, "rb").read()
    ev = [struct.unpack_from("<IB", data, i) for i in range(0, len(data) - 4, 5)]
    period = [1024, 1024, 1024]
    att = [15, 15, 15, 15]
    phase = [0.0, 0.0, 0.0, 0.0]
    out_bit = [0, 0, 0, 1]
    noise_ctl = 0
    lfsr = 0x4000
    latch = 0
    end = (ev[-1][0] if ev else 0) * RATE // 6000 + RATE
    buf = bytearray()
    ei = 0
    step = CLOCK / 16 / RATE  # counter ticks per output sample
    for n in range(end):
        t6 = n * 6000 // RATE
        while ei < len(ev) and ev[ei][0] <= t6:
            b = ev[ei][1]
            ei += 1
            if b & 0x80:
                latch = (b >> 5) & 3
                typ = (b >> 4) & 1
                if typ:
                    att[latch] = b & 15
                elif latch == 3:
                    noise_ctl = b & 7
                    lfsr = 0x4000
                else:
                    period[latch] = (period[latch] & 0x3F0) | (b & 15)
            elif latch < 3:
                period[latch] = (period[latch] & 15) | ((b & 0x3F) << 4)
        # each voice's output averaged over the sample (a box filter), so
        # high notes don't alias into noise
        s = 0.0
        for c in range(3):
            p = period[c] or 1024
            if p <= 4:  # far above hearing: the average level
                s += VOL[att[c]] * 0.5
                continue
            rem, acc = step, 0.0
            while rem > 0:
                d = min(rem, p - phase[c])
                acc += out_bit[c] * d
                phase[c] += d
                rem -= d
                if phase[c] >= p:
                    phase[c] -= p
                    out_bit[c] ^= 1
            s += VOL[att[c]] * acc / step
        np_ = [16, 32, 64, period[2] or 1024][noise_ctl & 3] * 2
        rem, acc = step, 0.0
        while rem > 0:
            d = min(rem, np_ - phase[3])
            acc += (lfsr & 1) * d
            phase[3] += d
            rem -= d
            if phase[3] >= np_:
                phase[3] -= np_
                if noise_ctl & 4:
                    fb = (lfsr ^ (lfsr >> 1)) & 1
                else:
                    fb = lfsr & 1
                lfsr = (lfsr >> 1) | (fb << 14)
        s += VOL[att[3]] * acc / step
        v = int((s / 4 - 0.25) * 2 * 30000)
        buf += struct.pack("<h", max(-32767, min(32767, v)))
    w = wave.open(dst, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(bytes(buf))
    w.close()
    print(f"{dst}: {end / RATE:.1f} s, {len(ev)} register writes")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
