#!/bin/sh
# Runs SOPWITH2.EXE headless in DOSBox (machine=pcjr) for N frames and saves
# what was on the screen as a PNG, plus the frame rate from LOG.TXT.
#   apps/sopwith/shot.sh <out.png> [frames] [cycles] [F = no frame limiter] [exe]
set -eu
cd "$(dirname "$0")"
out=$1; frames=${2:-60}; cyc=${3:-3000}; free=${4:-}; exe=${5:-SOPWITH2.EXE}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/c"
cp "$exe" "$tmp/c/SOP.EXE"
printf '[dosbox]\nmachine=pcjr\n[cpu]\ncore=normal\ncycles=fixed %s\n[autoexec]\nmount c %s/c\nc:\nSET SOPTEST=%s%s\nSOP.EXE -s -i -k -q\nexit\n' "$cyc" "$tmp" "$frames" "$free" > "$tmp/dosbox.conf"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${EMU_TIMEOUT:-120} dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1 || true
[ -f "$tmp/c/LOG.TXT" ] && cat "$tmp/c/LOG.TXT"
[ -f "$tmp/c/SHOT.BIN" ] || { echo "no SHOT.BIN: the game did not reach frame $frames"; exit 1; }
# CGA mode 4: 2 bits a pixel, even rows at 0, odd rows at 2000h; palette 1 (cyan, magenta, white)
python3 - "$tmp/c/SHOT.BIN" "$tmp/shot.ppm" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
pal = [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)]
out = bytearray()
for y in range(200):
    row = d[(y & 1) * 0x2000 + (y >> 1) * 80:][:80]
    for b in row:
        for s in (6, 4, 2, 0):
            out += bytes(pal[(b >> s) & 3])
open(sys.argv[2], "wb").write(b"P6 320 200 255\n" + bytes(out))
PY
magick "$tmp/shot.ppm" -scale 640x400! "$out"
cp "$tmp/c/SHOT.BIN" "${out%.png}.bin"   # the raw screen, to compare builds byte for byte
