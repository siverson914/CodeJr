#!/bin/sh
# Builds SOP26.EXE: Sopwith26, from the same source as apps/sopwith with
# SOPWITH26 defined (see README.md here for what that changes).
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
src=../sopwith
wasm -q -0 -ms -we -fo=sopsys_a.o $src/sopsys.asm
wasm -q -0 -ms -we -fo=sopgfx.o $src/sopgfx.asm
wcl -q -bt=dos -ms -0 -ox -w3 -dSOPWITH26 -i=$src -fe=SOP26.EXE $src/sopwith2.c $src/sopsys.c sopsys_a.o sopgfx.o
rm -f *.o *.err
ls -l SOP26.EXE
