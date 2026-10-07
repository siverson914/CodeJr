#!/bin/sh
# Builds JRDOOM.EXE (Open Watcom, 8088, large model, warnings are errors).
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
python3 gentab.py
python3 mkart.py
wasm -q -0 -ml -fo=fastcol.o fastcol.asm
wcl -q -bt=dos -ml -0 -ox -wx -we -fe=JRDOOM.EXE main.c render.c game.c level.c artdata.c tables.c fastcol.o ../../lib/jrgfx.lib
rm -f *.o
ls -l JRDOOM.EXE
