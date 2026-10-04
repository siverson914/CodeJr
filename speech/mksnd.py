#!/usr/bin/env -S uv run --quiet --script
# /// script
# dependencies = ["numpy", "scipy"]
# ///
"""Makes PCjr speech clips (.SND and .SNP) for SAY.EXE.

  mksnd.py --text "Hello Steve." -o out/HELLO          speech via gipity
  mksnd.py --text "Hi" --voice gemini:Charon -o HI     another voice
  mksnd.py song.wav -o SONG                            any audio file
  mksnd.py clips/HELLO.mp3 -o out/HELLO                text from clips/HELLO.txt
  mksnd.py clips/HELLO.mp3 --hz 10650 -o out/HELLO10   a faster chip clip

Writes OUT.SND (method 1: the SN76496's three voices as one DAC, 9.3 kHz
unless --hz) and OUT-chip.wav, a preview of how it should sound. With
--method pwm or both, also OUT.SNP (PWM on PIT channel 2, 15.9 kHz) and
OUT-pwm.wav. On the real PCjr the chip method sounded best and PWM
whined, so chip is the default.

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
    "chip": (1, 128, ".SND"),  # 9322 Hz; --hz overrides (SAY /T measures the limit)
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


def chip_table(top=2.2, robust=1.0, voices=3):
    """Up to 256 voice combinations whose summed levels cover 0..top.

    The fine levels only exist if the chip's 2 dB steps are exact. Real
    steps are off by some fraction of a dB, so each combination's spread
    under a 0.35 dB-per-step error model is estimated (fixed seed), and the
    pick for each target level trades distance against that spread. In
    simulation this costs 2.5 dB on a perfect chip and gains 4 dB on one
    with 0.25 dB step errors (see README.md).
    Returns (levels scaled to 0..1, three rows of OUT bytes, 256 each)."""
    top = 1.0 if voices == 1 else top * voices / 3
    combos = np.array([c + (15,) * (3 - voices)
                       for c in itertools.product(range(16), repeat=voices)])
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


def make_chip(y, rate, bits=0):
    """bits 0: one raw byte per sample; 2-4: ADPCM codes of that size.
    Returns (data, table, sample count, levels played)."""
    levels, rows = chip_table()
    target = frame(y, rate)
    table = bytes(rows[0] + rows[1] + rows[2])
    if bits:
        codes, idx = adpcm_encode(target, levels, bits)
        return pack_bits(codes, bits), table, len(idx), levels[np.minimum(idx, len(levels) - 1)]
    idx = quantize(target, levels, (0.5, 0.0))  # best in-band SNR (39.5 dB)
    return idx.astype(np.uint8).tobytes(), table, len(idx), levels[idx]


# ---- ADPCM: smaller clips, decoded by SAY before it plays ------------------
#
# IMA-style ADPCM on the sample bytes (the level-table indices, 0-255). The
# predictor P is the index times 256; each code moves it by a fraction of
# the current step and moves the step up or down. Decoding is shifts, adds
# and two table lookups per sample, no multiplies: say.c's adpcm_decode
# must match adpcm_decode_ref here bit for bit (speech/test.sh checks).

# IMA's step table cut at 16818, so a code's change (at most 1.875 steps)
# fits a signed 16-bit add, which is how player.asm's sp_dec applies it.
STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
         50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
         253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
         1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
         3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
         12635, 13899, 15289, 16818]
ADJ = {4: [-1, -1, -1, -1, 2, 4, 6, 8], 3: [-1, -1, 2, 4], 2: [-1, 2]}


def adpcm_step(p, si, code, bits):
    """One decoder step: (P, step index, code) -> (P, step index)."""
    step = STEPS[si]
    half = 1 << (bits - 1)
    m = code & (half - 1)
    diff = step >> (bits - 1)
    for k in range(bits - 1):  # magnitude bits, top one first
        if m & (half >> (k + 1)):
            diff += step >> k
    p = p - diff if code & half else p + diff
    p = min(max(p, 0), 65535)
    si = min(max(si + ADJ[bits][m], 0), len(STEPS) - 1)
    return p, si


def adpcm_steps_np(p, si, bits):
    """Vectorized adpcm_step over states (p, si) x every code.
    Returns arrays shaped (len(p), 2**bits)."""
    steps = np.array(STEPS)
    adj = np.array(ADJ[bits])
    half = 1 << (bits - 1)
    codes = np.arange(1 << bits)
    mag = codes & (half - 1)
    step = steps[si][:, None]
    diff = step >> (bits - 1)
    for k in range(bits - 1):
        diff = diff + np.where(mag & (half >> (k + 1)), step >> k, 0)
    p1 = np.where(codes & half, p[:, None] - diff, p[:, None] + diff)
    p1 = np.clip(p1, 0, 65535)
    s1 = np.clip(si[:, None] + adj[mag], 0, len(STEPS) - 1)
    return p1, s1


def adpcm_encode(target, levels, bits, beam=16):
    """target: wanted level per sample (0..1, as levels).

    Beam search: keeps the `beam` best decoder states (predictor, step
    index) and their code histories, so a code that costs a little now but
    sets up a better step later can win. It steers the predictor itself (in
    index units), not the rounded output: at the smallest step every code
    rounds to the same index, and an encoder looking only at the output
    would never grow the step. Returns (codes, decoded indices)."""
    want = np.interp(target, levels, np.arange(len(levels))) * 256 + 128
    want = np.minimum(want, (len(levels) - 1) * 256 + 255)
    n = len(target)
    ncode = 1 << bits
    p = np.zeros(1, dtype=np.int64)
    si = np.zeros(1, dtype=np.int64)
    cost = np.zeros(1)
    back = np.empty((n, beam), dtype=np.int32)   # parent state per kept state
    code = np.empty((n, beam), dtype=np.int16)
    for i in range(n):
        p1, s1 = adpcm_steps_np(p, si, bits)
        c1 = cost[:, None] + (p1 - want[i]) ** 2
        flat = c1.ravel()
        # best first, and only one survivor per (predictor, step) state
        order = np.argsort(flat, kind="stable")
        key = (p1.ravel()[order] << 7) | s1.ravel()[order]
        _, first = np.unique(key, return_index=True)
        keep = order[np.sort(first)][:beam]
        keep = keep[np.argsort(flat[keep], kind="stable")]
        k = len(keep)
        back[i, :k] = keep // ncode
        code[i, :k] = keep % ncode
        p, si, cost = p1.ravel()[keep], s1.ravel()[keep], flat[keep]
    j = 0  # the best final state, traced back
    codes = np.empty(n, dtype=np.int32)
    for i in range(n - 1, -1, -1):
        codes[i] = code[i, j]
        j = back[i, j]
    return codes, adpcm_decode_ref(codes, bits, len(levels))


def adpcm_decode_ref(codes, bits, nlevels=256):
    p, si, out = 0, 0, []
    for c in codes:
        p, si = adpcm_step(p, si, int(c), bits)
        out.append(min(p >> 8, nlevels - 1))
    return np.array(out, dtype=np.int32)


def pack_bits(codes, bits):
    """Codes packed most significant bit first, padded with zero bits."""
    acc, nacc, out = 0, 0, bytearray()
    for c in codes:
        acc = (acc << bits) | int(c)
        nacc += bits
        while nacc >= 8:
            nacc -= 8
            out.append((acc >> nacc) & 255)
    if nacc:
        out.append((acc << (8 - nacc)) & 255)
    return bytes(out)


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

def write_snd(path, method, divisor, data, text, table=b"", nsamples=None, bits=0):
    """Version 1 for raw samples; version 2 adds 4 header bytes, the first
    of which is the ADPCM code size (see README.md)."""
    t = text.encode("ascii", "replace")[:500]
    n = len(data) if nsamples is None else nsamples
    with open(path, "wb") as f:
        f.write(b"JRSN" + struct.pack("<BBHIHH", 2 if bits else 1, method, divisor, n, len(t),
                                      len(table)))
        if bits:
            f.write(struct.pack("<BBH", bits, 0, 0))
        f.write(t + table + data)


def ref_decode(path):
    """The samples SAY should end up with for a .SND file (tests)."""
    d = open(path, "rb").read()
    ver, n, tl, tb = d[4], struct.unpack("<I", d[8:12])[0], *struct.unpack("<HH", d[12:16])
    if ver == 1:
        return d[16 + tl + tb:16 + tl + tb + n]
    bits, data = d[16], d[20 + tl + tb:]
    codes, acc, nacc, it = [], 0, 0, iter(data)
    for _ in range(n):
        if nacc < bits:
            acc, nacc = (acc << 8) | next(it), nacc + 8
        nacc -= bits
        codes.append((acc >> nacc) & ((1 << bits) - 1))
    return adpcm_decode_ref(codes, bits).astype(np.uint8).tobytes()


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
    ap.add_argument("--method", choices=["chip", "pwm", "both"], default="chip",
                    help="chip (default: it won on the real PCjr), pwm (whines), or both")
    ap.add_argument("--hz", type=float, help="sample rate for the chip method (default 9322)")
    ap.add_argument("--codec", choices=["raw", "adpcm4", "adpcm3", "adpcm2"], default="raw",
                    help="chip method: raw bytes, or ADPCM with 4, 3 or 2 bits per sample"
                    " (SAY decodes it before playing; speech/grid.py compares them)")
    ap.add_argument("--keep", action="store_true", help="also keep the source audio as OUT.mp3")
    ap.add_argument("--ref-decode", metavar="SND",
                    help="tests: write the samples SAY should decode from SND to --out, exactly")
    ap.add_argument("-o", "--out", required=True, help="output path without extension, 8 chars")
    a = ap.parse_args()
    if a.ref_decode:
        open(a.out, "wb").write(ref_decode(a.ref_decode))
        return
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
        if m == "chip" and a.hz:
            divisor = round(PIT / a.hz)
        rate = PIT / divisor
        y = shape(x, SRC_RATE, rate)
        if m == "chip":
            bits = 0 if a.codec == "raw" else int(a.codec[-1])
            data, table, n, lv = make_chip(y, rate, bits)
            write_snd(a.out + ext, code, divisor, data, title, table, n, bits)
            preview_chip(lv, rate, a.out + "-chip.wav")
        else:
            data, lv = make_pwm(y, rate, divisor)
            n = len(data)
            write_snd(a.out + ext, code, divisor, data, title)
            preview_pwm(lv, divisor, a.out + "-pwm.wav")
        print("%s%s: %s, %.0f Hz, %s, %.1f s, %d KB" % (
            base.upper(), ext, m, rate, a.codec if m == "chip" else "raw", n / rate,
            (len(data) + 1023) // 1024))


if __name__ == "__main__":
    main()
