#!/bin/sh
# Builds the PCjr test tools (they share the client's serial code):
# CJTERM.EXE, a serial test terminal, and UARTDUMP.EXE, which prints the
# serial chip's registers. HELLO.EXE is the smallest possible build check.
set -e
cd "$(dirname "$0")"
. ../client/owenv.sh
C=../client
wasm -q -0 -ms -fo=serisr.o $C/serisr.asm
wcl -q -bt=dos -ms -0 -os -wx -i=$C -fe=CJTERM.EXE cjterm.c $C/serial.c serisr.o
wcl -q -bt=dos -ms -0 -os -wx -i=$C -fe=UARTDUMP.EXE uartdump.c
wcl -q -bt=dos -ms -0 -os -wx -fe=HELLO.EXE hello.c
rm -f *.o *.err
ls -l CJTERM.EXE UARTDUMP.EXE HELLO.EXE
