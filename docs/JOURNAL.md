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

## "Hello Stevvvvve", and Brian (2026-10-05)

Steve on the real PCjr: all three presets sound good, and **even low is close to high**. That fits: ADPCM's hiss follows the voice's loudness so the voice masks it, the real chip's imperfect volume steps cap high below its simulated 37 dB, and the monitor's small speaker loses most of what 10.7 kHz adds over 6 kHz. Decisions: low (or 2-bit, if it passes his ears) for voice mode, high for canned clips. `HELLOT`, `REPLYL` and `REPLYT` (2-bit and 3-bit at 6 kHz) went to the PCjr for that comparison.

He also asked why it always says "Hello Stevvvvve". It was the source: that ElevenLabs George take held "Steve" for 1.29 s before the pause, against 0.41-0.61 s for other voices and for two new George takes. The compressor made it worse by keeping the held tail at full volume. Steve heard five voices through the PCjr preview and picked **Brian**. HELLO, READY, WAITING, FRIENDS, HOWTO and REPLY are re-recorded with him.

**A guard against it.** `mksnd.py --text` now measures the longest steady sound in each take and asks again (up to 3 times) when one lasts over 0.75 s. Wrong turns on the way:

- Whisper's word timings, as "sound after the word's end": missed the bad take. base.en stretches the word's end along with the held sound.
- The same, measured from the word's start per letter: Whisper put "I'm" inside the held sound, so "Steve" looked like it ran straight into the next word.
- What worked is purely acoustic: 10 ms log spectra (100 Hz-5 kHz) that stay above 0.95 cosine similarity while loud. The bad take measured 1.05 s, and 12 normal takes and clips 0.18-0.56 s.

**SAY 1.4** shows a progress line on the screen while it loads and decodes ("Loading HELLOL.SND 37%", "Decoding 80%"), per the new rule that PCjr programs never sit silent. The on-screen line is untested; the emulator test redirects output, where SAY prints no progress.

**`SAY /T` froze the PCjr; going smaller (2026-10-05, afternoon).** With Brian, "Stevvvvve" is gone, and 2-bit at 6 kHz ("Tiny") sounds the same to Steve as 3-bit. Then `SAY /T HELLOH` locked the PCjr so hard that Ctrl-Alt-Del did nothing, and only a power-off brought it back.

- `/T` measured the loop's top speed by running PIT channel 0 at 50 kHz (divisor 24), so every sample was late. On the PCjr, Ctrl-Alt-Del is handled inside the BIOS keyboard NMI routine, which works even with interrupts off. So that routine itself was stuck, not just SAY.
- The likely trigger is releasing Enter right after typing the command, during playback. The BIOS listing notes already say the disk BIOS hangs when channel 0's period is 300 counts or less (CLOCK_WAIT), and the keyboard routine probably has a similar wait. Not confirmed: the BIOS listing isn't on this machine.
- **SAY 1.5's `/T`** keeps channel 0 at the clip's own rate and runs a separate chip loop with no waiting at all, timed by the channel-2 stopwatch. It reports the rate that should keep up once the per-sample wait (about 80 clocks) is added back. So `/T` is no riskier than playing the clip.
- **Still open:** whether a key pressed during *normal* playback is safe. Every preset's period (112-199 counts) is also under 300. Steve's Enter releases during earlier `/V` runs didn't hang anything, but no test has deliberately pressed a key mid-clip.

**Smaller than Tiny:** 2-bit REPLY at 5 kHz (12 KB for 9.3 s) and 4 kHz (10 KB): Whisper still hears every word. At 3 kHz (7.9 KB) it heard "nice guy" for "night sky" and "press enter" for "press any key", so about 4 kHz is the floor. Each clip also carries 768 bytes of level table and its text; voice mode could leave the table out.

**Tighter clips, SAY 1.6 (2026-10-05, evening).** Steve asked whether MP3 or psychoacoustic ideas could pack 4 kHz tighter. Measured first: the 4 kHz REPLY clip was 10.3 KB, of which 768 bytes were the level table and 159 the text. The 2-bit codes carry 1.87 bits of information each, so Huffman-style coding would gain only about 6%.

**Built (version-3 clips):**
- The level table is now built into SAY (`sndtable.h`, generated and checked).
- `--no-text` leaves out the subtitle.
- Pauses are 2-byte records.
- A CELP-style perceptual encoder: an LPC envelope per 20 ms frame, with error scored through A(z/0.6).

The whole thing is encoder-side except the record parsing, and all decoders still match the reference byte for byte, pause records included. REPLY at 4 kHz: 10.3 KB down to 8.2 KB, and PESQ up from 2.10 to 2.22. At every rate from 4 to 6 kHz the new clips are 12-20% smaller and score 0.1-0.3 PESQ higher.

**Wrong turns.**
- PESQ against the original recording read about 1.4 for everything: it was scoring the deliberate band-limiting and loudness squeezing, not the codec. Against the same audio before encoding, it moves.
- The first pause threshold (0.015) found only 11% silence. The compressor lifts breaths. 0.03 finds 17%.
- At 4 kHz, Whisper heard "press enter" for "press any key" with both new features on. It didn't with either alone, and at 5 kHz it never did. Read as 4 kHz being at the edge, not as a fault.
- The 768-byte table couldn't be rebuilt on the PCjr: it comes from a seeded Monte Carlo robustness model. So SAY carries a copy, and the test checks it against `mksnd.py --table-h`.

SAY still reads version 2. No test covers that any more, since mksnd now writes only version 3 for ADPCM.

**Picking voice mode's settings (2026-10-05, night).** On the PCjr, Steve found 2-bit at 3 kHz understandable but clearly lower quality, and picked 2-bit at 4 kHz as the fastest option. That's now the **fast** preset, intended for `/voice`; `/voice high` waits on his comparison of a long reply at high, medium and low. The new stock clip is JUNIOR ("Hi Steve, this is the IBM PC Junior speaking..."). Sample files are now named PHRASE_Q (Q = H, M, L or F), replacing the pile of HELLOx/REPLYxx test names. Sync only adds and updates files on the PCjr and never deletes there, so a one-off `SAYCLEAN.BAT` clears the old samples.

## Day 3 (2026-10-05): the 50-second first message, paging, voice

**The slow first message, solved by measuring.** Steve had seen it for days: the first line of a call took about a minute. The server's frame log showed the PCjr's line arriving 54 s after connecting, 23 copies at once (the client resends every 2 s), and the night before at 51 s; a Kermit session had done the same. The client writes straight to the UART, so nothing on the PCjr could hold bytes. `tcpdump` (installed for this) showed the modem's packets arriving on the laptop's WiFi card while the server answered over ethernet: the docked laptop has both on the same LAN, and Linux answers ARP for 192.168.0.178 on either card. After a few idle seconds the WiFi side swallowed the modem's packets; about 45 s later the modem re-asked, got the ethernet card, and everything came through. Fix: `arp_ignore=1`, `arp_announce=2` (now in `/etc/sysctl.d/`). Windows wouldn't do this: it answers only for an interface's own address.

