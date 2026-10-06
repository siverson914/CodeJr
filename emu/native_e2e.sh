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
port=${E2E_PORT:-4872}
fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }

(cd server && go build -o codejr ./cmd/codejr) || exit 1
client/build.sh >/dev/null || exit 1

run() { # $1 = label, $2 = extra client options
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/PROGS/"
	cp client/CODEJR.EXE "$tmp/ws/BIG.BIN" # a real binary, about 25 KB
	mkdir -p "$tmp/ws/proj"; printf 'IN A FOLDER\n' > "$tmp/ws/proj/notes.txt" # a project folder
	echo old > "$tmp/c/CODEJR/PROGS/BIG.BIN" # the pull must replace it
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR.CFG" # where /setup leaves it
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 1000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 %s "/T:make hello" /X:110 /S:SCREEN.TXT\nexit\n' $port "$tmp" "$2" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	p="$tmp/c/CODEJR/PROGS"
	echo "== $1"
	check "server saw the native client" "grep -q 'started (native client)' '$tmp/server.log'"
	check "the 25 KB binary arrived byte for byte" "cmp -s '$tmp/ws/BIG.BIN' '$p/BIG.BIN'"
	check "HELLO.BAS arrived with DOS line endings" "printf '10 PRINT \"HI\"\r\n20 END\r\n' | cmp -s - '$p/HELLO.BAS'"
	check "server recorded both transfers" "[ \$(grep -cE 'native: sent (BIG.BIN|HELLO.BAS)' '$tmp/server.log') = 2 ]"
	check "the launchers arrived without an address" "grep -q 'CODEJR.EXE /P:1 /L:1 %1' '$p/CODEJR.BT' && ! grep -q '/D:' '$p/CODEJR.BT' '$p/SETUP.BAT'"
	check "screen shows the typed line" "grep -q '> make hello' '$p/SCREEN.TXT'"
	check "a project folder arrived" "printf 'IN A FOLDER\r\n' | cmp -s - '$p/PROJ/NOTES.TXT'"
	check "no temporary file left behind" "[ ! -e '$p/CJPULL.\$\$\$' ]"
	check "client hung up cleanly" "grep -q 'client hung up' '$tmp/server.log'"
	check "screen shows the reply" "grep -q 'you asked: make hello' '$p/SCREEN.TXT'"
	check "screen shows the transfer report" "grep -q 'Saved BIG.BIN (.* KB) in .* line' '$p/SCREEN.TXT'"
	check "screen shows where the time went" "grep -q 's waiting, .* receiving, .* unpacking' '$p/SCREEN.TXT'"
	grep -E 'client says|native: sent' "$tmp/server.log" | sed 's/^[^ ]* [^ ]* /      /'
	if [ $fail = 1 ]; then
		echo "--- server log"; cat "$tmp/server.log"
		echo "--- screen"; cat "$p/SCREEN.TXT" 2>/dev/null
	fi
	rm -rf "$tmp"
}

# /run: the client lists the programs here, writes RUNNEXT.BAT for the
# one picked and hangs up, for C:\CODEJR.BAT to run it.
runrun() { # $1 = line typed, $2 = what RUNNEXT.BAT must run, $3 = label
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/PROGS/"
	printf '@ECHO OFF\r\n' > "$tmp/c/CODEJR/PROGS/ALPHA.BAT"
	printf '10 SYSTEM\r\n' > "$tmp/c/CODEJR/PROGS/BETA.BAS"
	mkdir -p "$tmp/c/CODEJR/PROGS/GAME"
	printf '@ECHO OFF\r\n' > "$tmp/c/CODEJR/PROGS/GAME/PLAY.BAT"
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR.CFG"
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 1000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:9600 /L:1 "/T:%s" /X:60 /S:SCREEN.TXT\nexit\n' $port "$tmp" "$1" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	echo "== $3"
	check "RUNNEXT.BAT runs it" "grep -qF '$2' '$tmp/c/CODEJR/RUNNEXT.BAT'"
	check "client hung up for it" "grep -q 'client hung up' '$tmp/server.log'"
	check "screen says it is running" "grep -q 'Running ' '$tmp/c/CODEJR/PROGS/SCREEN.TXT'"
	[ $fail = 1 ] && { echo "--- screen"; cat "$tmp/c/CODEJR/PROGS/SCREEN.TXT"; cat "$tmp/c/CODEJR/RUNNEXT.BAT"; }
	rm -rf "$tmp"
}

