#!/bin/sh
# Makes a preview video of the demo (no boot sequence): runs JRAI /VIDEO
# headless in DOSBox (frame-locked, so emulator speed doesn't matter),
# renders the logged sound chip writes to audio, and muxes an MP4.
#   games/jrai/video.sh [out.mp4]     (about 5 minutes)
set -e
cd "$(dirname "$0")"
out=${1:-$PWD/jrai-preview.mp4}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
EMU_TIMEOUT=1500 ../../lib/emu.sh "$tmp/run" 60000 JRAI.EXE /VIDEO >/dev/null 2>&1 || true
python3 render.py "$tmp/run/SND.BIN" "$tmp/audio.wav"
# frames are named by time (T/4); a phrase or a slow scene skips numbers,
# so repeat the last frame to keep the video in step with the audio
mkdir "$tmp/seq"
last=""
n=$(ls "$tmp/run" | sed -n 's/^F\([0-9]*\)\.PNG$/\1/p' | sort -n | tail -1)
i=0
while [ "$i" -le "$((10#$n))" ]; do
	f=$(printf "%s/run/F%04d.PNG" "$tmp" "$i")
	[ -e "$f" ] && last=$f
	[ -n "$last" ] && ln -s "$last" "$(printf "%s/seq/%05d.png" "$tmp" "$i")"
	i=$((i + 1))
done
ffmpeg -loglevel error -y -framerate 15 -start_number 0 -i "$tmp/seq/%05d.png" -i "$tmp/audio.wav" \
	-vf "scale=640:480:flags=neighbor" -c:v libx264 -pix_fmt yuv420p -crf 18 -c:a aac -b:a 128k -shortest "$out"
ls -l "$out"
