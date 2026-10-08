package session

import (
	"bytes"
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestContextNote(t *testing.T) {
	loc := time.FixedZone("PDT", -7*3600)
	now := time.Date(2026, 10, 3, 14, 16, 0, 0, loc)
	got := contextNote(now, "Encinitas, CA 92024")
	for _, want := range []string{"Saturday, October 3, 2026, 2:16 PM PDT", "Encinitas, CA 92024"} {
		if !strings.Contains(got, want) {
			t.Fatalf("contextNote = %q, want it to contain %q", got, want)
		}
	}
	if strings.Contains(contextNote(now, ""), "The PCjr is in") {
		t.Fatal("location mentioned when none was set")
	}
}

func TestProvide(t *testing.T) {
	w := &Workspace{Dir: t.TempDir()}
	if err := w.Init(); err != nil {
		t.Fatal(err)
	}
	files := map[string][]byte{"NATIVE.BAT": []byte("@ECHO OFF\n")}
	if err := w.Provide(files); err != nil {
		t.Fatal(err)
	}
	pending, hashes, _, _ := w.Pending()
	if len(pending) != 1 || string(pending[0].Data) != "@ECHO OFF\r\n" {
		t.Fatalf("pending = %+v", pending)
	}
	w.MarkSynced(hashes)
	// Providing the same file again must not queue it a second time.
	w.Provide(files)
	if pending, _, _, _ := w.Pending(); len(pending) != 0 {
		t.Fatalf("resent unchanged file: %+v", pending)
	}
}

func TestSetupHasDialAddress(t *testing.T) {
	for _, f := range DOSFiles(DOSSetup{Host: "10.0.0.5", Port: 4860, COM: "COM2", Baud: 9600}) {
		if f.Name == "CODEJR.CFG" {
			if !strings.Contains(string(f.Data), "DIAL=10.0.0.5:4860\r\n") {
				t.Fatalf("CODEJR.CFG = %q", f.Data)
			}
			return
		}
	}
	t.Fatal("no CODEJR.CFG")
}

func TestProjectFolders(t *testing.T) {
	w := &Workspace{Dir: t.TempDir()}
	w.Init()
	os.MkdirAll(filepath.Join(w.Dir, "snake", "deep"), 0o755)
	os.WriteFile(filepath.Join(w.Dir, "snake", "snake.bas"), []byte("10 SYSTEM\n"), 0o644)
	os.WriteFile(filepath.Join(w.Dir, "TOP.BAS"), []byte("10 END\n"), 0o644)
	os.MkdirAll(filepath.Join(w.Dir, "toolongname"), 0o755)
	os.WriteFile(filepath.Join(w.Dir, "toolongname", "A.BAS"), []byte("x"), 0o644)
	files, _, problems, err := w.Pending()
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	for _, f := range files {
		names = append(names, f.Name)
	}
	if strings.Join(names, " ") != `SNAKE\SNAKE.BAS TOP.BAS` {
		t.Fatalf("files = %v", names)
	}
	if len(problems) != 2 { // the folder inside a folder, and the long folder name
		t.Fatalf("problems = %+v", problems)
	}
	if !ValidPath(`SNAKE\SNAKE.BAS`) || !ValidPath("snake/snake.bas") || ValidPath(`A\B\C.BAS`) || ValidPath(`LONGFOLDER\A.BAS`) {
		t.Fatal("ValidPath")
	}
}

func TestOrganize(t *testing.T) {
	w := &Workspace{Dir: t.TempDir()}
	w.Init()
	for _, n := range []string{"JM1.SND", "JRAI.EXE", "HELLO.BAS", "SAY.EXE", "CODEJR.EXE", "CJTERM.EXE", "JUNIOR_M.SND"} {
		os.WriteFile(filepath.Join(w.Dir, n), []byte(n), 0o644)
	}
	// The PCjr has them all, loose.
	_, hashes, _, _ := w.Pending()
	w.MarkSynced(hashes)
	w.SeedHave()

	moved, err := w.Organize()
	if err != nil {
		t.Fatal(err)
	}
	want := `BASIC\HELLO.BAS JRAI\JM1.SND JRAI\JRAI.EXE SAY\JUNIOR_M.SND SAY\SAY.EXE UTILS\CJTERM.EXE`
	if strings.Join(moved, " ") != want {
		t.Fatalf("moved %v", moved)
	}
	if _, err := os.Stat(filepath.Join(w.Dir, "CODEJR.EXE")); err != nil {
		t.Fatal("CODEJR.EXE must stay at the top")
	}
	// Nothing is sent again: the records moved with the files.
	if files, _, _, _ := w.Pending(); len(files) != 0 {
		t.Fatalf("would resend %+v", files)
	}
	if w.Have(`JRAI\JM1.SND`) == nil || w.Have("JM1.SND") != nil {
		t.Fatal("the kept copy didn't move")
	}
	// Running it again moves nothing.
	if moved, _ := w.Organize(); len(moved) != 0 {
		t.Fatalf("second run moved %v", moved)
	}
	bat := OrganizeBat()
	for _, line := range []string{`IF NOT EXIST JRAI\JRAI.EXE IF EXIST JRAI.EXE COPY JRAI.EXE JRAI >NUL`, `IF NOT EXIST JRAI\NUL MD JRAI`, `IF EXIST JM?.SND COPY JM?.SND JRAI >NUL`, `IF EXIST JRAI\JM?.SND IF EXIST JM?.SND DEL JM?.SND`} {
		if !strings.Contains(bat, line) {
			t.Fatalf("ORGANIZE.BAT lacks %q", line)
		}
	}
}

func TestSafeDOS(t *testing.T) {
	for _, c := range []string{"DIR", "dir /w", "DIR/W", `TREE C:\CODEJR /F`, "TYPE LOG.TXT", "MEM", "CHKDSK", "MD NEW", `CD\`, "DIR | SORT", "TYPE A.TXT | FIND \"x\"", "VER"} {
		if !SafeDOS(c) {
			t.Errorf("%q should run without asking", c)
		}
	}
	for _, c := range []string{"DATE", "TIME", "PAUSE", "DIR | MORE", "DIR /P", "DEL *.*", "COMP A B", "FORMAT C:", "date"} {
		if Prompting(c) == "" {
			t.Errorf("%q would wait for typing and must be refused", c)
		}
	}
	for _, c := range []string{"DIR", "DATE 10-07-2026", "DEL OLD.BAS", "TYPE A.TXT", "CHKDSK"} {
		if why := Prompting(c); why != "" {
			t.Errorf("%q refused: %s", c, why)
		}
	}
	for _, c := range []string{"DEL OLD.BAS", "DATE 10-07-2026", "ERASE *.*", "REN A B", "COPY A B", "FORMAT C:", "RD X", "DIR > LIST.TXT", "ECHO hi >> A.TXT", "CHKDSK /F", "SNAKE.EXE", "DIR | DEL X", "", "DEBUG"} {
		if SafeDOS(c) {
			t.Errorf("%q must ask first", c)
		}
	}
}

func TestKeyScript(t *testing.T) {
	k, err := parseKeys("{2}q{0.5}{SPACE}ab{ESC}")
	if err != nil {
		t.Fatal(err)
	}
	// 2 s = 36 ticks = 18 units; 0.5 s = 9 ticks = 5 units (rounded).
	want := []scriptKey{{18, 16, 'q'}, {5, 57, ' '}, {0, 30, 'a'}, {0, 48, 'b'}, {0, 1, 27}}
	if len(k) != len(want) {
		t.Fatalf("got %+v", k)
	}
	for i := range want {
		if k[i] != want[i] {
			t.Fatalf("key %d: got %+v, want %+v", i, k[i], want[i])
		}
	}
	// A pause too long for one byte is split with a key that only waits.
	if k, _ := parseKeys("{60}x"); len(k) != 3 || k[0].scan != 0 || k[2].scan != 45 {
		t.Fatalf("long pause: %+v", k)
	}
	for _, bad := range []string{"{", "{NOPE}", "\x01", strings.Repeat("a", 31)} {
		if _, err := parseKeys(bad); err == nil {
			t.Errorf("%q should be refused", bad)
		}
	}
	for s, want := range map[string]byte{"": 0, "3": 3, "last": 255} {
		if got, err := parseShot(s); err != nil || got != want {
			t.Errorf("parseShot(%q) = %d, %v", s, got, err)
		}
	}
	if _, err := parseShot("soon"); err == nil {
		t.Error(`parseShot("soon") should fail`)
	}
}

func TestDecodeShot(t *testing.T) {
	// Mode 8: pixel (2,0) color 14 is the high nibble of byte 1 in bank 0;
	// pixel (5,1) color 9 is the low nibble of byte 2 in bank 1.
	mem := make([]byte, 16384)
	mem[1] = 0xE0
	mem[0x2000+2] = 0x09
	b := append([]byte{'J', 'S', 8, 0x3F, 0x00, 0x40}, mem...)
	pngData, what, err := DecodeShot(b)
	if err != nil || !strings.Contains(what, "160x200") {
		t.Fatalf("%v %q", err, what)
	}
	img, err := png.Decode(bytes.NewReader(pngData))
	if err != nil {
		t.Fatal(err)
	}
	at := func(x, y int) color.RGBA { return img.(*image.RGBA).RGBAAt(x*4+1, y*2) }
	if at(2, 0) != rgbi[14] || at(5, 1) != rgbi[9] || at(0, 0) != rgbi[0] {
		t.Fatalf("pixels: %v %v %v", at(2, 0), at(5, 1), at(0, 0))
	}
	if _, _, err := DecodeShot([]byte("nope")); err == nil {
		t.Fatal("junk accepted")
	}
}
