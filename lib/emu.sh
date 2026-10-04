#!/bin/sh
# Run a DOS program headless in DOSBox (machine=pcjr) and copy its output
# files back.  Never opens a window.
#   lib/emu.sh <outdir> <cycles> <EXE> [args...]
# Copies every file the program wrote (LOG.TXT, *.PPM ...) into <outdir>.
set -u
out=$1; cyc=$2; exe=$3; shift 3
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/c" "$out"
cp "$exe" "$tmp/c/"
base=$(basename "$exe")
printf '[dosbox]\nmachine=pcjr\n[cpu]\ncore=normal\ncycles=fixed %s\n[autoexec]\nmount c %s/c\nc:\n%s %s > OUT.TXT\nexit\n' "$cyc" "$tmp" "$base" "$*" > "$tmp/dosbox.conf"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${EMU_TIMEOUT:-120} dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1
rc=$?
rm -f "$tmp/c/$base"
cp "$tmp"/c/* "$out/" 2>/dev/null
for f in "$out"/*.PPM; do [ -e "$f" ] && magick "$f" "${f%.PPM}.PNG"; done
exit $rc
