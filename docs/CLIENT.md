# CodeJr native client plan

**Status (end of 2026-10-03).** This is the plan as written before the client existed. For the current state read `HANDOFF.md`.

- **Stages 1-4 are done:** toolchain, serial driver, protocol, chat and files. `CODEJR.EXE` is version 0.3.
- **Stages 5-8 are not started:** menus and file browser, run and report, tools for Claude, the live log.
- **What changed from this plan:**
  - RTS must stay off on this PCjr; the plan assumed raising it would help.
  - Frames gained a length byte, and CR is escaped along with FEND, FESC, XON, XOFF and 0xFF.
  - The link runs one frame at a time (the PCjr garbles what it receives while it transmits).
  - Files are not sent as `FILE`/`DATA`/`END` messages. The client pulls them in compressed bursts (`server/internal/cjp/pull.go`, `client/pull.c`), optionally at 37287 baud.
  - The receive interrupt handler is in assembly (`client/serisr.asm`).

Replace MS-Kermit on the PCjr with our own program, `CODEJR.EXE`, so the
server can do more than draw text: send files with real progress, run
programs on the PCjr, collect their logs and screenshots, and give Claude
tools to test on real hardware without anyone copying text back and forth.

MS-Kermit stays as the fallback: a caller without the client still gets
today's ANSI screen and Kermit transfers.

## Why

Most of the first day's trouble was fighting MS-Kermit, not the protocol:

- It only raises RTS in half duplex, and the WiFi modem won't send without RTS.
- APC is off by default, it renames files instead of overwriting, it doesn't
  answer status queries, and it drops characters while the PCjr is busy.
- It can't run programs for us (`RUN` needs APC UNCHECKED) or report back.

Our client sets the serial port up exactly as needed, and both ends speak a
protocol we designed for this.

## The loop it enables

You type "make a Jumpman-style game".

1. Claude writes it, compiles it on the laptop if it's C or assembly, and runs
   it in the emulator. It reads the log and a screenshot and fixes bugs.
2. The server sends it to the PCjr. The client shows a progress bar.
3. Claude asks the PCjr to run it (`pcjr_run`). It runs full screen on the
   real monitor, graphics and sound included, and you play.
4. While it runs, the program writes debug lines to `LOG.TXT`, or live over
   the link. When it exits, the client sends back the log, the exit code and
   a snapshot of the last screen.
5. Claude reads them and fixes what broke, and the loop repeats.

## Pieces

### 1. CODEJR.EXE (the PCjr client)

Written in C for the 8088, compiled on the laptop with Open Watcom v2 (small
memory model). Target size under 40 KB.

- **Serial driver:** talks to the UART directly (PCjr port at 0x2F8), with
  interrupt-driven receive into a ring buffer. It raises **DTR and RTS
  itself**, which fixes the modem problem without half-duplex tricks.
- **Modem:** dials with AT commands, recognizes CONNECT/NO CARRIER, and hangs
  up with `+++ATH`. Address and port live in `CODEJR.CFG`.
- **Screen:** writes video memory directly, 80x25 text in 16 colors.
  - Chat pane, input line, status bar.
  - Transfer panel with per-file progress bars.
  - Menus: Files (browse `C:\CODEJR\PROGS`, run, view, delete), Transfers,
    Settings, Help.
  - Fast enough to redraw instantly, with no escape codes over the wire.
- **Sound:** short chimes on the SN76496 (port 0xC0): reply done, file saved,
  error.
- **Run:** loads programs with DOS EXEC (INT 21h 4Bh), keeps its serial ISR
  installed, restores text mode afterwards, then reports back.
- **Snapshot:** after a program exits, before switching back to text mode,
  copies the video page and sends it compressed (RLE). The server turns it
  into a PNG for Claude.
- **Live log (later):** while a program runs, the client stays resident with
  a software interrupt (for example INT 60h) that programs call to send a
  debug line straight over the link. There's a tiny `CJLOG` helper for C,
  assembly and BASIC (BASIC can use `LOG.TXT`).

### 2. The protocol

Both ends are ours, so it's small and binary:

