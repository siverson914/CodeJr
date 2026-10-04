package claude

import (
	"os"
	"strings"
	"testing"
)

// testdata/turn.jsonl is a real `claude -p` stream-json transcript: one
// Write tool call followed by a short text reply.
func TestParseRealTranscript(t *testing.T) {
	f, err := os.Open("testdata/turn.jsonl")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	var text strings.Builder
	var tools []string
	var done Event
	if !parse(f, func(e Event) {
		switch e.Kind {
		case Text:
			text.WriteString(e.Text)
		case ToolUse:
			tools = append(tools, e.Name+" "+e.Input["file_path"].(string))
		case Done:
			done = e
		}
	}) {
		t.Fatal("no result event")
	}
	if len(tools) != 1 || tools[0] != "Write /work/HELLO.BAS" {
		t.Fatalf("tools = %v", tools)
	}
	if !strings.Contains(text.String(), "HELLO.BAS") {
		t.Fatalf("text = %q", text.String())
	}
	if done.SessionID == "" || done.IsError {
		t.Fatalf("done = %+v", done)
	}
}