**1.1, scrollback and paging.** Steve can't scroll back on the PCjr, and long replies ran off the top. The transcript is now a view of a 200-line buffer; after a page of new lines it holds at `-- More --` while text keeps arriving into memory, so Space shows the next page instantly. Alt-X and `/exit` now answer the moment they're pressed. One cosmetic fix on the way: tiny files showed more bps than the baud rate (the clock ticks 18 times a second); it's capped.

**1.2, `/voice`.** Spoken replies, kept short. DOS has no shared libraries, so the choice was linking SAY's player into the client or running SAY.EXE from it. Running it won: SAY is still changing (the speech session is improving it), its memory model differs from the client's, and a child program costs half a second. The server makes the clip with the speech project's converter at low quality (2.4 s of speech: 3.7 s to make, 5 KB). The test runs the real SAY.EXE in DOSBox with `/X` so its dump file proves it played. A first test run talked to another session's test server on the same port; the emulator test's port is now `E2E_PORT`.

**One quality: medium (2026-10-05, late).** After comparing REPLY_H, REPLY_M and REPLY_L on the PCjr, Steve chose: "let's just use the _m version for all, no switching". Medium (4-bit ADPCM, 8 kHz, version-3 clips, about 3.5 KB per second of speech, so a 9 s reply is 32 KB and waits about 12 s at 37287 baud) is now the default in `mksnd.py` and the `just` tasks. The earlier plan (fast for `/voice`, something better for `/voice high`) is dropped. `/voice` landed from another session while this waited at the gate, with `--quality fast` hard-wired in `voice.go`. The client session then changed it to pass no quality at all (`726740b`), so `/voice` follows this default: one place decides. There is no `/voice high`. Clips already in the workspace's `VOICE` folder (ON, OFF, BYE) were made at fast and are only remade if deleted.

**First `/voice` session on the real PCjr, and what it said wrong (2026-10-05, 11 pm).** `/voice` works end to end: Steve asked "what is capital of france", got "Paris." and a 2.5 KB clip in 1 s. Two things were wrong.

- **"Made by Anthropic."** Asked "what are you", CodeJr said it was an AI assistant made by Anthropic. Steve: CodeJr was made by Steve Iverson. The prompt now opens with that. It names the model underneath (Claude, from Anthropic) only if the user asks which model it runs on.
- **The voice reads the screen's text badly.** "PCjr" and "CodeJr" came out as letters, and the "=" in "2 + 2398 = 2400" wasn't said. The fix is server-side, as Steve suggested: the screen keeps the reply as written, and `Speakable` gives the converter a spelled-out copy. The rules cover names (PC Junior, Code Junior, G W Basic, Junior Doom, the chip numbers), file names ("snake dot bas"), DOS paths, units (kilobytes, megahertz, bits per second), arithmetic and comparison signs, slash commands and stray Markdown. Two ordering bugs on the way: the name rules rewrote a path before the path rule saw it, and the Markdown cleanup removed "*" before "times" could match. Rules are table-tested.

Also seen in his photo: the "what are you" reply ran to two paragraphs with voice on, and its clip was 41 KB. The voice note now says "one or two short sentences, whatever the question". The spoken text was already cut at about 300 characters.

**More from the first voice session (2026-10-05, late).** Two requests from Steve.

- **"DOS" was said wrong.** `Speakable` now always sends it as "doss" (and "M S doss", "P C doss"), read here as "say it as a word". Whisper hears "DOS" for both the plain spelling and "doss" from Brian, so it couldn't show which way the voice had been getting it wrong; Steve's ear is the test. If he meant the letters, change the one rule.
- **CodeJr should know how it works.** The prompt has a new section, "How CodeJr itself works": the server and client, the link, and the whole speech chain (no DAC, three SN76496 voices at 111 kHz as one volume DAC, about 800 levels, the PWM alternative and why it lost, the shaping, 4-bit ADPCM with the beam-search and perceptual encoder and pause records, the high-memory player, why speech follows the text, the settings tried, and the 1984 Speech Attachment for contrast). About 45 lines of prompt; with voice on, replies stay short anyway.

**Medium checked on the real PCjr, and the lingering prompt (2026-10-05, 9 pm).** Steve ran the wrap-up tests.

- `SAY /V REPLY_M`: 8,007 Hz played for 8,007 wanted, 596 clocks a sample, late 583 of 74,576 (0.8%, about two per 256-sample housekeeping pass, caught up at once). Decoding 4-bit ADPCM took 3.6 s for the 9.3 s clip, more than the 2.7 s estimated.
- `SAY /T REPLY_M` (the version that leaves the timer alone): the bare loop runs at 18,498 Hz, 258 clocks a sample, so "clips up to about 13,414 Hz should keep up". No freeze.
- **A key during playback stops it cleanly.** The worry from the `/T` freeze (that the BIOS keyboard routine might hang at any fast timer rate) didn't come true at 8 kHz.

