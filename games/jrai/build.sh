#!/bin/sh
# Builds JRAI.EXE (Open Watcom, 8088, large model, warnings are errors).
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
LIB=../../lib
wasm -q -0 -ml -we -fo=player.o ../../speech/player.asm   # SAY's player, built in (voice.c)
SRC="main.c music.c draw.c boot.c plasma.c fire.c tunnel.c meet.c scroll.c voice.c"
wcl -q -bt=dos -ml -0 -ox -wx -we -i=$LIB -i=../../speech -fe=JRAI.EXE $SRC player.o $LIB/jrgfx.lib
rm -f *.o *.err
ls -l JRAI.EXE
