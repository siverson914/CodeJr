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
REM /setup delivers files to C:\; move them into C:\CODEJR.
IF EXIST C:\CODEJR.INI COPY C:\CODEJR.INI C:\CODEJR >NUL
IF EXIST C:\CODEJR.INI DEL C:\CODEJR.INI
IF EXIST C:\CODEJR.TXT COPY C:\CODEJR.TXT C:\CODEJR >NUL
IF EXIST C:\CODEJR.TXT DEL C:\CODEJR.TXT
IF EXIST C:\CODEJR.CFG COPY C:\CODEJR.CFG C:\CODEJR >NUL
IF EXIST C:\CODEJR.CFG DEL C:\CODEJR.CFG
IF EXIST C:\CODEJR.EXE COPY C:\CODEJR.EXE C:\CODEJR >NUL
IF EXIST C:\CODEJR.EXE DEL C:\CODEJR.EXE
IF EXIST C:\SCRDUMP.EXE COPY C:\SCRDUMP.EXE C:\CODEJR >NUL
IF EXIST C:\SCRDUMP.EXE DEL C:\SCRDUMP.EXE
CD \CODEJR\PROGS
`

// StubBat is C:\CODEJR.BAT. It never changes again: DOS reads a batch
// file by position, so one that is rewritten while it runs goes wrong,
// and this one is running whenever CodeJr is. The real launcher is
// C:\CODEJR\CJLOOP.BAT; a new one arrives as CJLOOP.NEW and is swapped in
// here, before it starts. Without it, MS-Kermit.
const StubBat = `@ECHO OFF
REM CodeJr. This file stays as it is: the launcher is C:\CODEJR\CJLOOP.BAT.
IF NOT EXIST C:\CODEJR\NUL MD C:\CODEJR
IF EXIST C:\CJLOOP.NEW COPY C:\CJLOOP.NEW C:\CODEJR >NUL
IF EXIST C:\CJLOOP.NEW DEL C:\CJLOOP.NEW
IF EXIST C:\CODEJR\CJLOOP.NEW COPY C:\CODEJR\CJLOOP.NEW C:\CODEJR\CJLOOP.BAT >NUL
IF EXIST C:\CODEJR\CJLOOP.NEW DEL C:\CODEJR\CJLOOP.NEW
IF EXIST C:\CODEJR\CJLOOP.BAT C:\CODEJR\CJLOOP.BAT %1 %2 %3
C:\CJKERMIT.BAT
`

// LoopBat is C:\CODEJR\CJLOOP.BAT: the native client, in a loop. The
// client asks for another round by leaving a file: RUNNEXT.BAT (a program
// /run picked: run it, wait for a key, call back) or REDIAL.TXT (the
// server is restarting: call back at once). The client is in C:\CODEJR;
// before 1.0.22 it was in PROGS.
const LoopBat = `@ECHO OFF
REM CodeJr's launcher. Programs CodeJr writes land in C:\CODEJR\PROGS.
` + prepare + `SET CJEXE=C:\CODEJR\CODEJR.EXE
IF NOT EXIST %CJEXE% SET CJEXE=C:\CODEJR\PROGS\CODEJR.EXE
IF NOT EXIST %CJEXE% GOTO KERMIT
:AGAIN
IF EXIST C:\CODEJR\RUNNEXT.BAT DEL C:\CODEJR\RUNNEXT.BAT
IF EXIST C:\CODEJR\REDIAL.TXT DEL C:\CODEJR\REDIAL.TXT
%CJEXE% /P:1 /L:1 %1 %2 %3
IF EXIST C:\CODEJR\REDIAL.TXT GOTO AGAIN
IF NOT EXIST C:\CODEJR\RUNNEXT.BAT GOTO DONE
COMMAND /C C:\CODEJR\RUNNEXT.BAT
ECHO.
ECHO Press a key to go back to CodeJr.
PAUSE >NUL
CD \CODEJR\PROGS
GOTO AGAIN
:KERMIT
SET CJEXE=
C:\CJKERMIT.BAT
:DONE
SET CJEXE=
CD \
`

// NativeBat is what C:\CODEJR.BAT holds.
const NativeBat = StubBat

// KermitBat is C:\CJKERMIT.BAT: the MS-Kermit fallback. The modem may be
// at CodeJr's fast speed, so the client puts it back first (/R:1).
const KermitBat = `@ECHO OFF
REM CodeJr through MS-Kermit, the fallback. CODEJR is the usual way in.
` + prepare + `IF EXIST C:\CODEJR\CODEJR.EXE C:\CODEJR\CODEJR.EXE /R:1
C:\MSK314\KERMIT TAKE C:\CODEJR\CODEJR.INI
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
		{Name: "CJLOOP.NEW", Data: toDOS([]byte(LoopBat))},
		{Name: "CODEJR.CFG", Data: toDOS([]byte(cfg))},
		{Name: "CODEJR.INI", Data: toDOS([]byte(ini))},
		{Name: "CODEJR.TXT", Data: toDOS([]byte(modem))},
	}
}

// LooseSystem are the names CodeJr's own files had at the top of PROGS
// before they moved to C:\CODEJR.
var LooseSystem = []string{"CODEJR.EXE", "SCRDUMP.EXE", "RUN.BAT", "SETUP.BAT", "ORGANIZE.BAT", "CODEJR.BT", "CJKERMIT.BT"}

// ProgFiles are CodeJr's own files, kept in the workspace's system folder
// so a sync delivers them to C:\CODEJR with CODEJR.EXE. SETUP.BAT installs
// the two files that live in C:\ from the .BT copies. None holds the
// server's address: the client reads C:\CODEJR\CODEJR.CFG.
func ProgFiles() map[string][]byte {
	sys := func(n string) string { return SysDir + "/" + n }
	return map[string][]byte{
		sys("ORGANIZE.BAT"): []byte(OrganizeBat()),
		sys("RUN.BAT"): []byte(`@ECHO OFF
REM Runs a GW-BASIC program, then resets the screen: GW-BASIC leaves the
REM PCjr color palette scrambled (blue on tan).
GWBASIC %1
MODE CO80
`),
		sys("SETUP.BAT"): []byte(`@ECHO OFF
REM Installs CodeJr's two files in C:\ (CODEJR and CJKERMIT).
COPY C:\CODEJR\CODEJR.BT C:\CODEJR.BAT >NUL
COPY C:\CODEJR\CJKERMIT.BT C:\CJKERMIT.BAT >NUL
ECHO Done. Type CODEJR to start CodeJr, or CJKERMIT for the Kermit way in.
`),
		// A new launcher: C:\CODEJR.BAT swaps it in on the next start.
		sys("CJLOOP.NEW"): toDOS([]byte(LoopBat)),
		// .BT isn't a text extension, so these go as they are: DOS endings.
		sys("CODEJR.BT"):   toDOS([]byte(StubBat)),
		sys("CJKERMIT.BT"): toDOS([]byte(KermitBat)),
	}
}
