// Command codejr is the CodeJr server: an IBM PCjr dials in through a WiFi
// modem running MS-Kermit, chats with Claude Code, and gets the files
// Claude writes copied onto its own disk.
package main

import (
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
	"codejr/internal/session"
)

func main() {
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
	progs := map[string][]byte{}
	for name, text := range session.ProgFiles {
		progs[name] = []byte(text)
	}
	if err := ws.Provide(progs); err != nil {
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
		Location:  *location,
		LinkLog:   *linkLog,
		Claude: claude.Config{
			Bin:          *bin,
			Dir:          dir,
			SystemPrompt: session.SystemPrompt(*baud),
			Model:        *model,
			Tools:        []string{"Read", "Write", "Edit", "Glob", "Grep", "WebSearch", "WebFetch"},
		},
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
