# CodeJr journal

How CodeJr got built, step by step, including what broke and how each problem was found. Newest entries go at the bottom. Times are Pacific.

---

## Day 0: the PCjr comes back to life (2026-10-02)

**The SD card wouldn't show up.** The laptop's card reader had dropped the card mid-write a few minutes earlier, leaving the FAT filesystem marked dirty. Reinserting it brought it back. A `fsck.vfat` repair (after closing Nautilus, which was holding the mount open) cleared it. The card turned out to hold a 23.5 MB PC DOS 3.30 partition: the PCjr's boot disk.

**Boot files.** Added `CONFIG.SYS` (`DEVICE=JRCONFIG.SYS`, `DEVICE=ANSI.SYS`) and `AUTOEXEC.BAT` (a blue `PROMPT $e[1;44m$p$g`, `JRIDERTC /D`, `PATH=C:\`, `VER`).

- JRCONFIG 3.10, the PCjr memory driver, came from Mike Brutman's PCjr archive.
- `JRIDERTC` was the jr-IDE's clock utility (`RTC.COM` from the jr-IDE project, renamed). At the time we didn't know the PCjr had a jr-IDE.
- `ANSI.SYS` was added because the prompt's color codes need it.

**First Kermit session.** MS-Kermit 3.14 was already on the card (`C:\MSK314`). Typing `AT` came back `AATT` or `AAATTT`: several things were echoing at once (Kermit's local echo, the modem's command echo, and the modem's online echo).

## Day 1: the idea (2026-10-03, morning)

**The idea:** a "mini Claude Code" for the PCjr. You type a request at the PCjr; a server on the laptop runs Claude; Claude's replies show on the PCjr, and the files it writes land on the PCjr's own disk.

**Research.** The closest existing projects:

- An Apple II terminal for the real `claude` CLI over a WiFi modem. Files stay on the modern machine.
- Nick Bild's PCjr GPT-3 (Kermit plus a Raspberry Pi). Chat only.
- AiBridgeMCP Nano, an ESP32 bridge for 8-bit machines.

None of them put the AI's files onto the vintage machine's own disk and run them there.

**Decisions:**

- **Name:** CodeJr (7 characters, ends in JR).
- **Port:** 4860, the PCjr's IBM machine type.
- **Server language:** Go, because the user wanted to learn a compiled language.
- **Claude:** headless Claude Code (`claude -p`) on the user's subscription, after checking the docs: the Agent SDK's docs disallow claude.ai logins.
- **Version 1 needs nothing on the PCjr but MS-Kermit.** The server sends Kermit's APC escape sequence (`ESC _ receive ESC \`) to make Kermit start a file transfer by itself.

**The modem's line endings.** `OK` printed on top of `AT`: the modem ended its replies with a bare carriage return. The user found `ATR1` (CR+LF line endings) and the working Kermit settings themselves, using another Claude chat on their phone: COM2, 9600 8N1, no flow control, local echo off, `ATE0 ATR1 AT&W`.

## Day 1: building the server (2026-10-03, about 08:00-09:00)

**Kermit protocol, written in Go** rather than driving C-Kermit through a pty, since the server already owns the socket.

- Short packets, block checks 1 and 3 (CRC-16), control and 8th-bit prefixing.
- Tests: CRC check values, round trips, a simulated noisy line, and interop against the real C-Kermit in both directions.
- The C-Kermit interop test caught that C-Kermit auto-switches to text mode, so the tests force binary.
- The noisy-line test later caught a real bug: if the final ACK is lost, the sender gave up even though every file had arrived.

**Screen.** VT100 escape codes only, 7-bit ASCII:

- a header, a scroll region for the transcript, a divider, an input line and a status bar
- word wrap at 79 columns, so the terminal never auto-wraps
- Unicode transliterated to ASCII
- output paced to the line speed

**Emulator.** DOSBox in PCjr mode, running the real MS-Kermit 3.14 (fetched from the Wayback Machine, because columbia.edu blocks scripted downloads), with its serial port wired straight to the server over TCP. The first full run: the emulated PCjr asked for a snake game, Claude wrote `SNAKE.BAS` in 33 s, and Kermit saved it to the emulated C: drive in 4 s.

- A DOSBox window popped up on the user's screen during a test. All later emulator tests run with `SDL_VIDEODRIVER=dummy`, so no window is created at all.

**`/setup`.** Installs the launcher (`CODEJR.BAT`) and Kermit settings (`CODEJR.INI`) over the connection, so the SD card never has to come downstairs.

## Day 1: first calls from the real PCjr (2026-10-03, 08:50-09:40)

**Chat worked on the first call.** Every file transfer failed with Kermit's "Unable to create file". Kermit's own transfer screen showed `CODEJR.BAT as C:\\CODEJR.BAT`: MS-Kermit 3.14 takes backslashes literally, so the doubled backslash I sent stayed doubled. DOSBox's DOS had tolerated it, which is why the emulator didn't catch it. **Fix:** a single backslash.

**Gray bands behind the text.** MS-Kermit erases and scrolls with its own default colors, not the current ones, as real DEC terminals did. **Fix:** the server sends `SET TERMINAL COLOR 0, 37, 40, 20` through APC, and every color code sets its background explicitly.

**Scrambled screen after transfers.** The PCjr needs a moment after a transfer to close the file and restore Kermit's screen, and anything sent meanwhile is lost.

- I tried asking the terminal "are you back?" (Device Attributes, then a status report). MS-Kermit didn't answer reliably.
- **Fix:** a fixed 1.5 s pause after every transfer.

**Dropped characters ("VE" instead of "STEVE").** The PCjr's UART has no FIFO, and keyboard interrupts make it miss bytes. **Fix:** a short pause after Enter, and extra time allowed for each line feed, because scrolling is slow on a 4.77 MHz machine.

**Programs folder.** At the user's suggestion, programs moved to `C:\CODEJR\PROGS`. Kermit can't create folders, so the launcher does.

**Redial over an open call.** Leaving Kermit without `/quit` keeps the modem connected, so a second `CODEJR` sent its `ATDT` down the old call as chat text. **Fix:** the server recognizes an `ATD...` line, answers `CONNECT` and starts a fresh screen. `/quit` now also tells Kermit to `EXIT` through APC.

## Day 1: the modem saga (2026-10-03, 09:00-12:30)

This took the longest, and most of it was self-inflicted.

1. **To fix the doubled typing,** I suggested `ATF1`, which on a Hayes modem turns off online echo. **But the modem runs Zimodem**, where `ATF` sets **flow control**: F0 is RTS/CTS, F1 is XON/XOFF.
2. **Kermit's script never saw `CONNECT`,** so it reported "No answer" while the server had in fact answered. **Fix:** the server now repeats `CodeJr READY - press Enter` until Enter arrives, and the script waits for `READY` instead of the modem's own wording.
3. **Data stopped reaching the PCjr.** The server's TCP send queue piled up, while the PCjr's typing still got through. When I suggested `ATF0` to "turn flow control off", it was actually RTS/CTS, which made it worse. **Lesson: read the firmware's README before suggesting any AT command.**
4. **`AT&K0` (no flow control) worked, but `AT&W` doesn't save it,** so it came back after every power cycle.
5. **The modem's own settings (`ATI1`, `AT+CONFIG`) were fine all along:** 9600, F1, E0, R1.
6. **The real clue came from the user:** the modem only talked back when Kermit was set to `SET DUPLEX HALF`. In MS-Kermit, half duplex means "local echo plus RTS/CTS handshake". **Workaround:** `CODEJR.INI` uses half duplex. That made Kermit echo typing locally, so the server's echo was turned off (`/echo off`).

**Cold boot worked** at about 12:40: power on, type `CODEJR`, and you're talking to Claude.

## Day 1: the native client, stage 1-2 (2026-10-03, 12:40 onward)

**Why.** Most of the day went into fighting MS-Kermit's defaults (APC off, renaming instead of overwriting, RTS only in half duplex, no status answers). The plan in `docs/CLIENT.md` replaces Kermit with our own PCjr program, plus tools for Claude to build, run and test programs on the real PCjr.

**Smaller fixes along the way:**

- **Web search and fetch.** They were enabled for the PCjr's Claude, but each use was silently denied: headless mode can't ask for approval. **Fix:** `--allowedTools WebSearch,WebFetch`.
- **Old instructions.** Claude kept naming the old folder, because a resumed conversation reuses its original system prompt. **Fix:** `--system-prompt-snapshot off`.
- **Renamed files.** `/setup` files got renamed when Kermit was started by hand: MS-Kermit's default renames instead of overwriting. **Fix:** every APC `receive` is preceded by `set file type binary, set file collision overwrite`.
- **The clock.** The PCjr does have a jr-IDE Rev.B, with a DS12885 real-time clock. Its clock had never been set. `JRIDERTC` also misparses its options unless both groups are given, so the boot line is `JRIDERTC /D /O`.

**Stage 1, toolchain.** Open Watcom v2, a prebuilt Linux snapshot in `~/.local/opt/openwatcom` with no root needed. A hello-world `.EXE` built on the laptop reports "machine ID FD (PCjr)" in DOSBox.

**Stage 2, CJTERM**, a test terminal with our own interrupt-driven serial driver. The debugging went like this:

1. **Hung in the emulator.** The status line overflowed an 81-byte buffer. Fixed.
2. **Real PCjr, first run.** Mostly blank screen and `rx:1`. I suspected the baud rate, but the user had simply forgotten the parameters. From then on, tests ship with batch files (`CJTEST.BAT`).
3. **Real PCjr, with dialing.** The call connected and 589 bytes arrived with 0 lost, but almost nothing displayed. I swapped hand-drawn BIOS output for plain DOS output (the user's suggestion), which also gives ANSI colors through `ANSI.SYS`.
4. **Still nothing readable.** `RX.LOG`, fetched back with `/get`, showed all 629 received bytes were `0x00`.
5. **Error counters added.** Every byte was a framing error plus a break, in both interrupt and polled mode. So the interrupt code is fine, and the receive line itself looks wrong.
6. **MS-Kermit's source** (`msxibm.asm`) uses the same divisor (12 for 9600) and the same register sequence, so the speed setting isn't it.
7. **`UARTDUMP` run right after Kermit:** divisor 12, LCR 03, **MCR 01 (DTR on, RTS off)**. `CJTERM` reusing Kermit's exact speed and format (`/K:1`) still got framing errors. So the remaining difference is RTS.
8. **Kermit's source again:** in half duplex it raises RTS only while sending a line, and drops it after the Enter goes out. **So Kermit always receives with RTS off.** `CJTERM` held RTS on.

**Current theory:** with RTS asserted, something (the cable or the modem's adapter) corrupts the PCjr's receive line. That explains every symptom: full duplex (RTS never raised) received nothing, Kermit's half duplex (RTS pulsed while sending) worked, and RTS held on gave framing errors.

**Next test:** `RTSTEST.BAT` runs `CJTERM` with RTS always off (`/R:0`), then RTS only while sending (`/R:2`).

**Result, 13:43: RTS off works.** Run 1 (`/R:0`) chatted with the server in plain full duplex through our own driver: "this is from rts test", "its outputting below the status line". Root cause confirmed: **on this PCjr, raising RTS corrupts the data it receives from the WiFi modem.** Kermit's half duplex only worked because it drops RTS while receiving. `CJTERM` now defaults to RTS off.

The odd placement (text below the status line) is because `ANSI.SYS` doesn't support the scroll region (DECSTBM) the server uses for Kermit's screen. Expected in a test terminal. The native client draws its own screen.

**Stage 2 done.** Next: Stage 3, the CodeJr protocol (framing, CRC, acknowledgements) in Go and C, tested over a noisy simulated line and in the emulator.

## Day 1: Stage 3, the CodeJr protocol and the native client (2026-10-03, 13:45-14:10)

**Protocol (CJP/1).**

- **Frames:** `FEND type seq ack len payload CRC-16 FEND`, with SLIP-style escaping of FEND, FESC, XON, XOFF and 0xFF. XON/XOFF are escaped because the modem uses XON/XOFF flow control; 0xFF because of telnet.
- **Reliability:** up to 4 frames in flight, cumulative acknowledgements, go-back-N resends on timeout. The PCjr acknowledges a frame only after handling it, so a slow disk write slows the server down.
- **Handshake:** the client waits for the server's `READY` banner and sends a `HELLO` frame. The server spots the frame's first byte and switches to the native front end; anyone sending a plain Enter still gets the Kermit screen.
- **Implementations:** Go in `server/internal/cjp`, C in `client/cjp.c`. The C file is plain portable C, so it builds both for the PCjr and on the laptop for tests.

**Bugs the tests caught:**

1. **Link deadlock.** The Go link's reader blocked whenever nobody was reading incoming messages, so it stopped processing acknowledgements and the other direction stalled. This would have happened whenever the server was busy sending a file. **Fix:** incoming messages go into an unbounded queue.
2. **CRC miss on a dropped byte.** The C-versus-Go interop test, run over a line that drops and flips bytes, saw one dropped byte slip past the 16-bit CRC: a 1-in-65,536 event that turned up because the test damages hundreds of frames per run. **Fix:** a length byte in every frame, so a dropped or extra byte always fails. A new test drops each byte of a frame in turn and checks that every one is rejected.
3. **The telnet filter** threw away NUL bytes, which the protocol uses (sequence 0, ack frames). **Fix:** NULs now pass.
4. **A stale test** still expected the old APC text from before the "binary, overwrite" receive settings.

**CODEJR.EXE (21 KB)** draws its own 80x25 screen straight into video memory: a header, a 21-line transcript, a divider, the input line and a status bar. It keeps RTS off, dials, waits for `READY`, says `HELLO`, chats, and receives files with a progress bar on the status line. Each file is written to the current directory, and only then acknowledged with a result. In the emulator, Claude's file arrived in **200 ms**; Kermit took seconds.

**Testing (`./test.sh` runs it all):**

- **Go unit tests:** Kermit, protocol, link, screen, text, pacing, wrapper.
- **Go integration tests:** the full Kermit session and the full native session against fake clients, covering the handshake, turns, transfers, a refused file, commands and hang-up.
- **Interop tests:** C-Kermit against our Kermit, and the C client protocol against the Go protocol over clean and noisy lines.
- **Client build:** both `.EXE`s with all warnings as errors.
- **Headless emulator runs** of the MS-Kermit path and the native path, checking the files that arrive and the client's own screen capture.

**First real run of CODEJR.EXE (14:10): it worked, but chat text crawled.** Kermit had been faster. Cause: the server sent every streamed piece of Claude's reply (a few characters each) as its own frame, and each frame needed a window slot and an acknowledgement through the PCjr and the modem. The line sat mostly idle. **Fix:** the server collects text into full frames (sent every 100 ms, when a frame fills, or before anything else goes out), and its window went from 4 to 8 frames. Each side picks its own window, so the client didn't change. A new test checks that 60 streamed pieces arrive in 5 frames (it was 61).

**Polish from the first real native session (14:15).**

- **Lines ran together:** Claude's replies don't end with a newline, so the next prompt or command was printed on the same line ("try /help./new", "Paris.> what time is this"). **Fix:** the native front end finishes the current line before echoing input and after every reply. There's a test for it.
- **Date, time and location:** Claude said it "can't see a clock". The current date and time now go into the system prompt on every turn (not once at startup, where it would go stale), plus the PCjr's location from a new `-location` setting ("Encinitas, CA 92024"). Looking up a location from the IP address wasn't practical: the PCjr's address is a private LAN one, and geolocating the home's public IP would mean sending it to a third party.

**Slow transfers on the real PCjr (14:18).** `HELLO.BAS`, 42 bytes, took 15 s over the native protocol. **Suspected cause:** while the PCjr creates or writes a file on its SD card, it can miss serial input; the lost frames then wait out the 2 s resend timer, several times over. **Fix:** the client sends XOFF to the modem (which uses XON/XOFF flow control) before any disk work, waits for the line to go quiet, keeps any frames that arrive meanwhile in a small queue, does the disk work, then sends XON. Both decoders ignore raw XON/XOFF, which matters in the emulator, where there's no modem to swallow them. To check the theory, the server now logs resends and bad frames per transfer and per call, and the client shows bytes lost, bad frames and resends after each file and on Alt-S.

**No BASIC cartridge.** `BASICA HELLO` said the cartridge is required: PC DOS's `BASICA.COM` on a PCjr is only a stub for the cartridge. GW-BASIC is on the card and works, including writing files. Claude's instructions now target standard GW-BASIC (no cartridge-only PCjr extras) and say to run programs with `GWBASIC NAME`; the user is adding GW-BASIC's folder to `PATH`. GW-BASIC showed odd colors (grey on brown), probably because it doesn't set the PCjr's color palette. Still open.

**Protocol work parked (14:50).** The client's own counters (Alt-S) showed the real cause of the slowness: **the PCjr loses received bytes**, about 80 in a short session, spoiling 11 frames. Kermit lost bytes at the same rate; it just didn't notice ("VE" for "STEVE"). Every spoiled frame then waited out a timer. A test modeling a receiver that loses 1 byte in 300 reproduced it: 13 KB took 40 s. Ideas in progress: fast resend on duplicate acks, then selective repeat with Naks and 64-byte frames. The selective-repeat rewrite of `server/internal/cjp/link.go` doesn't pass its tests yet, so it is not deployed; the live server runs the previous build. The other open question is 2400 baud instead of 9600: the PCjr would have 4 ms per character instead of 1 ms to service its serial interrupt, which should stop the losses outright.

## Day 1: a graphics library and two games, built by three agents in parallel (2026-10-03, 14:55)

**The goal:** a reusable PCjr graphics library, plus two games to exercise it: a Doom-style shooter and a flight simulator that should beat Microsoft Flight Simulator 1.0 on the same class of hardware.

**How the work is split:** to let three agents work at once, the library's interface was written first as a shared contract, `lib/jrgfx.h`:

- **Graphics:** 160x200 and 320x200 in 16 colors, drawn into a back buffer in fast RAM, then copied to the PCjr's slow shared video memory once per frame. Lines, filled triangles and polygons, sprites, text, palette, vsync.
- **Math:** 16.16 fixed point with sin/cos tables (the 8088 has no FPU).
- **Input, time, sound:** raw keyboard state, a millisecond timer, and the SN76496's 3 voices plus noise.
- **Testing:** a debug log, and a PPM screenshot of the back buffer.

Each agent owns one folder (`lib/`, `games/doom/`, `games/flight/`) and tests headless in the emulated PCjr, looking at the screenshots it takes. The games code against the header and use temporary local versions of anything the library hasn't delivered yet; the library agent keeps `lib/STATUS.md` current for the other two.

**The library is done (15:20).** Every function in `jrgfx.h` is implemented, tested headless, and builds with all warnings as errors. Hardware facts were checked against the IBM PCjr Technical Reference, the IBM BIOS listing and the DOSBox source, and three of them changed the design:

- **B800 only reaches the first 16 KB of video memory,** so 320x200 mode (32 KB) is written through its real segment.
- **320x200 mode can overwrite DOS memory.** This PCjr reserves only 16 KB of video memory (JRCONFIG says 1C00h), but the BIOS puts the 32 KB mode at 1800h. The library claims a free 32 KB block instead and moves the video page there.
- **The PCjr's INT 9 is a software interrupt,** with the scan code in AL. The keyboard handler reads AL and passes it on to the BIOS.

**Biggest cost per frame:** copying the back buffer to the slow shared video memory takes about 183 ms in 160x200 mode (DOSBox estimate, roughly 6 fps before drawing anything), so the games draw small views and copy only changed rows. An assembly textured-column routine was added for the raycaster. I re-ran the primitive test and the demo headless myself; the screenshot shows every primitive drawn correctly.

**The real cause of the slow native protocol: `ATDT` (15:45).** The investigation, in order:

1. **The server's frame timeline at 2400 baud:** the PCjr accepted the first frame of each burst and rejected those right behind it. Each resend got exactly one more frame through.
2. **The PCjr's own counters:** **0 bytes lost** in its serial port, yet **49 bad frames**. So nothing was dropped on arrival; bytes were removed before they ever reached the PCjr.
3. **The Zimodem README:** in Zimodem, the `T` in `ATDT` doesn't mean tone dialing. It means **"translate this connection as TELNET"**, and telnet drops a NUL byte that follows a CR. Binary frames contain CR NUL fairly often (sequence and ack numbers, lengths, checksums), so frames arrived a byte short and failed their checks. Kermit's packets are printable text, so Kermit never hit it.

**Fixes:**

- **Server:** escapes CR in every frame, which helps the client already on the PCjr, since its decoder un-escapes any escaped byte.
- **New client:** escapes CR too, and dials with `ATD` instead of `ATDT`.
- **On the way, the client** no longer calls the BIOS on every frame. The cursor is set directly in the CRTC, and the keyboard check reads the BIOS buffer pointers instead of calling INT 16h. Loss counters (late-interrupt overruns, framing errors, bad frames) now go into the chat history as client messages.

**Misreads along the way:** I first blamed disk writes (the XOFF fix), then the pacer delaying acks, then genuine byte loss (selective repeat, now parked in the scratchpad), then BIOS calls. The client counters plus the README settled it. **Lesson:** count on both ends before theorizing.

**The actual mechanism (16:05): the PCjr corrupts what it receives while it transmits.** The CR fix didn't help. A closer reading of the timeline did: the PCjr decoded most frames fine (each duplicate ack means "a good frame, but out of order"). What failed was always **the frame right after one it had just accepted**, which is exactly when the PCjr sends its ack. So anything arriving while the PCjr transmits is damaged.

That explains the whole day:

- holding RTS on garbled everything;
- Kermit only worked in half duplex;
- letters went missing while typing ("VE");
- full-window bursts lost the frame that followed each ack.

It points at the cable or the modem's RS-232 adapter coupling the PCjr's outgoing lines into its receive line.

- **Immediate fix (server only):** the native link sends one frame at a time (window 1), so the PCjr only ever answers into a quiet line.
- **Next (client):** hold acks until the line goes quiet, then acknowledge a whole burst, to get the 8-frame window back.
- **Worth checking in hardware:** the serial cable and adapter wiring.

**JRFLIGHT is done (16:10).**

- **Picture:** 160x200 in 16 colors. A 160x120 windshield over an 80-line instrument panel: airspeed, altimeter, attitude indicator, heading tape, vertical speed, throttle.
- **Scenery:** 56 objects. Two airports, the town of Juniper, a lake and river, roads, farm fields, and five snow-capped mountains.
- **Rendering and physics:** all 16-bit integer math, with dithered filled polygons. Gauges only redraw the area their needle touched. Flight physics include stall, takeoff roll, climb and crashes.
- **Speed in the emulator:** 3 to 5 fps at a rough 8088 speed, so about 2 to 4 fps expected on a real PCjr: comparable to or better than MSFS 1.0, with filled 16-color graphics instead of wireframe.
- **Bypassing the library:** it skips lib's drawing and present entirely. It draws straight into the PCjr's packed 4-bit format, so the view reaches video memory in 15 to 23 ms instead of about 110 ms. That confirms the turbo library's main direction, and its requests (a packed drawing mode, the row table, a cheaper timer) were passed to the turbo agent.

**The turbo library (16:20).** A new fast path (`fx_*`) draws straight into the hidden video page in the PCjr's own 4-bit format. The vertical-retrace interrupt (IRQ5) flips pages through port 3DFh, so there is no present copy at all. Highlights:

- one-pass raycaster columns that never need clearing;
- byte-wide polygon spans (REP STOSW) with free dithering;
- compiled sprites (the sprite becomes generated 8088 code, 8x faster);
- palette cycling applied at the next retrace;
- a cheap tick timer;
- tear-free double buffering for the games' existing copy code, by adding just `fx_begin`/`fx_flip` around it.

**Speed:** the raycaster demo went from 1.2 to 5.0 fps in DOSBox (7.9 in low detail). The estimate for a real PCjr is 16-23 fps for a 160x100 low-detail view.

**Tried and dropped:**

- **Delta presents** look good in DOSBox but cost more than a plain copy on a real 8088: DOSBox undercharges repeated compares.
- **Quarter-square multiply tables** and **compiled scalers** were evaluated and rejected.
- **Trixter's 160x100 line-doubled mode** is understood but untested: DOSBox can't display it.

**Testing:** a new pixel-exact test (T3) compares every fast primitive against the old API. The library build is now test-gated: it builds under a temporary name, runs the tests headless, and only then replaces the library the games link against. I re-ran T3 (0 failures) and the fast demo myself. Both games were asked to switch to the fast path.

**JRFLIGHT on the fast path, plus speed options (16:55).**

- **New options:** window sizes (full 160x120, medium 128x96, small 96x72 with a cockpit frame), wide 2-pixel columns, line doubling, a one-key `/FAST` preset, and Trixter's 160x100 hardware line-doubling mode as an untested `/LD` switch.
- **Page flipping works:** each saved frame was checked pixel for pixel against what's on screen after the flip. That check also caught an older bug: one panel row (194) was never copied to video.
- **The gains were modest:** 3.1 to 3.7 fps by default, and 3.7 to 6.0 with `/FAST` (DOSBox at cycles=400). The flight sim is **CPU-bound** rather than fill-bound. In the small low-detail view, culling 55 objects takes about 53 ms per frame, transforming about 140 vertices with 8088 multiplies about 77 ms, filling about 54 ms, and the attitude indicator about 30 ms. Removing the copy only saved 15-23 ms of a 200-330 ms frame.
- **Expected on a real PCjr:** about 2-3 fps by default and 3-4 fps with `/FAST`.

**JRDOOM is done (17:00).**

- **The level:** a 32x32 map with 7 rooms in 6 wall textures, sliding doors, a red-key locked door and an exit switch.
- **Enemies and weapons:** zombiemen and imps with simple AI; a pistol and a shotgun; health, ammo and key pickups.
- **Doom feel:** a Doom-style status bar with a face, damage and muzzle flashes done through the palette, a bobbing weapon drawn as a compiled sprite, and SN76496 sound effects. View size and detail keys work like Doom's.
- **Renderer:** rays are cast in assembly every 4th column, and the columns between are filled when both ends see the same wall. Wall textures are stored as pre-shaded color runs and drawn by jumping into unrolled store code. Sprites use the wall heights as the depth buffer. The view is drawn straight into the hidden page.
- **Speed (DOSBox at cycles=400):** 9.3 fps at full view in low detail, up to 14-17 fps in the small view (it was 2.4 fps in the first working version). Its own assembly wall drawer beat the library's `fx_wall` (45 ms against 104-193 ms).
- **Testing:** an autopilot (`/DEMO`) plays the whole level to the exit switch at every view size and detail level.

## Day 1: burst file transfer (2026-10-03, 17:30-18:20)

**Why.** Files crawled over the native link (116 bytes in 28 s on the real PCjr; Kermit manages about 235 bytes/s). Two facts shaped the fix: the PCjr garbles what it receives while it transmits, and its UART has no FIFO, so an interrupt per byte can't keep up with fast input while the program also does other work.

**Design: the client pulls.**

- The server offers a file (`FileOffer`), cut into 4089-byte chunks, each compressed on its own.
- For each chunk the client sends one small `Pull` request (chunk number plus a 32-bit map of the blocks it still needs) and then **only listens**: it turns the UART interrupt off and polls the chip in a tight loop, storing raw bytes. Nothing is decoded until the line is quiet.
- The server answers with the requested 128-byte blocks back to back, each one framed and checksummed. It keeps no state; every request is answered the same way.
- Blocks the chunk doesn't have are sent as two bare frame markers, so a burst always has exactly two markers per requested block. The client stops listening by counting markers instead of waiting for silence.
- The client then checks every block, asks again for only the damaged ones, unpacks the chunk, checks a CRC of the unpacked bytes, and writes to disk while the line is silent.

**Speed.** The PCjr's UART clock is 1.7895 MHz, so its true speeds are 111861 divided by a whole number: 9322, 18643, 37287, 55930. Zimodem's source (`doBaudCommand`) accepts any rate from 128 to 921600, so the client can ask the modem for exactly 37287 (`ATB37287`), check that it still answers `AT`, and put it back to 9600 when the call ends. It never saves the setting, so a power cycle always restores 9600. The same source confirmed that `ATD` with no `T` dials with no telnet translation.

**Other pieces.**

- **Compression:** LZ4 block format. The compressor is in Go (hash chains, lazy matching); the decoder is about 50 lines of C. Measured on real files: the games shrink to 65-71%, the client itself to 88%, BASIC source to about 70%. Chunk size barely matters (71% at 4 KB against 65% for the whole file). LZSA2 would do somewhat better; LZ4 was chosen for the tiny, table-free decoder.
- **Table-driven CRC-16** on the PCjr: the bit-by-bit version would have cost about 0.4 s per 4 KB, as much as receiving it.
- **Assembly interrupt handler** (`client/serisr.asm`): saves four registers instead of all of them, roughly 50 µs per byte instead of 170, so chat frames survive at 37287 baud.
- **Server pacing** follows the speed the client reports, and no longer adds scrolling time for line feeds in binary data.
- **Kermit sessions** now leave files over 40 KB for the native client.

**Bugs the tests caught.**

- A line typed while a file was going over was thrown away by the server. The emulator test types its line early and hit it. Now such messages wait and run after the transfer.
- The client's speed change started with a bare Enter, which in the emulator (no modem in between) started a Kermit session on the server.

**Testing.**

- Go unit tests for the compressor (round trips, every length from 0 to 300, 2000 corrupted inputs, real files) and for chunking and bursts.
- The PCjr's own C code (`pull.c`, `lz.c`, `cjp.c`), built for the host, fetching JRDOOM.EXE from the Go server over a line that drops and flips 0.4% of bytes in both directions: intact, 129 requests for 22 chunks. On a clean line: 22 requests, 70 KB on the wire for the 88.5 KB file.
- A fake version-2 client against the real session code.
- The real `CODEJR.EXE` in the emulated PCjr at both speeds, pulling a 25 KB binary and comparing it byte for byte: **890 bytes/s at 9600 and 2,992 bytes/s at 37287** (emulator figures).

**Not yet known:** how the real PCjr, cable and modem behave at 37287. The client reports bytes/s, requests and damaged blocks after every file, so the first real transfer will say. **Not built yet:** sending only the changed parts of an edited file.

**First real pull transfers, and the stall bug (19:45-20:10).**

- **The one-frame-at-a-time link works on the real PCjr.** The old client fetched the new `CODEJR.EXE` (24,900 bytes) in 60 s, 413 bytes/s, with a single resend. That confirms the "garbles what it receives while it transmits" theory, and already beats Kermit's 235 bytes/s.
- **Pulls at 9600 worked chunk after chunk** (a 4 KB chunk every 4.6-5 s, one request each), then failed with "no answer from the server" on both games.
- **Cause, from the server's frame timeline:** partway through a burst the WiFi link stalled for longer than the client's 0.17 s silence limit. The client took that for the end of the burst and asked for the missing blocks. The server finished the old burst first and then sent the new answer behind it. From then on every request was answered by data queued behind earlier answers, the client's marker counting stopped on stale frames, and after 20 rounds without progress it gave up.
- **Fixes:**
  1. The client waits 0.5 s of silence before deciding a burst is over.
  2. After any burst that didn't bring everything, the client first takes whatever is still arriving until the line is quiet, and only then asks again.
  3. The server abandons a burst it is still writing when a new request arrives (`Burster`), so answers can't queue up.
- **The fetch loop moved into `pull.c`** (`pull_fetch`), so the host test runs exactly the code the PCjr runs. The interop test now includes a line that stalls for 0.7 s every 2.5 KB, with and without noise: JRDOOM arrives intact with 42 requests for 22 chunks (74 with noise).
- **Speed in chat:** the progress bar and the end-of-transfer lines (the client's own and the server's "Saved ...", Kermit sessions too, both directions) show bps counted as the line counts them, 10 bits per byte, so the figure compares directly with the baud rate. Compressed files can come out above it.

**Lesson:** the emulator's link never stalls. Timing assumptions need a test with a hostile line, not just a noisy one.

## Day 1: wrap-up (2026-10-03, 21:00)

- **bps, not kbps.** Transfer speeds are shown as bits per second counted the way the line counts them, 10 per byte (8 data bits plus start and stop), so a perfect transfer at 9600 baud reads 9600. Counted at 8 bits it would top out at 7680 and look 20% short.
- **`just` tasks** replace ad hoc commands: `just status`, `restart` (refuses during a call), `log`, `link`, `transcript`, `queue`, `test`, `deploy-client`, `deploy-games` and more. Live data moved out of the repo to `~/.local/share/codejr/` (`workspace/` and `logs/`), so the server uses the same files from any checkout or gw worktree. Sessions start in `~/Github/CodeJr`, the gw workspace root, which has its own `CLAUDE.md`.
- **Docs for whoever continues:** `docs/HANDOFF.md` (state, test list, next steps, hard-won facts, mistakes), a `CLAUDE.md` at the repo root so new sessions load the rules, a rewritten README, and status notes on the two plan files.

**Where day 1 ended.** Proven on the real PCjr: the Kermit path, cold boot to `CODEJR`, native chat, the one-frame-at-a-time link at 413 bytes/s. Passing in tests only: the pull protocol's stall fix, the 37287 baud switch, the graphics library and both games. Waiting in the queue for the PCjr: client 0.3, JRDOOM and JRFLIGHT.

**The lessons, in one place:**

1. Read the modem firmware's documentation before suggesting AT commands (`ATF`, `ATDT`).
2. Put counters on both ends before theorizing; four wrong causes were chased before the PCjr's own counters settled it.
3. The emulator's timing is wrong and its serial line is perfect. Test timing code against a line that drops, flips and stalls, and label emulator-only results.
4. Check for a call before restarting the server.
5. A stopped or failed multi-step command may have applied its first steps. Look at the files.
6. Measure before promising a gain.

## Day 2: the repo, one address, a safer install (2026-10-03, evening)

**Git.** The repo went into a private GitHub repo, `siverson914/CodeJr`. The first commit is the state at the end of day 1.

**Why "NATIVE".** Steve asked why the launcher is called that. It was a dev label for "the native client, not Kermit", and the naming is backwards: `CODEJR` at `C:\` starts the old Kermit fallback while the real client hides behind `NATIVE` and `FAST`. The plan is to flip them once the native client's file pull is proven on the real PCjr, since Kermit is still the repair path.

**One address.** The laptop's address was typed into every launcher. Now `/setup` writes it once into `C:\CODEJR\CODEJR.CFG` (the client already read `DIAL=` from there), taken from the address the PCjr dialed, and the launchers carry no address. The Kermit settings file gets the same value in the same `/setup`.

**`/setup` installs the native client.** The server writes `NATIVE.BAT`, `FAST.BAT` and `RUN.BAT` into the workspace at startup, `just start` copies `CODEJR.EXE` into a fresh workspace, and `/setup` finishes with a sync. `CODEJR.EXE` (25 KB) is under the 40 KB Kermit limit, so the 0.3 client now reaches the PCjr over the proven Kermit path instead of the unproven pull.

**A failed update no longer deletes the client.** The client used to write a pulled file straight over the old one and delete it on failure, so a stalled pull of `CODEJR.EXE` left no client at all. It now pulls into `CJPULL.$$$` and renames at the end. The emulator test now pre-places an old copy and checks that it gets replaced, that the launchers arrive without an address, and that no temporary file is left. The native test's "exactly two transfers" check broke when the launchers joined the queue; it now counts the two files by name.

**Speeds, for the record.** 9600 is the modem's saved, normal speed. 2400 was tried once during the slow-link hunt and wasn't the fix (the real cause was `ATDT` dropping bytes). `FAST` is 37287 and hasn't run on the real PCjr.

All tests pass (`just test`, emulator results are emulator-only). Deployed: client 0.3 with the rename fix, server restarted with no call in progress.

## JRAI: "PCjr MEETS AI", a demo (2026-10-03, night)

Steve asked for 80s/90s demoscene effects on the PCjr, picked plasma, fire, tunnel and a sine scroller, and asked for it "beyond epic" with an AI theme: AI faces, robots, a PCjr meeting AI. It's in `games/jrai/` (README there has the details). A separate session was building a spinning cube demo in `demos/cube/` at the same time.

**The shape.** Boot in 40-column text: `C>CODEJR`, Steve's request typed in, "CodeJr is thinking", and the thinking is real: the precompute runs under it. Then a robot voice says "Hello, P C Junior" over a palette plasma, the title, fire with 1984 / 2026 / PCjr / + AI burning in it, a tunnel "into the neural net", the PCjr meeting a robot (the robot speaks its first line, they talk in a speech box, a heart rises, "friends, forever" spoken), and a sine scroller finale with raster bars and greetings (8088 MPH, Area 5150, riq, Mike Brutman, IBM Boca Raton, and Steve).

**Sound.** A small tracker in the retrace interrupt, chained after the library's IRQ5 handler: lead with vibrato, bass, 60 Hz chord arpeggios, noise drums. The voice is SAM (1982) via the `sam-js` npm package, stored as 4-bit attenuations and played through tone voice 0's volume register with the tone at 111 kHz, paced by the PIT counter. No TTS was installed; SAM is also the right voice for a 1984 machine.

**Wrong turns.**

- The plasma came out black. It was computed before graphics started, and `jr_rowoff` (the library's row offsets) is all zeros until `gfx_init`, so every row landed on row 0. The prep now computes offsets itself.
- Parts drifted from the music by one pattern. The song position moves to the next pattern on the last tick of the old one, but the "pattern started at" time was only set when the new pattern's first row played, one tick later. A part starting in that tick took the previous pattern's start time. Found by logging pattern starts in a frame-locked run.
- First benchmark at DOSBox `cycles=400`: the fire ran at 1.5 fps. It called a 32-bit `rnd()` (a long multiply) for every burning letter cell and walked the letter bitmaps bit by bit. Now: a list of cell offsets, a noise table, the simulation in `#pragma aux` assembly (two cells per word add: heat 0-63 means a sum of four fits in a byte, so there is no carry between cells) with `XLAT` cooling, and output that skips cell pairs whose shades the page already shows. 8.8 fps.
- Meeting 8 fps to 41 by redrawing only what changed on each page. Finale 7.4 to 18.8 by filling each row once (bar color or black) instead of clear-then-bars, and a precomputed column mask for the scroller. Startup precompute 42 s to 8.7 s: texture built a row at a time from templates, tunnel geometry computed for one quadrant and mirrored, 16-bit math, table-driven plasma colors, code assembled a row at a time in a near buffer.

**The tunnel writes its own code**: one 14-byte instruction group per pair of screen bytes, about 43 KB, patched each frame with the offset (BX) and the target page (ES). 17 fps in DOSBox at 400 cycles, unchanged by everything else.

**A real-PCjr risk found while writing it.** This PCjr reserves only one 16 KB video page. An 86 KB program loads low, probably right where the library would claim a second page, so the library may fall back to one page and a RAM buffer, and it only copies regions its own `fx_*` calls touched. Most of JRAI's drawing doesn't go through `fx_*` (generated code, assembly, direct writes). So with one page JRAI marks the whole screen dirty each frame. Tested with `/ONEPAGE` in DOSBox. JRDOOM and JRFLIGHT may have the same problem with their direct writes; not checked.

**A preview video.** `/VIDEO` runs frame-locked, saves every frame and logs every SN76496 register write with its time; `render.py` emulates the chip (square waves box-filtered against aliasing, unipolar output so the speech trick works) and `video.sh` muxes an MP4 (`just jrai-video`). That is how anyone can hear it without a PCjr: DOSBox runs with the dummy audio driver here.

Emulator-only so far: everything. Nobody has run JRAI on the real PCjr or heard its sound chip play it.

## JRCUBE and a faster library (2026-10-03, night)

Steve asked for 80s-style demos that show off the PCjr: a spinning cube that casts a shadow, colorful and high-tech, with the light circling it and moving the shadow, and to improve the graphics library as needed. It's in `demos/cube/` (README there); the library changes are in `lib/jrfast.c`. The JRAI session was running at the same time.

**The scene.** A synthwave sky with stars and neon mountains, a magenta grid floor that scrolls toward you, a cube tumbling on three axes with neon edges, an orb circling it (glow, a two-spark trail, a pool of light on the floor under it), the cube's shadow on the grid, a chrome scroller, palette fades in and out.

**New library calls**, each because the demo needed it:

- `fx_track` / `fx_restore`: every fx_ call records the bytes it drew, per page and per row, and the next frame on that page copies just those back from a background buffer. No clear, no full copy.
- `fx_poly_rows`: one pattern byte per row. With a light value interpolated down the face and a 2x2 ordered dither between ramp colors, each face gets 13 shades from 4 colors at the price of a flat fill.
- `fx_poly_src`: a polygon copied from another buffer. The shadow is copied from a darkened copy of the floor, so the grid shows through it, dimmer.
- `fx_pal_anim`: palette rotation done by the retrace IRQ. The floor's horizontal lines are drawn 4 times as dense as they look, in 4 palette entries with one lit; rotating them scrolls the floor at a steady pace with nothing redrawn. The shadowed floor has its own 4 entries, rotated in step.
- `fx_line` got asm inner loops (pixel = old ^ ((old ^ color) & mask), the mask toggles F0h/0Fh), clipped sprites plot directly, and `fx_compile` generates each variant once.

All verified by new T3 checks (pixel compares against gfx_fill_poly coverage, and "restore gives back the background byte for byte on both pages after 5 frames of everything"). Both games rebuilt against the new library and produce the same `/DEMO:150` results as before.

**Wrong turns.**

- The first cube was a flat sheet in some frames. It wasn't the math: the chrome ramp went through blue, the floor's color, so dark faces vanished. Neon edges fixed the outline, and the ramp dropped blue.
- The distant grid stopped halfway to the horizon. The projection truncated toward zero, so two lines on either side of the view axis landed on the same row and the "stop when lines merge" test fired early. Fixed with floor division and a higher-precision projection, then tuned so lines stop where the lit ones get 3 rows apart, with a dithered haze beyond.
- The asm line hung T3. Its parameter block was read through DS, but inside the library DS isn't DGROUP (the compiler itself reads statics through SS). The other new loops already used SS; the line loop now does too.
- `far` is a Watcom keyword; a variable named `far` gave a page of errors.
- Startup took 5.5 s in DOSBox at 400 cycles, 3.2 s of it in the sprite builder: two 32-bit divides per sprite pixel in the demo, and `fx_compile` generating every variant twice through a per-pixel accessor. The whole startup is now 3.2 s.
- Found by the JRAI session's notes: with one video page the library only copies what fx_ calls touched, and JRCUBE's scroller and first background are direct writes. They now call `fx_dirty`; `/ONEPAGE` tests it (8.2 fps).

**Speed** (DOSBox `cycles=fixed 400`, emulator only): 5.2 fps for the first version, 10.2 now (12.7 with the edges off; E toggles them). The big steps: 16-bit IMUL/IDIV helpers and a table square root (math 40 ms to 24), multiply-then-shift-left instead of 32-bit shifts (24 to 11), restore and extent tracking in asm (restore 26 ms to 10), the line loop in asm (edges 32 to 20, then 15), and the math moved in front of `fx_begin` so it overlaps the wait for the last flip.

Emulator-only so far: everything. Not run on the real PCjr.

**First real run of the day-2 install (9:52 pm).** `/setup` over Kermit worked: launcher, settings, then `CODEJR.EXE` 0.3 and the three launchers in 1 m 50 s. Two problems:

- **The games vanished from the queue.** The Kermit sync skipped them as too big for Kermit but still recorded them as delivered. Old bug, newly hit because `/setup` now ends with a sync. Fixed: a skipped file keeps its place in the queue.
- **`NATIVE` needed the address typed in.** `/setup` replaced `CODEJR.BAT` while it was running; DOS reads a batch file by position, so it resumed partway into the new file (an extra 15 s Kermit call at 9:55 pm) and probably skipped the lines that move `CODEJR.CFG` into `C:\CODEJR`. Client 0.4 also looks in `C:\CODEJR.CFG`. The emulator test now dials from that file with no `/D`.

**`FAST` on the real PCjr, for the first time:** the client said `baud=37287` and chat worked (1 frame resent by the server). The PCjr's Alt-S counters climbed to 53 framing errors and 16 bad frames in a short chat, so 37287 is noisy on this line. No file pull at that speed yet. Also seen: the Alt-S line printed in the middle of a reply line; it should wait for the line to end.

## SAY: a real voice for the PCjr (2026-10-03, night)

Steve asked what the best speech ever done on a PCjr was, and whether we could do better. The best period answer is IBM's PCjr Speech Attachment, an add-on with a speech chip. Without one, the known trick is the SN76496 volume register used as a DAC, which is what JRAI's SAM voice does. He picked the plan: a modern voice made on the laptop (ElevenLabs through gipity), shaped hard for the PCjr, and two players to compare by ear. "Make it awesome." It's in `speech/` (README there).

**What got built.**

- `mksnd.py` turns any audio, or text via `gipity generate speech`, into `.SND` (all three SN76496 tone voices at period 1, summed: up to 256 of the ~800 level combinations, one byte per sample, 9.3 kHz) and `.SNP` (PWM on PIT channel 2, 15.9 kHz). It also writes preview WAVs of what each should sound like.
- `SAY.EXE` plays them. The player is copied into a "last fit" DOS block above 128 KB (code, stack, tables, samples) and paced by IRQ0 at the sample rate with `STI; HLT; CLI`, so each write lands a fixed number of clocks after the tick. A key stops it.
- `just clip`, `just speech`, `just deploy-speech`, and `speech/test.sh` in `just test`.

**Wrong turns.**

- The first emulator run hung. The linker had put the player's segment at offset 680h of a code frame it shared with the C code, so every internal address was off by 680h once the code was copied to offset 0 of the new block. A group of its own (`PLAYGRP`) fixed it. Found by printing the far address of `sp_begin`.
- 11 kHz for the chip method was the plan, and DOSBox played it fine. Counting real 8088 clocks said otherwise: 4 clocks per code byte, about 150 clocks to take the interrupt, and each SN76496 write holds the CPU in READY wait states for about 43 clocks (32 of the chip's clocks; the fact was already in `lib/STATUS.md`). Three writes per sample came to about 466 clocks against a 432-clock budget. The loop went from 55 bytes to 40 (`LODS` with ES, `LOOP`, the 64K and keyboard checks every 256 samples only), and the rate dropped to 9.3 kHz (512 clocks). PWM writes have no wait state; its loop is 22 bytes at 15.9 kHz. Both are estimates. `SAY /V` counts samples whose tick arrived before the loop was ready (the 8259's request register, read before each `HLT`), so the real PCjr will say.
- The first level table picked the 256 combinations closest to an even grid: 39.5 dB speech-band SNR on a perfect chip. A simulation with each 2 dB step off by 0.25 dB dropped it to 27 dB, because neighboring levels used unrelated voice settings and their errors became noise. The table now weighs each combination's spread under a step-error model: 37 dB perfect, 30 dB at 0.25 dB, 23 dB at 0.5 dB.
- Noise shaping at full strength made the chip method worse in the speech band (37.5 dB against 39.0 without); half strength measured best (39.5). PWM, with only 72 levels, gains from second-order shaping.

**How it sounds, as far as a laptop can tell.** Whisper (small.en) transcribes all ten previews (five clips, both methods) word for word. The old one-voice 4-bit method, applied to the same HELLO clip: "Hello to you! I'm Cor, and I'm talking to you from an IBM TV union."

**Open on the real PCjr:** whether period 1 really reads as a steady level on this chip (JRAI depends on it too), how exact its volume steps are, whether the PWM carrier whines, and the late counts. A 6-second clip is 56 KB (chip) or 95 KB (PWM), minutes at 413 bytes/s.

Emulator-only so far: playback (DOSBox, 0 late samples at 1000 cycles, 0 and 1 at 400). The quality numbers are simulations assuming ideal hardware unless stated.

**Speed trials on the real PCjr (10:21-10:33 pm).** `JRFLIGHT.EXE` (94.6 KB), sent again at each speed:

| Speed | Time | Requests | Bytes on the line |
|---|---|---|---|
| 37287 | 36.1 s | 24 for 24 chunks | 68,625 |
| 27965 | 42.8 s | 24 | 68,625 |
| 55930 | 49.4 s | 81 | 87,392 |

At 55930 the PCjr lost 56 bytes to late interrupts and 221 blocks arrived damaged, so it was slower than 37287. 27965 was perfectly clean and still slower. The old notes listed only even divisors (9322, 18643, 37287, 55930); divisors 4 and 5 (27965, 22372) are valid too. 37287 became the client's default in 0.5.

Two misreadings fixed on the way. The transfer line counted uncompressed file bytes, so it showed 9606 bps on a 9600 line; it now shows the line's real speed. And "damaged blocks" climbed even when no chunk was fetched twice: it was counting stray noise bytes between frames (it tracked the framing errors). A fragment under 6 bytes no longer counts. The client also printed a counter line every time a counter changed, which flooded the chat at 37287; the counters now show only on Alt-S.

One more mix-up worth recording: asked to run `CODEJR /P:1 /F:55930`, Steve typed it at `C:\`, where `CODEJR` is the Kermit launcher, which ignored the option and showed 9600. That settled the rename.

**Client 0.6: the rename and the UI requests.** `C:\CODEJR.BAT` is now the native client in a loop, and `C:\CJKERMIT.BAT` the Kermit path. `/run` lists the programs in `PROGS` with numbers; a number writes `C:\CODEJR\RUNNEXT.BAT` and hangs up, and the batch file runs it, waits for a key and dials back (BASIC goes through `RUN.BAT`; Claude is now told to end programs with `SYSTEM`). The client shows a typed line the moment Enter is pressed, hides the prompt until the server says `Ready`, and shows "CodeJr is thinking..." as a line the reply replaces. "Claude" became "CodeJr" in what the PCjr shows. A native client can only write into `PROGS`, so `SETUP.BAT` (sent with the client) copies the launchers into `C:\`. The `/T` test option now types through the keyboard path, so the emulator test covers the echo and `/run`.

**SAY on the real PCjr, and SAY 1.1 (10:40-11:05 pm).** Steve played both HELLO clips. "Chip version is best. The other sounds good but has ton of high pitched noise." So the period-1 trick works on this PCjr (good news for JRAI too), and PWM's 15.9 kHz carrier is audible on this monitor. His photo of `SAY /V` settled the timing:

- Chip: late on about 40,950 of 56,592 samples (72%), three runs alike. PWM: 17,338 of 96,583 (18%).
- So my clock estimates were short by roughly 10-20%. The PWM loop's own code is under 100 clocks, which means taking the timer interrupt and running the handler costs about 200 on the real machine, much more than the 150 I assumed. When the loop runs late it just plays slower, so the chip clip came out somewhat slow and low.
- Both blocks landed high (A98F and 9FCB): this PCjr has memory up to 736 KB.

**SAY 1.1.**

- The chip loop polls the 8259 instead of taking an interrupt: OCW3 0Ch, then a read of port 20h (bit 7 = tick waiting; the read acknowledges it), then EOI. Interrupts stay off. It should save about 100 clocks per sample.
- PIT channel 2, unused by the chip method, is a stopwatch read every 256 samples. `/V` now prints the rate reached and the clocks per sample.
- `/T` plays with a 50 kHz tick, so the loop runs flat out and reports the fastest rate this machine can play.
- `/H` keeps the 1.0 interrupt loop.
- mksnd makes chip clips only by default, and `--hz` sets the rate.

**Wrong turns.**

- Skipping writes to unchanged voices looked promising until the simulation said the voices already change only 1.7 times per sample. On a bus where every code byte is 4 clocks, the compare code eats the savings.
- The first DOSBox run of the poll loop exited at once: "PIC: poll command not handled". DOSBox 0.74 doesn't implement the 8259 poll command. Hence `/H`: the emulator test plays chip clips with the interrupt loop, so **the poll loop is real-PCjr only, and untested**. The stopwatch agreed with the clip's rate to 0.1% in DOSBox (9,329 Hz for 9,321).
- Splicing the new loops into `player.asm` matched `c_key:` instead of `key:` and duplicated the old tail. The assembler's "already defined" errors caught it.

Queued for the PCjr: SAY 1.1 and HELLO10.SND (HELLO at 10,650 Hz). HELLO.SND is already there.

**Voice mode groundwork: ADPCM and the quality grid (11:15 pm-11:45 pm).** Steve asked for compressed audio decoded on the PCjr, with lower rates and bit depths tried, aiming at a voice mode where the PCjr's Claude speaks its replies "semi-fast".

- **MP3 is out:** decoding it in real time took about a 486 at 66 MHz, and the PCjr is a few hundred times slower. General-purpose compression barely helps (zlib 1.4x, xz 1.6x), because the noise-shaped samples are deliberately noisy. So: lossy, and simple enough for an 8088. That means ADPCM.
- **`speech/grid.py`** encodes a 9.6 s reply-style clip at 6-10.7 kHz, raw and 4/3/2-bit ADPCM, and with 3, 2 or 1 chip voices, then measures size, link wait at 2.6 KB/s, SNR and Whisper. Whisper heard every word in every cell except one voice at 6 kHz (JRAI's setup). So intelligibility isn't the limit; hiss and dullness are, and that's for ears. A listening page with all 24 versions went to Steve.
- **Under 1.0x:** 3-bit at 6 kHz and 2-bit up to 9.3 kHz arrive faster than they play.
- **SAY 1.2** reads version-2 clips (one extra header byte: the code size), loads the codes at the end of the block and decodes in place. The decoder in `player.asm` runs from the high block with two tables `say.c` builds in DGROUP: a lookup, an add, an overflow check, a store per sample. DOSBox: 0.8 s per 6 s of speech, against 7.6 s for the first (C) version. 3-bit stays on the C decoder. The emulator test now checks both decoders against mksnd.py's reference, byte for byte, at all three sizes.

**Wrong turns.**

- The first encoder stalled at silence: at the smallest step every code rounds to the same output index, so a greedy choice by output never grew the step. It now steers the predictor itself.
- The assembly decoder diverged at sample 1,686: at the largest IMA steps a change can exceed 32,767 and wrap a signed 16-bit add. IMA's table is now cut at 16,818 in the codec itself (Python and both decoders). Quality unchanged.
- A beam search over encoder choices gained only 1.5 dB, and a slope predictor and other step tables 0.2-0.5 dB. At 9.3 kHz this squeezed, brightened speech has little left to predict.
- The emulator test failed once on "late 1": a timer tick that landed during setup counted as a late first sample. The player now takes any pending tick before it starts counting.
- `jcxz` and a `jmp short` across the new code were out of range. wasm reported it at the target label, not the jump.

Not run on the real PCjr yet: SAY 1.2, the assembly decoder, and its real speed.

## Speech quality presets (2026-10-04, 6:00 am)

From the listening page, Steve picked three presets: **high** (raw, 10.7 kHz), **medium** (4-bit ADPCM, 8 kHz) and **low** (3-bit ADPCM, 6 kHz). `mksnd.py --quality`, and `SPEECH_QUALITY` for the `just` tasks; high is the default.

- **Low needed a fast 3-bit decoder.** 3-bit codes don't line up with bytes, so `sp_dec` takes 3 bytes (8 codes) at a time and shifts each code out of its byte, or byte pair, by a fixed amount: no bit buffer. In DOSBox, 3-bit HELLO (9.3 kHz) decodes in 219 ms instead of 1,592, and it matches the reference byte for byte.
- **The player outgrew its slot.** It was 0x3E9 bytes of a 0x400 slot before the 3-bit path. The block layout now gives code 2 KB: tables at 0800h, stack to 0E00h, samples from 0E00h.
- The emulator test plays one clip at each preset (rates 10,662, 8,013 and 5,998 Hz on the stopwatch, all 0 late with `/H`). Whether the real PCjr keeps up at 10.7 kHz is the open question for high.

## Day 2, evening: never drop a key, folders, tools, run reports

**A dead-looking minute.** After a cold boot `CODEJR` was still the old Kermit launcher (`SETUP` hadn't run), which then spent 6.5 minutes sending 90 KB at Kermit speed: the new client plus the speech session's clips. Then a native call sat for 51 seconds with nothing from the PCjr. The likely cause was 0.6 itself: while the prompt was hidden it ignored Enter silently. Steve's rule from this, now in `CLAUDE.md`: every PCjr program answers input at once, shows a spinner while busy, and never drops input.

**0.7:** a line typed while busy waits and goes on `Ready`, with a status message; a spinner (CP437 dots and stars, like Claude Code's) turns where the prompt goes. Each transfer over 4 KB prints a second line: seconds waiting for bursts, receiving, the gap that ends a burst, unpacking (the rest, CPU), and disk. In DOSBox `JRFLIGHT.EXE` showed 19.5 s receiving and 4.8 s unpacking; DOSBox's CPU timing is wrong, so the real split is still unknown.

**Work moved to a git worktree** (`~/Github/CodeJr/CodeJr-native`, branch `native`, pushed to `main`): the JRAI session edits the main checkout all the time, and I had already swept its unfinished work into one of my commits once.

**0.8, project folders.** One level of folders in the workspace, mirrored on the PCjr. Clients announce `dirs=1`; the rest skip folder files with a note. `/run` searches folders too (it collects the folder names first: a DOS find inside a folder ends the outer search).

**Build and run tools.** A small MCP server inside the `codejr` binary. First attempt failed silently: Go's `exec` looks up `wcl` in the server's own `PATH`, not the one handed to the child, so it now uses the Watcom folder's full path. Second: a relative `-repo` broke once the compiler ran in a scratch folder. The DOSBox screen dump first came back full of DOSBox's banner; `@echo off`, quiet mounts and `cls` fixed it. Real `claude -p` in restricted mode loaded the tools, built a JRGFX test, ran it and described the screenshot correctly.

**0.9, run reports.** `/run` now writes a `RUNNEXT.BAT` that records a failing exit and dumps the text screen right after the program; the next call sends exit, screen and the program's fresh `LOG.TXT` to the server, which puts them in front of Claude's next prompt once. An emulator test runs a C program that prints `GAME OVER`, writes `score 42` and exits 3, then calls back; all three facts reach the prompt. One full-suite run hit a timing flake in an old session test (30 runs alone pass); it probably raced with the new DOSBox test loading the CPU.

**1.0, changes only.** Measured first: two builds of `JRFLIGHT.EXE` around a one-line change (`if (argc > 7) return 9;` in `main`) differ in 45,131 of 97,784 bytes, because everything after shifts. A rolling-hash diff (16-byte blocks of the old file, copies extended byte by byte) turns that into a 4.7 KB script, 4.4 KB compressed, against 64 KB for the whole file: about 15 times less on the line. The server keeps exact copies of what it sent; the client checks its own copy's size and CRC before replaying a script and the result's CRC after, and the server falls back to the whole file if the PCjr's copy is different. Emulator test: send, change (bytes inserted), send again as changes; then spoil the PCjr's copy and check the fallback. Building the scratch copy of the game also showed `games/flight/build.sh src` leaves out `jrfast.c` and doesn't link; the build tool includes it.
