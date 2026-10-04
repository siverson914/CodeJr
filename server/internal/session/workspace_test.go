package session

import (
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
