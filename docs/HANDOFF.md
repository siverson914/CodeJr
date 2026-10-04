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
| On the PCjr | MS-Kermit 3.14, started by `C:\CODEJR.BAT` | `CODEJR.EXE` (our own program, `client/`) |
| Screen | drawn by the server with VT100 codes | drawn by the client in video memory |
| Files | Kermit protocol, started with APC | pull protocol (bursts, compressed) |
| Speed measured on the real PCjr | 235 bytes/s | 413 bytes/s (old link); pull not yet measured |
| Status | works, proven, the fallback | chat works; file pull fixed but unproven |

## State at handoff

**Works on the real PCjr (seen in logs or photos):**

- Kermit path end to end: chat, file transfer, `/setup`, `/get`, cold boot to `CODEJR`.
- Native client: dialing, chat, colors, own screen.
- The one-frame-at-a-time link (window 1): 24,900 bytes in 60 s with one resend.
- Web search and fetch for the PCjr's Claude; date, time and location in its prompt.

**Works only in tests and the emulator so far:**

- The pull protocol's stall fix (client 0.3). The first real pulls (client 0.2) worked chunk after chunk, then failed when the WiFi link stalled mid-burst. The fix is in `client/pull.c` (`pull_fetch`) and `cjp.Burster`.
- `FAST.BAT` (modem and PCjr at 37287 baud). Never run on the real machine.
- The bps display.
- Both games and the whole graphics library. Nobody has played them with a real keyboard, heard the sound, or seen the palette on the real monitor.

**Queued for the PCjr** (`just queue`; sent on its next connection): `CODEJR.EXE` 0.3, `JRDOOM.EXE`, `JRFLIGHT.EXE` (the updated one), and new `NATIVE.BAT`, `FAST.BAT`, `RUN.BAT` without the address.

**Address and install (day 2):** the server's address lives only in `C:\CODEJR\CODEJR.CFG` (`DIAL=host:port`), written by `/setup` from the address the PCjr dialed; the launchers pass no `/D` (an explicit `NATIVE /D:host:port` still overrides). The server writes `NATIVE.BAT`, `FAST.BAT` and `RUN.BAT` into the workspace at startup (`ProgFiles` in `dosfiles.go`), and `/setup` ends with a sync, so it installs the native client too (files under 40 KB go over Kermit; the games wait for the native client). The client pulls into `CJPULL.$$$` and renames only when the file is whole, so a failed update keeps the old `CODEJR.EXE`.

**On the PCjr now:** `C:\CODEJR.BAT` and `C:\CODEJR\CODEJR.INI` (Kermit launcher, half duplex), `C:\MSK314\`, and in `C:\CODEJR\PROGS`: `CODEJR.EXE` 0.2, `NATIVE.BAT`, `FAST.BAT`, `RUN.BAT`, the first `JRFLIGHT.EXE`, a few BASIC programs, and test tools (`CJTERM`, `UARTDUMP`, `SCRDUMP`).

**The server is running** in the background (`just status`; `just restart` to rebuild and restart it), logging to `~/.local/share/codejr/logs/`. Nothing in the repo is committed to git yet.

## First things to test on the real PCjr

Ask Steve to do these in order and send the `[client]` line or a photo each time.

0. **Install over Kermit.** At `C:\` run `CODEJR`, Enter at READY, `/setup`. It installs `CODEJR.CFG`, then sends `CODEJR.EXE` 0.3 and the three launchers (about 2 minutes). `/quit`.
1. **Pull at 9600.** `CD \CODEJR\PROGS`, then `NATIVE`. It should dial with no address on the line (from `CODEJR.CFG`) and fetch both games. Expect roughly 7000-9000 bps. If it says there's no server to dial, `CODEJR.CFG` didn't land: `NATIVE /D:192.168.0.178:4860`.
2. **Fast speed.** Run `FAST`. Watch for "The modem didn't answer at 37287 baud" (it then stays at 9600 by itself). Note the bps and the damaged-block count. If many blocks are damaged, try `CODEJR /P:1 /F:18643 /D:192.168.0.178:4860`. If it is clean, try `/F:55930`.
3. **After `FAST` exits,** check the Kermit path still dials (`CD \`, `CODEJR`). If not, the modem stayed at the fast speed: a power cycle restores 9600 (the client never saves the speed).
4. **Run the games:** `JRDOOM` and `JRFLIGHT`. Check keyboard, sound, colors and speed. Both take `/BENCH`, `/PROF` and `/DEMO:n` and write `LOG.TXT`. To get a log back, use the Kermit path: `CODEJR` from `C:\`, then `/get LOG.TXT` (the native client can't upload yet).
5. **Risky graphics paths:** `JRFLIGHT /LD` (hardware 160x100 mode, never displayed anywhere), and 320x200 mode in `lib/DEMO.EXE` (this PCjr reserves only 16 KB of video memory; the library claims its own block).
6. **Typing in the native client while text arrives:** do letters get lost? Alt-S prints the client's loss counters into the chat.

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
| `client/` | `codejr.c`, `cjp.c`, `pull.c`, `lz.c`, `serial.c`, `serisr.asm`, `screen.c`; `cjterm.c` test terminal; `test/` host harnesses |
| `lib/` | JRGFX graphics library; read `STATUS.md` and `FAST.md` |
| `games/doom`, `games/flight` | JRDOOM and JRFLIGHT, each with README and NOTES |
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

1. **Prove the transfers** (the test list above), then tune: the silence timeout (`PULL_GAP`), the speed, block size.
2. **Steve's open UI requests for the native client** (asked for, not done):
   - Show his line in the chat the moment he presses Enter, and show the prompt only when he can type.
   - Put "CodeJr is thinking" in the chat as a line that is erased when the reply starts, not in the status bar. Say "CodeJr", not "Claude".
   - A program list inside the client: number the programs in `PROGS`, type a number to run one (through GW-BASIC for `.BAS`), and come back to the chat afterwards. BASIC programs should end with `SYSTEM` so they return.
3. **Close the loop from the plan** (`docs/CLIENT.md`, stages 5-8): file upload in the native client, `RUN` and `RESULT` (run a program on the PCjr, return its exit code, `LOG.TXT` and a screen snapshot), then tools for Claude (`pcjr_run`, `build`, `emu_run`) through a small MCP server.
4. **Measure in real clock cycles:** get MartyPC running headless and re-benchmark the library and games. Retest the ideas DOSBox judged (table multiplies, delta copies, wide pixels).
5. **Games:**
   - JRFLIGHT is limited by math, not drawing. Ideas: mountains as a scrolling panorama, visibility by grid square, fewer multiplies, redraw the attitude indicator only where it changed.
   - JRDOOM: redraw only changed columns; try the 160x100 hardware mode.
   - A new trick to try: switch video pages partway down the screen so the status bar lives on a page drawn once.
6. **Transfer extras:** send only the changed parts of an edited file; LZSA2 instead of LZ4; get a window above 1 back on the chat link by acknowledging only when the line is quiet (the parked selective-repeat link is in `docs/attic/`).
7. **Housekeeping:** once the native client is proven, rename the launchers: `CODEJR` should start the native client (trying the fast speed, falling back to 9600 by itself, which replaces `NATIVE` and `FAST`), and the Kermit path becomes `CJKERMIT`. Run `gw init` in `~/Github/CodeJr` (ask first); fix the `AUTOEXEC.BAT` clock line and set the clock; have someone look at the serial cable.
8. **Ideas Steve floated:** other "brains" behind the same server (OpenCode, a local model), and publishing CodeJr so other PCjr owners can run their own copy. A shared public server would need an API key, logins and per-user workspaces.
