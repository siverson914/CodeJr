#!/bin/sh
# Makes a video of JRCUBE with its music: runs JRCUBE /VIDEO headless in
# DOSBox (fixed 1/15 s steps, so emulator speed doesn't matter), renders
# the logged sound chip writes to audio (games/jrai/render.py emulates the
# SN76496) and muxes an MP4. Never opens a window.
#   demos/cube/video.sh [out.mp4] [frames] [extra JRCUBE options]
# The whole song is 1064 frames (about 10 minutes to make).
set -e
out=$(realpath -m "${1:-$(dirname "$0")/shots/jrcube.mp4}")
n=${2:-1064}
shift 2 2>/dev/null || shift $#
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
EMU_TIMEOUT=3000 ../../lib/emu.sh "$tmp/run" 60000 JRCUBE.EXE /VIDEO:$n "$@" >/dev/null 2>&1 || true
python3 ../../games/jrai/render.py "$tmp/run/SND.BIN" "$tmp/audio.wav"
ffmpeg -loglevel error -y -framerate 15 -start_number 1 -i "$tmp/run/F%03d.PNG" -i "$tmp/audio.wav" \
	-vf "scale=640:480:flags=neighbor" -c:v libx264 -pix_fmt yuv420p -crf 18 -c:a aac -b:a 128k -shortest "$out"
ls -l "$out"
