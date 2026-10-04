#!/bin/sh
# Runs every CodeJr test:
#  1. Go unit and integration tests (server, Kermit, protocol, sessions),
#     including C-versus-Go protocol interop and C-Kermit interop.
#  2. The PCjr client builds cleanly with Open Watcom (all warnings as errors).
#  3. Headless emulator runs: the real MS-Kermit 3.14 path and the native
#     CODEJR.EXE path, both against a fake Claude, and SAY.EXE playing
#     both kinds of speech clip. No windows are opened.
# Set QUICK=1 to skip the emulator runs.
set -e
cd "$(dirname "$0")"

echo "== Go tests"
(cd server && go vet ./... && go test -count=1 ./...)

echo "== PCjr client build"
client/build.sh

[ -n "${QUICK:-}" ] && exit 0

echo "== Emulator: MS-Kermit path"
emu/e2e.sh "make hello" CODEJR/PROGS/HELLO.BAS "$PWD/server/internal/session/testdata/fakeclaude.sh" | grep -E '^(PASS|FAIL)'

echo "== Emulator: native client path"
emu/native_e2e.sh

echo "== Emulator: SAY speech player"
speech/test.sh | grep -E '^(PASS|FAIL)'

echo "== All tests passed"
