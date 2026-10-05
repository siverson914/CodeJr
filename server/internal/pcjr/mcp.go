package pcjr

import (
	"bufio"
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"sort"
)

// A minimal MCP server over stdio (newline-delimited JSON-RPC 2.0): just
// enough for Claude Code to list and call the two tools.

type rpcMsg struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"`
	Params  json.RawMessage `json:"params,omitempty"`
}

var toolList = []map[string]any{
	{
		"name": "build",
		"description": "Compile a project folder's C and assembly for the PCjr (Open Watcom, 8088, large model) into FOLDER\\FOLDER.EXE. " +
			"Programs that include jrgfx.h are linked with the JRGFX graphics library (see JRGFX/JRGFX.H). " +
			"Returns the compiler's messages. The .EXE goes to the PCjr after the turn.",
		"inputSchema": map[string]any{
			"type": "object",
			"properties": map[string]any{
				"project": map[string]any{"type": "string", "description": "the project folder, e.g. SNAKE"},
			},
			"required": []string{"project"},
		},
	},
	{
		"name": "run",
		"description": "Run a .EXE or .COM from a project folder in a headless emulated PCjr (DOSBox) and return what it printed, its LOG.TXT, " +
			"the text screen when it ended, and any .PPM screenshots it wrote (as images). Nobody can press keys: give programs a test or demo " +
			"option that runs by itself and exits. Emulator timing is not the real PCjr's. Up to 120 seconds.",
		"inputSchema": map[string]any{
			"type": "object",
			"properties": map[string]any{
				"project": map[string]any{"type": "string", "description": "the project folder, e.g. SNAKE"},
				"program": map[string]any{"type": "string", "description": "the program in it, e.g. SNAKE.EXE"},
				"args":    map[string]any{"type": "string", "description": "command line arguments, e.g. /DEMO"},
				"seconds": map[string]any{"type": "integer", "description": "stop it after this long (default 20, at most 120)"},
			},
			"required": []string{"project", "program"},
		},
	},
}

// Serve answers MCP requests on in/out until in closes.
func Serve(ctx context.Context, e Env, in io.Reader, out io.Writer) error {
	sc := bufio.NewScanner(in)
	sc.Buffer(make([]byte, 1<<20), 8<<20)
	enc := json.NewEncoder(out)
	reply := func(id json.RawMessage, result any) {
		enc.Encode(map[string]any{"jsonrpc": "2.0", "id": id, "result": result})
	}
	fail := func(id json.RawMessage, code int, msg string) {
		enc.Encode(map[string]any{"jsonrpc": "2.0", "id": id, "error": map[string]any{"code": code, "message": msg}})
	}
	for sc.Scan() {
		var m rpcMsg
		if json.Unmarshal(sc.Bytes(), &m) != nil || m.Method == "" {
			continue
		}
		if len(m.ID) == 0 {
			continue // a notification
		}
		switch m.Method {
		case "initialize":
			var p struct {
				ProtocolVersion string `json:"protocolVersion"`
			}
			json.Unmarshal(m.Params, &p)
			if p.ProtocolVersion == "" {
				p.ProtocolVersion = "2025-06-18"
			}
			reply(m.ID, map[string]any{
				"protocolVersion": p.ProtocolVersion,
				"capabilities":    map[string]any{"tools": map[string]any{}},
				"serverInfo":      map[string]any{"name": "pcjr", "version": "1"},
			})
		case "ping":
			reply(m.ID, map[string]any{})
		case "tools/list":
			reply(m.ID, map[string]any{"tools": toolList})
		case "tools/call":
			var p struct {
				Name      string `json:"name"`
				Arguments struct {
					Project string `json:"project"`
					Program string `json:"program"`
					Args    string `json:"args"`
					Seconds int    `json:"seconds"`
				} `json:"arguments"`
			}
			if err := json.Unmarshal(m.Params, &p); err != nil {
				fail(m.ID, -32602, err.Error())
				continue
			}
			reply(m.ID, e.call(ctx, p.Name, p.Arguments.Project, p.Arguments.Program, p.Arguments.Args, p.Arguments.Seconds))
		default:
			fail(m.ID, -32601, "no method "+m.Method)
		}
	}
	return sc.Err()
}

func (e Env) call(ctx context.Context, tool, project, program, args string, secs int) map[string]any {
	text := func(s string, isErr bool) map[string]any {
		return map[string]any{"content": []any{map[string]any{"type": "text", "text": s}}, "isError": isErr}
	}
	switch tool {
	case "build":
		out, ok, err := e.Build(ctx, project)
		if err != nil {
			return text(err.Error(), true)
		}
		if !ok {
			return text("The build failed:\n"+out, true)
		}
		return text(out, false)
	case "run":
		r, err := e.Run(ctx, project, program, args, secs)
		if err != nil {
			return text(err.Error(), true)
		}
		content := []any{map[string]any{"type": "text", "text": r.Text}}
		var names []string
		for n := range r.Images {
			names = append(names, n)
		}
		sort.Strings(names)
		for _, n := range names {
			content = append(content,
				map[string]any{"type": "text", "text": n + ":"},
				map[string]any{"type": "image", "mimeType": "image/png", "data": base64.StdEncoding.EncodeToString(r.Images[n])})
		}
		return map[string]any{"content": content, "isError": false}
	}
	return text(fmt.Sprintf("no tool %q", tool), true)
}
