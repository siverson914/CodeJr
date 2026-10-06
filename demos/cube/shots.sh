#!/bin/sh
# Renders JRCUBE headless at fixed time steps and makes an animated GIF.
#   [AT=pattern] demos/cube/shots.sh [frames] [outdir]   (default: pattern 12, demos/cube/shots)
# Each frame is 4 retraces (1/15 s) of demo time, so the GIF plays at the
# demo's real speed. Never opens a window.
set -e
n=${1:-60}; out=$(realpath -m "${2:-$(dirname "$0")/shots}")
cd "$(dirname "$0")"
rm -rf "$out/raw"; mkdir -p "$out/raw"
EMU_TIMEOUT=600 ../../lib/emu.sh "$out/raw" 30000 JRCUBE.EXE /GIF:$n /AT:${AT:-12} >/dev/null || true
magick -delay 7 -loop 0 "$out"/raw/F*.PNG -sample 200%x240% -layers Optimize "$out/jrcube.gif"
cp "$out/raw/F$(printf %03d $((n / 2))).PNG" "$out/frame.png"
rm -rf "$out/raw"
ls -l "$out/jrcube.gif"
