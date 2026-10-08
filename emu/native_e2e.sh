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

# The scenarios are independent (own folder, own port), and each mostly
# waits on an emulated PCjr, so they run several at a time: one after
# another they took over 20 minutes. job runs one in the background with
# its output kept apart; leading NAME=value words are set for it alone.
jobs=$(mktemp -d)
nj=0
job() {
	nj=$((nj + 1))
	(
		port=$((port + nj))
		while :; do
			case $1 in [A-Z]*=*) export "$1"; shift ;; *) break ;; esac
		done
		"$@"
	) > "$jobs/$(printf %02d $nj).txt" 2>&1 &
	[ $((nj % ${E2E_JOBS:-6})) = 0 ] && wait
	return 0
}

(cd server && go build -o codejr ./cmd/codejr) || exit 1
client/build.sh >/dev/null || exit 1
apps/say/build.sh >/dev/null || exit 1 # SAY.EXE, for the /voice runs

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
	check "CodeJr's own files arrived one level up, without an address" "grep -q 'CJEXE% /P:1 /L:1 %1' '$tmp/c/CODEJR/CJLOOP.NEW' && [ -s '$tmp/c/CODEJR/SCRDUMP.EXE' ] && ! grep -q '/D:' '$tmp/c/CODEJR/CJLOOP.NEW' '$tmp/c/CODEJR/SETUP.BAT'"
	check "none of them landed among the programs" "[ ! -e '$p/SETUP.BAT' ] && [ ! -e '$p/SCRDUMP.EXE' ]"
	check "screen shows the typed line" "grep -q '> make hello' '$p/SCREEN.TXT'"
	check "a project folder arrived" "printf 'IN A FOLDER\r\n' | cmp -s - '$p/PROJ/NOTES.TXT'"
	check "no temporary file left behind" "[ ! -e '$p/CJPULL.\$\$\$' ]"
	check "client hung up cleanly" "grep -q 'client hung up' '$tmp/server.log'"
	check "screen shows the reply" "grep -q 'you asked: make hello' '$p/SCREEN.TXT'"
	check "screen shows the transfer report" "grep -q 'Saved .* (.* KB) in .* line' '$p/SCREEN.TXT'"
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
	cp client/SCRDUMP.EXE "$tmp/ws/BIG.BIN"
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
	{ head -c 3000 client/SCRDUMP.EXE; printf 'NEW CODE HERE'; tail -c +3001 client/SCRDUMP.EXE; } > "$tmp/ws/BIG.BIN"
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
	cp client/CODEJR.EXE apps/say/SAY.EXE "$tmp/c/CODEJR/PROGS/"
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

job voicerun /voice "/voice plays its clip" 0 ON.SND
job voicerun "hi there" "a voiced reply is spoken" 1 REPLY.SND
# Remote work: /dos runs a command on the PCjr and shows what it printed,
# /send uploads a file, and the laptop can ask for both through the
# control socket while a call is up (what Claude's dos and fetch tools use).
remoterun() {
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS/GAME" "$tmp/ws"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/PROGS/"
	printf 'a note from the PCjr\r\n' > "$tmp/c/CODEJR/PROGS/NOTE.TXT"
	printf 'score 42\r\n' > "$tmp/c/CODEJR/PROGS/GAME/LOG.TXT"
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR.CFG"
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 3000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:37287 %s "/T:%s" /X:50 /S:SCREEN.TXT\nexit\n' $port "$tmp" "${COPT:-}" "$1" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1 &
	dp=$!
	# From the laptop, while the call is up.
	sleep 25
	server/codejr ctl -workspace "$tmp/ws" dos ${CTLCMD:-VER} > "$tmp/ctl-dos.txt" 2>&1
	server/codejr ctl -workspace "$tmp/ws" fetch 'GAME\LOG.TXT' > "$tmp/ctl-fetch.txt" 2>&1
	wait $dp
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	echo "== $2"
	shift 2
	for c in "$@"; do eval "$c"; done
	[ $fail = 1 ] && { echo "--- server log"; cat "$tmp/server.log"; echo "--- screen"; cat "$tmp/c/CODEJR/PROGS/SCREEN.TXT"; echo "--- ctl"; cat "$tmp/ctl-dos.txt" "$tmp/ctl-fetch.txt"; ls -R "$tmp/ws"; }
	rm -rf "$tmp"
}

