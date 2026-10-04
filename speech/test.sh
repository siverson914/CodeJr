#!/bin/sh
# SAY.EXE in the emulated PCjr (headless): converts the short FRIENDS clip
# (no network), plays both versions, a speed test and a missing file, and
# checks that every sample was played, none was late, and the stopwatch
# agrees with the clip's rate. Then it decodes FRIENDS as 4-, 3- and 2-bit
# ADPCM (the asm decoder, and the C one with /C) and compares each with
# mksnd.py's reference decoder, byte for byte. DOSBox counts instructions, not clocks, so
# this says the loops work, not that a real PCjr keeps up; SAY /V and
# SAY /T on the PCjr are the real test.
#
# DOSBox doesn't implement the 8259 poll command that the chip method uses
# by default (it exits with "PIC: poll command not handled"), so the chip
# runs here use /H, the interrupt-timed loop. The poll loop is real-PCjr only.
set -e
cd "$(dirname "$0")"
./build.sh >/dev/null
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir -p "$t/c"
./mksnd.py clips/FRIENDS.mp3 --method both -o "$t/c/FRIENDS" >/dev/null
for b in 4 3 2; do
	./mksnd.py clips/FRIENDS.mp3 --codec adpcm$b -o "$t/c/FRI$b" >/dev/null
	./mksnd.py --ref-decode "$t/c/FRI$b.SND" -o "$t/REF$b.BIN"
done
cp SAY.EXE "$t/c/"
printf '[dosbox]\nmachine=pcjr\n[cpu]\ncore=normal\ncycles=fixed 1000\n[autoexec]\nmount c %s/c\nc:\nSAY /I FRIENDS > OUT.TXT\nSAY /V /Q /H FRIENDS FRIENDS.SNP >> OUT.TXT\nSAY /T /Q /H FRIENDS >> OUT.TXT\nSAY NOPE >> OUT.TXT\nSAY /X /V /Q /H FRI4 >> OUT.TXT\ncopy SAYDUMP.BIN D4.BIN\nSAY /X /V /Q /H FRI3 >> OUT.TXT\ncopy SAYDUMP.BIN D3.BIN\nSAY /X /V /Q /H FRI2 >> OUT.TXT\ncopy SAYDUMP.BIN D2.BIN\nSAY /X /C /Q /H FRI4 >> OUT.TXT\ncopy SAYDUMP.BIN C4.BIN\nexit\n' "$t" > "$t/d.conf"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 dosbox -conf "$t/d.conf" >/dev/null 2>&1 || true
out=$(tr -d "\r" < "$t/c/OUT.TXT" 2>/dev/null)
echo "$out" | sed 's/^/  /'
ok=1
echo "$out" | grep -q 'FRIENDS.SND: chip (3 voices)' || ok=0
echo "$out" | grep -Eq 'FRIENDS.SND method 1 .* played ([0-9]+) of \1, late 0$' || ok=0
echo "$out" | grep -Eq 'FRIENDS.SNP method 2 .* played ([0-9]+) of \1, late 0$' || ok=0
echo "$out" | grep -Eq 'speed 93[0-9][0-9] Hz \(wanted 932[12]\)' || ok=0
echo "$out" | grep -q 'can play chip clips at' || ok=0
echo "$out" | grep -q "can't open NOPE.SND" || ok=0
echo "$out" | grep -Eq 'FRI4.SND method 1 .* played ([0-9]+) of \1, late 0$' || ok=0
for f in D4:4 D3:3 D2:2 C4:4; do	# the 8088 decoders against mksnd.py's, byte for byte
	if cmp -s "$t/c/${f%:*}.BIN" "$t/REF${f#*:}.BIN"; then echo "  decoder ${f%:*}: matches"; else echo "  decoder ${f%:*}: DIFFERS"; ok=0; fi
done
[ $ok = 1 ] && echo "PASS speech" || { echo "FAIL speech"; exit 1; }
