#!/bin/sh
# Builds lib/JRGFX.LIB (large model, 8088) and the test/demo programs.
# The library is built as jrgfx_n.lib and renamed to jrgfx.lib only after
# the tests and the demo linked against it, so games linking jrgfx.lib
# never see a half-written file (rename is atomic).
set -e
cd "$(dirname "$0")"
. ../client/owenv.sh
CF="-q -bt=dos -ml -0 -ox -wx"
wcl $CF -c jrgfx.c jrtab.c jrfast.c
rm -f jrgfx_n.lib
wlib -q -n -b jrgfx_n.lib +jrgfx.o +jrtab.o +jrfast.o
for t in test/*.c; do
	n=$(basename "$t" .c | tr a-z A-Z)
	(cd test && wcl $CF -fe=$n.EXE $(basename "$t") ../jrgfx_n.lib)
done
wcl $CF -fe=DEMO.EXE demo.c fastdemo.c jrgfx_n.lib
# gate: install the library only if T1 and T3 pass headless
t=$(mktemp -d)
EMU_TIMEOUT=90 ./emu.sh "$t/1" 3000 test/T1.EXE 8 >/dev/null || true
EMU_TIMEOUT=90 ./emu.sh "$t/3" 3000 test/T3.EXE 0 >/dev/null || true; grep -E "FAIL|restore|poly_rows|foreign|fx_split|T3 done" "$t/3/LOG.TXT" >&2 || true
if grep -q "vram mismatches 0" "$t/1/LOG.TXT" 2>/dev/null && grep -q "T3 done: 0 failures" "$t/3/LOG.TXT" 2>/dev/null; then
	mv -f jrgfx_n.lib jrgfx.lib
	echo "jrgfx.lib installed (T1, T3 pass)"
else
	echo "TESTS FAILED: jrgfx.lib NOT replaced; logs in $t" >&2
	rm -f *.o test/*.o
	exit 1
fi
rm -rf "$t"
rm -f *.o test/*.o
true
