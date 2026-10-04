#!/bin/sh
# Builds SAY.EXE (Open Watcom, 8088, large model, warnings are errors).
set -e
cd "$(dirname "$0")"
. ../client/owenv.sh
wasm -q -0 -ml -we player.asm
wcl -q -bt=dos -ml -0 -ox -wx -we -fe=SAY.EXE say.c player.o
rm -f *.o *.err
ls -l SAY.EXE
