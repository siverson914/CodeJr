# CodeJr tasks. Run `just` to list them.

location := env_var_or_default("CODEJR_LOCATION", "Encinitas, CA 92024")

# Live data lives outside the repo, so the server uses the same files from
# any checkout or gw worktree: workspace/ (Claude's folder, mirrored to
# C:\CODEJR\PROGS on the PCjr) and logs/.
home := env_var_or_default("CODEJR_HOME", env_var("HOME") / ".local/share/codejr")
ws := home / "workspace"
logdir := home / "logs"

# List the tasks
default:
    @just --list --unsorted

# ---- server ---------------------------------------------------------------

# Build the server
build:
    cd server && go build -o codejr ./cmd/codejr

# Start the server in the background (extra options pass through: just start -baud 2400)
start *args: build
    #!/bin/sh
    if pgrep -x codejr >/dev/null; then echo "already running (just restart)"; exit 1; fi
    mkdir -p "{{ws}}" "{{logdir}}"
    # A fresh workspace gets the client too, so /setup can install it.
    [ -f "{{ws}}/CODEJR.EXE" ] || [ ! -f client/CODEJR.EXE ] || cp client/CODEJR.EXE "{{ws}}/"
    setsid server/codejr -workspace "{{ws}}" -location "{{location}}" -linklog "{{logdir}}/link.log" -trace "{{logdir}}/trace.bin" {{args}} >> "{{logdir}}/server.log" 2>&1 &
    sleep 1
    tail -1 "{{logdir}}/server.log"

# Stop the server (hangs up any call)
stop:
    #!/bin/sh
    if pkill -x codejr; then sleep 1; echo stopped; else echo "not running"; fi

# Restart the server, unless the PCjr is on a call
restart *args:
    #!/bin/sh
    if ss -tn | grep -q ':4860 '; then
        echo "The PCjr is connected. A restart hangs up. Use: just restart-now"; exit 1
    fi
    just restart-now {{args}}

# Restart the server even if the PCjr is on a call
restart-now *args: stop
    just start {{args}}

# Is the server up, and is the PCjr connected?
status:
    #!/bin/sh
    pgrep -ax codejr | cut -c1-100 || echo "server: not running"
    ss -ltn | grep -q ':4860 ' && echo "listening on 4860" || echo "not listening"
    ss -tn | grep ':4860 ' | awk '{print "call from", $5}' | grep . || echo "no call"
    echo "data in {{home}}"
    echo "last log line: $(tail -1 "{{logdir}}/server.log" 2>/dev/null | cut -c1-140)"

# Follow the server log
logs:
    tail -n 30 -f "{{logdir}}/server.log"

# Show the last n lines of the server log
log n="30":
    @tail -n {{n}} "{{logdir}}/server.log" | cut -c1-200

# Frame-by-frame timeline of the last native call (first n lines)
link n="80":
    #!/bin/sh
    start=$(grep -n -- '--- call' "{{logdir}}/link.log" | tail -1 | cut -d: -f1)
    tail -n +"${start:-1}" "{{logdir}}/link.log" | head -n {{n}}

# What the PCjr's Claude and Steve said in the latest conversation
transcript n="40":
    #!/usr/bin/env python3
    import glob, json, os
    files = sorted(glob.glob(os.path.expanduser("~/.claude/projects/*[Cc]ode[Jj]r*workspace*/*.jsonl")), key=os.path.getmtime)
    if not files:
        raise SystemExit("no transcripts")
    for line in open(files[-1]).read().splitlines()[-{{n}}:]:
        o = json.loads(line)
        m = o.get("message", {})
        if o.get("type") == "user" and isinstance(m.get("content"), str):
            print("\nUSER:", m["content"])
        elif o.get("type") == "assistant":
            for c in m.get("content", []):
                if c.get("type") == "text":
                    print("CODEJR:", c["text"])
                elif c.get("type") == "tool_use":
                    print("  [tool]", c["name"])

# ---- what goes to the PCjr ------------------------------------------------

# Files waiting to be sent to the PCjr on its next connection
queue:
    #!/usr/bin/env python3
    import hashlib, json, os
    ws = "{{ws}}"
    try:
        synced = json.load(open(f"{ws}/.codejr/synced.json"))
    except FileNotFoundError:
        synced = {}
    waiting = 0
    for name in sorted(os.listdir(ws)):
        path = os.path.join(ws, name)
        if name.startswith(".") or not os.path.isfile(path):
            continue
        data = open(path, "rb").read()
        if synced.get(name.upper()) != hashlib.sha256(data).hexdigest()[:16]:
            print(f"{name:14} {len(data):7} bytes")
            waiting += len(data)
    print(f"{waiting} bytes waiting" if waiting else "nothing waiting: the PCjr has everything")

# Build the PCjr client (CODEJR.EXE, CJTERM.EXE)
client:
    client/build.sh

# Build the client and queue it for the PCjr
deploy-client: client
    cp client/CODEJR.EXE "{{ws}}/CODEJR.EXE"
    @echo "queued: sent on the PCjr's next connection"

# Regenerate the Kermit launcher files in dos/ (what /setup installs)
dos-files: build
    server/codejr -write-dos dos

# ---- graphics library and games -------------------------------------------

# Build the graphics library (runs its tests headless first)
lib:
    lib/build.sh

# Build JRDOOM
doom:
    games/doom/build.sh

# Build JRFLIGHT
flight:
    games/flight/build.sh

# Build both games
games: doom flight

# Build JRCUBE (demos/cube) and render its GIF headless
cube:
    demos/cube/build.sh
    demos/cube/shots.sh 100

# Build both games and queue them for the PCjr
deploy-games: games
    cp games/doom/JRDOOM.EXE games/flight/JRFLIGHT.EXE "{{ws}}/"
    @echo "queued: sent on the PCjr's next connection"

# Build JRAI, the "PCjr MEETS AI" demo
jrai:
    games/jrai/build.sh

# Preview video of JRAI with its sound, made headless (about 2 minutes)
jrai-video out="games/jrai/jrai-preview.mp4": jrai
    games/jrai/video.sh "$PWD/{{out}}"

# Build JRAI and queue it for the PCjr
deploy-jrai: jrai
    cp games/jrai/JRAI.EXE "{{ws}}/"
    @echo "queued: sent on the PCjr's next connection"

# ---- tests ----------------------------------------------------------------

# Everything: Go tests, client build, both emulator paths (about 6 minutes, no windows)
test:
    ./test.sh

# Go tests and client build only
test-quick:
    QUICK=1 ./test.sh

# Go tests only
test-go:
    cd server && go vet ./... && go test -count=1 ./...

# The real client in the emulated PCjr, at 9600 and at the fast speed
emu-native:
    emu/native_e2e.sh

# The real MS-Kermit path in the emulated PCjr
emu-kermit:
    emu/e2e.sh "make hello" CODEJR/PROGS/HELLO.BAS "$PWD/server/internal/session/testdata/fakeclaude.sh"

# ---- upkeep ---------------------------------------------------------------

# Format the Go code
fmt:
    cd server && gofmt -l -w .

# Remove object files left by the builds
clean:
    find client lib games -name '*.o' -type f -delete
    find client lib games -name '*.err' -type f -delete