- Frames: `SOH len seq type payload CRC-16`, byte-stuffed so frame bytes
  never appear in the data. Every frame is acknowledged. Two frames may be in
  flight; retransmit after a timeout.
- Frames stay small (about 128 bytes): the PCjr's UART has no FIFO, and
  keyboard interrupts make it miss bytes.
- On connect the client sends `HELLO CODEJR/1 <version>`. If the server sees
  it, it switches to the native front end; otherwise it uses today's
  Kermit/ANSI one.

| Type | Direction | Meaning |
|---|---|---|
| `TEXT` | server to client | a chunk of Claude's reply |
| `STATUS` | server to client | status line ("Claude is thinking 12s") |
| `INPUT` | client to server | a line you typed |
| `KEY` | client to server | Ctrl-C and other control keys |
| `FILE` / `DATA` / `END` | both ways | file transfer with size, for progress bars |
| `RUN` | server to client | run a program: name, args, timeout, which log to return |
| `RESULT` | client to server | exit code, run time, log file, screen snapshot |
| `LOG` | client to server | a live debug line from a running program |
| `DIR` | both ways | list `C:\CODEJR\PROGS` with sizes and dates |
| `SOUND` | server to client | play a chime or a short tune |

### 3. Server

- **Front ends:** split today's `session` into the shared core (Claude turns,
  workspace sync, commands) and two front ends: `ansi` (Kermit, today's code)
  and `native` (the protocol above).
- **Tools for Claude:** a small MCP server built into `codejr`, given to
  `claude -p` with `--mcp-config`. These are narrow, purpose-built tools, not
  a general shell:
  - `pcjr_run(file, args, timeout)`: run on the real PCjr; returns exit code,
    log and snapshot.
  - `pcjr_get(file)` / `pcjr_dir()`: read files and listings from the PCjr.
  - `build(file)`: NASM (`cpu 8086`) or Open Watcom, returns compiler errors.
  - `emu_run(file, keys, seconds)`: run in DOSBox `machine=pcjr` headless;
    returns log and screenshot. MartyPC later, for exact timing.
- **Web:** turn on Claude Code's WebSearch and WebFetch tools (read-only), for
  docs, old PCjr manuals and news.
- **Workspace:** move it out of the git repo (to `~/.local/share/codejr/`),
  so Claude Code doesn't tell the PCjr's Claude about the repo.

### 4. Logging convention

Programs Claude writes follow one format, so logs can be read back reliably:

```
T=1234 GAME START LEVEL=1
T=1290 KEY LEFT X=12 Y=5
T=2011 ERROR SPRITE OFFSCREEN X=330
```

`T` is ticks (18.2 per second). The system prompt teaches it, and the BASIC,
C and assembly helpers write it.

## Stages

1. **Toolchain:** install Open Watcom v2 and NASM. A hello-world `.EXE` built
   here runs in DOSBox `machine=pcjr`.
2. **Serial proof:** a minimal client: serial driver, dial, dumb terminal.
   Proves that raising RTS ourselves fixes the modem issue in full duplex,
   tested on the real PCjr.
3. **Protocol:** both ends, with tests: a Go implementation plus the C client
   in the emulator, over a noisy simulated line, like the Kermit tests.
4. **Chat and files:** the native front end. Chat pane, input, status, file
   transfers with progress. `/setup` installs `CODEJR.EXE`, sent over Kermit
   once.
5. **UI:** menus, file browser, transfers panel, settings, chimes.
6. **Run and report:** `RUN`/`RESULT`, logs, screen snapshots. The
   `pcjr_run` tool, and Claude's test-on-the-PCjr loop.
7. **Emulator and build tools:** `build` and `emu_run` for Claude, with
   screenshots.
8. **Live log:** the resident INT 60h channel and the `CJLOG` helpers.

Quick wins that don't need the client, doable now: web search and fetch, and
moving the workspace out of the repo.

## Open questions

- **Port:** COM1 vs COM2 on this PCjr. Kermit warned "unknown address for
  port, assuming 02F8"; the client will use 0x2F8 directly.
- **Speed:** whether 9600 holds up for transfers with our own ISR, or 4800 is
  safer.
- **Memory:** how much RAM the client leaves for programs it runs (it stays
  resident during EXEC); it needs to be small.
