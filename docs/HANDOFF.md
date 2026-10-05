# CodeJr handoff

Written 2026-10-03, at the end of the first working session. Read this first. `docs/JOURNAL.md` has the full story of how each thing was found; this file is what you need to continue.

## Where things live

- **Start sessions in `~/Github/CodeJr`** (the gw workspace root; it has its own `CLAUDE.md`). The repo is `~/Github/CodeJr/CodeJr`. Run `just` from inside the repo.
- **Live data is outside the repo,** in `~/.local/share/codejr/`: `workspace/` (Claude's folder for the PCjr) and `logs/`. Every checkout and gw worktree shares it. `CODEJR_HOME` overrides the location.
- **Git:** private repo `github.com/siverson914/CodeJr`, branch `main`. `gw init` hasn't been run in `~/Github/CodeJr` yet (ask first).

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
- `SAY.EXE` and its speech clips (`speech/`, README there): ElevenLabs speech (voice "Brian") through gipity, played on the SN76496's three voices combined (`.SND`). **SAY 1.0 played on the real PCjr:** the chip version sounded best, PWM (`.SNP`) whined, but the chip loop was late on 72% of samples (playing slow). SAY 1.1 polls the 8259 instead of taking an interrupt per sample and has a stopwatch (`/V`, `/T`); the poll loop can't run in DOSBox and is untested.

**Queued for the PCjr:** `CODEJR.EXE` 0.9 (0.6's setup files went over on day 2), plus the JRAI session's demo files.

**Launchers (day 2):** `C:\CODEJR.BAT` starts the native client (`C:\CODEJR\PROGS\CODEJR.EXE /L:1`) in a loop: `/run` writes `C:\CODEJR\RUNNEXT.BAT` and hangs up, the batch file runs it, waits for a key and calls back. If `CODEJR.EXE` is missing it falls back to Kermit. `C:\CJKERMIT.BAT` is the Kermit path. `/setup` (Kermit) installs both, plus `CODEJR.INI`, `CODEJR.TXT` and `CODEJR.CFG` (`DIAL=host:port`, the only copy of the address), then syncs. A native client can only write into `PROGS`, so the server keeps `SETUP.BAT` and `.BT` copies of both launchers in the workspace; `SETUP` copies them to `C:\`. Keep `CJKERMIT.BAT` unchanged between versions: `/setup` rewrites it while it runs, and DOS reads a batch file by position (that cost one confusing run on day 2). The client pulls into `CJPULL.$$$` and renames when whole, so a failed update keeps the old `CODEJR.EXE`.

**Client 0.6 UI:** it says `ui=1` in its HELLO and the server answers `CODEJR/2 server ui`. Then the client echoes typed lines itself, shows the `>` prompt only after the server's `Ready` message, and shows `Temp` lines ("CodeJr is thinking...") that the next text erases. Older clients get the old behavior. 0.7 keeps a line typed while CodeJr is busy (sent on `Ready`, with a status message) and turns the prompt's spot into a spinner while busy. The transfer line shows the line's real speed (bytes on the wire, so it can't exceed the baud rate); Alt-S shows the error counters and the last file's compression, damage and request counts.

**On the PCjr now:** `C:\CODEJR.BAT` (still the old Kermit launcher until `SETUP` runs), `C:\CODEJR\CODEJR.INI`, `C:\CODEJR\CODEJR.CFG`, `C:\MSK314\`, and in `C:\CODEJR\PROGS`: `CODEJR.EXE` 0.5 (if Steve synced), `NATIVE.BAT`, `FAST.BAT`, `RUN.BAT`, `JRDOOM.EXE`, `JRFLIGHT.EXE`, BASIC programs, test tools.

**The server is running** in the background (`just status`; `just restart` to rebuild and restart it), logging to `~/.local/share/codejr/logs/`.

**Client 0.8, project folders:** the workspace may hold one level of folders (`SNAKE/SNAKE.BAS` on the laptop is `SNAKE\SNAKE.BAS` on the PCjr). Clients that say `dirs=1` create the folder and receive them; Kermit and older clients skip them with a note. `/run` lists programs inside folders and finds them by number, file name or folder name. Claude is told to give each new program its own folder.

**Build and run tools (server):** `codejr mcp` is a small MCP server (`server/internal/pcjr`) that `claude -p` starts through `--mcp-config`. `build PROJECT` compiles the folder's `.C`/`.ASM` with Open Watcom (8088, large model; JRGFX's sources are linked in when a file includes `jrgfx.h`) into `PROJECT\PROJECT.EXE`, objects in a scratch folder. `run PROJECT PROGRAM` runs it in headless DOSBox and returns what it printed, `LOG.TXT`, the text screen (via `SCRDUMP.EXE`) and any `.PPM` screenshots as images. No shell. `JRGFX/JRGFX.H` is kept in the workspace so Claude can read the API. Tested with real `claude -p` in restricted mode: it built a JRGFX program, ran it and described the screenshot correctly. GW-BASIC isn't on the laptop, so BASIC can't run in the emulator.

**Client 0.9, run reports:** `/run` writes `RUNNEXT.BAT` so that after the program it notes a nonzero exit (`LASTERR.TXT`) and saves the text screen with `SCRDUMP` (`LASTSCR.TXT`) before anything clears it; BASIC now runs `GWBASIC` directly for that reason, then `MODE CO80`. On the next call the client sends a report (exit, screen, the program's `LOG.TXT` if written during the run) as `Result` messages; the server keeps it in `.codejr/lastrun.txt` and puts it in front of the next prompt, once. Only with servers that say `results`.

**Client 1.0, changes only:** the server keeps an exact copy of what it last sent of each file (`.codejr/have/`, seeded at startup from files the PCjr already has) and, for clients that say `delta=1`, sends an edit script instead of the whole file when the script is under 70% of the file's compressed size (`cjp.Delta`: copy ranges from the old file plus new bytes, found with a rolling hash, so shifted code still matches). The `DeltaOffer` carries the old copy's size and CRC; the client checks them, replays the script into `CJPULL.$$$` and checks the new file's size and CRC before replacing anything. If its copy differs it says so and the server sends the file whole. Measured on `JRFLIGHT.EXE` with a one-line code change: 45 KB of the 97 KB file changed, yet the script was 4.4 KB on the line instead of 64 KB.

## First things to test on the real PCjr

Ask Steve to do these in order and send the `[client]` line or a photo each time.

1. **Get 1.0:** connect with `CODEJR`; it downloads by itself (plus the JRAI files, about 3.5 minutes). Hang up and call again so 1.0 runs.
2. **The UI:** your line appears at once, a spinner turns where the prompt goes, "CodeJr is thinking..." shows until the reply. Type a line while it's busy: it should wait and go by itself.
3. **A C program, start to finish:** ask "write a bouncing ball in C with JRGFX in a BALL folder, test it, then I'll run it". Watch for `[builds BALL]` and `[tests BALL.EXE in the emulator]`, then `/run ball`.
4. **Run report:** after the program ends and CodeJr calls back, ask "how did that go?". CodeJr should know what was on the screen and whether it failed.
5. **Transfer timing:** after a file over 4 KB, the second line shows waiting, receiving, gap, unpacking and disk times. Send a photo of one for `JRAI.EXE` or a game.
6. **Changes only:** after CodeJr rebuilds a program you already have, the line should read "sent N bytes of changes".
7. **Run the games:** `JRDOOM` and `JRFLIGHT`. Check keyboard, sound, colors and speed. Both take `/BENCH`, `/PROF` and `/DEMO:n` and write `LOG.TXT`. To get a log back, use the Kermit path: `CJKERMIT`, then `/get LOG.TXT` (the native client can't upload yet).
8. **Risky graphics paths:** `JRFLIGHT /LD` (hardware 160x100 mode, never displayed anywhere), and 320x200 mode in `lib/DEMO.EXE` (this PCjr reserves only 16 KB of video memory; the library claims its own block).
9. **Typing in the native client while text arrives:** do letters get lost? Alt-S prints the client's loss counters into the chat.
10. **Speech timing.** SAY 1.1-1.4's `/T` froze the PCjr (power-off needed): never run `/T` from those versions. With SAY 1.5: `SAY /T HELLOH` (chipmunk; the line to note is "chip clips up to about N Hz should keep up here"), then `SAY /V HELLO` (9.3 kHz) and `SAY /V HELLO10` (10.65 kHz). Want: late 0 and a speed matching "wanted". Then re-make the clips at the highest rate with some margin below the `/T` figure (`mksnd.py --hz`, then make it the default in `METHODS`). If `SAY` hangs or makes no sound with polling but `SAY /H HELLO` works, the poll command misbehaves on this 8259: say so in the journal.
11. **Quality presets (SAY 1.3).** `SAY /V HELLOH` (high: raw 10.7 kHz), `SAY /V HELLOM` (medium: 4-bit ADPCM, 8 kHz), `SAY /V HELLOL` (low: 3-bit ADPCM, 6 kHz). Want late 0 on all three, especially high; note each "decoded ADPCM" time. If high is late, lower its rate in `QUALITY` in `speech/mksnd.py`.
12. **Tighter clips (SAY 1.6).** `REPLYV6`, `REPLYV5`, `REPLYV4`, `REPLYV3` (2-bit at 6/5/4/3 kHz, new pipeline) against the old `REPLY5`/`REPLY4`/`REPLY3`, and `REPLYL6` (3-bit, 6 kHz). Ask for the lowest rate that still sounds acceptable: that sets voice mode's preset.

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
| `client/` | `codejr.c`, `cjp.c`, `pull.c`, `lz.c`, `serial.c`, `serisr.asm`, `screen.c`; `cjterm.c` test terminal; `test/` host harnesses |
| `lib/` | JRGFX graphics library; read `STATUS.md` and `FAST.md` |
| `games/doom`, `games/flight` | JRDOOM and JRFLIGHT, each with README and NOTES |
| `speech/` | `SAY.EXE` (`say.c`, `player.asm` with the ADPCM decoder), `mksnd.py` converter (`--quality high/medium/low`, `--codec`), `grid.py` quality grid, `clips/` (gipity mp3 + text), `test.sh` |
| `demos/cube` | JRCUBE, a shaded spinning cube with a moving light and real-time shadow (README there) |
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
