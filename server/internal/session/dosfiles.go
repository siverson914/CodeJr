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

// prepare makes the folders and moves files /setup left in C:\ into
// C:\CODEJR. Both launchers start with it.
const prepare = `C:
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
`

// NativeBat is C:\CODEJR.BAT: the native client, in a loop. When /run
// picks a program, the client writes C:\CODEJR\RUNNEXT.BAT and hangs up;
// this runs it, waits for a key, and calls back. Without the client it
// falls back to Kermit.
const NativeBat = `@ECHO OFF
REM CodeJr: talk to Claude from the PCjr. CJKERMIT is the Kermit way in.
REM Programs Claude writes land in C:\CODEJR\PROGS.
` + prepare + `IF NOT EXIST C:\CODEJR\PROGS\CODEJR.EXE GOTO KERMIT
:AGAIN
IF EXIST C:\CODEJR\RUNNEXT.BAT DEL C:\CODEJR\RUNNEXT.BAT
C:\CODEJR\PROGS\CODEJR.EXE /P:1 /L:1 %1 %2 %3
IF NOT EXIST C:\CODEJR\RUNNEXT.BAT GOTO DONE
COMMAND /C C:\CODEJR\RUNNEXT.BAT
ECHO.
ECHO Press a key to go back to CodeJr.
PAUSE >NUL
CD \CODEJR\PROGS
GOTO AGAIN
:KERMIT
C:\MSK314\KERMIT TAKE C:\CODEJR\CODEJR.INI
:DONE
CD \
`

// KermitBat is C:\CJKERMIT.BAT: the MS-Kermit fallback. Keep it the same
// from one version to the next: /setup rewrites it while it runs, and DOS
// reads a batch file by position.
const KermitBat = `@ECHO OFF
REM CodeJr through MS-Kermit, the fallback. CODEJR is the usual way in.
` + prepare + `C:\MSK314\KERMIT TAKE C:\CODEJR\CODEJR.INI
CD \
`

// DOSFiles returns the launchers, the Kermit settings, the modem notes and
// CODEJR.CFG, the one place the native client finds the server's address.
// All are sent to C:\ because Kermit can't create folders; the launchers
// make C:\CODEJR and C:\CODEJR\PROGS and move the rest into C:\CODEJR.
// They are written with DOS line endings.
func DOSFiles(c DOSSetup) []kermit.File {
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

Then from DOS, type CODEJR to dial in (CJKERMIT for the Kermit way in).
Programs Claude writes are in C:\CODEJR\PROGS.

If the screen ever looks scrambled, press Ctrl-L to redraw it.
To leave: type /quit, or press Ctrl-] then C and type EXIT.
`
	cfg := fmt.Sprintf("; CodeJr native client settings. Written by /setup; type /setup to refresh.\nDIAL=%s:%d\n", c.Host, c.Port)
	return []kermit.File{
		{Name: "CODEJR.BAT", Data: toDOS([]byte(NativeBat))},
		{Name: "CJKERMIT.BAT", Data: toDOS([]byte(KermitBat))},
		{Name: "CODEJR.CFG", Data: toDOS([]byte(cfg))},
		{Name: "CODEJR.INI", Data: toDOS([]byte(ini))},
		{Name: "CODEJR.TXT", Data: toDOS([]byte(modem))},
	}
}

// ProgFiles are the files the server keeps in the workspace, so a sync
// delivers them with CODEJR.EXE. SETUP.BAT installs the two launchers in
// C:\ from the .BT copies (a native client can only write to PROGS). None
// holds the server's address: the client reads C:\CODEJR\CODEJR.CFG.
func ProgFiles() map[string][]byte {
	return map[string][]byte{
		"RUN.BAT": []byte(`@ECHO OFF
REM Runs a GW-BASIC program, then resets the screen: GW-BASIC leaves the
REM PCjr color palette scrambled (blue on tan).
GWBASIC %1
MODE CO80
`),
		"SETUP.BAT": []byte(`@ECHO OFF
REM Installs the CodeJr launchers in C:\ (CODEJR and CJKERMIT).
COPY C:\CODEJR\PROGS\CODEJR.BT C:\CODEJR.BAT >NUL
COPY C:\CODEJR\PROGS\CJKERMIT.BT C:\CJKERMIT.BAT >NUL
IF EXIST C:\CODEJR\PROGS\NATIVE.BAT DEL C:\CODEJR\PROGS\NATIVE.BAT
IF EXIST C:\CODEJR\PROGS\FAST.BAT DEL C:\CODEJR\PROGS\FAST.BAT
ECHO Done. Type CODEJR to start CodeJr, or CJKERMIT for the Kermit way in.
`),
		// .BT isn't a text extension, so these go as they are: DOS endings.
		"CODEJR.BT":   toDOS([]byte(NativeBat)),
		"CJKERMIT.BT": toDOS([]byte(KermitBat)),
	}
}
