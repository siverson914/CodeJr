#!/bin/sh
# Regenerates the README screenshots in shots/ by running scripted demo
# flights headless in DOSBox (machine=pcjr, no window). The demo is
# frame-locked, so the pictures don't depend on emulator speed; a high
# cycle count just makes it quick.
set -e
cd "$(dirname "$0")"
EMU=../../lib/emu.sh
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p shots
# name scene options frame
shot() {
	rm -rf "$tmp/out"
	EMU_TIMEOUT=120 $EMU "$tmp/out" 20000 JRFLIGHT.EXE /DEMO:$(($4 + 1)) /SCENE:$2 $3 /SHOT:$4 >/dev/null 2>&1 || true
	magick "$tmp/out/SHOT01.PPM" -scale 640x400! "shots/$1.png"
	echo "shots/$1.png"
}
shot 1-takeoff-roll     0 /HI 30
shot 2-rotate-left-view 0 /HI 62
shot 3-climbout-back    0 /HI 74
shot 4-town             1 /HI 25
shot 5-turn             1 /HI 52
shot 6-mountains        3 /HI 10
shot 7-mountain-turn    3 /HI 40
shot 8-final-approach   2 /HI 128
shot 9-low-detail-town  1 /LO 25
shot 10-medium-window   1 "/LO /SIZE:1" 25
shot 11-fast-preset     1 /FAST 25
shot 12-fast-approach   2 /FAST 128
