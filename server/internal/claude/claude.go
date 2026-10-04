// Package claude runs headless Claude Code (`claude -p`) for one turn and
// turns its stream-json output into a few simple events.
package claude

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os/exec"
	"strings"
	"syscall"
)

// Config says how to start Claude.
type Config struct {
	Bin          string   // path to the claude binary (default "claude")
	Dir          string   // working directory: the PCjr workspace
	SystemPrompt string   // appended to Claude Code's own system prompt
	Model        string   // optional model alias or ID
	Tools        []string // built-in tools Claude may use
}

// EventKind is what happened.
type EventKind int

const (
	Text     EventKind = iota // a chunk of reply text
	ToolUse                   // Claude called a tool (Name, Input set)
	Thinking                  // Claude started a new step; useful for the status line
	Done                      // the turn ended (SessionID, IsError, Result, CostUSD set)
)

// Event is one thing for the screen to show.
type Event struct {
	Kind      EventKind
	Text      string
	Name      string
	Input     map[string]any
	SessionID string
	IsError   bool
	Result    string
	CostUSD   float64
}

// Run sends prompt to Claude, resuming session when it isn't empty, and
// calls emit for each event. It returns once Claude exits. Canceling ctx
// stops Claude.
func Run(ctx context.Context, cfg Config, session, prompt string, emit func(Event)) error {
	bin := cfg.Bin
	if bin == "" {
		bin = "claude"
	}
	args := []string{
		"-p",
		"--output-format", "stream-json",
		"--verbose",
		"--include-partial-messages",
		// File tools only, confined to the workspace, no MCP servers, no
		// user settings: anyone who dials in gets exactly this.
		"--restricted",
		"--strict-mcp-config",
		"--tools", strings.Join(cfg.Tools, ","),
		"--permission-mode", "acceptEdits",
		// Read-only web tools need explicit approval; there's nobody to ask.
		"--allowedTools", "WebSearch,WebFetch",
		"--permission-prompts", "none",
		// Re-read our system prompt on every turn; by default a resumed
		// conversation keeps the prompt it started with.
		"--system-prompt-snapshot", "off",
	}
	if cfg.SystemPrompt != "" {
		args = append(args, "--append-system-prompt", cfg.SystemPrompt)
	}
	if cfg.Model != "" {
		args = append(args, "--model", cfg.Model)
	}
	if session != "" {
		args = append(args, "--resume", session)
	}

	cmd := exec.CommandContext(ctx, bin, args...)
	cmd.Dir = cfg.Dir
	cmd.Stdin = strings.NewReader(prompt)
	// Own process group, so canceling also stops anything Claude started.
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	cmd.Cancel = func() error { return syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM) }
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("starting claude: %w", err)
	}

	sawDone := parse(stdout, emit)
	err = cmd.Wait()
	if ctx.Err() != nil {
		return ctx.Err()
	}
	if err != nil && !sawDone {
		msg := strings.TrimSpace(stderr.String())
		if msg == "" {
			msg = err.Error()
		}
		return fmt.Errorf("claude: %s", lastLine(msg))
	}
	return nil
}

func lastLine(s string) string {
	if i := strings.LastIndexByte(s, '\n'); i >= 0 {
		return s[i+1:]
	}
	return s
}

// The parts of stream-json we read.
type message struct {
	Type      string  `json:"type"`
	Subtype   string  `json:"subtype"`
	SessionID string  `json:"session_id"`
	IsError   bool    `json:"is_error"`
	Result    string  `json:"result"`
	CostUSD   float64 `json:"total_cost_usd"`
	Event     *struct {
		Type         string `json:"type"`
		ContentBlock *struct {
			Type string `json:"type"`
			Name string `json:"name"`
		} `json:"content_block"`
		Delta *struct {
			Type string `json:"type"`
			Text string `json:"text"`
		} `json:"delta"`
	} `json:"event"`
	Message *struct {
		Content []struct {
			Type  string         `json:"type"`
			Name  string         `json:"name"`
			Input map[string]any `json:"input"`
		} `json:"content"`
	} `json:"message"`
}

// parse reads stream-json lines and reports whether a result arrived.
func parse(r io.Reader, emit func(Event)) bool {
	sc := bufio.NewScanner(r)
	sc.Buffer(make([]byte, 1<<20), 16<<20)
	done := false
	for sc.Scan() {
		var m message
		if json.Unmarshal(sc.Bytes(), &m) != nil {
			continue
		}
		switch m.Type {
		case "stream_event":
			if m.Event == nil {
				continue
			}
			switch {
			case m.Event.Type == "message_start":
				emit(Event{Kind: Thinking})
			case m.Event.Delta != nil && m.Event.Delta.Type == "text_delta":
				emit(Event{Kind: Text, Text: m.Event.Delta.Text})
			}
		case "assistant":
			if m.Message == nil {
				continue
			}
			for _, c := range m.Message.Content {
				if c.Type == "tool_use" {
					emit(Event{Kind: ToolUse, Name: c.Name, Input: c.Input})
				}
			}
		case "result":
			done = true
			emit(Event{Kind: Done, SessionID: m.SessionID, IsError: m.IsError, Result: m.Result, CostUSD: m.CostUSD})
		}
	}
	return done
}
