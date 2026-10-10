#!/bin/sh
# Builds lib/JRGFX.LIB (large model, 8088) and the test/demo programs.
# The library is built as jrgfx_n.lib and renamed to jrgfx.lib only after
# the tests and the demo linked against it, so games linking jrgfx.lib
# never see a half-written file (rename is atomic).
set -e
cd "$(dirname "$0")"
. ../client/owenv.sh
CF="-q -bt=dos -ml -0 -ox -wx"
# Every jr*.c and jr*.asm here is a module of the library (and shared/*
# too: additions CodeJr made from the PCjr, saved by apps/save-projects.sh).
mods=""
for f in jr*.c shared/*.c; do
	[ -f "$f" ] || continue
	o=$(basename "${f%.c}").o
	wcl $CF -c -i=. -fo=$o "$f"
	mods="$mods +$o"
done
for f in jr*.asm shared/*.asm; do
	[ -f "$f" ] || continue
	o=$(basename "${f%.asm}").o
	wasm -q -0 -ml -fo=$o "$f"
	mods="$mods +$o"
done
rm -f jrgfx_n.lib
wlib -q -n -b jrgfx_n.lib $mods
for t in test/*.c; do
	n=$(basename "$t" .c | tr a-z A-Z)
	(cd test && wcl $CF -fe=$n.EXE $(basename "$t") ../jrgfx_n.lib)
done
wcl $CF -fe=DEMO.EXE demo.c fastdemo.c jrgfx_n.lib
# gate: install the library only if T1, T3, TKEYS, TJT and TMUSIC pass headless
t=$(mktemp -d)
EMU_TIMEOUT=90 ./emu.sh "$t/1" 3000 test/T1.EXE 8 >/dev/null || true
EMU_TIMEOUT=60 ./emu.sh "$t/k" 3000 test/TKEYS.EXE >/dev/null || true
EMU_TIMEOUT=90 ./emu.sh "$t/j" 3000 test/TJT.EXE /TEST:2 >/dev/null || true
EMU_TIMEOUT=180 ./emu.sh "$t/m" 3000 test/TMUSIC.EXE >/dev/null || true
EMU_TIMEOUT=90 ./emu.sh "$t/3" 3000 test/T3.EXE 0 >/dev/null || true; grep -E "FAIL|restore|poly_rows|foreign|fx_split|T3 done" "$t/3/LOG.TXT" >&2 || true
grep -E "FAIL" "$t/k/LOG.TXT" "$t/j/LOG.TXT" "$t/m/LOG.TXT" >&2 || true
if grep -q "vram mismatches 0" "$t/1/LOG.TXT" 2>/dev/null && grep -q "T3 done: 0 failures" "$t/3/LOG.TXT" 2>/dev/null && grep -q "TKEYS done: 0 failures" "$t/k/LOG.TXT" 2>/dev/null \
	&& grep -q "JT done: 0 failures" "$t/j/LOG.TXT" 2>/dev/null && grep -q "TMUSIC done: 0 failures" "$t/m/LOG.TXT" 2>/dev/null; then
	mv -f jrgfx_n.lib jrgfx.lib
	echo "jrgfx.lib installed (T1, T3, TKEYS, TJT, TMUSIC pass)"
else
	echo "TESTS FAILED: jrgfx.lib NOT replaced; logs in $t" >&2
	rm -f *.o test/*.o
	exit 1
fi
rm -rf "$t"
rm -f *.o test/*.o
true
