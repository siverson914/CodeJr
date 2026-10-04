#!/bin/sh
# Renders JRCUBE headless at fixed time steps and makes an animated GIF.
#   demos/cube/shots.sh [frames] [outdir]
# Each frame is 4 retraces (1/15 s) of demo time, so the GIF plays at the
# demo's real speed. Never opens a window.
set -e
cd "$(dirname "$0")"
n=${1:-60}; out=${2:-shots}
rm -rf "$out/raw"; mkdir -p "$out/raw"
EMU_TIMEOUT=600 ../../lib/emu.sh "$out/raw" 30000 JRCUBE.EXE /GIF:$n >/dev/null || true
magick -delay 7 -loop 0 "$out"/raw/F*.PNG -scale 200% -layers Optimize "$out/jrcube.gif"
cp "$out/raw/F$(printf %03d $((n / 2))).PNG" "$out/frame.png"
ls -l "$out/jrcube.gif"
