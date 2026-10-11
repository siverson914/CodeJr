#!/bin/sh
# Builds CHROMEJR.EXE, ChromeJr's PCjr half (Open Watcom, 8088, small
# model, warnings are errors), with CodeJr's serial port and link code.
set -e
cd "$(dirname "$0")"
. ../../client/owenv.sh
C=../../client
wasm -q -0 -ms -fo=serisr.o $C/serisr.asm
wcl -q -bt=dos -ms -0 -os -wx -i=$C -fe=CHROMEJR.EXE chromejr.c $C/cjp.c $C/serial.c serisr.o
rm -f *.o
ls -l CHROMEJR.EXE
