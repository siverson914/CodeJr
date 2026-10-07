#!/bin/sh
# Builds JRCUBE.EXE (large model, 8088 only, warnings are errors).
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
LIB=../../lib
wcl -q -bt=dos -ml -0 -ox -wx -we -i=$LIB -fe=JRCUBE.EXE cube.c music.c shapes.c $LIB/jrgfx.lib
rm -f *.o *.err
ls -l JRCUBE.EXE