# A /run's results reach CodeJr on the next call: the program's screen,
# its LOG.TXT and a failing exit status end up in Claude's next prompt.
runreport() {
	tmp=$(mktemp -d)
	g="$tmp/c/CODEJR/PROGS/GAME"
	mkdir -p "$g" "$tmp/ws"
	cp client/CODEJR.EXE client/SCRDUMP.EXE "$tmp/c/CODEJR/PROGS/"
	printf '#include <stdio.h>\nint main(void)\n{\n\tFILE *f = fopen("LOG.TXT", "w");\n\tprintf("GAME OVER\\n");\n\tif (f) { fprintf(f, "score 42\\n"); fclose(f); }\n\treturn 3;\n}\n' > "$tmp/game.c"
	(. client/owenv.sh && cd "$tmp" && wcl -q -bt=dos -ms -0 -fe=c/CODEJR/PROGS/GAME/GAME.EXE game.c >/dev/null)
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR.CFG"
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	# First call: /run, then what C:\CODEJR.BAT does next. Second call: talk.
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 1000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:9600 /L:1 "/T:/run game" /X:60\nCOMMAND /C C:\\CODEJR\\RUNNEXT.BAT\nexit\n' $port "$tmp" > "$tmp/dosbox1.conf"
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 1000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:9600 /L:1 "/T:how did it go" /X:60 /S:SCREEN.TXT\nexit\n' $port "$tmp" > "$tmp/dosbox2.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox1.conf" >/dev/null 2>&1
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox2.conf" >/dev/null 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	echo "== /run results reach CodeJr"
	check "server got the run report" "grep -q 'run report for GAME.GAME.EXE' '$tmp/server.log'"
	check "the next prompt has the program's screen" "grep -q 'GAME OVER' '$tmp/ws/.lastprompt'"
	check "the next prompt has its LOG.TXT" "grep -q 'score 42' '$tmp/ws/.lastprompt'"
	check "the next prompt says it failed" "grep -q 'errorlevel 1 or more' '$tmp/ws/.lastprompt'"
	check "the PCjr cleared its report files" "[ ! -e '$tmp/c/CODEJR/LASTRUN.TXT' ] && [ ! -e '$tmp/c/CODEJR/LASTREP.TXT' ]"
	[ $fail = 1 ] && { echo "--- server log"; cat "$tmp/server.log"; echo "--- prompt"; cat "$tmp/ws/.lastprompt"; ls -R "$tmp/c/CODEJR"; }
	rm -rf "$tmp"
}

# Changes only: the server sends an edit script for the PCjr's copy, and
# the whole file when that copy isn't the one it expects.
deltarun() {
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/PROGS/"
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR.CFG"
	cp client/CJTERM.EXE "$tmp/ws/BIG.BIN"
	call() { # one call that types a line, so the sync runs
		server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" >> "$tmp/server.log" 2>&1 &
		sp=$!
		sleep 0.5
		printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 1000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:37287 /X:25 /S:SCREEN.TXT\nexit\n' $port "$tmp" > "$tmp/dosbox.conf"
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1
		kill $sp 2>/dev/null; wait $sp 2>/dev/null
	}
	call # the first copy goes whole
	# Change it the way a rebuild does: insert bytes, so the rest shifts.
	{ head -c 3000 client/CJTERM.EXE; printf 'NEW CODE HERE'; tail -c +3001 client/CJTERM.EXE; } > "$tmp/ws/BIG.BIN"
	call
	echo "== changes only"
	check "the second copy went as changes" "grep -q 'BIG.BIN: sending changes' '$tmp/server.log'"
	check "the PCjr rebuilt it byte for byte" "cmp -s '$tmp/ws/BIG.BIN' '$tmp/c/CODEJR/PROGS/BIG.BIN'"
	check "screen says changes" "grep -q 'bytes of changes' '$tmp/c/CODEJR/PROGS/SCREEN.TXT'"
	# Spoil the PCjr's copy: the script must be refused, then it goes whole.
	printf 'X' | dd of="$tmp/c/CODEJR/PROGS/BIG.BIN" bs=1 seek=100 conv=notrunc 2>/dev/null
	printf 'MORE' >> "$tmp/ws/BIG.BIN"
	call
	check "a different copy is refused, then sent whole" "grep -q 'changes refused' '$tmp/server.log'"
	check "and arrives byte for byte" "cmp -s '$tmp/ws/BIG.BIN' '$tmp/c/CODEJR/PROGS/BIG.BIN'"
	[ $fail = 1 ] && { echo "--- server log"; cat "$tmp/server.log"; echo "--- screen"; cat "$tmp/c/CODEJR/PROGS/SCREEN.TXT"; }
	rm -rf "$tmp"
}