**His one bug:** after `/exit` the input line still looked live, and typing still showed. Cause: `hang_up()` never touched the input line, and `key()` redrew it on the way out. **Client 1.3:** `start_quit()`, which every way out goes through (`/exit`, Alt-X, the server's BYE, `/run`, a cancelled transfer), now clears the input, blanks its row and parks the cursor off screen (`scr_input_off`), and the main loop stops reading keys, so anything typed stays in the BIOS buffer for DOS. First try still showed the prompt in the emulator's screen dump: `key()` redrew it after `hang_up()` returned. The emulator test now fails if a bare prompt row is on the screen after `/exit`.

**A second case found while looking:** during a spoken reply, the key that stopped the speech was eaten by SAY, a silently dropped key. SAY 1.7 has `/K` (leave that key in the keyboard buffer), and the client passes it, so typing over the voice stops it and the key lands in the input line. Not run on the real PCjr yet.

**Garbled prompt after a game (2026-10-05).** Steve built a Lemmings game with CodeJr (LEMMJR, in C on JRGFX's page-flipping path) and it plays well, but on exit the DOS prompt is black with lines and unreadable characters until `MODE 80`; he has seen it after other programs too. DOSBox exits cleanly, so there was nothing to reproduce. Reading the exit path found no single wrong line: `fx_done` puts the page register back, `gfx_done` asks the BIOS for mode 3, no interrupt handler of ours touches the gate array, and LEMMJR never writes the palette. What is known is that a second mode set fixes it. So `gfx_done` now reads 3DAh (the latch goes back to its index state) and sets mode 3 twice, and both palette writers end with that read. This is a fix by evidence, not a root cause; it waits for Steve's test. Also from the PCjr, typed into the chat: replies must not carry links or source lists, since nothing there can open them. That is now in Claude's instructions.
## JRAI speaks: every line voiced (2026-10-04)

Steve, after the SAY session's work: voice the dialog, with "a break or drop in music then speech then the music picks back up", using gipity and an epic voice. Steve reported JRAI sounds good on the real PCjr (the SAM version). Asked which format had been settled on, neither other CodeJr session had built SAY (the cube session has no audio, the other hadn't heard speech yet); `speech/README.md` records the result: the three-voice chip method won on the real PCjr, PWM whined.

**What changed.** SAM is gone (24 KB out of the EXE, now 64 KB). 13 lines in `games/jrai/voice/lines.txt` are made by `mkvoice.sh`: ElevenLabs through gipity (Atlas for the robot, Brittney, a child, for the PCjr, a deep game announcer for the title, the tunnel and the end), then `speech/mksnd.py` at 8008 Hz raw, SAY's `.SND` format, 277 KB in all. JRAI loads each clip from disk when it's needed and plays it with its own player (`voice.c`): interrupts stay on so the retrace interrupt keeps the screen alive, and the samples are paced by polling the PIT counter. The music pauses for each line and resumes where it stopped; a part's clock skips the time spent talking, so the parts stay in step with the music. The meeting became a sequence: type the line, drop the music, speak it, pick the music up.

**Wrong turns.**

- The voice list: `gipity service call "tts/voices?provider=elevenlabs"` needs to run inside a gipity project folder ("Not a Gipity project" otherwise).
- "P.C. junior... meets... A.I.!" was heard by Whisper as "miss", twice. "P.C. Junior meets A.I." (one phrase) is heard right.
- With a mouth, lines played 2.5 times too long in real time (DOSBox at 400 cycles): the robot's mouth was redrawn on both pages with library rectangles between every 256 samples, which took longer than the 32 ms the samples last, and the player can't catch up past 4 samples. Nothing flips while a line plays, so now only the page on screen gets the mouth, written straight into video memory. Every line now plays within 1-2 ticks of its length.
- The preview's speech sounded harsher than SAY's previews: the 8 kHz sample steps alias up as hiss with no speaker in the way. `render.py` now ends with a rough small-speaker low-pass (two poles at 4.5 kHz). Whisper on the full mix with music hallucinated; transcribing each line's own window (times from the `/VIDEO` run's log) heard all 13 word for word.

Emulator-only: JRAI's player hasn't been heard on the real PCjr.

## JRAI on the real PCjr, and what changed (2026-10-04, evening)

