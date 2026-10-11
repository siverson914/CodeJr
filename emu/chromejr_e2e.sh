#!/bin/sh
# Headless end-to-end test of CHROMEJR.EXE: the emulated PCjr dials a
# ChromeJr server (a real hidden Chromium, a stand-in for the model) and
# browses two local test pages with the arrow keys. No window is opened.
# Emulator only: its serial line never stalls or loses bytes.
#   emu/chromejr_e2e.sh
set -u
cd "$(dirname "$0")/.."
port=${E2E_PORT:-4890}
fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }

(cd server && go build -o chromejr ./cmd/chromejr) || exit 1
apps/chromejr/build.sh >/dev/null || exit 1
# The test pages from a local web server: a short address, as typed.
wport=$((port + 50))
python3 -m http.server --bind 127.0.0.1 $wport -d apps/chromejr/test > /dev/null 2>&1 &
web=$!
trap 'kill $web 2>/dev/null' EXIT
pages="http://127.0.0.1:$wport"

run() { # $1 = label, $2 = port, the rest: CHROMEJR's /T options
	label=$1 p=$2
	shift 2
	tmp=$(mktemp -d)
	mkdir -p "$tmp/c"
	cp apps/chromejr/CHROMEJR.EXE "$tmp/c/"
	server/chromejr -listen 127.0.0.1:$p -profile "$tmp/profile" -claude apps/chromejr/test/fakeclaude.py > "$tmp/server.log" 2>&1 &
	sp=$!
	sleep 2
	printf '[dosbox]\nmachine=pcjr\n[cpu]\ncycles=fixed 3000\n[serial]\nserial1=nullmodem server:127.0.0.1 port:%s transparent:1\n[autoexec]\nmount c %s/c\nc:\nCHROMEJR /P:1 /D:x /F:9600 %s /X:35 /S:SCREEN.TXT\nexit\n' $p "$tmp" "$*" > "$tmp/dosbox.conf"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 150 dosbox -conf "$tmp/dosbox.conf" >/dev/null 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	s="$tmp/c/SCREEN.TXT"
	echo "== $label"
}
report() {
	if [ $fail = 1 ]; then
		echo "--- server log"; cat "$tmp/server.log"
		echo "--- screen"; cat "$s" 2>/dev/null
	fi
	rm -rf "$tmp"
}

# A page laid out: the title bar, the search box, items with prices,
# the first item picked.
run "a page" $port "/T:$pages/shop.html"
check "the client asked for markup" "grep -q 'client says .*CHROMEJR.*markup=1' '$tmp/server.log'"
check "the page was laid out" "grep -q 'filter: list, 4 choices' '$tmp/server.log'"
check "title bar" "head -1 '$s' | grep -q 'Test Shop.*ChromeJr'"
check "address bar shows where we are" "sed -n 2p '$s' | grep -q 'Go to: http://127.0.0.1:.*shop.html'"
check "the search box" "grep -q ' 1 Search:' '$s'"
check "an item with its price" "grep -Eq '^ 3 +Blue Widget +\\\$6.00' '$s'"
check "its detail line" "grep -q 'Calm and blue' '$s'"
check "the action" "grep -q '4 Add to cart' '$s'"
check "the first item is picked (both its rows)" "grep -Eq '^>2 +Red Widget' '$s' && grep -q '^>   Shiny and red' '$s'"
check "client hung up cleanly" "grep -q 'client hung up' '$tmp/server.log'"
report

# Down, Enter: the second item opens; Backspace comes back (from the
# cache); then the box: Up to it, type, Enter.
run "arrows, Enter, back, a box" $((port + 1)) "/T:$pages/shop.html" "/T:~DE" "/T:~B" "/T:~UU" "/T:widgets"
check "Down and Enter pressed the Blue Widget" "grep -q 'press 3 (view 3, \"Blue Widget\")' '$tmp/server.log'"
check "the product page was laid out" "grep -q 'filter: product' '$tmp/server.log'"
check "Backspace went back, from the cache" "grep -q 'back: \"Test Shop\"' '$tmp/server.log' && grep -q 'cached' '$tmp/server.log'"
check "typing with the box picked filled it" "grep -q 'fill 1 (view 1)' '$tmp/server.log'"
check "the search landed on the Blue Widget page" "head -1 '$s' | grep -q 'Blue Widget'"
check "its text" "grep -q 'A blue widget for six dollars' '$s'"
report

# Alt-W: the page itself, as numbered text, after the view.
run "the raw page" $((port + 2)) "/T:$pages/shop.html" "/T:~W"
check "Alt-W asked for the raw page" "grep -q 'typed: \"w\"' '$tmp/server.log'"
check "it shows with the page's own numbers" "grep -q 'Red Widget\[2\] \\\$5.00' '$s'"
check "and its title" "head -1 '$s' | grep -q 'Test Shop'"
check "client hung up cleanly" "grep -q 'client hung up' '$tmp/server.log'"
report

[ $fail = 0 ] && echo "== All ChromeJr tests passed"
exit $fail
