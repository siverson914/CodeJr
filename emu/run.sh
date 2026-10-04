#!/bin/sh
# Starts an emulated PCjr (DOSBox machine=pcjr) that dials the CodeJr server
# on 127.0.0.1:${PORT:-4860}. Start the server first.
# HEADLESS=1 runs with no window at all (for automated tests).
cd "$(dirname "$0")"
[ -f c/MSK314/KERMIT.EXE ] || ./setup.sh
sed "s/@PORT@/${PORT:-4860}/; s/@AUTOEXEC@/${AUTOEXEC:-CODEJR}/" dosbox.conf.in > dosbox.conf
if [ -n "$HEADLESS" ]; then
	export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
fi
exec dosbox -conf dosbox.conf "$@"
