#!/usr/bin/env -S uv run --quiet --script
# /// script
# dependencies = ["numpy", "scipy"]
# ///
"""Makes PCjr speech clips (.SND and .SNP) for SAY.EXE.

  mksnd.py --text "Hello Steve." -o out/HELLO          speech via gipity
  mksnd.py --text "Hi" --voice gemini:Charon -o HI     another voice
  mksnd.py song.wav -o SONG                            any audio file
  mksnd.py clips/HELLO.mp3 -o out/HELLO                text from clips/HELLO.txt

Writes OUT.SND (method 1: the SN76496's three voices as one DAC, 9.3 kHz)
and OUT.SNP (method 2: PWM on PIT channel 2, 15.9 kHz), plus OUT-chip.wav
and OUT-pwm.wav: previews of what each should sound like on the PCjr.

The audio is shaped for a 4.77 MHz machine and a small monitor speaker:
bass below 200 Hz cut, presence lifted, dynamics squeezed hard so quiet
syllables stay above the quantization floor, and the error of each
sample fed into the next (noise shaping) so the remaining hiss sits high
where speech cares least. See README.md for the method details.
"""
import argparse, itertools, os, shutil, struct, subprocess, sys, tempfile
from fractions import Fraction

import numpy as np
from scipy import signal

PIT = 1193182
SRC_RATE = 44100

# Voices by friendly name. ElevenLabs needs ids through gipity.
VOICES = {
    "george": ("elevenlabs", "JBFqnCBsd6RMkjVDRZzb"),
    "brian": ("elevenlabs", "nPczCjzI2devNBz1zQrb"),
    "daniel": ("elevenlabs", "onwK4e9ZLuTAKqWW03F9"),
    "rachel": ("elevenlabs", "21m00Tcm4TlvDq8ikWAM"),
}
DEFAULT_VOICE = "george"

METHODS = {
    # name: (method byte, PIT divisor, extension)
    "chip": (1, 128, ".SND"),  # 9322 Hz: the READY waits make 11 kHz too tight
    "pwm": (2, 75, ".SNP"),    # 15909 Hz, also the PWM carrier
}


# ---- getting audio ---------------------------------------------------------

def tts(text, voice, path):
    """Speech from gipity. voice: a VOICES name, provider:voice, or an id."""
    if voice in VOICES:
        provider, vid = VOICES[voice]
    elif ":" in voice:
        provider, vid = voice.split(":", 1)
    else:
        provider, vid = "elevenlabs", voice
    cmd = ["gipity", "generate", "speech", text, "--provider", provider, "--voice", vid,
           "-o", path, "--json"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or not os.path.exists(path):
        sys.exit("gipity failed:\n" + r.stdout + r.stderr)


def decode(path):
    """Any audio file -> mono float32 at SRC_RATE."""
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SRC_RATE),
                        "-f", "f32le", "-"], capture_output=True, check=True)
    return np.frombuffer(r.stdout, dtype=np.float32).astype(np.float64)


# ---- shaping the sound -----------------------------------------------------

def trim(x, rate, floor_db=-45, pad=0.04):
    env = np.abs(x)
    thr = env.max() * 10 ** (floor_db / 20)
    idx = np.nonzero(env > thr)[0]
    if len(idx) == 0:
        return x
    p = int(pad * rate)
    return x[max(0, idx[0] - p): idx[-1] + p]


def compress(x, rate, thr_db=-26, ratio=5.0, attack=0.002, release=0.08):
    """Feed-forward compressor: makes every syllable nearly as loud as the loudest."""
    a_att = np.exp(-1 / (attack * rate))
    a_rel = np.exp(-1 / (release * rate))
    env = np.empty_like(x)
    e = 0.0
    ax = np.abs(x)
    for i, v in enumerate(ax):  # plain loop: ~250k samples, fine
        a = a_att if v > e else a_rel
        e = a * e + (1 - a) * v
        env[i] = e
    env_db = 20 * np.log10(np.maximum(env, 1e-9) / max(env.max(), 1e-9))
    over = np.maximum(env_db - thr_db, 0)
    gain_db = -over * (1 - 1 / ratio)
    return x * 10 ** (gain_db / 20)


