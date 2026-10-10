#!/bin/sh
# Builds a project the way CodeJr's build tool does (server/internal/pcjr):
# every .c and .asm in apps/NAME into NAME.EXE, 8088, large model, with the
# JRGFX library (lib/jr*, lib/shared) when a file includes one of its headers.
#   apps/build.sh lemmjr
set -e
cd "$(dirname "$0")/$1"
. ../../client/owenv.sh
LIB=../../lib
exe=$(echo "$1" | tr a-z A-Z).EXE
objs=""
for a in *.asm; do
	[ -f "$a" ] || continue
	wasm -q -0 -ml -fo="${a%.asm}.o" "$a"
	objs="$objs ${a%.asm}.o"
done
src=$(ls *.c 2>/dev/null || true)
# The library's headers: a program that includes one links the library,
# built here from its sources as a .lib (only the parts used go in).
heads=$(cd $LIB && ls jr*.h shared/*.h 2>/dev/null | sed 's|.*/||' | sed 's/\./\\./g' | paste -sd'|')
if [ -n "$src" ] && grep -qiE "include *\"($heads)\"" $src; then
	t=$(mktemp -d)
	mods=""
	for f in $LIB/jr*.c $LIB/shared/*.c; do
		[ -f "$f" ] || continue
		o=$t/$(basename "${f%.c}").o
		wcl -q -bt=dos -ml -0 -ox -w4 -c -i=$LIB -i=$LIB/shared -fo=$o "$f"
		mods="$mods +$o"
	done
	for f in $LIB/jr*.asm $LIB/shared/*.asm; do
		[ -f "$f" ] || continue
		o=$t/$(basename "${f%.asm}").o
		wasm -q -0 -ml -fo=$o "$f"
		mods="$mods +$o"
	done
	wlib -q -n -b $t/jr.lib $mods
	wcl -q -bt=dos -ml -0 -ox -w4 -i=$LIB -i=$LIB/shared -fe=$exe $src $objs $t/jr.lib
	rm -rf "$t"
else
	wcl -q -bt=dos -ml -0 -ox -w4 -fe=$exe $src $objs
fi
rm -f *.o *.err
ls -l $exe
