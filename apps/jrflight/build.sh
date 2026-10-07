#!/bin/sh
# Builds JRFLIGHT.EXE (large model, 8088 only, warnings are errors).
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
LIB=../../lib
CF="-q -bt=dos -ml -0 -ox -wx -i=$LIB"
SRC="flight.c phys.c render3d.c raster.c world.c panel.c"
if [ "$1" = "src" ] || [ ! -f $LIB/jrgfx.lib ]; then
	# lib sources directly (if the prebuilt library is missing or stale)
	wcl $CF -fe=JRFLIGHT.EXE $SRC $LIB/jrgfx.c $LIB/jrtab.c
else
	wcl $CF -fe=JRFLIGHT.EXE $SRC $LIB/jrgfx.lib
fi
rm -f *.o *.err
ls -l JRFLIGHT.EXE
