#!/bin/sh
# Builds MUSTEST.EXE, logs the whole JRCUBE song headless in DOSBox
# (lockstep, so emulator speed doesn't matter) and renders it to
# shots/song.wav with games/jrai/render.py.
#   demos/cube/musictest.sh [ticks]     (default: the whole song, 38*112)
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
LIB=../../lib
ticks=${1:-4256}
wcl -q -bt=dos -ml -0 -ox -wx -we -i=$LIB -fe=MUSTEST.EXE musictest.c music.c $LIB/jrgfx.lib
rm -f *.o *.err
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
../../lib/emu.sh "$tmp/run" 30000 MUSTEST.EXE /LOG:$ticks || true
cat "$tmp/run/OUT.TXT"
mkdir -p shots
python3 ../../games/jrai/render.py "$tmp/run/SND.BIN" shots/song.wav
