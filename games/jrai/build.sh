#!/bin/sh
# Builds JRAI.EXE (Open Watcom, 8088, large model, warnings are errors).
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
LIB=../../lib
SRC="main.c music.c draw.c boot.c plasma.c fire.c tunnel.c meet.c scroll.c voice.c"
wcl -q -bt=dos -ml -0 -ox -wx -we -i=$LIB -fe=JRAI.EXE $SRC $LIB/jrgfx.lib
rm -f *.o *.err
ls -l JRAI.EXE