job remoterun "/dos VER" "/dos, and the laptop's control socket" \
	'check "the screen shows what VER printed" "grep -qi dosbox $tmp/c/CODEJR/PROGS/SCREEN.TXT"' \
	'check "the screen was repainted" "grep -q \"CodeJr 1\" $tmp/c/CODEJR/PROGS/SCREEN.TXT"' \
	'check "the laptop ran a command on the PCjr" "grep -qi dosbox $tmp/ctl-dos.txt"' \
	'check "the laptop fetched a file from a folder" "printf \"score 42\n\" | cmp -s - $tmp/ws/GAME/LOG.TXT"'
# A command that could change something runs only if the user presses Y
# (/A:Y and /A:N answer for the user here).
job COPT=/A:N CTLCMD="DEL NOTE.TXT" remoterun "hi" "a risky command, refused at the PCjr" \
	'check "the PCjr asked" "grep -q \"CodeJr wants to run: DEL NOTE.TXT\" $tmp/c/CODEJR/PROGS/SCREEN.TXT"' \
	'check "the laptop was told no" "grep -q \"said no\" $tmp/ctl-dos.txt"' \
	'check "the file is still there" "[ -e $tmp/c/CODEJR/PROGS/NOTE.TXT ]"'
job COPT=/A:Y CTLCMD="DEL NOTE.TXT" remoterun "hi" "a risky command, allowed at the PCjr" \
	'check "the file is gone" "[ ! -e $tmp/c/CODEJR/PROGS/NOTE.TXT ]"'
job remoterun "/send note.txt" "/send uploads a file" \
	'check "the file reached the workspace" "printf \"a note from the PCjr\n\" | cmp -s - $tmp/ws/NOTE.TXT"' \
	'check "the screen says so" "grep -q \"CodeJr has NOTE.TXT now\" $tmp/c/CODEJR/PROGS/SCREEN.TXT"'