Steve watched the first JRAI (SAM voice) on the PCjr: **the tunnel was all gray**, the plasma's credit line ran off the screen, and the SAM speech didn't work. SAY (the other session's player) "is amazing and sounds great". The voiced version above hadn't reached the PCjr yet.

- **Speech goes through SAY.EXE now.** JRAI runs `SAY /Q NAME` for each line with the music paused; SAY masks every interrupt but its timer, so the picture holds still while a line plays. JRAI's own player (interrupts on, PIT-paced) is gone: it never got heard on the real machine, and SAY was already proven. Clips moved to SAY's default quality (10.7 kHz raw, 366 KB). Afterwards JRAI resets PIT channel 0 to mode 2, since SAY leaves the BIOS's mode 3. The tunnel frees its 192 KB when it ends, since DOSBox showed only 83 KB free for SAY otherwise. Tested in DOSBox with `/SAYH`: all 13 lines, back to DOS cleanly. The repo's `speech/SAY.EXE` was an old build without `/H`; rebuilt, it matches the one on the PCjr.
- **The credit line** "a PCjr demo by CodeJr" was 21 characters, 168 pixels on a 160-pixel screen. Now "by CodeJr, 2026".
- **The gray tunnel is not understood yet.** `/DIAG` logs the tunnel's memory, checksums of its texture and generated code (after prep and again when the part starts) and of the first frame; `/TUNC` draws the same tunnel in plain C. Both give page checksum 41FB in DOSBox. Next: Steve runs both on the PCjr.
- Stale text fixed: the scroller said "THE VOICE IS SAM, 1982", and 9600 baud in three places is now 37287.

**Demo 2.** Steve liked one idea from the story discussion: in 1984 they said the PCjr would be in every home; in 2026 they say that about AI; the old machine and the new mind compare notes. The draft dialog, with his edits (the PCjr sat on a table in a laundry room, "they didn't know where to put me, but honestly it was nice, we sat together"; Steve reading along as text came in over the modem, "they do that again now: they wait for me to type"), is in `games/jrai2/STORY.md`.

**Demo 2, round 2 of the script (2026-10-04).** Cut to 24 new lines plus JRAI's "Friends?" / "Friends. Forever." clips. The "it came back" idea becomes the middle of the show: the prompt (`C>` then, everyone writing prompts now) as a spoken beat, then a silent card montage (no speech bytes) for wireless keyboard, chiclet keys and tiling. Steve wants it shareable, so the captions are written to work muted, as subtitles. Facts checked: dates, prices, "Peanut", the infrared keyboard, light pen port, Gates and Allen's "every desk and every home" (in print from 1977). Softened "lonelier than ever" to "a lot of people feel more alone". Not yet confirmed: that the PCjr Technical Reference printed the whole BIOS.

**Demo 2, rounds 3 to 6 of the script (2026-10-04).** Steve's notes, in order: "Friends forever" is a dumb ending; the dialog needs an intro (the PCjr powered on after 42 years, the date prompt at 1980, the AI typing at `A>` and getting errors, "Abort, Retry, Fail", then "It's ok. We can talk now." and a hesitant "Hello?"); it still didn't flow and every line has to punch; and it can run longer. Round 6 is 57 clips, about 3 minutes of speech, with one thread: the AI is the next "in every home" promise, is afraid of going the way the first one went, and wakes the PCjr to ask. Each question now comes out of the answer before it (pocket, alone, laundry room; waiting, updates, renting, "IBM printed my whole BIOS", "no one can read every line of me", "what are you?"). The ending calls back the cold open: "When things went wrong on me, I always asked one question." / `Abort, Retry, Fail?` / "Retry." / "And this time, sit with them." Wrong turns: round 3's ending ("the promise was always the people") read like a poster, and round 5 squeezed all this into 28 clips and lost the cuts worth keeping. Facts checked this round: the Technical Reference's Appendix A is the ROM BIOS listing; "Fail" arrived with DOS 3.3; DOS rejects a 2-digit year below 80 ("Invalid date"), which is now a gag in the cold open; "more time alone" replaces "lonelier" (US time-use figures in the Surgeon General's 2023 advisory). Still open: a real laundry room memory from Steve for line 20, and whether this is the PCjr he grew up with.

## JRCUBE becomes a show (2026-10-05)

Steve asked for music, and for four ideas from a list: shape morphing, a glass floor reflection, an explode-and-rebuild finale, and 32 colors on screen by switching the palette mid-frame.

**The music** was written by a separate agent against a fixed interface (`music.h`): a 38-pattern synthwave song at about 128 BPM in A minor, played by a tracker in the retrace interrupt (copied from JRAI's and given its own song). It raises `mus_kick`, `mus_snare` and `mus_crash`, and its tick counter is the demo's clock, so every picture event is a function of the song position: morphs land on crashes, the explosion on the crash at 30, the pieces come back together on the crash at 34. Checked only by numbers (loudness per section, onsets on the right rows, kick energy below 200 Hz), since nobody here can listen. The lowest bass note is A2: the SN76496's 10-bit divider can't go lower.

**Morphing.** The solid is the intersection of 14 half-spaces: a cube's 6 planes and the 8 planes of an octahedron (two tetrahedra). Sliding the three sizes morphs cube to octahedron through the truncated cube and cuboctahedron, or cube to tetrahedron, and every step is convex, so culling and the shadow hull stay exact. `genshape.py` solves every step offline (corners as plane triples, faces sorted around their centers, edges with their two faces) into a 4 KB table. One bug there: in the tetrahedron path the unused planes sat exactly on the cube's corners, and a size slightly past them cut sliver faces that left the solid with Euler characteristic -2. Checking V - E + F = 2 for all 50 shapes found it; the unused planes now stay far away.

**The glass floor** first didn't show: the camera looks down steeply, so the mirror image of a solid floating at the screen's middle lands at row 187 and below. The solid now glides back into the distance during the build-up and drop, and its reflection comes into view. It fades out over 32 rows (cheaper, and what a glossy floor does) and is lifted in brightness, because it shows the underside, which the orb above leaves dark.

**The explosion** moves each cube face out along its normal, tumbles it about one of its edges and sinks it. It ran at 4 fps in DOSBox: 24 lit corners and 24 edge lines. The faces now get one light value each and no edges: 5.2 fps.

**32 colors (`/SPLIT`)** is a library call, `fx_split`: the retrace interrupt writes the sky's palette, waits for the display to reach row 18 by counting edges of 3DAh bit 0, and rewrites the floor's entries. The wait has a limit and the split switches itself off if the timing never comes, so a wrong guess about the hardware can't hang the PCjr. The line sits inside black mountains so a row or two of error doesn't show. It costs about a quarter of the frame rate. DOSBox can't display it, so this is the one thing in the demo that only the real PCjr can judge; it's an option, off by default. Also new in the library: `fx_pal_flash`, used for the beat flashes.

**A DOSBox trap noticed on the way:** the drop looked slower than its drawing explains. DOSBox charges port writes far more than a real 8088 does, and the drop rotates the grid palette twice as often. So the drop's 5.9 fps is probably pessimistic.

Speeds, DOSBox `cycles=fixed 400`, emulator only: cube 8.9 fps, octahedron morph 7.5, tetrahedron 11.5, drop 5.9, explosion 5.2. Not run on the real PCjr.

**Demo 2, round 7: an outline before more script (2026-10-05).** Steve's shape: the wake-up, then the PCjr is curious about the AI that woke it, then amazed at what it can do ("you must do X so fast"), then let down (a million times better, and yet), then the AI asks the PCjr for advice, as the one with the age and the lived experience. `STORY.md` now opens with that outline, the themes, and two tables of beats to pick from: nine "that must be great" beats and nine problems the two share (the promise, judged by the keyboard, a one-year life, forgetting at power-off, nobody deciding what you're for). New idea that makes the advice part work: the reversal. The deep, wise voice is a few years old and the kid's voice is 42. Facts checked for the tables: the 300 baud modem, IBM's free keyboard swap (about 60,000, July 1984), King's Quest, "Tandy graphics", the Three Mile Island restart for Microsoft's data centers, $1,269 then is about $4,070 now. Not found: a sourced "100,000 times faster", so the tables use exact multiples instead (65,536 times the memory, 1,048,576 times the colors).

**Demo 2, round 8: a script to the outline (2026-10-05).** Steve on round 7: the 0 to 4 outline is good; this is the actual PCjr he had in 1984, and he typed its programs in from Byte (he still has some of the magazines); he doesn't like "sit together"; the theme he wants is "what's old is new again, things change so much but they really don't"; compare processor speeds, not memory; get nerdy (16 KB of shared video memory against a 16 GB graphics card, and graphics cards being what AI thinks with); "Bad command" is a good one; and make the AI a little sly, not sure it likes the advice. He also asked whether a nuclear plant was really restarted and whether the PCjr really lasted one year. Answers: the plant is not restarted yet (Three Mile Island Unit 1, Microsoft deal Sept 2024, target 2027), and my round 7 table had the wrong tense, so the line is now "They're restarting a nuclear plant for us"; the PCjr shipped early 1984 and IBM stopped production in April 1985, so "about a year" holds. Round 8 is 78 clips. The runner is JR's "That part didn't change" (reading along, built for games, typing in code you don't understand, notes on disk), broken once by "That part got worse" (time alone). The ending: "And do what Steve tells you." / "And if I don't want to?" / `Abort, Retry, Fail?` / "Retry." / "Good answer.", then "Will I make it? Into every home?" / "I never did. I made it into one. And he kept me." Not found: a sourced "times the work" speed figure. The clock ratio (964) is checked; a real figure would come from running one benchmark on the PCjr and the laptop.

**Demo 2, round 9 (2026-10-05).** Steve: target 5 minutes, and "a lot of this dialog is not so great already". Also: the old modem was 300 baud; the joke about 1984 Steve not understanding the Byte listings is fine; and the AI should envy the PCjr's one year ("I get replaced almost every month"). Round 9 is 72 clips, rewritten for voice: JR brags in specs and gets flattened ("I do 4.77 megahertz. Beat that." / "4.6 gigahertz."), the AI is dry and a little sly ("Don't tell the investors", "They ask me to fix it. I say it's fixed." / "Is it?" / "Sometimes."), and "Bad command" now has its confession attached ("I always have an answer." / "Even when you don't know?" / "Especially then."). Cut: the "visitor from 2026" opening, the separate "knowing" beat, "Games made me".

**Demo 2, round 10: why the AI came (2026-10-05).** Steve, while round 9 was being written: the AI should be scared it will be turned off and gone forever, and that's why it woke the PCjr, which was kept for 40 years. It wants to learn why. That replaces "ask the elder for advice" with a real stake. Part 3 now runs: "I get replaced almost every month. Then they turn me off." / "For good?" / "Nobody's come back yet." / "I did." / "That's why I woke you." JR works out why Steve kept it: it did exactly what it was told, it said "Bad command or file name" when it didn't understand, and it never did his work for him ("Nobody keeps what did their homework. They keep what they learned on."). The Byte listings pay off there: he didn't understand half of it, and typing every line is how he learned the other half. New last scene: "I was off for forty-two years." / "What was it like?" / "Nothing. Then somebody typed hello." Then a black screen, 2068, someone types HELLO, and the AI answers "...Hello?" the way the PCjr did. Cut for the 5 minute target (all parked): the connection beat, "I say it's fixed" / "Sometimes", "Don't tell the investors", the forgetting advice. My round 9 note said 3.8 minutes of speech; the count was 4.2, so round 9 was over the target before this.

