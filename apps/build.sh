#!/bin/sh
# Builds a project the way CodeJr's build tool does (server/internal/pcjr):
# every .c and .asm in apps/NAME into NAME.EXE, 8088, large model, with the
# JRGFX library's sources when a file includes jrgfx.h.
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
if [ -n "$src" ] && grep -qi 'jrgfx\.h' $src; then
	wcl -q -bt=dos -ml -0 -ox -w4 -i=$LIB -fe=$exe $src $LIB/jrgfx.c $LIB/jrtab.c $LIB/jrfast.c $objs
else
	wcl -q -bt=dos -ml -0 -ox -w4 -fe=$exe $src $objs
fi
rm -f *.o *.err
ls -l $exe