# The iteration loop from the laptop: a program built during a call is
# pushed, run in its folder on the PCjr, and its output, exit code and
# LOG.TXT come back in one step. $1 = 1 for /trust on (no question).
looprun() {
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws/.codejr"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/PROGS/"
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR.CFG"
	[ "$1" = 1 ] && touch "$tmp/ws/.codejr/trust"
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 3000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:37287 %s /X:70 /S:SCREEN.TXT\nexit\n' $port "$tmp" "$2" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1 &
	dp=$!
	sleep 25
	# "Build" a program now, as an edit-compile-run loop would.
	mkdir -p "$tmp/ws/GAME"
	printf '#include <stdio.h>\nint main(int argc, char **argv)\n{\n\tFILE *f = fopen("LOG.TXT", "w");\n\tprintf("GAME OVER %%s\\n", argc > 1 ? argv[1] : "");\n\tif (f) { fprintf(f, "score 42\\n"); fclose(f); }\n\treturn 3;\n}\n' > "$tmp/game.c"
	(. client/owenv.sh && cd "$tmp" && wcl -q -bt=dos -ms -0 -fe=ws/GAME/GAME.EXE game.c >/dev/null && rm -f ws/GAME/*.o game.o)
	server/codejr ctl -workspace "$tmp/ws" run GAME GAME.EXE /TEST > "$tmp/run.txt" 2>&1
	# A CD on the PCjr must not move where later files land.
	server/codejr ctl -workspace "$tmp/ws" dos CD GAME > /dev/null 2>&1
	printf 'later\n' > "$tmp/ws/LATER.TXT"
	server/codejr ctl -workspace "$tmp/ws" sync now > "$tmp/sync.txt" 2>&1
	wait $dp
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	echo "== $3"
	check "the new build reached its folder" "[ -s '$tmp/c/CODEJR/PROGS/GAME/GAME.EXE' ]"
	check "its output came back, with the argument" "grep -q 'GAME OVER /TEST' '$tmp/run.txt'"
	check "its exit code came back" "grep -q 'exit code 3' '$tmp/run.txt'"
	check "its LOG.TXT came back" "grep -q 'score 42' '$tmp/run.txt'"
	check "it ran in its folder" "[ -s '$tmp/c/CODEJR/PROGS/GAME/LOG.TXT' ] && [ ! -e '$tmp/c/CODEJR/PROGS/LOG.TXT' ]"
	check "a later file still lands in PROGS" "[ -s '$tmp/c/CODEJR/PROGS/LATER.TXT' ]"
	[ $fail = 1 ] && { echo "--- server log"; cat "$tmp/server.log"; echo "--- run"; cat "$tmp/run.txt" "$tmp/sync.txt"; echo "--- screen"; cat "$tmp/c/CODEJR/PROGS/SCREEN.TXT"; ls -R "$tmp/c/CODEJR"; }
	rm -rf "$tmp"
}

job looprun 1 "" "the loop from the laptop, with /trust on"
job looprun 0 "/A:Y" "the loop from the laptop, asking first"

# The real launcher: C:\CODEJR.BAT (the stub that never changes) swaps in
# a new CJLOOP.BAT and starts the client from C:\CODEJR. A newer client
# then arrives in place, and a polite server stop tells the PCjr to call
# back.
launchrun() {
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws/.system"
	server/codejr -write-dos "$tmp/dos" >/dev/null 2>&1 || { mkdir -p "$tmp/dos"; server/codejr -write-dos "$tmp/dos" >/dev/null 2>&1; }
	cp "$tmp/dos/CODEJR.BAT" "$tmp/c/CODEJR.BAT"
	cp "$tmp/dos/CJKERMIT.BAT" "$tmp/c/CJKERMIT.BAT"
	cp "$tmp/dos/CJLOOP.NEW" "$tmp/c/CODEJR/CJLOOP.NEW"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/CODEJR.EXE"
	# The server has a "newer" client: the same one with a byte appended.
	{ cat client/CODEJR.EXE; printf 'X'; } > "$tmp/ws/.system/CODEJR.EXE"
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR/CODEJR.CFG"
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 3000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\nCODEJR /F:37287 /X:45\nexit\n' $port "$tmp" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 200 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1 &
	dp=$!
	sleep 30
	kill -TERM $sp; wait $sp 2>/dev/null # a polite stop, as `just restart` does
	wait $dp
	echo "== the launcher, files one level up, and a polite restart"
	check "the stub swapped the new launcher in" "cmp -s '$tmp/dos/CJLOOP.NEW' '$tmp/c/CODEJR/CJLOOP.BAT'"
	check "the launcher started the client from C:\\CODEJR" "grep -q 'started (native client)' '$tmp/server.log'"
	check "the newer client replaced it in place" "cmp -s '$tmp/ws/.system/CODEJR.EXE' '$tmp/c/CODEJR/CODEJR.EXE'"
	check "nothing of CodeJr's is among the programs" "[ ! -e '$tmp/c/CODEJR/PROGS/CODEJR.EXE' ] && [ ! -e '$tmp/c/CODEJR/PROGS/SETUP.BAT' ]"
	check "the stopping server told the PCjr to call back" "grep -q 'told the PCjr to call back' '$tmp/server.log'"
	[ $fail = 1 ] && { echo "--- server log"; cat "$tmp/server.log"; ls -R "$tmp/c"; cat "$tmp/c/CODEJR.BAT"; }
	rm -rf "$tmp"
}

job launchrun

# Scripted keys and a screenshot: a graphics program that waits for a key
# is run from the laptop with "{3}q" and a picture taken at 2 seconds.
# (/K:B: in DOSBox a key goes into the BIOS buffer; on the real PCjr the
# client issues INT 9, which this can't test.)
keysrun() {
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c/CODEJR/PROGS" "$tmp/ws/.codejr" "$tmp/ws/GAME"
	cp client/CODEJR.EXE "$tmp/c/CODEJR/PROGS/"
	printf "DIAL=x:1\r\n" > "$tmp/c/CODEJR.CFG"
	touch "$tmp/ws/.codejr/trust"
	cat > "$tmp/game.c" <<'CEOF'
#include <stdio.h>
#include <conio.h>
#include <time.h>
#include "jrgfx.h"
int main(void)
{
	int c = '?';
	clock_t t0 = clock();
	if (gfx_init(GFX_160x200) != 0)
		return 1;
	gfx_clear(1);
	gfx_rect(10, 10, 100, 100, 14);
	gfx_present();
	while (!kbhit() && (clock() - t0) / CLOCKS_PER_SEC < 25)
		;
	if (kbhit())
		c = getch();
	gfx_done();
	printf("key=%c\n", c);
	return c == 'q' ? 7 : 1;
}
CEOF
	(. client/owenv.sh && cd "$tmp" && wcl -q -bt=dos -ml -0 -i="$OLDPWD/lib" -fe=ws/GAME/GAME.EXE game.c "$OLDPWD/lib/jrgfx.c" "$OLDPWD/lib/jrtab.c" "$OLDPWD/lib/jrfast.c" >/dev/null 2>&1; rm -f ws/GAME/*.o *.o)
	server/codejr -listen 127.0.0.1:$port -workspace "$tmp/ws" -claude "$claudebin" -speech "" > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 0.5
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 3000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\ncd \\CODEJR\\PROGS\nCODEJR /P:1 /F:37287 /K:B /X:90 /S:SCREEN.TXT\nexit\n' $port "$tmp" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 240 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1 &
	dp=$!
	sleep 45 # the initial files, then GAME.EXE goes with the run
	server/codejr ctl -workspace "$tmp/ws" -keys '{3}q' -shot 2 run GAME GAME.EXE > "$tmp/run.txt" 2>&1
	wait $dp
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	echo "== scripted keys and a screenshot"
	check "the program got the scripted key" "grep -q 'key=q' '$tmp/run.txt' && grep -q 'exit code 7' '$tmp/run.txt'"
	check "a screenshot came back" "grep -q 'screenshot: mode 8' '$tmp/run.txt' && [ -s '$tmp/ws/.codejr/shot.png' ]"
	check "it shows what the program drew" "[ \"\$(magick '$tmp/ws/.codejr/shot.png' -format '%[pixel:p{320,300}] %[pixel:p{40,20}]' info:)\" = 'srgb(0,0,170) srgb(255,255,85)' ]"
	[ $fail = 1 ] && { echo "--- run"; cat "$tmp/run.txt"; echo "--- server log"; tail -15 "$tmp/server.log"; magick "$tmp/ws/.codejr/shot.png" -format '%[pixel:p{320,300}] %[pixel:p{40,20}]\n' info: 2>&1; cp "$tmp/ws/.codejr/shot.png" /tmp/keysrun-shot.png 2>/dev/null; }
	rm -rf "$tmp"
}

job keysrun

job pagerun long "a long reply stops at More" "line 20" "!line 21"
job pagerun /exit "/exit answers at once" "Hanging up" "!^>  *.\{0,1\}$"
job deltarun
job runreport
job runrun "/run 2" 'GWBASIC BETA.BAS' "/run by number: BASIC through GW-BASIC"
job runrun "/run game" 'CD \CODEJR\PROGS\GAME' "/run by folder name"
job run "9600 baud" "/F:9600"
job run "fast: /F:37287" "/F:37287"
wait
cat "$jobs"/*.txt
if grep -q '^FAIL' "$jobs"/*.txt; then fail=1; fi
rm -rf "$jobs"
exit $fail