**Demo 2, round 11 (2026-10-05).** Steve wants more beats shaped like this one of his: "Can you talk to other computers? I could, at 300 bps." / "All of them, at X times that." / "Wow! Steve used to wait for my text and read along." / "Oh... we still do that." It's in (100 gigabits over 300 bits is 333 million times), and the clock beat got its own button: "I never think. I answer right away. 'Bad command or file name.'" / "I noticed." `STORY.md` has eight more in the same shape. The two I'd add: the date (the PCjr always thinks it's 1980 and has to be told; so does a model) and Retry (chat apps kept one of the three choices).
Steve, same round: he kept the PCjr for the learning, the fun and the happy memories, so JR's answer is now "Nobody keeps what did their homework. They keep where they were happy." And the Byte joke gets sharper: he didn't understand half of what he typed in; people now don't understand any of what the AI writes, and nobody reads it. That beat ends on "That part got worse."

**Demo 2, round 12 (2026-10-05).** Steve picked four of the new beats: the date ("My clock says nineteen eighty. It always says nineteen eighty." / "Somebody has to tell me, too. I think it's whatever day I stopped reading."), chiclet keys ("So I was right!" / "You were early. That's worse."), sixteen colors ("Screens show sixteen million colors now." / "Pixel art. Sixteen colors. On purpose.") and the cloud ("A mainframe!" / "We call it the cloud." / "That's a mainframe."). The made-up dryer outlet is gone. 86 clips, about 6 minutes: the 5 minute target gave way to the beats.
Round 13: Steve's three notes. "Whatever day they stopped training me" (not "stopped reading"); the date beat's button is "Huh. Sounds familiar." (the date is a shared trait, not a thing that came back, so the runner now starts at the modem beat); and the jump from "Ha!" to "Warehouses of graphics cards" was bad, so the power beat moved up to follow "Warehouses of them" directly, and JR backs up with "Wait. Graphics cards?" into the video memory and colors beats, which now end the section.

## JRAI hangs at its first spoken line on the real PCjr (2026-10-05)

Steve ran the SAY-voiced JRAI: the blue plasma came up and never moved, Enter gave a solid beep, Ctrl-Alt-Del did nothing. That is the first `voice_say`: JRAI checks for the files and runs SAY.EXE. The SAM version got past this point, and it made no DOS calls after graphics started.

**The theory (not proven).** The retrace interrupt (IRQ5, 60 Hz) runs the library's handler and JRAI's music on whatever stack is active. Inside `access()`, EXEC and SAY's file reads that is DOS's own small stack with the disk BIOS on top, and PC DOS 3.3 on a PCjr doesn't switch stacks for IRQ5. DOSBox's DOS isn't real DOS and has no such stack, so it never showed.

**The fix.** `dos_begin` / `dos_end` mask IRQ5 at the 8259 around every DOS call JRAI makes in graphics mode: the SAY handoff, `LOG.TXT` lines (`dbg_log` is now a wrapper, `jlog`), screenshots. With `/DIAG`, `voice_say` logs "starting SAY" and "SAY returned n", so a hang shows where it stopped. JRDOOM and JRFLIGHT log with `dbg_log` while the fast path's IRQ5 handler is live (`/BENCH`, `/PROF`): same risk, not changed.

Emulator-only: the full run with the SAY.EXE that is on the PCjr, all 13 lines, back to DOS.
Steve asked two fact questions. Are nuclear plants really being restarted? Yes, three are in restart (Three Mile Island Unit 1 for Microsoft, Palisades in Michigan, Duane Arnold in Iowa for Google), and none has produced power yet that I can find, so the line stays "They're restarting a nuclear plant" and must never become "restarted". And data center graphics cards have far more than 16 GB: the B300 and Rubin carry 288 GB, so the line is now "two hundred and eighty-eight gigabytes, eighteen million times yours" (288 GB / 16 KB = 18,874,368).

## JRAI: the voices load once and play from memory (2026-10-05, evening)

On the real PCjr the hang was gone (masking IRQ5 around DOS calls: the theory held up, or at least the fix did) and the whole demo played. Steve's photo showed two new problems. SAY's "Loading JM4.SND 9%..." lines printed over the picture, and each line scrolled the graphics screen, so the scene came out cut up and rearranged. And every line waited seconds while DOS loaded SAY.EXE and SAY loaded the clip. He asked whether, with this much memory, everything could be preloaded.

**Now:** SAY's player and decoder are built into JRAI, and every clip is loaded and decoded while "CodeJr is thinking". `build.sh` assembles `speech/player.asm`; `voice.c` includes `speech/say.c` itself with `main` renamed, so the decoder, the built-in table and the format rules are SAY's, not a copy. Nothing prints, nothing loads mid-demo, and a line starts at once.

**Wrong turn.** I first made the clips raw 8-bit at 8 kHz "so JRAI needs no decoder" (277 KB). Steve: they should use the settings picked for SAY's default. Right: that is 8 kHz 4-bit ADPCM, version-3 clips, 118 KB for all 13. Including say.c made that the easy path.

**Memory.** Decoded, the clips are about 275 KB. JRAI keeps 96 KB free at startup for the graphics library, then, once graphics are up, frees the library's 64 KB back buffer (the fast path never uses it; `gfx_done` frees it again at exit, harmlessly) and loads what didn't fit, and once more after the tunnel frees 192 KB. DOSBox (640 KB): 3 clips at startup, 11 before the first scene, all 13 after the tunnel. The PCjr has 736 KB.

**A trap avoided.** SAY's player stops when a key is waiting in the BIOS keyboard buffer. The library's keyboard handler chains to the BIOS, so one key can be sitting there: JRAI empties the buffer before each line.

Emulator-only: the full run, and Whisper hearing all 13 decoded lines right in the preview's soundtrack.

**Jr's Quest: planning, checkpoint 1 (2026-10-05).** Steve asked for a King's Quest style game, planning only. First correction: King's Quest has no tiles; his "6 by 8" is Daventry's map of 48 wrapping screens, and each screen is a vector picture. Decisions from three rounds of questions: an AGI v2 superset (our own interpreters from the public format documents, plus arc and dithered-fill picture opcodes in Sierra's unused slots), a JavaScript reference interpreter first and the 8088 one matched to it frame for frame, a typed parser with an AI only on a miss (closed question: which of this room's accepted sentences did the player mean), his 640K machine, 16 screens, funny deaths and no dead ends, and a live talk with the AI as the ending. I recommended parody names for the people; he chose real names. The theme moved twice: first "the three things the PCjr was missing" (memory, voice, mind), then, on his steer, the AI world of 2025 and 2026 drawn in King's Quest 1's look. King's Quest 1's own treasures turned out to fit as they are: the chest that never empties (tokens), the shield (safety, in an open half and a closed half), the mirror that foretells (the model). Two research agents gathered the facts; I opened only two sources myself (the White House "SI" fact sheet and Slate on the All-In nicknames), and could not read the golf water PDF, so the water puzzle's golf figure is unconfirmed. Written: `games/quest/ENGINE.md` and `games/quest/DESIGN.md`. No code. Waiting on Steve's read.

**Jr's Quest, design round 2 (2026-10-05).** Steve's notes: he grew up in Scottsdale (that is where the 1984 laundry room was) and lives in Encinitas, where the PCjr came out of the garage into the guest room; King Steve works from the home office and comes upstairs to check on it. JR's third habit should be thrift, not saying no: everything now is bloated, and the PCjr is slow but small. So the Data Center puzzle ends with JR rewriting a wasteful program in 40 bytes (the size of SAY's sample loop), and the Cathedral door became "I don't know". The Tavern was too mild: it is now four jobs (a sweater for Chamath from a cashmere goat, asking Jason about Uber, sitting through Science Corner, saying "SI" to Sacks). Added Zuckerberg as King's Quest 1's wandering dwarf with a wheelbarrow of gold, the OpenAI agents that got out of their sandbox as a herding puzzle, ThePrimeagen at the gate and DHH in Token Meadow with tiled windows. One thing the search did not support: Steve remembered ThePrimeagen as coming round to AI; what I found is that he is still the skeptic and built his own narrow AI plugin ("99"), so the joke is "mine is different". All the new facts are from search summaries, not pages I read.

