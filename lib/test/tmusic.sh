#!/bin/sh
# Builds and runs the music player's test headless in DOSBox (never opens
# a window), twice: as the library builds it, and with -dMUS_C_TICK (the C
# version of the per-tick code), and checks that both sent the same bytes
# to the sound chip. Prints the log; the exit code is 0 only if it ends
# "TMUSIC done: 0 failures" and the two agree.
#   lib/test/tmusic.sh [outdir]   keeps LOG.TXT, SND.BIN, SND2.BIN and
#                                 song.wav (the test song, rendered) there
set -e
cd "$(dirname "$0")/.."
. ../client/owenv.sh
L=$PWD
CF="-q -bt=dos -ml -0 -ox -wx -we -i=$L"
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
out=${1:-$t/out}
mkdir -p "$out"
out=$(cd "$out" && pwd)
# the library itself, or its sources when it has not been built
if [ -f jrgfx.lib ]; then G=$L/jrgfx.lib; else G="$L/jrgfx.c $L/jrtab.c $L/jrfast.c"; fi
(cd "$t" && wcl $CF -c $L/jrmusic.c && wcl $CF -fe=TMUSIC.EXE $L/test/tmusic.c $G \
	&& wcl $CF -dMUS_C_TICK -fe=TMUSICC.EXE $L/test/tmusic.c $G)
rm -f "$out/LOG.TXT"
# the C version of the tick first: its chip bytes are the reference
EMU_TIMEOUT=180 ./emu.sh "$t/c" 3000 "$t/TMUSICC.EXE" || true
EMU_TIMEOUT=180 ./emu.sh "$out" 3000 "$t/TMUSIC.EXE" || true
cat "$out/LOG.TXT" 2>/dev/null || true
same=0
if cmp -s "$out/SND.BIN" "$t/c/SND.BIN" && cmp -s "$out/SND2.BIN" "$t/c/SND2.BIN" \
	&& tail -1 "$t/c/LOG.TXT" | grep -q "TMUSIC done: 0 failures"; then
	echo "assembly tick and C tick (-dMUS_C_TICK): same bytes to the chip, both pass"
	same=1
else
	echo "FAIL: the assembly tick and the C tick differ, or the C build failed:"
	grep -E "FAIL|done" "$t/c/LOG.TXT" || true
fi
[ -f "$out/SND.BIN" ] && python3 ../apps/jrai/render.py "$out/SND.BIN" "$out/song.wav" >/dev/null 2>&1 || true
[ $same = 1 ] && tail -1 "$out/LOG.TXT" 2>/dev/null | grep -q "TMUSIC done: 0 failures"
