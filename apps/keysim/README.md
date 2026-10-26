# KEYSIM

A test, not a game. KEYSIM checks the JRKEYS key layer (`lib/jrkeys.h`,
the part games use to read held keys) against what the real PCjr keyboard
was measured to do. CodeJr wrote it on the PCjr side on 2026-10-10, while
Junris and Xonix Junior had keys "getting stuck".

`apps/keytest` had just recorded the real keyboard in two runs (731 scan
codes, one run under game conditions): it loses nothing, every press has
its release, and only the newest key held repeats (after 0.6 s, then every
96 ms). So a game should see a held key as held for as long as it is held,
whatever else is tapped meanwhile.

KEYSIM needs no one at the keyboard. It feeds the layer scripted scan
codes through `key_feed()`, runs its tick, and after every tick compares
what the layer says is down with what the "fingers" hold. The cases:

| | |
|---|---|
| A | hold Left for 150 ticks |
| B | hold Left with the keyboard repeating it, tap Up, keep holding Left (the repeats stop, as on the real keyboard) |
| C | hold Right, tap Left, keep holding Right |
| D | a tap that starts and ends between two ticks must show for one tick |
| E | roll from Left to Right: the newest key wins |
| F | the mash KEYTEST recorded: Right down, Left down, Right up, Left up, 40 times |

It prints the first bad tick of each run and how long it stayed wrong, and
ends with a count. The results go to the screen and to `LOG.TXT`; the exit
code is 1 if anything failed.

## Run

    apps/build.sh keysim
    lib/emu.sh /tmp/keysim 3000 apps/keysim/KEYSIM.EXE

In the emulator on 2026-10-25: `564 checks, 0 failed`. Nothing in it depends
on timing or the real keyboard, so the emulator result stands for the
PCjr. It was sent to the PCjr on 2026-10-10; no run there is logged.

`lib/test/tkeys.c` covers the same ground as part of the library's own
tests (`just test`). KEYSIM is kept as CodeJr wrote it.

The working copy is the `KEYSIM` folder of the live workspace
(`C:\CODEJR\PROGS\KEYSIM` on the PCjr); `just save-projects` copies its
source here.
