#!/bin/sh
# Headless end-to-end test of the native client: the emulated PCjr runs
# CODEJR.EXE, which dials a test server, pulls a binary file waiting in the
# workspace, types a line, pulls the file Claude writes, and hangs up. Run
# once at 9600 baud and once switching to the fast speed. A fake Claude is
# used unless a claude binary is given. No window is opened.
#   emu/native_e2e.sh [claude-binary]
set -u
cd "$(dirname "$0")/.."
claudebin=${1:-$PWD/server/internal/session/testdata/fakeclaude.sh}
port=4872
fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }

(cd server && go build -o codejr ./cmd/codejr) || exit 1
client/build.sh >/dev/null || exit 1

run() { # $1 = label, $2 = extra client options
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/PROGS/"
	cp client/CODEJR.EXE "$tmp/ws/BIG.BIN" # a real binary, about 25 KB
	echo old > "$tmp/c/CODEJR/PROGS/BIG.BIN" # the pull must replace it
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 1000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /D:x:1 %s "/T:make hello" /X:60 /S:SCREEN.TXT\nexit\n' $port "$tmp" "$2" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	p="$tmp/c/CODEJR/PROGS"
	echo "== $1"
	check "server saw the native client" "grep -q 'started (native client)' '$tmp/server.log'"
	check "the 25 KB binary arrived byte for byte" "cmp -s '$tmp/ws/BIG.BIN' '$p/BIG.BIN'"
	check "HELLO.BAS arrived with DOS line endings" "printf '10 PRINT \"HI\"\r\n20 END\r\n' | cmp -s - '$p/HELLO.BAS'"
	check "server recorded both transfers" "[ \$(grep -cE 'native: sent (BIG.BIN|HELLO.BAS)' '$tmp/server.log') = 2 ]"
	check "the launchers arrived without an address" "grep -q 'CODEJR /P:1 %1' '$p/NATIVE.BAT' && ! grep -q '/D:' '$p/FAST.BAT'"
	check "no temporary file left behind" "[ ! -e '$p/CJPULL.\$\$\$' ]"
	check "client hung up cleanly" "grep -q 'client hung up' '$tmp/server.log'"
	check "screen shows the reply" "grep -q 'you asked: make hello' '$p/SCREEN.TXT'"
	check "screen shows the transfer report" "grep -q 'BIG.BIN: .* bytes in' '$p/SCREEN.TXT'"
	grep -E 'client says|native: sent' "$tmp/server.log" | sed 's/^[^ ]* [^ ]* /      /'
	if [ $fail = 1 ]; then
		echo "--- server log"; cat "$tmp/server.log"
		echo "--- screen"; cat "$p/SCREEN.TXT" 2>/dev/null
	fi
	rm -rf "$tmp"
}

run "9600 baud" ""
run "fast: /F:37287" "/F:37287"
exit $fail
