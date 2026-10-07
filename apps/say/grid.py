#!/usr/bin/env -S uv run --quiet --script
# /// script
# dependencies = ["numpy", "scipy", "faster-whisper"]
# ///
"""Quality grid for voice mode: sample rate x codec x chip voices.

  apps/say/grid.py [clip ...]        default: REPLY HELLO from clips/

For each setting it encodes the clips, decodes them exactly as SAY would,
and reports bytes per second of speech, link time at 2.6 KB/s (37287 baud,
measured), speech-band SNR (ideal chip), and whether Whisper (small.en)
still hears every word. Previews go to out/grid/, results to
out/grid/results.json. Takes a few minutes.
"""
import json, os, re, subprocess, sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from scipy import signal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location("mksnd", os.path.join(os.path.dirname(os.path.abspath(__file__)), "mksnd.py"))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

LINK = 2600  # bytes/s, JRFLIGHT at 37287 baud on the real PCjr
RATES = [6000, 7000, 8000, 9322, 10653]
CODECS = ["raw", "adpcm4", "adpcm3", "adpcm2"]
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out", "grid")


def settings():
    for r in RATES:
        for c in CODECS:
            yield (r, c, 3)
    for v in (2, 1):
        yield (9322, "raw", v)
    yield (6000, "raw", 1)  # about what JRAI does


def run(job):
    clip, rate, codec, voices = job
    x = m.decode(os.path.join(HERE, "clips", clip + ".mp3"))
    divisor = round(m.PIT / rate)
    rate = m.PIT / divisor
    t = m.frame(m.shape(x, m.SRC_RATE, rate), rate)
    levels, rows = m.chip_table(voices=voices)
    if codec == "raw":
        idx = m.quantize(t, levels, (0.5, 0.0) if voices > 1 else (0.0, 0.0))
        nbytes = len(idx)
    else:
        bits = int(codec[-1])
        codes, idx = m.adpcm_encode(t, levels, bits)
        assert (np.minimum(m.adpcm_decode_ref(codes, bits), len(levels) - 1) == idx).all()
        nbytes = len(m.pack_bits(codes, bits))
    got = levels[idx]
    sos = signal.butter(6, min(3400, rate * 0.45), fs=rate, output="sos")
    a, b = signal.sosfilt(sos, t), signal.sosfilt(sos, got)
    snr = 10 * np.log10(np.sum((a - a.mean()) ** 2) / np.sum((a - b) ** 2))
    name = "%s-%d-%s-%dv" % (clip, round(rate / 100) * 100, codec, voices)
    wav = os.path.join(OUT, name + ".wav")
    m.preview_chip(got, rate, wav)
    secs = len(idx) / rate
    return dict(clip=clip, rate=round(rate), codec=codec, voices=voices, name=name,
                seconds=round(secs, 2), bytes=nbytes, bps=round(nbytes / secs),
                link_per_sec=round(nbytes / secs / LINK, 2), snr=round(float(snr), 1), wav=wav)


def words(s):
    return re.sub(r"[^a-z' ]", " ", s.lower()).split()


def wer(ref, hyp):
    r, h = words(ref), words(hyp)
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        prev, d[0] = d[0], i
        for j in range(1, len(h) + 1):
            cur = min(d[j] + 1, d[j - 1] + 1, prev + (r[i - 1] != h[j - 1]))
            prev, d[j] = d[j], cur
    return d[len(h)] / max(len(r), 1)


def main():
    clips = sys.argv[1:] or ["REPLY", "HELLO"]
    os.makedirs(OUT, exist_ok=True)
    jobs = [(c, r, k, v) for c in clips for (r, k, v) in settings()]
    with ProcessPoolExecutor() as ex:
        res = list(ex.map(run, jobs))
    from faster_whisper import WhisperModel
    w = WhisperModel("small.en", device="cpu", compute_type="int8")
    for r in res:
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", r["wav"], "-ac", "1", "-ar", "16000",
                              "-f", "f32le", "-"], capture_output=True, check=True).stdout
        segs, _ = w.transcribe(np.frombuffer(raw, dtype=np.float32), beam_size=5, temperature=0)
        r["heard"] = " ".join(s.text.strip() for s in segs)
        r["wer"] = round(wer(open(os.path.join(HERE, "clips", r["clip"] + ".txt")).read(), r["heard"]), 3)
        print("%-28s %6d B/s  link %.2fx  SNR %5.1f dB  WER %4.0f%%  %s" % (
            r["name"], r["bps"], r["link_per_sec"], r["snr"], 100 * r["wer"], r["heard"][:60]), flush=True)
    json.dump(res, open(os.path.join(OUT, "results.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
