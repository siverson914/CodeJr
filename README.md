# CodeJr

Claude Code for the IBM PCjr. Sit at the PCjr, type "write me a snake game", and Claude writes it straight onto the PCjr's own disk.

```
PCjr ──serial── WiFi modem ──TCP 4860── codejr (Go server on a modern machine)
                                          └─ claude -p (headless Claude Code)
```

The server answers the call, keeps the chat going, runs Claude in a workspace folder (`~/.local/share/codejr/workspace`), and copies every file Claude writes to `C:\CODEJR\PROGS` on the PCjr. There are two clients:

- **`CODEJR.EXE`** (in `client/`): a 25 KB DOS program that draws its own screen and pulls files in compressed bursts.
- **MS-Kermit 3.14**: needs nothing installed but Kermit. The server draws the screen with VT100 codes and starts Kermit transfers remotely. Slower, and the fallback that always works.

Also here: **JRGFX** (`lib/`), a 16-color graphics, input and sound library for the PCjr, and two games built on it: **JRDOOM** (`games/doom`), a Doom-style raycaster, and **JRFLIGHT** (`games/flight`), a filled-polygon flight simulator.

## Status

Day one of the project. The Kermit path and native chat are proven on a real PCjr. The fast file pull, the fast modem speed, the library and the games pass their tests in an emulated PCjr but are not yet proven on real hardware. `docs/HANDOFF.md` has the details.

## Running it

Everything routine is a `just` task; run `just` to list them.

```sh
just start      # build and start the server (port 4860; data and logs in ~/.local/share/codejr)
just status     # is it up, is the PCjr connected
just test       # all tests, about 6 minutes, no windows
```

Needs Go, C-Kermit and DOSBox for the tests, and Open Watcom v2 in `~/.local/opt/openwatcom` to build the DOS programs. Claude Code must be installed and logged in.

## First call from a PCjr

With MS-Kermit on the PCjr and the modem working:

```
C:\MSK314\KERMIT
SET PORT COM2
SET SPEED 9600
SET DUPLEX HALF
SET TERMINAL APC ON
CONNECT
ATD192.168.0.178:4860
```

Press Enter at `CodeJr READY`, then type `/setup`. That installs `CODEJR.BAT` in `C:\`. From then on type `CODEJR` at the DOS prompt. A `/sync` there fetches `CODEJR.EXE`, `NATIVE.BAT` and `FAST.BAT`; after that, run `NATIVE` (9600 baud) or `FAST` (37287 baud) from `C:\CODEJR\PROGS`.

The settings above are for one particular PCjr and Zimodem WiFi modem. The reasons for each are in `docs/HANDOFF.md`.

## In a session

Type a request and press Enter. Commands: `/files`, `/sync`, `/send NAME`, `/new`, `/quit`, `/help` (Kermit sessions also have `/get NAME`, `/setup` and `/echo`). To run a BASIC program on the PCjr: `RUN NAME` at the DOS prompt.

## Docs

- `docs/HANDOFF.md`: current state, what to test, what to build next, facts and mistakes.
- `docs/JOURNAL.md`: how it was built and debugged, step by step.
- `docs/PLAN.md`, `docs/CLIENT.md`: the original plans, with status notes.
- `lib/STATUS.md`, `lib/FAST.md`: the graphics library.
- `games/doom/README.md`, `games/flight/README.md`: the games.
