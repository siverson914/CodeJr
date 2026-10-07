// Command codejr is the CodeJr server: an IBM PCjr dials in through a WiFi
// modem running MS-Kermit, chats with Claude Code, and gets the files
// Claude writes copied onto its own disk.
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"net/netip"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"

	"codejr/internal/claude"
	"codejr/internal/pcjr"
	"codejr/internal/session"
)

func main() {
	// codejr mcp -workspace DIR -repo DIR: the PCjr's build and run tools,
	// started by Claude Code over stdio (see internal/pcjr).
	// codejr organize -workspace DIR: sort the workspace's loose files into
	// app folders, the same way ORGANIZE.BAT does on the PCjr.
	if len(os.Args) > 1 && os.Args[1] == "organize" {
		fs := flag.NewFlagSet("organize", flag.ExitOnError)
		wsDir := fs.String("workspace", "", "Claude's folder for the PCjr")
		fs.Parse(os.Args[2:])
		w := &session.Workspace{Dir: *wsDir}
		moved, err := w.Organize()
		for _, m := range moved {
			fmt.Println(m)
		}
		if err != nil {
			log.Fatal(err)
		}
		fmt.Printf("%d files moved. Run ORGANIZE on the PCjr to match.\n", len(moved))
		return
	}
	if len(os.Args) > 1 && os.Args[1] == "mcp" {
		fs := flag.NewFlagSet("mcp", flag.ExitOnError)
		ws := fs.String("workspace", "", "Claude's folder for the PCjr")
		repo := fs.String("repo", "", "the CodeJr checkout (lib/, client/)")
		fs.Parse(os.Args[2:])
		w, _ := filepath.Abs(*ws)
		r, _ := filepath.Abs(*repo)
		if err := pcjr.Serve(context.Background(), pcjr.Env{Workspace: w, Repo: r}, os.Stdin, os.Stdout); err != nil {
			log.Fatal(err)
		}
		return
	}

	listen := flag.String("listen", ":4860", "address to listen on (4860 is the PCjr's IBM machine type)")
	wsDir := flag.String("workspace", "workspace", `Claude's working folder; mirrors C:\CODEJR\PROGS on the PCjr`)
	baud := flag.Int("baud", 9600, "serial speed between the PCjr and its modem; output is paced to it")
	model := flag.String("model", "", "Claude model (default: your Claude Code default)")
	bin := flag.String("claude", "claude", "path to the claude command")
	allow := flag.String("allow", "127.0.0.0/8,::1/128,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16,100.64.0.0/10",
		"comma-separated networks allowed to connect")
	com := flag.String("com", "COM2", "Kermit serial port that /setup writes into CODEJR.INI")
	writeDOS := flag.String("write-dos", "", "write the PCjr launcher files into this folder and exit")
	dosHost := flag.String("dos-host", "", "address for -write-dos (default: this machine's LAN address)")
	location := flag.String("location", "", `where the PCjr is, told to Claude (e.g. "Encinitas, CA 92024")`)
	linkLog := flag.String("linklog", "", "append a timeline of native protocol frames to this file (diagnostics)")
	trace := flag.String("trace", "", "append everything sent to callers to this file (for debugging the screen)")
	speech := flag.String("speech", "auto", `speech converter for /voice (apps/say/mksnd.py); "auto" finds it in the repo, "" turns voice off`)
	flag.Parse()

	_, portStr, err := net.SplitHostPort(*listen)
	if err != nil {
		log.Fatalf("-listen: %v", err)
	}
	port, _ := strconv.Atoi(portStr)

	if *writeDOS != "" {
		host := *dosHost
		if host == "" {
			host = lanAddr()
		}
		for _, f := range session.DOSFiles(session.DOSSetup{Host: host, Port: port, COM: strings.ToUpper(*com), Baud: *baud}) {
			if err := os.WriteFile(filepath.Join(*writeDOS, f.Name), f.Data, 0o644); err != nil {
				log.Fatal(err)
			}
		}
		log.Printf("wrote PCjr files to %s (dial %s:%d on %s)", *writeDOS, host, port, *com)
		return
	}

	dir, err := filepath.Abs(*wsDir)
	if err != nil {
		log.Fatal(err)
	}
	ws := &session.Workspace{Dir: dir}
	if err := ws.Init(); err != nil {
		log.Fatal(err)
	}
	if err := ws.SeedHave(); err != nil {
		log.Print(err)
	}
	if cfg := speechTool(*speech); cfg != "" {
		go session.EnsureCannedClips(cfg, dir, log.Printf)
	}
	if err := ws.Provide(session.ProgFiles()); err != nil {
		log.Fatal(err)
	}
	var nets []netip.Prefix
	for _, s := range strings.Split(*allow, ",") {
		p, err := netip.ParsePrefix(strings.TrimSpace(s))
		if err != nil {
			log.Fatalf("-allow: %v", err)
		}
		nets = append(nets, p)
	}

	cfg := session.Config{
		Workspace: ws,
		Baud:      *baud,
		Port:      port,
		COM:       strings.ToUpper(*com),
		Speech:    speechTool(*speech),
		Location:  *location,
		LinkLog:   *linkLog,
		Claude: claude.Config{
			Bin:          *bin,
			Dir:          dir,
			SystemPrompt: session.SystemPrompt(*baud),
			Model:        *model,
			Tools:        []string{"Read", "Write", "Edit", "Glob", "Grep", "WebSearch", "WebFetch"},
			MCPConfig:    mcpConfig(dir),
			Allow:        []string{"mcp__pcjr__build", "mcp__pcjr__run"},
		},
	}
	scrdump, _ := os.ReadFile(filepath.Join(repoDir(), "client", "SCRDUMP.EXE"))
	sayExe, _ := os.ReadFile(filepath.Join(repoDir(), "apps", "say", "SAY.EXE"))
	if err := ws.Provide(map[string][]byte{
		"JRGFX/JRGFX.H": jrgfxHeader(),
		// /run captures the screen with it after a program ends.
		"SCRDUMP.EXE": scrdump,
		// /voice plays clips with it; the repo's build matches the clips
		// the converter makes (nil, and skipped, if it isn't built).
		"SAY/SAY.EXE": sayExe,
	}); err != nil {
		log.Print(err)
	}

	ln, err := net.Listen("tcp", *listen)
	if err != nil {
		log.Fatal(err)
	}
	log.Printf("CodeJr listening on %s, workspace %s, %d baud", ln.Addr(), dir, *baud)

	// One caller at a time: they share the workspace and the conversation.
	var mu sync.Mutex
	busy := false
	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Print(err)
			continue
		}
		addr := conn.RemoteAddr().String()
		ap, _ := netip.ParseAddrPort(addr)
		if !allowed(nets, ap.Addr().Unmap()) {
			log.Printf("%s: refused (not in -allow)", addr)
			conn.Close()
			continue
		}
		mu.Lock()
		if busy {
			mu.Unlock()
			log.Printf("%s: refused (busy)", addr)
			fmt.Fprint(conn, "\r\nCodeJr is busy with another caller. Try again later.\r\n")
			conn.Close()
			continue
		}
		busy = true
		mu.Unlock()
		go func() {
			defer func() { mu.Lock(); busy = false; mu.Unlock() }()
			var c net.Conn = conn
			if *trace != "" {
				if f, err := os.OpenFile(*trace, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644); err == nil {
					defer f.Close()
					c = tracedConn{conn, f}
				}
			}
			session.Serve(cfg, c, addr)
		}()
	}
}