def shape(x, rate, out_rate):
    """Band-limit, brighten, squeeze, and resample to out_rate."""
    x = trim(x, rate)
    sos = signal.butter(4, 200, "highpass", fs=rate, output="sos")
    x = signal.sosfilt(sos, x)
    # presence: +6 dB shelf from ~2 kHz, which also undoes the PCjr's
    # sample-and-hold droop near the top of the band
    b, a = signal.butter(1, 2000, "highpass", fs=rate)
    x = x + 1.0 * signal.lfilter(b, a, x)
    x = compress(x, rate)
    frac = Fraction(out_rate / rate).limit_denominator(2000)
    y = signal.resample_poly(x, frac.numerator, frac.denominator)
    y /= np.percentile(np.abs(y), 99.9)
    return np.tanh(1.2 * y) / np.tanh(1.2)  # soft limit the few peaks left


def frame(y, rate, ramp=0.08, quiet=0.06):
    """Signal in [-1, 1] -> level in [0, 1], with slow ramps from and back to
    0 so the step to the resting mid level is a soft swell, not a click."""
    n = int(ramp * rate)
    q = np.full(int(quiet * rate), 0.5)
    up = 0.5 * (1 - np.cos(np.linspace(0, np.pi, n))) * 0.5
    return np.concatenate([up, q, 0.5 + 0.5 * y, q, up[::-1]])


# ---- method 1: three SN76496 voices ----------------------------------------

AMP = [10 ** (-k / 10) for k in range(15)] + [0.0]  # 2 dB per attenuation step


def chip_table(top=2.2, robust=1.0):
    """Up to 256 voice combinations whose summed levels cover 0..top.

    The fine levels only exist if the chip's 2 dB steps are exact. Real
    steps are off by some fraction of a dB, so each combination's spread
    under a 0.35 dB-per-step error model is estimated (fixed seed), and the
    pick for each target level trades distance against that spread. In
    simulation this costs 2.5 dB on a perfect chip and gains 4 dB on one
    with 0.25 dB step errors (see README.md).
    Returns (levels scaled to 0..1, three rows of OUT bytes, 256 each)."""
    combos = np.array(list(itertools.product(range(16), repeat=3)))
    amp = np.array(AMP)
    ideal = amp[combos].sum(1)
    rng = np.random.default_rng(7)
    draws = []
    for _ in range(300):
        v = []
        for _voice in range(3):
            db = np.concatenate([[0], np.cumsum(2 + rng.normal(0, 0.35, 15))])
            a = 10 ** (-db / 20)
            a[15] = 0
            v.append(a)
        draws.append(v[0][combos[:, 0]] + v[1][combos[:, 1]] + v[2][combos[:, 2]])
    spread = np.array(draws).std(0)
    pick = {int((np.abs(ideal - g) + robust * spread).argmin()) for g in np.linspace(0, top, 256)}
    pick = sorted(pick, key=lambda i: ideal[i])
    levels = ideal[pick] / top
    c = combos[pick]
    pad = 256 - len(pick)
    rows = [[0x90 | int(k) for k in c[:, 0]], [0xB0 | int(k) for k in c[:, 1]],
            [0xD0 | int(k) for k in c[:, 2]]]
    rows = [r + [r[-1]] * pad for r in rows]
    return levels, rows


def quantize(target, levels, shaping):
    """Nearest-level quantizer with error feedback. levels: sorted array."""
    out = np.empty(len(target), dtype=np.int32)
    e1 = e2 = 0.0
    k1, k2 = shaping
    for i, t in enumerate(target):
        want = t + k1 * e1 + k2 * e2
        j = int(np.searchsorted(levels, want))
        if j == len(levels) or (j > 0 and want - levels[j - 1] < levels[j] - want):
            j -= 1
        out[i] = j
        e2 = e1
        e1 = want - levels[j]
        e1 = max(-0.1, min(0.1, e1))  # keep the loop stable at the rails
    return out


def make_chip(y, rate):
    levels, rows = chip_table()
    target = frame(y, rate)
    idx = quantize(target, levels, (0.5, 0.0))  # best in-band SNR (39.5 dB)
    table = bytes(rows[0] + rows[1] + rows[2])
    return idx.astype(np.uint8).tobytes(), table, levels[idx]


# ---- method 2: PWM on PIT channel 2 ----------------------------------------

def make_pwm(y, rate, divisor):
    # count c: the line is low for c PIT clocks, then high, so the level is
    # (divisor - c) / divisor. Keep away from both ends of the period.
    counts = np.arange(divisor - 2, 1, -1)  # level rises as count falls
    levels = (divisor - counts) / divisor
    target = frame(y, rate) * (levels[-1] - levels[0]) + levels[0]
    idx = quantize(target, levels, (1.6, -0.64))  # 2nd order, noise up high
    return counts[idx].astype(np.uint8).tobytes(), levels[idx]


