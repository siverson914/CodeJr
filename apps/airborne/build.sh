#!/bin/sh
# Builds AIRBORNE.EXE the way CodeJr's build tool does (apps/build.sh: large
# model, 8088 only, JRGFX from lib/), after a compile with warnings as
# errors. AIRBORNE.DAT comes from tools/mkdat.py (the original disk's data,
# kept out of the repo). game.c and game.h, the original's code translated
# into C, are kept out of the repo too: they live in PORT on this laptop.
set -e
cd "$(dirname "$0")"
PORT=${AIRBORNE_PORT:-$HOME/.local/share/codejr/airborne/port}
[ -f "$PORT/game.c" ] || { echo "no $PORT/game.c (the game core is not in the repo)"; exit 1; }
ln -sf "$PORT/game.c" game.c; ln -sf "$PORT/game.h" game.h   # links, ignored by git
. ../../client/owenv.sh
wcl -q -bt=dos -ml -0 -ox -wx -we -zs -i=../../lib airborne.c game.c
cd .. && sh build.sh airborne >/dev/null
cd airborne && rm -f *.o *.err
ls -l AIRBORNE.EXE
