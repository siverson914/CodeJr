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

// testdata/think.jsonl is a real transcript of a turn that thought first:
// the thoughts aren't in it, only how many tokens of them so far.
func TestParseThinking(t *testing.T) {
	f, err := os.Open("testdata/think.jsonl")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	thought, size := 0, 0
	parse(f, func(e Event) {
		switch e.Kind {
		case Thought:
			if e.Tokens < thought {
				t.Fatalf("thought went down: %d after %d", e.Tokens, thought)
			}
			thought = e.Tokens
		case Done:
			size = e.Tokens
		}
	})
	if thought < 500 || size < 1000 {
		t.Fatalf("thought = %d, size = %d", thought, size)
	}
}

// A tool call is reported while it is still being written, with its file
// as soon as the name is in.
func TestParseToolInput(t *testing.T) {
	f, err := os.Open("testdata/turn.jsonl")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	var first, last Event
	n := 0
	parse(f, func(e Event) {
		if e.Kind != ToolInput {
			return
		}
		if n == 0 {
			first = e
		}
		last = e
		n++
	})
	if n < 2 || first.Name != "Write" || first.Text != "" || !strings.HasSuffix(last.Text, "/HELLO.BAS") || last.Bytes == 0 {
		t.Fatalf("n = %d, first = %+v, last = %+v", n, first, last)
	}
}
