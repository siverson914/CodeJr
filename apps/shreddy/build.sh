#!/bin/sh
# Builds SHREDDY.EXE the way CodeJr's build tool does (apps/build.sh: large
# model, 8088 only, JRGFX compiled from lib/, lines.asm assembled), after a
# compile with warnings as errors.
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
wcl -q -bt=dos -ml -0 -ox -wx -we -zs -i=../../lib shreddy.c
cd .. && sh build.sh shreddy >/dev/null
cd shreddy && rm -f *.o *.err
ls -l SHREDDY.EXE
