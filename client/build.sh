#!/bin/sh
# Builds the PCjr programs (Open Watcom, 8088, small model, warnings are
# errors): CODEJR.EXE, the client, CJTERM.EXE, the serial test terminal,
# and SCRDUMP.EXE.
set -e
cd "$(dirname "$0")"
. ./owenv.sh
wasm -q -0 -ms -fo=serisr.o serisr.asm
wcl -q -bt=dos -ms -0 -os -wx -fe=CODEJR.EXE codejr.c cjp.c pull.c lz.c screen.c serial.c serisr.o
wcl -q -bt=dos -ms -0 -os -wx -fe=CJTERM.EXE cjterm.c serial.c serisr.o
# SCRDUMP: the server's run tool uses it to capture the text screen.
wcl -q -bt=dos -ms -0 -os -wx -fe=SCRDUMP.EXE scrdump.c
rm -f scrdump.o
ls -l CODEJR.EXE CJTERM.EXE SCRDUMP.EXE