// tracedConn copies what we send into a file.
type tracedConn struct {
	net.Conn
	f *os.File
}

func (t tracedConn) Write(b []byte) (int, error) {
	t.f.Write(b)
	return t.Conn.Write(b)
}

// lanAddr finds the address other machines on the LAN reach us at. The
// UDP "dial" only picks a route; nothing is sent.
func lanAddr() string {
	c, err := net.Dial("udp", "192.168.0.1:9")
	if err != nil {
		return "127.0.0.1"
	}
	defer c.Close()
	return c.LocalAddr().(*net.UDPAddr).IP.String()
}

func allowed(nets []netip.Prefix, a netip.Addr) bool {
	for _, n := range nets {
		if n.Contains(a) {
			return true
		}
	}
	return false
}

func init() {
	log.SetFlags(log.Ltime)
	log.SetOutput(os.Stdout)
}

// repoDir is the CodeJr checkout this binary was built in (server/codejr).
func repoDir() string {
	exe, err := os.Executable()
	if err != nil {
		return ""
	}
	return filepath.Dir(filepath.Dir(exe))
}

// mcpConfig starts this same binary as the PCjr tools' MCP server.
func mcpConfig(ws string) string {
	exe, err := os.Executable()
	if err != nil {
		return ""
	}
	b, _ := json.Marshal(map[string]any{"mcpServers": map[string]any{
		"pcjr": map[string]any{"command": exe, "args": []string{"mcp", "-workspace", ws, "-repo", repoDir()}},
	}})
	return string(b)
}

// jrgfxHeader is the graphics library's header, kept in the workspace so
// the PCjr's Claude can read it (the build tool uses the repo's copy).
func jrgfxHeader() []byte {
	b, err := os.ReadFile(filepath.Join(repoDir(), "lib", "jrgfx.h"))
	if err != nil {
		return nil
	}
	return b
}

// speechTool resolves the -speech flag.
func speechTool(f string) string {
	if f != "auto" {
		return f
	}
	p := filepath.Join(repoDir(), "apps", "say", "mksnd.py")
	if _, err := os.Stat(p); err != nil {
		return ""
	}
	return p
}
