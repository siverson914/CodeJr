#!/bin/sh
# SAY.EXE in the emulated PCjr (headless): converts the short FRIENDS clip
# (no network), plays both versions and a missing file, and checks that
# every sample was played and none was late. DOSBox counts instructions,
# not clocks, so "late 0" here says the loop works, not that a real
# PCjr keeps up; SAY /V on the PCjr is the real test.
set -e
cd "$(dirname "$0")"
./build.sh >/dev/null
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir -p "$t/c"
./mksnd.py clips/FRIENDS.mp3 -o "$t/c/FRIENDS" >/dev/null
cp SAY.EXE "$t/c/"
printf '[dosbox]\nmachine=pcjr\n[cpu]\ncore=normal\ncycles=fixed 1000\n[autoexec]\nmount c %s/c\nc:\nSAY /I FRIENDS > OUT.TXT\nSAY /V /Q FRIENDS FRIENDS.SNP >> OUT.TXT\nSAY NOPE >> OUT.TXT\nexit\n' "$t" > "$t/d.conf"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 dosbox -conf "$t/d.conf" >/dev/null 2>&1 || true
out=$(tr -d "\r" < "$t/c/OUT.TXT" 2>/dev/null)
echo "$out" | sed 's/^/  /'
ok=1
echo "$out" | grep -q 'FRIENDS.SND: chip (3 voices)' || ok=0
echo "$out" | grep -Eq 'FRIENDS.SND method 1 .* played ([0-9]+) of \1, late 0$' || ok=0
echo "$out" | grep -Eq 'FRIENDS.SNP method 2 .* played ([0-9]+) of \1, late 0$' || ok=0
echo "$out" | grep -q "can't open NOPE.SND" || ok=0
[ $ok = 1 ] && echo "PASS speech" || { echo "FAIL speech"; exit 1; }
