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
    names = []
    for name in sorted(os.listdir(ws)):
        path = os.path.join(ws, name)
        if name.startswith("."):
            continue
        if os.path.isdir(path): # an app folder: one level down
            names += [f"{name}/{f}" for f in sorted(os.listdir(path)) if os.path.isfile(os.path.join(path, f))]
        elif os.path.isfile(path):
            names.append(name)
    for name in names:
        data = open(os.path.join(ws, name), "rb").read()
        key = name.upper().replace("/", "\\")
        if synced.get(key) != hashlib.sha256(data).hexdigest()[:16]:
            print(f"{key:22} {len(data):7} bytes")
            waiting += len(data)
    print(f"{waiting} bytes waiting" if waiting else "nothing waiting: the PCjr has everything")

# Put the latest main live from this checkout: pull, build, queue the client, restart (refuses during a call)
live:
    git pull --ff-only
    just deploy-client
    apps/say/build.sh
    just restart

# Run a DOS command on the PCjr that is connected now: just pcjr "TREE C:\CODEJR /F"
[positional-arguments]
pcjr +command:
    @server/codejr ctl -workspace "{{ws}}" dos "$@"

# Send pending files, then run a program in its folder on the connected PCjr and show
# its output, exit code, screen and LOG.TXT: just pcjr-run LEMMJR LEMMJR.EXE /TEST
[positional-arguments]
pcjr-run folder +command:
    @server/codejr ctl -workspace "{{ws}}" run "$@"

# Send pending files to the connected PCjr now
pcjr-sync:
    @server/codejr ctl -workspace "{{ws}}" sync now

# Copy a file from the connected PCjr into the workspace: just pcjr-fetch LOG.TXT
[positional-arguments]
pcjr-fetch name:
    @server/codejr ctl -workspace "{{ws}}" fetch "$@"

# Sort the workspace's loose files into app folders (then run ORGANIZE on the PCjr)
organize: build
    server/codejr organize -workspace "{{ws}}"

# Build the serial test tools (tools/: CJTERM.EXE, UARTDUMP.EXE)
tools:
    tools/build.sh

# Build the PCjr client (CODEJR.EXE, SCRDUMP.EXE)
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
    apps/jrdoom/build.sh

# Build JRFLIGHT
flight:
    apps/jrflight/build.sh

# Build both games
games: doom flight

# Build JRCUBE (apps/jrcube) and render its GIF headless
cube:
    apps/jrcube/build.sh
    apps/jrcube/shots.sh 100

# Build the games and JRCUBE and queue them for the PCjr, each in its own folder
deploy-games: games
    apps/jrcube/build.sh
    mkdir -p "{{ws}}/JRDOOM" "{{ws}}/JRFLIGHT" "{{ws}}/JRCUBE"
    cp apps/jrdoom/JRDOOM.EXE "{{ws}}/JRDOOM/"
    cp apps/jrflight/JRFLIGHT.EXE "{{ws}}/JRFLIGHT/"
    cp apps/jrcube/JRCUBE.EXE "{{ws}}/JRCUBE/"
    @echo "queued: sent on the PCjr's next connection"

# Build JRAI, the "PCjr MEETS AI" demo
jrai:
    apps/jrai/build.sh

# Preview video of JRAI with its sound, made headless (about 2 minutes)
jrai-video out="apps/jrai/jrai-preview.mp4": jrai
    apps/jrai/video.sh "$PWD/{{out}}"

# JRAI's voice clips (ElevenLabs via gipity, made once; then apps/say/mksnd.py)
jrai-voice:
    apps/jrai/mkvoice.sh

# Build JRAI and queue it with its voice clips for the PCjr
deploy-jrai: jrai jrai-voice
    mkdir -p "{{ws}}/JRAI"
    cp apps/jrai/JRAI.EXE apps/jrai/voice/out/*.SND "{{ws}}/JRAI/"
    @echo "queued: JRAI.EXE and $(ls apps/jrai/voice/out/*.SND | wc -l) clips, sent on the PCjr's next connection"

# ---- speech ---------------------------------------------------------------
# Speech quality for the tasks below. Medium (4-bit ADPCM, 8 kHz) is the one
# setting for everything; SPEECH_QUALITY=high|low|fast is for experiments.

speech_quality := env_var_or_default("SPEECH_QUALITY", "medium")

# Build SAY.EXE, the PCjr speech player
say:
    apps/say/build.sh

# Convert every clip in apps/say/clips/ into apps/say/out/ (.SND and a preview WAV)
speech:
    #!/bin/sh
    set -e
    for f in apps/say/clips/*.mp3; do apps/say/mksnd.py "$f" --quality {{speech_quality}} -o "apps/say/out/$(basename "$f" .mp3)"; done

# New clip from text via gipity (voice: brian, george, daniel, rachel, openai:sage, gemini:Charon)
clip name text voice="brian":
    #!/bin/sh
    set -e
    n=$(echo "{{name}}" | tr a-z A-Z)
    apps/say/mksnd.py --text "{{text}}" --voice "{{voice}}" --quality {{speech_quality}} --keep -o "apps/say/out/$n"
    mv "apps/say/out/$n.mp3" "apps/say/clips/$n.mp3"
    printf '%s\n' "{{text}}" > "apps/say/clips/$n.txt"

# Queue SAY.EXE and clips for the PCjr: just deploy-speech HELLO READY (NAME.SNP for PWM)
deploy-speech +clips: say
    #!/bin/sh
    set -e
    mkdir -p "{{ws}}/SAY"
    cp apps/say/SAY.EXE "{{ws}}/SAY/"
    total=$(stat -c %s apps/say/SAY.EXE)
    for c in {{clips}}; do
        c=$(echo "$c" | tr a-z A-Z); case "$c" in *.*) ;; *) c="$c.SND";; esac
        if [ "${c##*.}" = SNP ]; then o="--method pwm"; else o="--quality {{speech_quality}}"; fi
        apps/say/mksnd.py "apps/say/clips/${c%.*}.mp3" $o -o "apps/say/out/${c%.*}"
        cp "apps/say/out/$c" "{{ws}}/SAY/"; total=$((total + $(stat -c %s "apps/say/out/$c")))
    done
    echo "queued $((total / 1024)) KB ({{speech_quality}} quality): about $((total / 2600 + 1)) s at 37287 baud; sent on the PCjr's next connection"

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
    find client lib games speech -name '*.o' -type f -delete
    find client lib games speech -name '*.err' -type f -delete
