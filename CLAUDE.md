# CodeJr

Claude Code for a real IBM PCjr: a Go server on this laptop, a native DOS client (and an MS-Kermit fallback) on the PCjr, a graphics library and two games.

**Read `docs/HANDOFF.md` before doing anything.** It has the current state, what is proven on the real machine and what isn't, the facts that took hours to find, and the mistakes not to repeat. `docs/JOURNAL.md` is the full history; append to it as you work (Steve wants the whole story, wrong turns included).

## Rules that are easy to break here

- Run `just` (from this repo folder) to see every routine task. Live data (Claude's workspace for the PCjr, logs) is in `~/.local/share/codejr/`, not in the repo. `just status` before touching the server; `just restart` refuses while the PCjr is on a call.
- Never open a window on Steve's screen. DOSBox runs with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` (the `just emu-*` tasks and `lib/emu.sh` do this).
- Before suggesting any modem AT command, check the Zimodem README or source. `ATF` is flow control and `ATDT` is telnet translation on this modem.
- On the PCjr: RTS stays off, one frame in flight, files go in bursts while the PCjr only listens. See "Hard-won facts" in the handoff.
- The emulator is not the PCjr: its timing is wrong and its serial line never stalls or loses bytes. Say which results are emulator-only.
- Add a counter on both ends before theorizing about a slow or broken link (`just link`, the client's Alt-S).
- Every PCjr program (the client, games, demos, tools) answers input at once: show the key, show that Enter was taken, and keep a spinner or progress moving while it waits or works. Queue input that has to wait; never drop it silently.
- In the PCjr C code, every `sprintf`, `strcpy` or `memcpy` into a fixed buffer needs its worst case counted: cap strings with `%.Ns`, size buffers with room to spare, and treat lengths from the server, the keyboard and the disk as hostile. An audit on 2026-10-07 found five overflows that tests never showed (a long file name in the progress line, a folder named `BACKUP.OLD`, a transfer across midnight).
- 8088 only (`-0`), no floating point, warnings are errors (`-wx`). NASM isn't installed; use `wasm`.
- No em dashes anywhere, including comments and docs. No AI attribution in commits. Ask before committing or pushing.
- `just test` (about 6 minutes) before saying something works; say so when a result is untested.