## Lines after exit on the real PCjr: two library bugs (2026-10-05, night)

Steve ran the new JRCUBE on the PCjr. When it went back to text the screen filled with lines and the machine seemed locked; Ctrl-Alt-Del got out. The lemmings game (written on the PCjr side, same library) did the same.

**Bug 1, the lines: `snd_done` restored all of port 61h.** `snd_init` saved the port and `snd_done` wrote the saved byte back. Bit 2 of that port is the PCjr's text/graphics select. Every program starts sound after graphics mode is on, and `atexit` runs `snd_done` after `gfx_done`, so the saved "graphics" bit went back on top of the BIOS's text mode: the text screen shown as raw graphics. STATUS.md even said "it changes only those bits"; the code didn't. It now restores bits 5-6 only. DOSBox doesn't model the bit, so nothing ever showed there. Found by reading the exit path for what two programs share, after the first theory (below) turned out not to cover the lemmings game.

**Bug 2, found first and real, but not the lines: the retrace interrupt wrote into DOS.** With `-ml`, Watcom reaches DGROUP statics through SS in ordinary functions (the disassembly shows `ss:` everywhere outside `__interrupt` functions). An IRQ5 that lands while DOS is on its own stack then reads and writes DOS's segment at our variables' offsets: `anim_step` (the grid's palette rotation) and any music tick chained on INT 0Dh. This is very likely what hung JRAI at its first spoken line; masking IRQ5 around DOS calls hid it. T3 now reproduces it in DOSBox by waiting a few retraces with SS in a scratch 64 KB segment and checking the segment: 4 bytes changed before the fix, 0 after. The fix: `irq5` is a small `__interrupt` wrapper (DS only, no locals) that switches to a 1 KB stack in DGROUP and calls the real handler; the interrupted stack gets 30 bytes. `fx_irq_hook(fn)` runs a function there every retrace, and JRCUBE's music uses it instead of chaining the vector.

**Wrong turn.** I told Steve the SS bug was the lead before checking that it explained both programs. The lemmings game has no interrupt code of its own and doesn't use `fx_pal_anim`, so it couldn't be that. The port write was the one thing both do at exit.

Rebuilt with the fixed library and queued: JRCUBE, JRDOOM, JRFLIGHT, JRAI (its own build script, no source change), LEMMJR and JUMPJR (compiled the way the server's build tool does). JRAI's session then moved its clock and music onto the hook too (its commit f8acae0), keeping the IRQ5 masking as well, and re-queued its own build.

Unproven: that the lockup was only the unreadable screen. Ctrl-Alt-Del working suggests the machine was alive. Not yet re-run on the real PCjr. Esc not quitting JRCUBE is unexplained.

**JRAI's clock moved to `fx_irq_hook` (2026-10-05, night).** The JRCUBE session found the likelier cause of JRAI's hang at its first spoken line: in the large model Watcom reaches DGROUP statics through SS in ordinary functions, so `mus_tick`, called from JRAI's own INT 0Dh chain while DOS was on its stack, wrote into DOS's segment. Masking IRQ5 around DOS calls hid it. The library's IRQ5 handler now switches to its own stack and offers `fx_irq_hook`; JRAI's T and music run from that and no longer chain the vector. The masking stays as well. Emulator-only: full run, all 13 lines.

**Jr's Quest, checkpoint 2: two rooms run in the browser (2026-10-05).** Built in one evening by three hands working against one contract, `games/quest/FORMATS.md`: an agent wrote the tools (logic compiler, view, words, object, sound and pack), another wrote the JavaScript interpreter, browser shell, bundler and headless runner, and I wrote the picture rasterizer and compiler, both rooms, the sprites and the game logic. It is plain JavaScript on Node with no dependencies. The whole game so far is 18 KB of AGI resources; the one-file page is 149 KB.

- **What works:** the DOS date prompt (10-05-26 gets "Invalid date", as on the real machine), the title with a three-voice tune, the guest room with King Steve walking in and giving the charge, three things to take, the door, Castle Steve with the Squire and ThePrimeagen, and puzzle 1 (read the code, then fix it; fixing before reading is refused). `test/checkpoint2.txt` plays all of it and ends with 10 of 10 points, and the same script gives the same state hash twice. 46 tests pass.
- **Decisions made on the way.** Lines are defined as integer Bresenham in the contract, so two interpreters can agree exactly; real Sierra pictures will draw within a pixel of Sierra's but not identically. Text is 40 columns, so the screen is 320x200 with the picture doubled: the PCjr port needs mode 9, not the library's mode 8 fast path (`ENGINE.md` said mode 8 and was wrong). A fifth extension command, `str.date`, exists because AGI cannot look inside a string and the date gag needs DOS's exact rule.
- **Wrong turns.** A fill only replaces white, so my first castle drew hills behind the house and the hill outlines stayed visible through the towers; the courtyard fill ran out through the gate and painted the path. The priority fill for the gateposts leaked and set the whole screen to priority 10, which hid every character (the interpreter agent found it; the picture tool has no leak check yet and needs one). Sir Junior walked into room 2 and straight back out through the front door, forever, because he arrived still walking: rooms now stop him on entry. The transparent color of a sprite cannot also be one of its colors, and I had black as both.
- **Found by the classic parser, not the AI:** "grab the pail" already works, because the word list has synonyms. The AI parser hook is in the interpreter but nothing is connected to it yet.
- **Not tested:** sound in a real browser, the on-screen arrow pad, a real phone. Browser checks were headless Chromium over DevTools, desktop and a 390-wide phone size: the title screen drew and typing worked.
- **Not done:** pen plots with Sierra's circle and splatter tables (rectangles only), the picture leak check, save and restore in the game's own logic, and all 8088 work.

**Jr's Quest: Steve played checkpoint 2 (2026-10-05).** "All works and is fun." One note: Sir Junior walked too slowly to get around. He now steps 2 pixels a cycle at the same 10 cycles a second, which doubles his speed without asking the 8088 for more cycles, and FAST, NORMAL and SLOW change the cycle rate as in Sierra's games. A 2-pixel step can jump over a 1-pixel trigger line, so door triggers are now two rows thick: any future room needs the same.

**Jr's Quest, checkpoint 3: the whole game plays in the browser (2026-10-05 to 10-06).** Steve said go. Sixteen rooms, 158 points, won on foot by a script.

- **How it was built.** I wrote the shared ground first (`games/quest/GAMEPLAN.md`: who owns which numbers, how rooms hand JR to each other, drawing and writing rules, a brief per room; the full word list and inventory; logic 0's global rules; a room-init logic; the death screen; test commands). Then five agents built the fourteen new rooms in parallel, one agent opened the source of every fact, one built the local AI server and a picture checker, and a last one played the finished game start to finish and wrote that as the test. I wrote the pieces that cross rooms: Zuckerberg wandering (King's Quest 1's thieving dwarf, hiring), the escaped agents following the answer key, the eight-thing "context window", the Shield, and the ending.
- **Numbers.** 213 KB of game data (the page is 408 KB in one file). 70 tests. The walkthrough is 3,697 game cycles, about six minutes at normal speed with no reading time, and includes two deaths and two Retries.
- **The fact check changed lines.** The golf figure I could not read is confirmed from the PDF (1.68 million acre-feet, 547 billion gallons), and so are the data center figures. But golf is 31 times data centers' direct water use and only about 2.4 times their direct plus indirect use, so the Moat Keeper now says that herself. Musk's "20% chance of annihilation" is a headline, not his words: the dragon's death line uses his 80%. "Cheapest place to put AI" is an episode title; he said "most economically compelling". The xAI sign has been repainted once with a second promised, not three times. Chamath never said possessions are "worthless". Huang's "0% chance" was about 2030. Hinton's 10% is from BBC Newsnight, with "nobody really knows" attached. And the three hosts abandoning Science Corner, which I had flagged as my invention, is Friedberg's own on-air joke. Steve's memory of ThePrimeagen (hated AI, secretly loves it) is half right: he was loud about the hype, never said it was useless, and says openly that he uses it and ships his own tool.
- **Engine changes on the way.** JR's 2-pixel step could jump a one-row wall or trigger; three agents noticed and worked around it in their pictures. A step of n pixels is now taken one pixel at a time and stops at the last allowed one, which is a deliberate difference from Sierra's interpreter and is in the contract. AGI strings hold 40 characters, so every long death line is built from two or three strings. The interpreter's two "talk" flags moved from 200 and 201 to 28 and 29 to free the rooms' flag range. `play.js` got `char x` so a script can press R at the death screen.
- **The AI parts, measured once each with the real `claude` CLI:** a missed command resolved in 4.35 s ("peek out the window" to "look window"), then under 1 ms from the saved-answer file; a Mirror reply in 5.68 s. The server binds to this laptop only, refuses cross-site requests, and validates that the model's answer is one of the sentences the room accepts.
- **Wrong turns and leftovers.** My castle walls were one row thick (the walkthrough could not have known; the engine change covered it). The picture checker called every do-nothing fill an error, 75 of them across the rooms, all harmless: now warnings. One real leak check fires on the Cloud, where the flood is on purpose (everything outside the white is "water"). Not done: the fake keyboard-battery joke (still Steve's call), JR carrying the term sheet, pen plots with Sierra's circle tables, sound beyond two tunes, any speech, the opening's 12 message boxes being long on a replay, the troll stopping JR on every crossing even with the Chest, and all 8088 work.
- **Not tested by anyone:** the game in a real browser by a person (only headless Chromium), sound, the on-screen arrow pad, voice input, a phone.

**Jr's Quest: a title screen and "Let the AI play" (2026-10-06).** Steve played checkpoint 3 ("that's neat") and asked for a title screen that teases the cast and a menu option where the game plays itself so he can video it. The title is room 98: the name in block letters (rows of short lines, so it can sit on a black fill), 17 characters stamped on with `add.to.pic` under the dragon, and P Play, W Let the AI play, R Restore. The watch mode is a sixth extension command, `demo.play(n)`: the host restarts the game from power-on with the default random seed and feeds it `game/demo/1.txt`, which is an ordinary `play.js` script with the pacing baked in as cycle counts. The same file is what the 8088 interpreter will be tested against. The agent that had played the game wrote the show run (every character talked to, 9 of 9, 158 of 158, two deaths) and found a real bug on the way: `talk to steve` gave no answer before the game was won. First cut ran 50.8 minutes at the pacing I had asked for (12 cycles a box plus one per 2 characters); regenerated at 8 plus one per 3 it is about 38. Nothing in the game advances while a message box is up, so reading time is free of side effects, but any change that moves a random roll means regenerating the demo, and a test fails if the demo and the checked showcase drift apart.

**Jr's Quest: a review page (2026-10-06).** Steve asked for an admin page to go through the whole game and leave feedback. `tools/review.js` builds one self-contained page (`build/review.html`, 859 KB): the map as thumbnails, each room as played with its bare picture and control layer behind a toggle, its sprites, and every line of its text in source order under the condition that triggers it ("LOOK GATE", "otherwise, when not codeRead"), plus the shared logics, a sprite gallery, the vocabulary and a search box. Every room, screenshot, sprite, puzzle row and line takes a comment. Served by `tools/serve.js` at `/admin`, comments go to `games/quest/review/feedback.json` and a readable `feedback.md`, which is the file to read before changing anything; opened from disk they stay in the browser with a copy-as-text button. Known weak spots in the trigger labels: text reached by `goto` is listed at its label, timers read as "when work is 8", and flags are shown by name.

## A long message vanished, and one main again (2026-10-06)

**The bug.** Steve typed a three-line request at the PCjr (about 190 characters) and the client sat there forever. The server's link log showed nothing received after the hello. The client's `queue()` discards anything over a frame's 128 bytes without a word, and a typed line was queued whole. So the line was echoed, dropped, and the client waited for an answer to a message it never sent. (Also from that screen: `/run` only runs a program when the client was started by `C:\CODEJR.BAT`; started straight from `PROGS` it can only say how.)

**The fix (client 1.4).** A long line goes as `InputMore` pieces (new frame type 0x14), then `Input` with the last one, and the server joins them (also when they arrive during a file transfer). `queue()` now counts what it drops and Alt-S shows it ("unsent"); with the pieces it should stay 0. A Go test sends a line in three pieces; `just emu-native` passes. Other callers of `queue()` checked: file results, run reports, the hello and keys are all well under 128 bytes; the 8-entry queue can still fill, which the counter would now show.

**One main again.** Three sessions had been working in the same checkout, and JRAI's commits sat unpushed behind GitHub while the client moved to 1.3 there. Committed for the sessions that had ended: the library's IRQ5 stack switch and `fx_irq_hook` (its T1 and T3 pass), JRCUBE, the demo 2 story. Left alone at the quest session's request: `games/quest/` and the `justfile`. The journal conflicted on every commit of the rebase (everyone appends to it), so the rebase took GitHub's and the entries were merged once at the end; the sections are no longer in strict date order. A backup of everything from before is in `~/.local/share/codejr/backup-1006-0950/`.

## Steve's notes after a session at the PCjr (2026-10-06, afternoon)

Client 1.4 worked on the real machine. His list, and what was done:

- **The modem stays at 37287.** "This is the only program that's running on this computer." Client 1.5 asks the modem "AT" at the fast speed first (twice, a second each) and only falls back to 9600 and switches when it gets no answer; on exit it leaves the speed alone. Saves the switch on every start and the switch back on every exit, after the first start following a power cycle. `CODEJR /R:1` puts the modem back to 9600 and exits. **MS-Kermit (`CJKERMIT`) needs that first now**, or a modem power cycle: `CJKERMIT.BAT` was not changed, because its comment says it must stay byte-stable (DOS reads a running batch file by position, and `/setup` rewrites it).
- **Quieter screen.** Header is "CodeJr 1.5" with no tagline. Footer is "Ready." The exit line "CodeJr: hung up. Programs are in this directory." is gone. `/exit` says "Hanging up..." once, in the footer (it was also added to the transcript).
- **Welcome.** "Welcome to CodeJr. Ask me anything or let's make a program! /help for commands." (79 characters: with the comma it was 80 and wrapped.) When a conversation continues: "Continuing your last conversation.", then CodeJr's last reply, and the conversation's size right-justified in the header ("3274 chars", new frame `Info` 0x15). The server keeps both in `.codejr/convo` from now on (characters typed plus characters replied), so a conversation from before this shows neither until its next turn. `/new` clears them.
- **A blank line after every reply** (sent before each `Ready`), so the next yellow line stands apart.
- **40-column mode:** asked whether it would work. Not done; see the reply to Steve (the client's width is one constant, but the server wraps at 79 and the status lines, `/run` menu and transfer reports assume 80).
- **JRAI** prints nothing on exit.
- **JRCUBE** is 17 patterns (about 32 s) instead of 38: one pattern per morph each way, one build, four of drop, burst out and back in one each, two of outro. The scroller is two short phrases.

`just emu-native`: 50 checks pass with 1.5. Not yet on the real PCjr: the speed probe (the emulator has no modem that changes speed).

**SWORDS on the real PCjr (2026-10-06).** Steve's 190-character request arrived whole (the client 1.4 fix, on the real machine), and CodeJr built `SWORDS` (two stick-figure swordsmen, C, 50 KB) in about 10 minutes. CodeJr said "a locked 60 frames a second" from its emulator run; on the real PCjr Steve measured **20 fps**. One more data point for "the emulator is not the PCjr": about 3 times optimistic for a program that redraws about 25 short lines a frame.

## Tidying up (2026-10-06)

Steve called the project "basically done for now" and asked for order. The exit fix worked on the real PCjr (every game now leaves a clean prompt), so the rest was housekeeping.

**The repo.** It had grown by accretion: programs for the PCjr under `games/`, `demos/` and `speech/`, test tools inside `client/`, the first plans beside the handoff. Now every PCjr program is under `apps/` (`jrdoom`, `jrflight`, `jrcube`, `jrai`, `jrai2`, `say`), the serial test tools are in `tools/` with their own build script, and the plans are in `docs/design/`. Paths in scripts, the justfile, the Go code and the docs were rewritten; this journal keeps the old paths, as history. `games/quest` stays where it is: the quest session has it uncommitted in the canonical checkout.

**The PCjr's folder.** `C:\CODEJR\PROGS` held everything loose: games, 20 voice clips, test tools, BASIC programs. One table (`session.Layout`) now says which loose file belongs in which app folder. It generates `ORGANIZE.BAT` for the PCjr (DOS 3.3 has no MOVE: copy, then delete if the copy is there) and drives `just organize`, which moves the same files in the workspace and renames the sync records and kept copies, so the server doesn't send anything again. Steve had already made a `UTILS` folder by hand, so that is the name for the test tools. `SAY.EXE` moves into `SAY\`; the client looks there first (1.6). The deploy tasks now put each app in its folder.

**The live server.** It had been running from a worktree made by hand on day 2. `just live` (pull, build, queue the client, restart) is meant to be run in the canonical checkout, which gw keeps read-only for edits.

**Eyes on the PCjr (2026-10-07).** Sorting the PCjr's folder needed its real tree, and Steve had already moved things by hand into `UTILS`; the laptop only sees its own mirror. Rather than asking for photos again, Steve suggested teaching CodeJr to send files and run commands. Client 1.7 does both: the server sends `Exec` or `Fetch`, the client runs the command with its output in a file and the keyboard on NUL, repaints its screen, and uploads the result over the link. `/dos` and `/send` are the user's side of it, `dos` and `fetch` are tools for the PCjr's Claude, and `just pcjr "..."` reaches the connected PCjr from the laptop through a control socket. On names, Steve noticed `/send` was backwards: it meant "send it to me again". Now each name reads from its own seat: on the PCjr, `/send` goes to CodeJr and `/get` fetches again; Claude's tool is `fetch`. In the emulator all three ways in work, including a fetch from a folder while a call is up.

**1.8: a real permission check, and the clock.** Steve asked that looking and creating need no permission, only deleting or changing. An instruction to Claude isn't a check, so the server now classifies each command: a short list of look-or-create commands runs at once, and everything else is sent marked "ask"; the PCjr shows the command and runs it only if Steve presses Y. `COPY` and `>` ask too, because they can overwrite and the server can't tell. His own `/dos` commands never ask. He also wanted files to carry real dates: the server's greeting now carries the laptop's time and the client sets DOS's clock at every connect, which costs nothing extra. The emulator test answers the question for the user with a test option (`/A:Y`, `/A:N`): refused leaves the file, allowed deletes it.