# A reply longer than the screen stops at -- More -- with the rest
# waiting in memory; /exit says it is hanging up at once.
pagerun() { # $1 = typed line, $2 = label, then grep patterns that must match SCREEN.TXT
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/PROGS/"
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR.CFG"
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 1000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:37287 "/T:%s" /X:30 /S:SCREEN.TXT\nexit\n' $port "$tmp" "$1" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	echo "== $2"
	label=$2
	shift 2
	for pat in "$@"; do
		case $pat in
		!*) check "screen lacks: ${pat#!}" "! grep -q -- '${pat#!}' '$tmp/c/CODEJR/PROGS/SCREEN.TXT'" ;;
		*) check "screen: $pat" "grep -q -- '$pat' '$tmp/c/CODEJR/PROGS/SCREEN.TXT'" ;;
		esac
	done
	[ $fail = 1 ] && { echo "--- screen"; cat "$tmp/c/CODEJR/PROGS/SCREEN.TXT"; }
	rm -rf "$tmp"
}

# /voice: the fixed "voice on" clip plays at once, and with voice on a
# reply is turned into VOICE\REPLY.SND and played with the real SAY.EXE
# (SAY /X leaves SAYDUMP.BIN behind as proof it ran).
voicerun() { # $1 = typed line, $2 = label, $3 = 1 to start with voice on, $4 = the clip that must play
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws/.codejr"
	cp client/CODEJR.EXE speech/SAY.EXE "$tmp/c/CODEJR/PROGS/"
	[ "$3" = 1 ] && touch "$tmp/ws/.codejr/voice"
	printf "DIAL=x:1\r\nSAY=/H /X\r\n" > "$tmp/c/CODEJR.CFG"
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "$PWD/server/internal/session/testdata/fakemksnd.sh" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 1.5 # the fixed clips are made in the background
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 3000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:37287 "/T:%s" /X:45 /S:SCREEN.TXT\nexit\n' $port "$tmp" "$1" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	echo "== $2"
	check "the clip reached the PCjr" "[ -s '$tmp/c/CODEJR/PROGS/VOICE/$4' ]"
	check "SAY.EXE played it" "[ -s '$tmp/c/CODEJR/PROGS/SAYDUMP.BIN' ]"
	check "the screen survived SAY" "grep -q 'CodeJr 1' '$tmp/c/CODEJR/PROGS/SCREEN.TXT'"
	[ $fail = 1 ] && { echo "--- server log"; cat "$tmp/server.log"; echo "--- screen"; cat "$tmp/c/CODEJR/PROGS/SCREEN.TXT"; ls -R "$tmp/c/CODEJR"; }
	rm -rf "$tmp"
}

voicerun /voice "/voice plays its clip" 0 ON.SND
voicerun "hi there" "a voiced reply is spoken" 1 REPLY.SND
pagerun long "a long reply stops at More" "line 20" "!line 21"
pagerun /exit "/exit answers at once" "Hanging up"
deltarun
runreport
runrun "/run 2" 'GWBASIC BETA.BAS' "/run by number: BASIC through GW-BASIC"
runrun "/run game" 'CD \CODEJR\PROGS\GAME' "/run by folder name"
run "9600 baud" "/F:9600"
run "fast: /F:37287" "/F:37287"
exit $fail
