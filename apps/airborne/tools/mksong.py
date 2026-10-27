#!/usr/bin/env -S uv run --quiet --script
# /// script
# dependencies = ["numpy", "scipy"]
# ///
"""Makes AIRSONG.SND: the original's startup song for the PCjr, in SAY's
chip format (apps/say: three tone voices at period 1 as a level DAC, one
byte per sample, raw). AIRBORNE.EXE plays it with SAY's player.asm.

  tools/mksong.py [AIRBORNE_DIR] [OUT_DIR]

The song file ("Air Startup Song", from the original disk, kept out of the
repo) is unsigned 8-bit at 8000 Hz after a 32-byte header. Music, not
speech: no trimming, compression or presence boost (mksnd.py's shape()),
just the original rate (PIT divisor 149, 8008 Hz) and a level fit.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "say"))
import mksnd  # noqa: E402

src = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else "~/.local/share/codejr/airborne")
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..")
raw = open(os.path.join(src, "files", "Air_Startup_Song.data"), "rb").read()[32:]
x = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
x -= x.mean()
divisor = 149  # 1193182 / 149 = 8008 Hz, the song's own 8000 within 0.1%
rate = mksnd.PIT / divisor
y = x / np.percentile(np.abs(x), 99.9)
y = np.tanh(1.2 * y) / np.tanh(1.2)
data, table, n, lv, _ = mksnd.make_chip(y, rate, 0)
path = os.path.join(out, "AIRSONG.SND")
mksnd.write_snd(path, 1, divisor, data, "", table, n)
os.makedirs(os.path.join(out, "build"), exist_ok=True)
mksnd.preview_chip(lv, rate, os.path.join(out, "build", "airsong-chip.wav"))
print("AIRSONG.SND: %.1f s at %.0f Hz, %d KB" % (n / rate, rate, (len(data) + 1023) // 1024))
