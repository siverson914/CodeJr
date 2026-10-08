#!/bin/sh
# Builds SOPWITH2.EXE (Open Watcom, 8088 only, small model as in the
# original). The original's own warnings (unused parameters and the like)
# are left alone, so this one build doesn't use -wx -we.
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
wasm -q -0 -ms -we -fo=sopsys_a.o sopsys.asm
wasm -q -0 -ms -we -fo=sopgfx.o sopgfx.asm
wcl -q -bt=dos -ms -0 -ox -w3 -fe=SOPWITH2.EXE sopwith2.c sopsys.c sopsys_a.o sopgfx.o
rm -f *.o *.err
ls -l SOPWITH2.EXE
