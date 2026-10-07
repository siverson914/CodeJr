package session

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// The PCjr's PROGS folder started flat. Layout says which loose files
// belong in which app folder; it drives both ORGANIZE.BAT, which moves
// them on the PCjr, and Organize, which moves them in the workspace and
// renames the sync records to match, so nothing is sent again.
//
// Only CodeJr's own files stay at the top: CODEJR.EXE, SCRDUMP.EXE,
// RUN.BAT, SETUP.BAT, ORGANIZE.BAT and the two launcher copies.

// Layout lists, per folder, DOS patterns for the loose files it takes.
var Layout = []struct {
	Dir      string
	Patterns []string
}{
	{"JRAI", []string{"JRAI.EXE", "JM?.SND", "JEND.SND", "JHELLO.SND", "JNET.SND", "JTITLE.SND"}},
	{"SAY", []string{"SAY.EXE", "SAYCLEAN.BAT", "JUNIOR*.SND", "REPLY*.SND", "HELLO*.SND", "HELLO*.SNP"}},
	{"JRDOOM", []string{"JRDOOM.EXE"}},
	{"JRFLIGHT", []string{"JRFLIGHT.EXE"}},
	{"JRCUBE", []string{"JRCUBE.EXE"}},
	{"BASIC", []string{"*.BAS"}},
	{"UTILS", []string{"CJTERM.EXE", "UARTDUMP.EXE", "CJPOLL.BAT", "CJTEST.BAT", "DIAG.BAT", "RTSTEST.BAT", "SLOW.BAT", "914.TXT", "*.LOG", "UK.TXT", "UC.TXT", "SCREEN.TXT"}},
}

// obsolete files are deleted on the PCjr by ORGANIZE.BAT.
var obsolete = []string{"NATIVE.BAT", "FAST.BAT", "CJPULL.$$$", "SAYDUMP.BIN"}

// OrganizeBat is ORGANIZE.BAT: run once in C:\CODEJR\PROGS. DOS 3.3 has
// no MOVE, so each group is copied, then deleted only if the copy is
// there.
func OrganizeBat() string {
	var b strings.Builder
	b.WriteString("@ECHO OFF\nREM Sorts the loose files in C:\\CODEJR\\PROGS into one folder per app.\nREM Safe to run again: it only moves what is still loose.\nC:\nCD \\CODEJR\\PROGS\n")
	for _, l := range Layout {
		fmt.Fprintf(&b, "ECHO %s...\nIF NOT EXIST %s\\NUL MD %s\n", l.Dir, l.Dir, l.Dir)
		for _, p := range l.Patterns {
			if strings.ContainsAny(p, "*?") {
				fmt.Fprintf(&b, "IF EXIST %s COPY %s %s >NUL\n", p, p, l.Dir)
			} else {
				// One file: a copy already in the folder is the newer one
				// (the server sends updates there), so never overwrite it.
				fmt.Fprintf(&b, "IF NOT EXIST %s\\%s IF EXIST %s COPY %s %s >NUL\n", l.Dir, p, p, p, l.Dir)
			}
			fmt.Fprintf(&b, "IF EXIST %s\\%s IF EXIST %s DEL %s\n", l.Dir, p, p, p)
		}
	}
	// CodeJr's own screen grabber must stay at the top (/run uses it); it
	// was once tidied into UTILS by hand.
	b.WriteString("IF NOT EXIST SCRDUMP.EXE IF EXIST UTILS\\SCRDUMP.EXE COPY UTILS\\SCRDUMP.EXE . >NUL\n")
	b.WriteString("IF EXIST SCRDUMP.EXE IF EXIST UTILS\\SCRDUMP.EXE DEL UTILS\\SCRDUMP.EXE\n")
	for _, f := range obsolete {
		fmt.Fprintf(&b, "IF EXIST %s DEL %s\n", f, f)
	}
	b.WriteString("ECHO Done. /run in CodeJr lists every program in its folder.\n")
	return b.String()
}

// folderFor returns the folder a loose top-level file belongs in, or "".
func folderFor(name string) string {
	up := strings.ToUpper(name)
	for _, l := range Layout {
		for _, p := range l.Patterns {
			if ok, _ := filepath.Match(p, up); ok {
				return l.Dir
			}
		}
	}
	return ""
}

// Organize moves the workspace's loose files into their folders and
// renames their sync records (synced.json and the kept copies), so the
// server believes the PCjr has them in the new places. Run ORGANIZE.BAT
// on the PCjr to make that true. It returns what it moved.
func (w *Workspace) Organize() ([]string, error) {
	des, err := os.ReadDir(w.Dir)
	if err != nil {
		return nil, err
	}
	synced := w.loadSynced()
	var moved []string
	for _, de := range des {
		n := de.Name()
		dir := folderFor(n)
		if dir == "" || !de.Type().IsRegular() || strings.HasPrefix(n, ".") {
			continue
		}
		if err := os.MkdirAll(filepath.Join(w.Dir, dir), 0o755); err != nil {
			return moved, err
		}
		dst := filepath.Join(w.Dir, dir, n)
		if _, err := os.Stat(dst); err == nil {
			// A newer copy is already in the folder: drop the loose one.
			os.Remove(filepath.Join(w.Dir, n))
		} else if err := os.Rename(filepath.Join(w.Dir, n), dst); err != nil {
			return moved, err
		}
		up := strings.ToUpper(n)
		key := dir + `\` + up
		if h, ok := synced[up]; ok {
			if _, there := synced[key]; !there {
				synced[key] = h
			}
			delete(synced, up)
		}
		if b := w.Have(up); b != nil {
			if w.Have(key) == nil {
				w.SetHave(key, b)
			}
			os.Remove(w.havePath(up))
		}
		moved = append(moved, key)
	}
	sort.Strings(moved)
	// Records of obsolete files only confuse /files.
	for _, f := range obsolete {
		delete(synced, f)
	}
	return moved, w.saveSynced(synced)
}
