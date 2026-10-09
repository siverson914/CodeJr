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
	"regexp"
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
	MCPConfig    string   // JSON for --mcp-config (our own tools), or ""
	Allow        []string // more tools allowed without asking (the MCP ones)
}

// EventKind is what happened.
type EventKind int

const (
	Text       EventKind = iota // a chunk of reply text
	ToolUse                     // Claude called a tool (Name, Input set)
	Thinking                    // Claude started a new step; useful for the status line
	Done                        // the turn ended (SessionID, IsError, Result, CostUSD, Tokens set)
	Thought                     // still thinking: Tokens of thought so far (the thoughts themselves aren't sent)
	ToolInput                   // a tool call is being written: Name, Text (its file, once known), Bytes so far
	Compacting                  // Claude Code is shortening a long conversation
	Compacted                   // done: Tokens is the conversation's new size
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
	Tokens    int // Done: the conversation's size, in tokens, as the last request saw it
	Bytes     int
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
		"--allowedTools", strings.Join(append([]string{"WebSearch", "WebFetch"}, cfg.Allow...), ","),
		"--permission-prompts", "none",
		// Re-read our system prompt on every turn; by default a resumed
		// conversation keeps the prompt it started with.
		"--system-prompt-snapshot", "off",
	}
	if cfg.SystemPrompt != "" {
		args = append(args, "--append-system-prompt", cfg.SystemPrompt)
	}
	if cfg.MCPConfig != "" {
		args = append(args, "--mcp-config", cfg.MCPConfig)
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
	Status    string  `json:"status"`           // system/status
	Estimated int     `json:"estimated_tokens"` // system/thinking_tokens
	Compact   *struct {
		PostTokens int `json:"post_tokens"`
	} `json:"compact_metadata"`
	Usage *usage `json:"usage"`
	Event *struct {
		Type         string `json:"type"`
		ContentBlock *struct {
			Type string `json:"type"`
			Name string `json:"name"`
		} `json:"content_block"`
		Delta *struct {
			Type        string `json:"type"`
			Text        string `json:"text"`
			PartialJSON string `json:"partial_json"`
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

type usage struct {
	Input      int     `json:"input_tokens"`
	CacheRead  int     `json:"cache_read_input_tokens"`
	CacheWrite int     `json:"cache_creation_input_tokens"`
	Output     int     `json:"output_tokens"`
	Iterations []usage `json:"iterations"`
}

// size is how big the conversation was at the turn's last request: what
// went in plus what came out. The top-level numbers add up every request
// of the turn, so the last iteration is used when there is one.
func (u *usage) size() int {
	if u == nil {
		return 0
	}
	if n := len(u.Iterations); n > 0 {
		u = &u.Iterations[n-1]
	}
	return u.Input + u.CacheRead + u.CacheWrite + u.Output
}

var filePathRE = regexp.MustCompile(`"file_path"\s*:\s*"((?:[^"\\]|\\.)*)"`)

// toolStep is how many more bytes of a tool call arrive between two
// ToolInput events: a long file takes minutes to write.
const toolStep = 512

// parse reads stream-json lines and reports whether a result arrived.
func parse(r io.Reader, emit func(Event)) bool {
	sc := bufio.NewScanner(r)
	sc.Buffer(make([]byte, 1<<20), 16<<20)
	done := false
	var tool, toolFile string // the tool call being written, and its file
	var toolJSON []byte
	toolSent := 0
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
			case m.Event.Type == "content_block_start" && m.Event.ContentBlock != nil:
				tool, toolFile, toolJSON, toolSent = "", "", toolJSON[:0], 0
				if m.Event.ContentBlock.Type == "tool_use" {
					tool = m.Event.ContentBlock.Name
					emit(Event{Kind: ToolInput, Name: tool})
				}
			case m.Event.Delta != nil && m.Event.Delta.Type == "input_json_delta" && tool != "":
				toolJSON = append(toolJSON, m.Event.Delta.PartialJSON...)
				if toolFile == "" {
					if f := filePathRE.FindSubmatch(toolJSON); f != nil {
						json.Unmarshal([]byte(`"`+string(f[1])+`"`), &toolFile)
						toolSent = 0 // say the name at once
					}
				}
				if toolSent == 0 || len(toolJSON)-toolSent >= toolStep {
					toolSent = len(toolJSON)
					emit(Event{Kind: ToolInput, Name: tool, Text: toolFile, Bytes: len(toolJSON)})
				}
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
		case "system":
			switch m.Subtype {
			case "thinking_tokens":
				emit(Event{Kind: Thought, Tokens: m.Estimated})
			case "status":
				if m.Status == "compacting" {
					emit(Event{Kind: Compacting})
				}
			case "compact_boundary":
				if m.Compact != nil {
					emit(Event{Kind: Compacted, Tokens: m.Compact.PostTokens})
				}
			}
		case "result":
			done = true
			emit(Event{Kind: Done, SessionID: m.SessionID, IsError: m.IsError, Result: m.Result, CostUSD: m.CostUSD, Tokens: m.Usage.size()})
		}
	}
	return done
}
