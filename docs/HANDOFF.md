# CodeJr handoff

Written 2026-10-03, at the end of the first working session. Read this first. `docs/JOURNAL.md` has the full story of how each thing was found; this file is what you need to continue.

## Where things live

- **Start sessions in `~/Github/CodeJr`** (the gw workspace root; it has its own `CLAUDE.md`). The repo is `~/Github/CodeJr/CodeJr`. Run `just` from inside the repo.
- **Live data is outside the repo,** in `~/.local/share/codejr/`: `workspace/` (Claude's folder for the PCjr) and `logs/`. Every checkout and gw worktree shares it. `CODEJR_HOME` overrides the location.
- **Git:** private repo `github.com/siverson914/CodeJr`, branch `main`. The workspace uses gw: every task runs in a session under `~/Github/CodeJr/.worktrees/` and lands with a gate (`just test`). The canonical checkout `~/Github/CodeJr/CodeJr` is read-only for edits and is what the live server runs from: `just live` there pulls, builds, queues the client and restarts.
- **Layout (2026-10-06):** `server/` and `client/` are CodeJr itself; `lib/` is the graphics library; every program that runs on the PCjr is under `apps/` (one folder each, with its own `build.sh`); `tools/` has the serial test tools; `docs/design/` has the original plans. On the PCjr, `C:\CODEJR\PROGS` holds CodeJr's own files at the top and one folder per app (`JRAI`, `SAY`, `JRDOOM`, `JRFLIGHT`, `JRCUBE`, `BASIC`, `UTILS`, `VOICE`, plus the projects CodeJr made). `session.Layout` (`organize.go`) is the one table behind both `ORGANIZE.BAT` (moves loose files on the PCjr) and `just organize` (moves them in the workspace and renames the sync records, so nothing is sent again).

## What CodeJr is

Claude Code for a real 1984 IBM PCjr. The user (Steve) sits at the PCjr, types a request, and Claude's files land on the PCjr's own disk.

```
PCjr ──serial── WiFi modem (Zimodem) ──TCP 4860── laptop: server/codejr (Go)
                                                     └─ claude -p (headless Claude Code,
                                                        Steve's subscription) in workspace/
```

There are two ways in from the PCjr, and the server tells them apart by itself:

| | Kermit path | Native path |
|---|---|---|
| On the PCjr | MS-Kermit 3.14, started by `C:\CJKERMIT.BAT` | `CODEJR.EXE` (our own program, `client/`), started by `C:\CODEJR.BAT` |
| Screen | drawn by the server with VT100 codes | drawn by the client in video memory |
| Files | Kermit protocol, started with APC | pull protocol (bursts, compressed) |
| Speed measured on the real PCjr | 235 bytes/s | pull: 783 bytes/s at 9600, 2,620 at 37287 (file bytes, compressed on the line) |
| Status | works, proven, the fallback | proven: chat, pull at 9600 and 37287; the main path |

## State at handoff

**Works on the real PCjr (seen in logs or photos):**

- Kermit path end to end: chat, file transfer, `/setup`, `/get`, cold boot to `CODEJR`.
- Native client: dialing, chat, colors, own screen, dialing from `CODEJR.CFG`.
- The one-frame-at-a-time link (window 1): 24,900 bytes in 60 s with one resend.
- The pull protocol (day 2, client 0.3/0.4): every file whole, damaged blocks fetched again. `JRFLIGHT.EXE` (94.6 KB) at each speed:

  | Speed | Time | Requests for 24 chunks | Notes |
  |---|---|---|---|
  | 9600 (`JRDOOM`, 88.6 KB) | 92 s | 23 for 22 | |
  | 27965 (divisor 4) | 42.8 s | 24 | clean |
  | **37287 (divisor 3)** | **36.1 s** | 24 | the default from 0.5 |
  | 55930 (divisor 2) | 49.4 s | 81 | 56 bytes lost: the PCjr can't keep up |

  Even on a clean line only about 57% of the line is used: the rest is the gap between a chunk's request and its burst, plus decompressing and writing between bursts. Measure where before changing it.
- Web search and fetch for the PCjr's Claude; date, time and location in its prompt.

**Works only in tests and the emulator so far:**

- The pull protocol's stall fix (client 0.3). The first real pulls (client 0.2) worked chunk after chunk, then failed when the WiFi link stalled mid-burst. The fix is in `client/pull.c` (`pull_fetch`) and `cjp.Burster`.
- Client 0.6: `/run`, the `CODEJR.BAT` run-and-call-back loop, local echo, the "thinking" line (see below).
- Both games and the whole graphics library. Nobody has played them with a real keyboard, heard the sound, or seen the palette on the real monitor.
- `SAY.EXE` and its speech clips (`apps/say/`, README there): ElevenLabs speech (voice "Brian") through gipity, one quality for everything: medium (4-bit ADPCM, 8 kHz), played on the SN76496's three voices combined (`.SND`). **SAY 1.0 played on the real PCjr:** the chip version sounded best, PWM (`.SNP`) whined, but the chip loop was late on 72% of samples (playing slow). SAY 1.1 polls the 8259 instead of taking an interrupt per sample and has a stopwatch (`/V`, `/T`); the poll loop can't run in DOSBox and is untested.

**Queued for the PCjr:** `CODEJR.EXE` 0.9 (0.6's setup files went over on day 2), plus the JRAI session's demo files.

**Launchers (day 2):** `C:\CODEJR.BAT` starts the native client (`C:\CODEJR\PROGS\CODEJR.EXE /L:1`) in a loop: `/run` writes `C:\CODEJR\RUNNEXT.BAT` and hangs up, the batch file runs it, waits for a key and calls back. If `CODEJR.EXE` is missing it falls back to Kermit. `C:\CJKERMIT.BAT` is the Kermit path. `/setup` (Kermit) installs both, plus `CODEJR.INI`, `CODEJR.TXT` and `CODEJR.CFG` (`DIAL=host:port`, the only copy of the address), then syncs. A native client can only write into `PROGS`, so the server keeps `SETUP.BAT` and `.BT` copies of both launchers in the workspace; `SETUP` copies them to `C:\`. Keep `CJKERMIT.BAT` unchanged between versions: `/setup` rewrites it while it runs, and DOS reads a batch file by position (that cost one confusing run on day 2). The client pulls into `CJPULL.$$$` and renames when whole, so a failed update keeps the old `CODEJR.EXE`.

**Client 0.6 UI:** it says `ui=1` in its HELLO and the server answers `CODEJR/2 server ui`. Then the client echoes typed lines itself, shows the `>` prompt only after the server's `Ready` message, and shows `Temp` lines ("CodeJr is thinking...") that the next text erases. Older clients get the old behavior. 0.7 keeps a line typed while CodeJr is busy (sent on `Ready`, with a status message) and turns the prompt's spot into a spinner while busy. The transfer line shows the line's real speed (bytes on the wire, so it can't exceed the baud rate); Alt-S shows the error counters and the last file's compression, damage and request counts.

**On the PCjr now:** `C:\CODEJR.BAT` (still the old Kermit launcher until `SETUP` runs), `C:\CODEJR\CODEJR.INI`, `C:\CODEJR\CODEJR.CFG`, `C:\MSK314\`, and in `C:\CODEJR\PROGS`: `CODEJR.EXE` 0.5 (if Steve synced), `NATIVE.BAT`, `FAST.BAT`, `RUN.BAT`, `JRDOOM.EXE`, `JRFLIGHT.EXE`, BASIC programs, test tools.

**The server is running** in the background (`just status`; `just restart` to rebuild and restart it), logging to `~/.local/share/codejr/logs/`.

**Client 0.8, project folders:** the workspace may hold one level of folders (`SNAKE/SNAKE.BAS` on the laptop is `SNAKE\SNAKE.BAS` on the PCjr). Clients that say `dirs=1` create the folder and receive them; Kermit and older clients skip them with a note. `/run` lists programs inside folders and finds them by number, file name or folder name. Claude is told to give each new program its own folder.

**Build and run tools (server):** `codejr mcp` is a small MCP server (`server/internal/pcjr`) that `claude -p` starts through `--mcp-config`. `build PROJECT` compiles the folder's `.C`/`.ASM` with Open Watcom (8088, large model; JRGFX's sources are linked in when a file includes `jrgfx.h`) into `PROJECT\PROJECT.EXE`, objects in a scratch folder. `run PROJECT PROGRAM` runs it in headless DOSBox and returns what it printed, `LOG.TXT`, the text screen (via `SCRDUMP.EXE`) and any `.PPM` screenshots as images. No shell. `JRGFX/JRGFX.H` is kept in the workspace so Claude can read the API. Tested with real `claude -p` in restricted mode: it built a JRGFX program, ran it and described the screenshot correctly. GW-BASIC isn't on the laptop, so BASIC can't run in the emulator.

**Client 0.9, run reports:** `/run` writes `RUNNEXT.BAT` so that after the program it notes a nonzero exit (`LASTERR.TXT`) and saves the text screen with `SCRDUMP` (`LASTSCR.TXT`) before anything clears it; BASIC now runs `GWBASIC` directly for that reason, then `MODE CO80`. On the next call the client sends a report (exit, screen, the program's `LOG.TXT` if written during the run) as `Result` messages; the server keeps it in `.codejr/lastrun.txt` and puts it in front of the next prompt, once. Only with servers that say `results`.

**Client 1.0, changes only:** the server keeps an exact copy of what it last sent of each file (`.codejr/have/`, seeded at startup from files the PCjr already has) and, for clients that say `delta=1`, sends an edit script instead of the whole file when the script is under 70% of the file's compressed size (`cjp.Delta`: copy ranges from the old file plus new bytes, found with a rolling hash, so shifted code still matches). The `DeltaOffer` carries the old copy's size and CRC; the client checks them, replays the script into `CJPULL.$$$` and checks the new file's size and CRC before replacing anything. If its copy differs it says so and the server sends the file whole. Measured on `JRFLIGHT.EXE` with a one-line code change: 45 KB of the 97 KB file changed, yet the script was 4.4 KB on the line instead of 64 KB.

**Client 1.1, scrollback and paging:** the transcript is a view of a 200-line scrollback buffer in far memory (`screen.c`). After a page of new lines since the user's last line, the view holds and the status bar says `-- More -- N new lines`; the client keeps receiving and acking, so the next page is already in memory when Space is pressed. PgUp/PgDn, the arrows and End scroll. Alt-X and `/exit` (also `/quit`, `/bye`) are handled by the client: "Hanging up..." appears at once.

**Client 1.3:** once CodeJr is leaving (any way out), the input line and cursor are gone and keys are left for DOS; and a key typed during a spoken reply stops the speech and lands in the input line (SAY's `/K`). Not yet run on the real PCjr.

**Client 1.2, `/voice`:** toggles a workspace flag. The voice gets a spelled-out copy of the reply (`Speakable` in `server/internal/session/speakable.go`: "PCjr" as "PC Junior", "=" as "equals", file names, units), while the screen keeps the text as written; add a rule there when the voice says something wrong. While on, Claude is told to answer in a sentence or two, and after each reply the server runs the speech project's converter (`apps/say/mksnd.py --text ...` at the converter's default quality, which Steve set to medium: gipity's neural voice, 4-bit ADPCM at 8 kHz, about 4 KB per second of speech, so a 5 s reply takes about 8 s to send) into `VOICE/REPLY.SND`, syncs it, and sends `Play`. The client plays it on `READY` (the line is quiet then) by running `SAY.EXE` as a child program with its output sent to NUL, so CodeJr stays connected and keeps its screen; a key stops the speech. Fixed clips `VOICE\ON.SND`, `OFF.SND`, `BYE.SND` are made once in the background at server start; `/exit` plays `BYE` with voice on. Making a 2.4 s clip takes 2 to 4 s. The server also provides the repo's `apps/say/SAY.EXE`, so the player matches the clips. `SAY=` in `CODEJR.CFG` passes up to two options to SAY (the tests use `/H /X`: DOSBox needs `/H`). SAY's code is not linked into the client on purpose: running SAY.EXE picks up the speech session's improvements with no client change.

**Client 1.7, remote work:** the server can ask the PCjr to run a DOS command and send back what it printed (`Exec`), or to send a file (`Fetch`); answers come as `Upload` messages over the acknowledged link, the PCjr's clean direction (about 1.5 KB/s). The client runs `COMMAND <NUL >C:\CODEJR\CMDOUT.TXT` through `system()`, repaints its whole screen from the scrollback buffer afterwards (`scr_refresh`), and uploads the file. Three ways in: the user types `/dos COMMAND` or `/send NAME` (and what `/dos` printed goes to Claude with the next message); the PCjr's Claude has `dos` and `fetch` tools; and from the laptop, `just pcjr "TREE C:\CODEJR /F"` and `just pcjr-fetch NAME` while the PCjr is connected. The last two go through a Unix socket in the workspace's `.codejr/` (`pcjr.CtlServe`, `session.Control`) that reaches the call in progress. Names read from each seat: on the PCjr `/send` goes to CodeJr and `/get` brings a file again (they were the other way round until 1.7, on the Kermit path too); Claude's tool is `fetch`. Permission is enforced, not just asked for (1.8): `session.SafeDOS` lists the commands that only look or create (`DIR`, `TYPE`, `TREE`, `MEM`, `VER`, `VOL`, `CHKDSK` without `/F`, `PATH`, `SET`, `ECHO`, `FIND`, `MORE`, `SORT`, `MD`, `CD`, `DATE`, `TIME`, `COMP`, with pipes between them); anything else, including any `>` or `<`, goes to the client marked "ask", which shows "CodeJr wants to run: ..." and runs it only on Y (N, Esc or 3 minutes of silence is a no, reported back as an error). The user's own `/dos` never asks.

**Where CodeJr's own files live (1.0.22):** in `C:\CODEJR`, one level above the programs: `CODEJR.EXE`, `SCRDUMP.EXE`, `CJLOOP.BAT`, `SETUP.BAT`, `ORGANIZE.BAT`, `RUN.BAT` and the `.BT` copies of the two files in `C:\`. `C:\CODEJR\PROGS` holds only program folders. In the workspace they are in the hidden `.system/` folder and travel as `..\NAME` (clients that say `sys=1`); an older client is sent just the new `CODEJR.EXE` at its old place so it can update. `just deploy-client` copies there.

**The launcher is two files (1.0.22):** `C:\CODEJR.BAT` is a stub that never changes again: it swaps `C:\CODEJR\CJLOOP.NEW` in as `CJLOOP.BAT` and starts it (MS-Kermit if there is none). `CJLOOP.BAT` holds the loop: start the client from `C:\CODEJR` (or `PROGS`, for an install that hasn't moved yet), and go round again if the client left `RUNNEXT.BAT` (a program picked with `/run`) or `REDIAL.TXT` (the server is restarting). A launcher change is now an ordinary download of `CJLOOP.NEW`. DOS reads a batch file by position, so the file that is running must never be rewritten: that rule cost a confused start on day 2 and is why the stub exists.

**Restarting the server (1.0.22):** `just restart` is always safe. On SIGTERM the server sends the connected client `Redial`; the client leaves `REDIAL.TXT`, hangs up, the launcher starts it again, and it redials (asking again every 8 seconds for up to a minute until the new server answers). `just live` does pull, build, queue and restart.

**Versions:** from 1.0.20 the client is numbered 1.0.N, counting up by one (Steve's choice; 1.9 was the 19th).

**The iteration loop (1.9):** `just pcjr-run FOLDER PROGRAM ARGS` (and the `run_on_pcjr` tool for the PCjr's Claude) does a whole edit-run-read cycle against the real machine: pending files are sent first (changes only), the client runs the program in its project folder with a clean screen, and uploads what it printed, its exit code (a `.EXE` or `.COM` is started directly), the text screen it left and its `LOG.TXT` if written during the run. `just pcjr-sync` only pushes. The client puts its current folder back after every command: in DOS a child's `CD` changes the parent's folder too, and received files land in the current folder. `/trust`, typed on the PCjr only, lets CodeJr run programs and change files without the Y/N question (a workspace flag); it is on for Steve's PCjr at his request. A program that hangs on the real PCjr can only be stopped at the PCjr: test programs need a mode that ends by itself. Nobody presses keys (stdin is NUL).

**Keys and screenshots for a run (1.0.23):** while a program started by `Run` is up, the client is still in memory behind it, with a hook on the timer tick (INT 1Ch). The hook presses scripted keys and copies the video memory. Keys: on the PCjr the keyboard path ends in a software INT 9 with the scan code in AL, so the hook issues exactly that (make, then break a tick later), which the BIOS and any program that hooked INT 9 (JRGFX's `key_init`) both see; on other machines (DOSBox) it puts the key in the BIOS buffer (`/K:B` forces that, `/K:I` the other). Screenshot: at N seconds, or every second keeping the latest ("last"), it copies 16 KB (32 KB in mode 9 or 10) from the page on screen, found through the BIOS's copies of the mode (40:49) and of port 3DFh (40:8A); after the program ends it is uploaded as a second file and `session.DecodeShot` makes a 640x400 PNG, kept as `.codejr/shot.png`. The palette registers can't be read, so colors are the standard 16. Use: `KEYS='{2}{SPACE}{3}q' SHOT=last just pcjr-run FOLDER PROGRAM`, or the `keys` and `screenshot` arguments of Claude's `run_on_pcjr`. Proven on the real PCjr on 2026-10-08: `KEYS='{4}{SPACE}{2}{SPACE}{9}{ESC}{2}{ESC}{2}{ESC}' SHOT=12 just pcjr-run LEMMJR LEMMJR.EXE` started a level, the picture showed the first lemming under the trapdoor, and the scripted Esc ended the game with exit code 0. From 1.0.24 a scripted key stays down 5 ticks (0.27 s), so a game that samples the keyboard once per slow frame still sees it; a pause shorter than that between two keys is stretched to it.

**The library is shared and grows (2026-10-09):** JRGFX is now the core plus modules, all in `lib/jr*`: `jrkeys` (game keys that survive lost release codes), `jrmusic` (tracker on the retrace interrupt, see `lib/MUSIC.md`), `jrtest` (the `/TEST` mode, see `lib/TESTING.md`). The server's build tool and `apps/build.sh` compile `lib/jr*`, `lib/shared` and the workspace's `JRGFX` folder into a `.lib` per build, so CodeJr on the PCjr side adds to the library by writing a file there, and `just save-projects` brings it back to `lib/shared`. Never copy a library file into a game folder. The server keeps the library's headers in the workspace's `JRGFX` folder. That folder is never sent to the PCjr. Emulator-tested only. The stuck-key cause (lost release codes) is unmeasured: `apps/keytest` (in the workspace as `KEYTEST`) records what the keyboard sends, and needs Steve at the keys.

**Long turns and long conversations (2026-10-08, server only):** the status line follows the stream: thinking with a token count (the thoughts themselves are not sent), "CodeJr is writing NAME, n KB so far" while a tool call is still being written (a whole game in one Write took 3.5 minutes with nothing on screen before this), "CodeJr is answering". A conversation over 80,000 tokens (`tidyAbove`, size kept in `.codejr/context`) is shortened with `/compact` after the turn. A resumed conversation is read in full on every turn; the prompt cache only helps within a turn. Untested on the real PCjr.

**A run on the PCjr has a time limit (2026-10-09):** 180 seconds unless asked (the `seconds` argument, `LIMIT=n just pcjr-run`, up to 600). At the limit the client presses Esc three times (appended to the run's key script by `withLimit`), and the result says the limit was reached. Before this, a game started with no way to end held the PCjr and the turn for 83 minutes. "N frames resent" in the thousands in the server log means exactly that: the Run frame is not acknowledged until the program ends. The resend timer now doubles up to 30 s once four tries in a row have met silence.

**CodeJr's own screen (1.0.25):** `just pcjr-screen out.png` asks the client for its text screen as it is now (a fetch of `*SCREEN`: video memory plus the ROM's character shapes, nothing written on the screen for it) and `tools/textshot.py` draws it. Emulator-tested; on the real PCjr the same bytes were captured with a stopgap DOS tool before the client could do it.

**Clock (1.8):** the server's greeting carries the laptop's local time (`t=YYYYMMDDhhmmss`) and the client sets DOS's date and time from it at every connect, so files written on the PCjr get real dates. It says "PCjr clock set: ..." only when the clock was off by more than a minute. This is DOS's clock, not the jr-IDE card's battery clock.

## First things to test on the real PCjr

Ask Steve to do these in order and send the `[client]` line or a photo each time.

1. **Get 1.2:** connect with `CODEJR`; it downloads by itself. Hang up and call again so 1.2 runs.
2. **Paging:** ask for something long ("tell me a long story"). It should stop at `-- More --`; Space shows the next page at once. PgUp/PgDn scroll back (on the PCjr keyboard these may need the Fn key).
3. **`/voice`:** "Voice on" should play right away; then ask a question: text first, speech a few seconds later; a key stops it. `/exit` should say goodbye.
4. **The UI:** your line appears at once, a spinner turns where the prompt goes, "CodeJr is thinking..." shows until the reply. Type a line while it's busy: it should wait and go by itself.
5. **A C program, start to finish:** ask "write a bouncing ball in C with JRGFX in a BALL folder, test it, then I'll run it". Watch for `[builds BALL]` and `[tests BALL.EXE in the emulator]`, then `/run ball`.
6. **Run report:** after the program ends and CodeJr calls back, ask "how did that go?". CodeJr should know what was on the screen and whether it failed.
7. **Transfer timing:** after a file over 4 KB, the second line shows waiting, receiving, gap, unpacking and disk times. Send a photo of one for `JRAI.EXE` or a game.
8. **Changes only:** after CodeJr rebuilds a program you already have, the line should read "sent N bytes of changes".
9. **Run the games:** `JRDOOM` and `JRFLIGHT`. Check keyboard, sound, colors and speed. Both take `/BENCH`, `/PROF` and `/DEMO:n` and write `LOG.TXT`. To get a log back: `/send LOG.TXT` in CodeJr, or `just pcjr-fetch JRDOOM\LOG.TXT` from the laptop.
10. **Risky graphics paths:** `JRFLIGHT /LD` (hardware 160x100 mode, never displayed anywhere), and 320x200 mode in `lib/DEMO.EXE` (this PCjr reserves only 16 KB of video memory; the library claims its own block).
11. **Typing in the native client while text arrives:** do letters get lost? Alt-S prints the client's loss counters into the chat.
12. **Speech timing.** SAY 1.1-1.4's `/T` froze the PCjr (power-off needed): never run `/T` from those versions. With SAY 1.5: `SAY /T HELLOH` (chipmunk; the line to note is "chip clips up to about N Hz should keep up here"), then `SAY /V HELLO` (9.3 kHz) and `SAY /V HELLO10` (10.65 kHz). Want: late 0 and a speed matching "wanted". Then re-make the clips at the highest rate with some margin below the `/T` figure (`mksnd.py --hz`, then make it the default in `METHODS`). If `SAY` hangs or makes no sound with polling but `SAY /H HELLO` works, the poll command misbehaves on this 8259: say so in the journal.
13. **Quality presets (SAY 1.3).** `SAY /V HELLOH` (high: raw 10.7 kHz), `SAY /V HELLOM` (medium: 4-bit ADPCM, 8 kHz), `SAY /V HELLOL` (low: 3-bit ADPCM, 6 kHz). Want late 0 on all three, especially high; note each "decoded ADPCM" time. If high is late, lower its rate in `QUALITY` in `apps/say/mksnd.py`.
14. **Tighter clips (SAY 1.6).** `REPLYV6`, `REPLYV5`, `REPLYV4`, `REPLYV3` (2-bit at 6/5/4/3 kHz, new pipeline) against the old `REPLY5`/`REPLY4`/`REPLY3`, and `REPLYL6` (3-bit, 6 kHz). Ask for the lowest rate that still sounds acceptable: that sets voice mode's preset.

## How to work in this repo

Everything routine is a `just` task (run `just` to list them all):

```sh
just status          # is the server up, is the PCjr connected
just restart         # rebuild and restart the server (refuses during a call; restart-now forces)
just log 40          # server log        just logs     # follow it
just link            # frame timeline of the last native call
just transcript      # what Steve and the PCjr's Claude said last
just queue           # files waiting to go to the PCjr
just test            # everything, about 6 minutes, no windows
just test-quick      # Go tests and client build
just emu-native      # the real client in a headless emulated PCjr, 9600 and fast
just emu-kermit      # the real MS-Kermit path, headless
just deploy-client   # build CODEJR.EXE and queue it for the PCjr
just deploy-games    # build both games and queue them
just lib             # graphics library, test-gated
just clip NAME "text" # new speech clip via gipity; just speech converts them all
just deploy-speech HELLO READY  # queue SAY.EXE and clips (NAME.SNP for a PWM version)
```

- **Toolchain:** Go, C-Kermit and DOSBox 0.74 from pacman. Open Watcom v2 is unpacked in `~/.local/opt/openwatcom` (`. client/owenv.sh`). NASM is not installed; use Open Watcom's `wasm`. `gcc` builds the host test harnesses. `magick` converts screenshots.
- **To deploy a new client:** run the tests, then `just deploy-client`. The server sends changed workspace files on the PCjr's next connection (`just queue` shows what's waiting).
- **To see what the PCjr's Claude said:** `just transcript` (the files are in `~/.claude/projects/*CodeJr-CodeJr-workspace*/`).
- **`emu/render.py`** (shows a trace as a screen) needs the `pyte` Python package in a venv; it is not installed permanently.

### Where things are

| Path | What |
|---|---|
| `server/cmd/codejr` | flags, listener, LAN-only access, one caller at a time |
| `server/internal/session` | `session.go` Kermit front end, `native.go` native front end, `workspace.go` sync state, `prompt.go` Claude's instructions, `dosfiles.go` files `/setup` installs |
| `server/internal/cjp` | native protocol: `cjp.go` frames, `link.go` reliable link, `pull.go` file pull and `Burster` |
| `server/internal/lz` | LZ4-format compressor |
| `server/internal/kermit` | Kermit protocol in Go |
| `server/internal/term` | VT100 screen, pacing, ASCII conversion |
| `server/internal/claude` | runs `claude -p` and parses its stream |
| `server/internal/pcjr` | the build and run tools (MCP over stdio, `codejr mcp`) |
| `client/` | `codejr.c`, `cjp.c`, `pull.c`, `lz.c`, `serial.c`, `serisr.asm`, `screen.c`, `scrdump.c`; `test/` host harnesses |
| `tools/` | serial test tools: `cjterm.c` test terminal, `uartdump.c`, `hello.c` (`tools/build.sh`) |
| `lib/` | JRGFX graphics library; read `STATUS.md` and `FAST.md` |
| `apps/jrdoom`, `apps/jrflight` | JRDOOM and JRFLIGHT, each with README and NOTES |
| `apps/lemmjr`, `jumpjr`, `swords`, `mandel`, `basic` | programs CodeJr wrote on the PCjr. The working copies are the folders of the live workspace; `just save-projects` copies their sources here (run it and commit after a session that changed one), `just project NAME` builds one the way CodeJr's build tool does |
| `apps/jrai`, `apps/jrai2` | the "PCjr MEETS AI" demo and the story for demo 2 |
| `apps/say/` | `SAY.EXE` (`say.c`, `player.asm` with the ADPCM decoder), `mksnd.py` converter (`--quality high/medium/low`, `--codec`), `grid.py` quality grid, `clips/` (gipity mp3 + text), `test.sh` |
| `apps/jrquest` | Jr's Quest, a King's Quest style adventure on our own AGI v2 compatible engine. The whole 16-room game plays in a browser on the JavaScript reference interpreter (no 8088 code yet). Read `DESIGN.md`, `ENGINE.md`, `FORMATS.md`, `GAMEPLAN.md` there. `just quest`, `just quest-test`, `just quest-serve` (AI parser and live ending through `claude -p`), `just quest-shots`, `just quest-lint` |
| `apps/sopwith` | Sopwith 2 (1984) built from its released source with Open Watcom, faithful to the original, drawing in assembly. Runs on the real PCjr at 13.3 fps unlimited (the game wants 9.1). README there has the test mode (`@1000FP`), the real-machine numbers and the licence terms |
| `docs/sopwith/` | the Sopwith write-up for readers of the repository (`README.md`), with its screenshots off the real PCjr and three charts (`*.svg`, made from the real-PCjr numbers by a script; redo them by hand if the numbers move) |
| `apps/sopwith26` | Sopwith26: the same source as `apps/sopwith` built with `SOPWITH26` defined. Title screen with the keys, any key to play, no dead menu entries, Esc quits. Changes to looks or play go here, inside `#ifdef SOPWITH26`; pause (P, or Esc then Esc to quit), top ten in `SOP26.HI`, starting level, and frame skipping that holds the game's pace (2009 ticks for 1000 frames on the real PCjr). `apps/sopwith/SPEED.md` is the full record of the speed work, written to be published |
| `apps/jrcube` | JRCUBE, a shaded spinning cube with a moving light and real-time shadow (README there) |
| `emu/` | headless emulator scripts |
| `docs/attic/` | a parked selective-repeat link (didn't pass its tests) |
| `~/.local/share/codejr/workspace/` | Claude's folder (outside the repo), mirrors `C:\CODEJR\PROGS`; state in its `.codejr/` |

## Hard-won facts (do not rediscover these)

### This PCjr

- **Never raise RTS.** With RTS on, everything the PCjr receives is framing errors. The client keeps RTS off (`ser_rts = 0`).
- **The PCjr garbles what it receives while it transmits.** So: one frame in flight on the link (`SetWindow(1)`), and files are pulled in bursts while the PCjr only listens. The cause is probably the cable or the modem's RS-232 adapter; nobody has checked the hardware.
- **MS-Kermit only works in half duplex** (`SET DUPLEX HALF`), because that drops RTS while receiving. Half duplex also makes Kermit echo typing, so the server's echo is off for it (the `noecho` file in the workspace's `.codejr/`).
- **Serial port:** BIOS COM1 is at 2F8, IRQ3 (Kermit calls it COM2). The UART clock is 1.7895 MHz, so real speeds are 111861 / divisor: 9322, 18643, 37287, 55930. No FIFO. A serial line carries 10 bits per byte.
- **No BASIC cartridge.** `BASICA` won't run; GW-BASIC is on the disk. GW-BASIC leaves the PCjr palette scrambled; `RUN.BAT` runs `MODE CO80` afterwards.
- **jr-IDE Rev.B** card: disk, memory to 640K (JRCONFIG 3.10), and a DS12885 clock that was never set. `JRIDERTC` misparses options unless both groups are given: use `JRIDERTC /D /O`. The `AUTOEXEC.BAT` line still says `/D` and errors at boot.
- **Video:** only 16 KB of video memory is reserved (segment 1C00h). 320x200x16 needs 32 KB and the BIOS would put it over DOS memory; `lib` claims its own block. `INT 9` on the PCjr is a software interrupt with the scan code in AL.
- Addresses: PCjr modem 192.168.0.119, laptop 192.168.0.178 (docked ethernet), port 4860 (`ufw` allows the LAN).

### Running commands on the PCjr from here (learned 2026-10-07)

- **A command that waits for typing hangs the PCjr,** and nothing on the laptop can stop it: DOS runs one program at a time. A bare `DATE` did it on the first day of remote commands (Steve had to press Ctrl-Break). `session.Prompting` now refuses commands known to prompt (`DATE` and `TIME` alone, `PAUSE`, `MORE`, `COMP`, `DEL *.*`, `DIR /P`, `FORMAT`, editors, BASIC), trust or not, and the client feeds commands a file of Enter presses instead of NUL (with NUL, `DATE` loops on end-of-file forever). Before running anything new, ask: could it prompt?
- **After a run that ended without hanging up** (Ctrl-Break, a crash), the modem is still on the call and passes "AT" to the server as data, so the next `CODEJR` got no answer and Steve had to reboot. Zimodem's README: `+++` followed by a second with no other characters disconnects. From 1.0.21 the client does that at startup when the modem is silent at both speeds (untested on the real modem), ignores Ctrl-C and Ctrl-Break itself (they still stop a DOS command it started), and always releases the serial interrupt on exit.
- `TREE` isn't on this PCjr. Use `DIR` per folder.
- Pass commands with backslashes straight to `codejr ctl` or through the `just pcjr` recipes as fixed in 1.0.20 (they once dropped the backslashes: `DIR C:CODEJRPROGS`).
- The disk: 24.6 MB, 4.8 MB free on 2026-10-07. Files written before client 1.8 are all dated 1-01-80.
- The client's self-update failed twice on 2026-10-07 ("can't close file", then "can't rename CJPULL.$$$") with space to spare; cause unknown. 1.0.20 swaps files without a moment with neither copy and reports DOS's error number.

### Leaving a game (open: fix untested on the real PCjr)

- **After a JRGFX game exits, the DOS prompt can be garbled** (black with lines) until `MODE 80`. Steve saw it with LEMMJR, which uses the page-flipping path, and with earlier programs. DOSBox doesn't show it. `MODE 80` is only a second BIOS mode set, so `gfx_done()` now resets the gate array's latch (a read of 3DAh) and sets mode 3 twice, and palette writes leave the latch expecting an index. No root cause was proven. If it still happens, get a photo and check whether JRCONFIG (the jr-IDE memory driver) hooks INT 10h. Programs must be rebuilt to get the fix.

### The laptop's network (fixed 2026-10-05)

- **The first message of a call took about 50 seconds** (since day 1, on both paths). A packet capture showed why: the docked laptop is on the LAN twice, ethernet (192.168.0.178, the address the modem dials) and WiFi (192.168.0.156). Linux answers ARP for any of its addresses on any interface, so the modem sometimes sent to the WiFi card while the server answered over ethernet; after a few idle seconds the WiFi side swallowed the modem's packets until the modem asked again about 45 s later. Fix: `net.ipv4.conf.all.arp_ignore=1` and `arp_announce=2` in `/etc/sysctl.d/90-codejr-arp.conf`. Turning the laptop's WiFi off while docked also works. `tcpdump` is now installed for the next time (`sudo tcpdump -i any -n port 4860 -w /tmp/cj.pcap`).

### The WiFi modem (Zimodem)

Check its README or source before suggesting any AT command: https://github.com/bozimmerman/Zimodem

- `ATF` is **flow control**, not echo: F0 RTS/CTS, F1 XON/XOFF. Suggesting `ATF1` then `ATF0` cost hours. The saved setting is F1, and it must stay.
- `ATDT` means "dial with **telnet translation**" (it drops the NUL after a CR). Use `ATD`. Frames also escape CR, XON, XOFF and 0xFF.
- `ATB` accepts any rate from 128 to 921600 and takes effect at once. `AT&K0` is not saved by `AT&W`.
- `ATI1` shows current settings; `AT+CONFIG` is a menu. Saved: B9600 E0 Q0 V1 X1 F1 R1.
- The WiFi link stalls for a few hundred ms now and then. Any "silence means done" timeout must allow for it.

### MS-Kermit 3.14

- APC is off by default. `SET TERMINAL APC ON` allows `RECEIVE`, `SEND` and `SET`; `RUN` needs `UNCHECKED`.
- Paths in APC commands take a single backslash.
- Existing files are renamed, not overwritten, unless `SET FILE COLLISION OVERWRITE`.
- It erases with its own colors (`SET TERMINAL COLOR 0, 37, 40, 20` fixes the gray bands) and does not answer status queries.
- Its source is useful: `msk315src.zip` via the Wayback Machine (columbia.edu blocks scripts).

### Headless Claude

- The Agent SDK must not use a claude.ai login; the server runs `claude -p` instead.
- Flags that matter: `--restricted --strict-mcp-config --tools ... --permission-mode acceptEdits --permission-prompts none`, `--allowedTools WebSearch,WebFetch` (or web tools are silently denied), `--system-prompt-snapshot off` (or a resumed conversation keeps its old instructions).
- The PCjr's Claude remembers being told something was blocked; `/new` starts fresh.

### The emulator lies in specific ways

- DOSBox charges every instruction one cycle. A multiply costs the same as an add, and video memory has no wait states. Frame rates and "which trick wins" from DOSBox are rough. MartyPC (cycle-accurate, supports the PCjr) is the planned fix; nobody has checked that it runs headless here.
- DOSBox's serial link never stalls and never loses bytes. Three real bugs got through because of that. Timing code needs a test with a line that drops, flips and stalls (`TestPullInteropC` has one).
- DOSBox runs BIOS calls as native code. Writing six characters through INT 10h in a graphics mode cost 22.7 ms on the real PCjr and 3.3 ms in DOSBox; Sopwith's slowest frames were score writes. And DOSBox hands a program zeroed memory, so a read of something never set gives the same answer every run there and a different one each run on the PCjr.
- A timer read (PIT count plus BIOS ticks, interrupts off) steps back 55 ms when a tick is waiting to be counted: check the 8259's request register (`out 20h,0Ah`; `in 20h` bit 0), and treat a count of 0 as just restarted.
- DOSBox's DOS accepts things real DOS doesn't (a doubled backslash in a path).
- DOSBox 0.74 can't display the PCjr's 160x100 line-doubled mode.

## Mistakes made in this session

1. **Guessing AT commands from Hayes habits.** Read the firmware's docs or source first.
2. **Theorizing before counting.** The slow-link hunt went through four wrong causes (disk writes, pacing, byte loss, BIOS calls). What settled it each time was a counter on the PCjr side plus the server's frame timeline (`just link`). Add the counter first.
3. **Trusting the emulator for timing and for "which optimization wins".**
4. **Restarting the server without checking for a call.** `just restart` refuses during a call. Steve is fine with a forced restart, but say so.
5. **Assuming a stopped or failed command did nothing.** A multi-step edit script that fails partway has applied its earlier steps. Check the file.
6. **Predicting gains instead of measuring.** Compression was promised at about 2x and delivered 1.4x on the games.
7. **A resend scheme that resends the whole window** on a line that loses bytes resends forever. Count resends in tests.

## How Steve likes to work

His global `~/.claude/CLAUDE.md` applies. In practice:

- Short answers, the answer first. No em dashes anywhere, including code comments and docs.
- Never open a window on his screen. Run DOSBox with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.
- `sudo` isn't available to you. Give him the command to run with the `!` prefix (no leading spaces).
- Nothing outward (commits, pushes, posts) without asking. No AI attribution in commits.
- He tests on the real PCjr and sends phone photos of its screen. Give him exact things to type, few at a time.
- Say how long a slow command will take; he may interrupt a silent one.
- He likes parallel agents for big independent pieces (the library and both games were built that way, against a shared header).
- Keep `docs/JOURNAL.md` going: he wants the whole story, wrong turns included.

## What to build next

In the order I'd do it.

1. **Transfer speed:** client 0.7 prints where each transfer's time goes; get a real reading before changing anything. Candidates: bigger chunks (fewer round trips), asking for the next chunk while writing this one (only if the serial interrupt survives disk writes), a shorter `PULL_GAP`.
2. **Done on day 2, untested on the real PCjr:** the UI requests (0.6-0.7), project folders (0.8), build and run tools, run reports (0.9), changes-only transfers (1.0).
3. **Next:** a graphics screenshot of real-PCjr runs (the run report is text only); a general file upload from the PCjr (`/get` exists only on the Kermit path).
4. **Measure in real clock cycles:** get MartyPC running headless and re-benchmark the library and games. Retest the ideas DOSBox judged (table multiplies, delta copies, wide pixels).
5. **Games:**
   - JRFLIGHT is limited by math, not drawing. Ideas: mountains as a scrolling panorama, visibility by grid square, fewer multiplies, redraw the attitude indicator only where it changed.
   - JRDOOM: redraw only changed columns; try the 160x100 hardware mode.
   - A new trick to try: switch video pages partway down the screen so the status bar lives on a page drawn once.
6. **Transfer extras:** send only the changed parts of an edited file; LZSA2 instead of LZ4; get a window above 1 back on the chat link by acknowledging only when the line is quiet (the parked selective-repeat link is in `docs/attic/`).
7. **Housekeeping:** run `gw init` in `~/Github/CodeJr` (ask first); fix the `AUTOEXEC.BAT` clock line and set the clock; have someone look at the serial cable.
8. **Ideas Steve floated:** other "brains" behind the same server (OpenCode, a local model), and publishing CodeJr so other PCjr owners can run their own copy. A shared public server would need an API key, logins and per-user workspaces.
9. **40-column mode for the client** (Steve asked on 2026-10-06, then parked it): the big font, as a switch (`CODEJR /W:40`) so both can be compared on the real screen. What was learned looking at it:
   - The client's width is one constant, `COLS` in `client/screen.c`; the transcript ring, cursor math and row drawing all use it. It needs to become a variable, with BIOS mode 1 (40x25) instead of mode 3 in `scr_init` and `scr_done`.
   - The server word-wraps replies at 79 columns (`wrapWidth` in `server/internal/session/native.go`), so the client must report its width in its hello (say `cols=40`) and the server wrap at 39.
   - Written for 80 columns and needing short forms: the `/run` menu (three columns, `client/codejr.c`), the transfer report and Alt-S counter lines, the status lines with hints ("CodeJr is thinking  12s   (Ctrl-C stops)"), the welcome line (79 characters), the header's right-justified size, and `nativeHelp`.
   - CodeJr's instructions (`prompt.go`) may describe the screen as 80 columns: tell it the real width, so code listings and tables fit.
   - The cost: half the text per screen, so replies reach "-- More --" about twice as often, and 40-column lines of code wrap. The emulator test's screen checks (`emu/native_e2e.sh`) assume 80 columns and would need a 40-column run.

10. **A resident helper for typing at the DOS prompt** (Steve's idea to log, 2026-10-08; not started). Today keys can only be pressed for a program CodeJr starts (the timer hook lives in the client, which is gone once it exits). A small program that stays resident after CodeJr exits could keep the same timer hook and feed keys through INT 9 with no 15-key limit; if it also listened to the modem, the laptop could type at the PCjr's DOS prompt live and grab the screen: a remote console for the PCjr. Costs and risks: a few KB of memory, and it shares the timer and the serial port (IRQ3) with whatever runs next, so a program that takes those over would clash with it. Two smaller ways were weighed: stuffing up to 15 keys into the BIOS keyboard buffer as the client exits (about 20 lines, one burst only), and the launcher loop, which already runs a command left in a file and covers most needs.