# ---- previews --------------------------------------------------------------

def speaker(x, rate):
    """Rough stand-in for a small monitor speaker: 150 Hz to 7 kHz."""
    sos = signal.butter(2, [150, min(7000, rate * 0.45)], "bandpass", fs=rate, output="sos")
    return signal.sosfilt(sos, x)


def write_wav(path, x, rate):
    x = x - x.mean()
    x = 0.9 * x / max(np.abs(x).max(), 1e-9)
    pcm = (x * 32767).astype("<i2").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, int(rate), int(rate) * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)


def preview_chip(levels, rate, path):
    out_rate = 44100
    t = np.arange(int(len(levels) * out_rate / rate))
    held = levels[np.minimum((t * rate / out_rate).astype(int), len(levels) - 1)]
    write_wav(path, speaker(held, out_rate), out_rate)


def preview_pwm(levels, divisor, path):
    # the real waveform at the PIT clock: low for c clocks, high for the rest
    hi = np.round(levels * divisor).astype(int)
    lo = divisor - hi
    wave = np.repeat(np.tile([0.0, 1.0], len(levels)), np.ravel(np.column_stack([lo, hi])))
    dec = 27  # 1193182 / 27 = 44192 Hz
    x = signal.resample_poly(wave, 1, dec)
    write_wav(path, speaker(x, PIT / dec), PIT / dec)


# ---- the file --------------------------------------------------------------

def write_snd(path, method, divisor, data, text, table=b""):
    t = text.encode("ascii", "replace")[:500]
    with open(path, "wb") as f:
        f.write(b"JRSN" + struct.pack("<BBHIHH", 1, method, divisor, len(data), len(t), len(table)))
        f.write(t + table + data)


def asciify(s):
    for a, b in (("\u2018", "'"), ("\u2019", "'"), ("\u201c", '"'), ("\u201d", '"'),
                 ("\u2014", ", "), ("\u2013", "-"), ("\u2026", "...")):
        s = s.replace(a, b)
    return s.encode("ascii", "replace").decode()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("audio", nargs="?", help="audio file (wav, mp3, anything ffmpeg reads)")
    ap.add_argument("--text", help="say this (speech from gipity)")
    ap.add_argument("--voice", default=DEFAULT_VOICE,
                    help="%s, provider:voice (openai:sage, gemini:Charon) or an ElevenLabs id"
                    % ", ".join(VOICES))
    ap.add_argument("--title", help="text SAY prints while playing (default: --text, or the"
                    " audio file's .txt twin, as in clips/)")
    ap.add_argument("--method", choices=["chip", "pwm", "both"], default="both")
    ap.add_argument("--keep", action="store_true", help="also keep the source audio as OUT.mp3")
    ap.add_argument("-o", "--out", required=True, help="output path without extension, 8 chars")
    a = ap.parse_args()
    if bool(a.audio) == bool(a.text):
        ap.error("give an audio file or --text, not both")
    base = os.path.basename(a.out)
    if len(base) > 8 or not base.replace("_", "").isalnum():
        ap.error("the name must be a DOS name: up to 8 letters or digits")
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        src = a.audio
        if a.text:
            src = os.path.join(tmp, "tts.mp3")
            tts(a.text, a.voice, src)
            if a.keep:
                shutil.copy(src, a.out + ".mp3")
        x = decode(src)
    title = a.title
    if title is None and a.audio and os.path.exists(os.path.splitext(a.audio)[0] + ".txt"):
        title = open(os.path.splitext(a.audio)[0] + ".txt").read().strip()
    title = asciify(title if title is not None else (a.text or ""))

    for m in (["chip", "pwm"] if a.method == "both" else [a.method]):
        code, divisor, ext = METHODS[m]
        rate = PIT / divisor
        y = shape(x, SRC_RATE, rate)
        if m == "chip":
            data, table, lv = make_chip(y, rate)
            write_snd(a.out + ext, code, divisor, data, title, table)
            preview_chip(lv, rate, a.out + "-chip.wav")
        else:
            data, lv = make_pwm(y, rate, divisor)
            write_snd(a.out + ext, code, divisor, data, title)
            preview_pwm(lv, divisor, a.out + "-pwm.wav")
        print("%s%s: %s, %.0f Hz, %.1f s, %d KB" % (base.upper(), ext, m, rate, len(data) / rate,
                                                  (len(data) + 1023) // 1024))


if __name__ == "__main__":
    main()
