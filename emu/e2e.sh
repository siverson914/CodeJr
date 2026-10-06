#!/bin/sh
# Headless end-to-end test: an emulated PCjr runs the real MS-Kermit 3.14,
# dials a codejr server, types a line, and we wait for a file to land on
# its drive C. No window is opened.
#   emu/e2e.sh "TYPED LINE" EXPECTED_FILE [claude-binary]
# EXPECTED_FILE is relative to emu/c (e.g. CODEJR/PROGS/SNAKE.BAS). With
# WAIT_LOG=pattern, wait for that in the server log instead (and keep the file).
set -u
cd "$(dirname "$0")"
line=$1 want=$2 claudebin=${3:-claude}
port=4870
tmp=$(mktemp -d)
trap 'kill $dp $sp 2>/dev/null; wait 2>/dev/null; rm -rf "$tmp"' EXIT

[ -f c/MSK314/KERMIT.EXE ] || ./setup.sh >/dev/null
(cd ../server && go build -o codejr ./cmd/codejr) || exit 1
[ -n "${WAIT_LOG:-}" ] || rm -f "c/$want"

# Kermit script for a fresh PCjr: plain settings, APC on, dial, type the line.
printf 'set port com1\r\nset speed 9600\r\nset local-echo off\r\nset terminal apc on\r\nset file type binary\r\nset file collision overwrite\r\noutput ATDT127.0.0.1:%s\\13\r\ninput 30 CONNECT\r\npause 1\r\noutput \\13\r\npause 3\r\noutput %s\\13\r\nconnect\r\n' "$port" "$line" > c/AUTO.INI
printf '@ECHO OFF\r\nIF NOT EXIST C:\\CODEJR\\NUL MD C:\\CODEJR\r\nIF NOT EXIST C:\\CODEJR\\PROGS\\NUL MD C:\\CODEJR\\PROGS\r\nCD \\CODEJR\\PROGS\r\nC:\\MSK314\\KERMIT TAKE C:\\AUTO.INI\r\nECHO done > C:\\EXITED.TXT\r\n' > c/AUTO.BAT
rm -f c/EXITED.TXT

mkdir -p "$tmp/ws"
../server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" -trace "$tmp/trace.bin" > "$tmp/server.log" 2>&1 &
sp=$!
sleep 0.5
HEADLESS=1 PORT=$port AUTOEXEC=AUTO ./run.sh > "$tmp/dosbox.log" 2>&1 &
dp=$!

ok=1
for i in $(seq 1 300); do
	if [ "$want" = EXITED.TXT ]; then
		[ -f c/EXITED.TXT ] && { ok=0; break; }
	elif [ -n "${WAIT_LOG:-}" ]; then
		grep -qE "$WAIT_LOG" "$tmp/server.log" && { ok=0; break; }
	else
		[ -f "c/$want" ] && grep -qE 'sent|setup done' "$tmp/server.log" && { ok=0; break; }
	fi
	grep -q 'failed' "$tmp/server.log" && break
	sleep 1
done
sleep 2
cat "$tmp/server.log"
if command -v python3 >/dev/null && python3 -c 'import pyte' 2>/dev/null; then
	python3 render.py "$tmp/trace.bin"
elif [ -x "${PYTE_PY:-}" ]; then
	"$PYTE_PY" render.py "$tmp/trace.bin"
fi
rm -f c/AUTO.INI c/AUTO.BAT
if [ $ok = 0 ]; then echo "PASS: $want arrived after ${i}s"; else echo "FAIL: $want did not arrive"; fi
exit $ok
