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
