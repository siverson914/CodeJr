#!/bin/sh
# Builds the PCjr programs (Open Watcom, 8088, small model, warnings are
# errors): CODEJR.EXE, the client, and CJTERM.EXE, the serial test terminal.
set -e
cd "$(dirname "$0")"
. ./owenv.sh
wasm -q -0 -ms -fo=serisr.o serisr.asm
wcl -q -bt=dos -ms -0 -os -wx -fe=CODEJR.EXE codejr.c cjp.c pull.c lz.c screen.c serial.c serisr.o
wcl -q -bt=dos -ms -0 -os -wx -fe=CJTERM.EXE cjterm.c serial.c serisr.o
ls -l CODEJR.EXE CJTERM.EXE
