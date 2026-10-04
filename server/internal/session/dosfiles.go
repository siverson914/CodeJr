package session

import (
	"fmt"
	"strings"

	"codejr/internal/kermit"
)

// DOSSetup describes how the PCjr reaches the server.
type DOSSetup struct {
	Host string // address the modem dials
	Port int
	COM  string // Kermit port: COM2 on this PCjr, COM1 in DOSBox
	Baud int
}

// ProgDir is where Claude's files live on the PCjr; the workspace mirrors it.
const ProgDir = `C:\CODEJR\PROGS`

// DOSFiles returns the launcher, the Kermit settings, the modem notes and
// CODEJR.CFG, the one place the native client and its launchers find the
// server's address. All are sent to C:\ because Kermit can't create
// folders; on its first run CODEJR.BAT makes C:\CODEJR and
// C:\CODEJR\PROGS and moves the others into C:\CODEJR, so only
// CODEJR.BAT stays in the root. They are written with DOS line endings.
func DOSFiles(c DOSSetup) []kermit.File {
	bat := `@ECHO OFF
REM CodeJr: talk to Claude from the PCjr.
REM Programs Claude writes land in C:\CODEJR\PROGS.
C:
IF NOT EXIST C:\CODEJR\NUL MD C:\CODEJR
IF NOT EXIST C:\CODEJR\PROGS\NUL MD C:\CODEJR\PROGS
REM /setup delivers new settings to C:\; move them into C:\CODEJR.
IF EXIST C:\CODEJR.INI COPY C:\CODEJR.INI C:\CODEJR >NUL
IF EXIST C:\CODEJR.INI DEL C:\CODEJR.INI
IF EXIST C:\CODEJR.TXT COPY C:\CODEJR.TXT C:\CODEJR >NUL
IF EXIST C:\CODEJR.TXT DEL C:\CODEJR.TXT
IF EXIST C:\CODEJR.CFG COPY C:\CODEJR.CFG C:\CODEJR >NUL
IF EXIST C:\CODEJR.CFG DEL C:\CODEJR.CFG
CD \CODEJR\PROGS
C:\MSK314\KERMIT TAKE C:\CODEJR\CODEJR.INI
CD \
`
	ini := fmt.Sprintf(`; CodeJr connection settings for MS-Kermit 3.14 (TAKE file).
; Written by the CodeJr server; type /setup again to refresh it.
set port %s
set speed %d
set parity none
set flow none
set terminal bytesize 8
; Half duplex: on this PCjr, MS-Kermit only raises RTS in half duplex, and
; the WiFi modem won't send to the PCjr without it. Half duplex also means
; Kermit echoes typing locally, so the server doesn't (/echo off).
set duplex half
set terminal newline off
set terminal color 0, 37, 40, 20
; Let the server start file transfers (RECEIVE and SEND only).
set terminal apc on
set file type binary
set file collision overwrite
set delay 0
echo Dialing CodeJr at %s:%d...
output ATDT%s:%d\13
; The server repeats "CodeJr READY" until we press Enter.
input 30 READY
if fail echo No answer yet. In the terminal, press Enter when you see READY.
output \13
connect
`, strings.ToLower(c.COM), c.Baud, c.Host, c.Port, c.Host, c.Port)
	modem := `CodeJr: one-time WiFi modem setup
==================================

Type these once in Kermit's terminal (CONNECT mode). The modem remembers
them after AT&W.

  ATE0     modem does not echo commands (the CodeJr server echoes instead)
  ATR1     modem ends its replies with CR+LF, so OK gets its own line
  ATQ0     modem sends result codes
  ATV1     ...as words (OK, CONNECT), not numbers
  ATF1     XON/XOFF flow control. Never ATF0 (RTS/CTS): the PCjr can't do
           it, and the modem then holds back everything. AT&K0 isn't saved.
  AT&W     save

Then from DOS, type CODEJR to dial in. Programs Claude writes are in
C:\CODEJR\PROGS.

If the screen ever looks scrambled, press Ctrl-L to redraw it.
To leave: type /quit, or press Ctrl-] then C and type EXIT.
`
	cfg := fmt.Sprintf("; CodeJr native client settings. Written by /setup; type /setup to refresh.\nDIAL=%s:%d\n", c.Host, c.Port)
	return []kermit.File{
		{Name: "CODEJR.BAT", Data: toDOS([]byte(bat))},
		{Name: "CODEJR.CFG", Data: toDOS([]byte(cfg))},
		{Name: "CODEJR.INI", Data: toDOS([]byte(ini))},
		{Name: "CODEJR.TXT", Data: toDOS([]byte(modem))},
	}
}

// ProgFiles are the launchers that live beside the programs in
// C:\CODEJR\PROGS. The server keeps them in the workspace, so a sync
// delivers them with CODEJR.EXE. None holds the server's address: the
// client reads it from C:\CODEJR\CODEJR.CFG.
var ProgFiles = map[string]string{
	"NATIVE.BAT": `@ECHO OFF
REM CodeJr native client at 9600 baud. Alt-X hangs up.
CODEJR /P:1 %1 %2 %3
`,
	"FAST.BAT": `@ECHO OFF
REM CodeJr native client, switching the modem to 37287 baud for the call
REM (an exact PCjr speed). Falls back to 9600 if the modem does not answer.
CODEJR /P:1 /F:37287 %1 %2 %3
`,
	"RUN.BAT": `@ECHO OFF
REM Runs a GW-BASIC program, then resets the screen: GW-BASIC leaves the
REM PCjr color palette scrambled (blue on tan).
GWBASIC %1
MODE CO80
`,
}
