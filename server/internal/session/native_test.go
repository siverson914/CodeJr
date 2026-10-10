package session

import (
	"bytes"
	"encoding/binary"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"codejr/internal/cjp"
	"codejr/internal/claude"
	"codejr/internal/kermit"
	"codejr/internal/lz"
)

// fakeClient plays CODEJR.EXE over the CodeJr protocol.
type fakeClient struct {
	t    *testing.T
	link *cjp.Link
	text strings.Builder
	stat string
}

// next waits for a message of type typ, collecting text and status lines
// on the way.
func (c *fakeClient) next(typ byte) cjp.Msg {
	c.t.Helper()
	deadline := time.After(10 * time.Second)
	for {
		select {
		case m := <-c.link.Recv():
			switch m.Type {
			case cjp.Text:
				c.text.Write(m.Payload[1:])
			case cjp.Status:
				c.stat = string(m.Payload)
			}
			if m.Type == typ {
				return m
			}
		case <-c.link.Done():
			c.t.Fatalf("link closed waiting for type %d; text so far %q", typ, c.text.String())
		case <-deadline:
			c.t.Fatalf("timed out waiting for type %d; text so far %q", typ, c.text.String())
		}
	}
}

// waitText waits until the transcript contains s.
func (c *fakeClient) waitText(s string) {
	c.t.Helper()
	deadline := time.After(10 * time.Second)
	for !strings.Contains(c.text.String(), s) {
		select {
		case m := <-c.link.Recv():
			if m.Type == cjp.Text {
				c.text.Write(m.Payload[1:])
			}
		case <-deadline:
			c.t.Fatalf("timed out waiting for %q; got %q", s, c.text.String())
		}
	}
}

func startNative(t *testing.T) (*fakeClient, *Workspace, chan struct{}) {
	return startNativeAs(t, "CODEJR/1 test 0.1", nil)
}

// startNativeAs connects a fake client that greets with hello. seed, if
// given, puts files in the workspace before the call.
func startNativeAs(t *testing.T, hello string, seed func(dir string)) (*fakeClient, *Workspace, chan struct{}) {
	dir := t.TempDir()
	ws := &Workspace{Dir: dir}
	ws.Init()
	if seed != nil {
		seed(dir)
	}
	fake, _ := filepath.Abs("testdata/fakeclaude.sh")
	cfg := Config{Workspace: ws, Baud: 0, Claude: claude.Config{Bin: fake, Dir: dir}}
	server, conn := net.Pipe()
	done := make(chan struct{})
	go func() { Serve(cfg, server, "native-test"); close(done) }()

	in := kermit.Pump(conn)
	// Dial like the client does, and wait for the READY banner.
	go conn.Write([]byte("ATDT192.168.0.178:4860\r"))
	var seen strings.Builder
	for !strings.Contains(seen.String(), "READY") {
		select {
		case b := <-in:
			seen.WriteByte(b)
		case <-time.After(5 * time.Second):
			t.Fatalf("no READY banner; got %q", seen.String())
		}
	}
	link := cjp.NewLink(in, conn, 300*time.Millisecond, false)
	c := &fakeClient{t: t, link: link}
	if err := link.Send(cjp.Hello, []byte(hello)); err != nil {
		t.Fatal(err)
	}
	if m := c.next(cjp.Hello); !strings.HasPrefix(string(m.Payload), "CODEJR/") {
		t.Fatalf("server hello = %q", m.Payload)
	}
	c.waitText("Welcome to CodeJr")
	return c, ws, done
}

// receiveFile collects one file transfer (FileBegin, FileData..., FileEnd)
// and acknowledges it like CODEJR.EXE does.
func (c *fakeClient) receiveFile() (string, []byte) {
	c.t.Helper()
	begin := c.next(cjp.FileBegin)
	size := binary.LittleEndian.Uint32(begin.Payload[:4])
	name := string(begin.Payload[4:])
	var data []byte
	deadline := time.After(10 * time.Second)
	for {
		select {
		case m := <-c.link.Recv():
			switch m.Type {
			case cjp.FileData:
				data = append(data, m.Payload...)
			case cjp.FileEnd:
				if uint32(len(data)) != size {
					c.t.Fatalf("%s: got %d bytes, header said %d", name, len(data), size)
				}
				c.link.Send(cjp.FileResult, append([]byte{0}, name...))
				return name, data
			case cjp.Text:
				c.text.Write(m.Payload[1:])
			}
		case <-deadline:
			c.t.Fatalf("timed out receiving %s", name)
		}
	}
}

func TestNativeCall(t *testing.T) {
	c, ws, done := startNative(t)

	// A turn: Claude writes HELLO.BAS and the server sends it over the link.
	c.link.Send(cjp.Input, []byte("hello there"))
	c.waitText("[writes HELLO.BAS]")
	name, data := c.receiveFile()
	if name != "HELLO.BAS" || string(data) != "10 PRINT \"HI\"\r\n20 END\r\n" {
		t.Fatalf("got %s %q", name, data)
	}
	c.waitText("Saved HELLO.BAS")
	if !strings.Contains(c.text.String(), "you asked: hello there") {
		t.Fatalf("reply missing: %q", c.text.String())
	}
	if strings.ContainsRune(c.text.String(), '\u2014') {
		t.Fatal("Unicode reached the client")
	}
	if ws.Session() != "fake-session" {
		t.Fatalf("session = %q", ws.Session())
	}

	// Synced: /files shows no star.
	c.text.Reset()
	c.link.Send(cjp.Input, []byte("/files"))
	c.waitText("HELLO.BAS")
	if strings.Contains(c.text.String(), "* HELLO.BAS") {
		t.Fatal("HELLO.BAS still marked unsynced")
	}

	// /get NAME brings it over again.
	c.link.Send(cjp.Input, []byte("/get hello.bas"))
	if name, _ := c.receiveFile(); name != "HELLO.BAS" {
		t.Fatalf("resent %s", name)
	}

	// A file the client refuses stays unsynced.
	os.WriteFile(filepath.Join(ws.Dir, "FULL.TXT"), []byte("x\n"), 0o644)
	c.link.Send(cjp.Input, []byte("/sync"))
	begin := c.next(cjp.FileBegin)
	if string(begin.Payload[4:]) != "FULL.TXT" {
		t.Fatalf("expected FULL.TXT, got %q", begin.Payload[4:])
	}
	c.next(cjp.FileEnd)
	c.link.Send(cjp.FileResult, append([]byte{1}, "disk full"...))
	c.waitText("Couldn't save FULL.TXT on the PCjr: disk full")
	if files, _, _, _ := ws.Pending(); len(files) != 1 || files[0].Name != "FULL.TXT" {
		t.Fatalf("pending after refusal: %+v", files)
	}

	// Unknown command, help, then /quit: the server says Bye and hangs up.
	c.link.Send(cjp.Input, []byte("/nope"))
	c.waitText("Unknown command")
	c.link.Send(cjp.Input, []byte("/help"))
	c.waitText("/files")
	// A line too long for one frame arrives in pieces: InputMore, then Input.
	c.link.Send(cjp.InputMore, []byte("/no"))
	c.link.Send(cjp.InputMore, []byte("pe "))
	c.link.Send(cjp.Input, []byte("again"))
	c.waitText("Unknown command")
	c.link.Send(cjp.Input, []byte("/quit"))
	c.next(cjp.Bye)
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		t.Fatal("server didn't hang up")
	}
}

func TestNativeClientBye(t *testing.T) {
	c, _, done := startNative(t)
	c.link.Send(cjp.Bye, nil)
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		t.Fatal("server didn't end the call after the client's Bye")
	}
}

func TestWrapper(t *testing.T) {
	var w wrapper
	long := strings.Repeat("word ", 40) // 200 columns
	got := w.feed(long) + w.flush()
	for _, line := range strings.Split(got, "\n") {
		if len(line) > wrapWidth {
			t.Fatalf("line %d columns: %q", len(line), line)
		}
	}
	if strings.Join(strings.Fields(got), " ") != strings.TrimSpace(long) {
		t.Fatalf("words changed: %q", got)
	}
	// Streamed in tiny pieces gives the same result as all at once.
	var w2 wrapper
	var b strings.Builder
	for i := 0; i < len(long); i += 3 {
		b.WriteString(w2.feed(long[i:min(i+3, len(long))]))
	}
	b.WriteString(w2.flush())
	if b.String() != got {
		t.Fatalf("streaming changed wrapping:\n%q\n%q", b.String(), got)
	}
	// A word longer than a line is split, not lost.
	var w3 wrapper
	huge := strings.Repeat("x", 100)
	if out := w3.feed(huge) + w3.flush(); strings.ReplaceAll(out, "\n", "") != huge {
		t.Fatalf("long word mangled: %q", out)
	}
}

func TestNativeTextIsCoalesced(t *testing.T) {
	c, _, _ := startNative(t)
	c.text.Reset()
	c.link.Send(cjp.Input, []byte("stream"))
	frames := 0
	deadline := time.After(10 * time.Second)
	for strings.Count(c.text.String(), "word") < 60 {
		select {
		case m := <-c.link.Recv():
			if m.Type == cjp.Text {
				frames++
				c.text.Write(m.Payload[1:])
			}
		case <-deadline:
			t.Fatalf("got %d words in %d frames", strings.Count(c.text.String(), "word"), frames)
		}
	}
	// 60 deltas plus the echoed prompt: without coalescing that's 61+
	// frames; with it, a handful.
	if frames > 8 {
		t.Fatalf("60 streamed pieces took %d frames", frames)
	}
	t.Logf("60 streamed pieces arrived in %d frames", frames)
}

func TestNativeReplyEndsItsLine(t *testing.T) {
	c, _, _ := startNative(t)
	c.text.Reset()
	// The fake Claude's reply has no trailing newline.
	c.link.Send(cjp.Input, []byte("hello there"))
	c.receiveFile()
	c.waitText("Saved HELLO.BAS")
	c.link.Send(cjp.Input, []byte("/new"))
	c.waitText("Fresh conversation")
	for _, line := range strings.Split(c.text.String(), "\n") {
		if strings.Contains(line, "/new") && !strings.HasPrefix(line, "/new") {
			t.Fatalf("command echoed mid-line: %q", line)
		}
		if strings.Contains(line, "> hello there") && !strings.HasPrefix(line, "> ") {
			t.Fatalf("prompt echoed mid-line: %q", line)
		}
	}
}

// pullFile fetches an offered file the way CODEJR.EXE 0.2 does: a Pull
// datagram per chunk, then the chunk's Block datagrams.
func (c *fakeClient) pullFile() (string, []byte) {
	c.t.Helper()
	offer := c.next(cjp.FileOffer)
	size := int(binary.LittleEndian.Uint32(offer.Payload))
	chunks := int(binary.LittleEndian.Uint16(offer.Payload[4:]))
	name := string(offer.Payload[6:])
	var data []byte
	for k := 0; k < chunks; k++ {
		blocks := map[int][]byte{}
		complete := func() ([]byte, bool) {
			b0, ok := blocks[0]
			if !ok {
				return nil, false
			}
			total := (7 + int(binary.LittleEndian.Uint16(b0[3:])) + cjp.BlockSize - 1) / cjp.BlockSize
			var p []byte
			for i := 0; i < total; i++ {
				b, ok := blocks[i]
				if !ok {
					return nil, false
				}
				p = append(p, b...)
			}
			return p, true
		}
		req := make([]byte, 6)
		binary.LittleEndian.PutUint16(req, uint16(k))
		binary.LittleEndian.PutUint32(req[2:], 0xFFFFFFFF)
		c.link.SendDatagram(cjp.Pull, req)
		deadline := time.After(10 * time.Second)
		var payload []byte
		for payload == nil {
			select {
			case m := <-c.link.Recv():
				if m.Type == cjp.Block && m.Ack == byte(k) {
					blocks[int(m.Seq)] = m.Payload
					if p, ok := complete(); ok {
						payload = p
					}
				}
			case <-deadline:
				c.t.Fatalf("%s: chunk %d never completed (%d blocks)", name, k, len(blocks))
			}
		}
		rawLen := int(binary.LittleEndian.Uint16(payload[1:]))
		raw := payload[7:]
		if payload[0] == 1 {
			var err error
			if raw, err = lz.Decompress(raw, rawLen); err != nil {
				c.t.Fatal(err)
			}
		}
		if len(raw) != rawLen || cjp.CRC16(raw) != binary.LittleEndian.Uint16(payload[5:]) {
			c.t.Fatalf("%s: chunk %d fails its checks", name, k)
		}
		data = append(data, raw...)
	}
	if len(data) != size {
		c.t.Fatalf("%s: %d bytes, offer said %d", name, len(data), size)
	}
	c.link.Send(cjp.FileResult, append([]byte{0}, name...))
	return name, data
}

func TestNativePull(t *testing.T) {
	// 10 KB: three chunks, part compressible and part not.
	big := append(bytes.Repeat([]byte("20 GOSUB 1000:REM AGAIN\r\n"), 200), make([]byte, 5000)...)
	for i := 5000; i < len(big); i += 3 {
		big[i] = byte(i * 31 >> 3)
	}
	c, ws, done := startNativeAs(t, "CODEJR/2 test 0.2 baud=115200", func(dir string) {
		os.WriteFile(filepath.Join(dir, "BIG.BIN"), big, 0o644)
	})

	// A line typed before the first transfer is done must not be lost.
	c.link.Send(cjp.Input, []byte("hello there"))

	// The file waiting in the workspace is offered right after the welcome.
	name, data := c.pullFile()
	if name != "BIG.BIN" || !bytes.Equal(data, big) {
		t.Fatalf("got %s, %d bytes", name, len(data))
	}

	// The early line now runs; the file Claude writes goes the same way.
	name, data = c.pullFile()
	if name != "HELLO.BAS" || string(data) != "10 PRINT \"HI\"\r\n20 END\r\n" {
		t.Fatalf("got %s %q", name, data)
	}
	// The server records the file once the client reports it saved.
	for i := 0; ; i++ {
		files, _, _, _ := ws.Pending()
		if len(files) == 0 {
			break
		}
		if i == 50 {
			t.Fatalf("still pending: %+v", files)
		}
		time.Sleep(100 * time.Millisecond)
	}

	c.link.Send(cjp.Input, []byte("/quit"))
	c.next(cjp.Bye)
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		t.Fatal("server didn't hang up")
	}
}

// A conversation past the tidy-up line is shortened after the turn, with
// Claude Code's own /compact, and the user is told.
func TestNativeTidiesLongConversation(t *testing.T) {
	c, ws, _ := startNative(t)
	c.link.Send(cjp.Input, []byte("big"))
	c.waitText("A big answer.")
	c.waitText("tidied up: 120K tokens down to 900")
	if b, _ := os.ReadFile(filepath.Join(ws.Dir, ".lastprompt")); string(b) != compactPrompt {
		t.Fatalf("the tidy-up sent %q", b)
	}
	if ws.Context() != 900 {
		t.Fatalf("context = %d", ws.Context())
	}
}

func TestDescribeWriting(t *testing.T) {
	for _, c := range []struct {
		e    claude.Event
		want string
	}{
		{claude.Event{Name: "Write"}, "CodeJr is writing a file"},
		{claude.Event{Name: "Write", Text: "/w/XONIXJR/xonixjr.c", Bytes: 300}, "CodeJr is writing XONIXJR.C"},
		{claude.Event{Name: "Write", Text: "/w/XONIXJR/xonixjr.c", Bytes: 12500}, "CodeJr is writing XONIXJR.C, 12 KB so far"},
		{claude.Event{Name: "Edit", Text: "/w/A.C", Bytes: 2048}, "CodeJr is changing A.C, 2 KB so far"},
		{claude.Event{Name: "mcp__pcjr__build"}, "CodeJr is getting a tool ready"},
	} {
		if got := describeWriting(c.e); got != c.want {
			t.Errorf("%+v: %q, want %q", c.e, got, c.want)
		}
	}
	if kilo(850) != "850" || kilo(1200) != "1.2K" || kilo(153163) != "153K" {
		t.Errorf("kilo: %s %s %s", kilo(850), kilo(1200), kilo(153163))
	}
}

// /usage: a turn that wrote a file counts overall, for today, for this
// call, and the file that went to the PCjr is counted with its size.
func TestNativeUsage(t *testing.T) {
	c, ws, _ := startNative(t)
	c.link.Send(cjp.Input, []byte("hello there"))
	c.waitText("[writes HELLO.BAS]")
	c.receiveFile()
	c.waitText("Saved HELLO.BAS")
	c.text.Reset()
	c.link.Send(cjp.Input, []byte("/usage"))
	c.waitText("Counted since")
	out := c.text.String()
	for _, want := range []string{"all time", "today", "this call", "Messages", "Files sent", "Files changed"} {
		if !strings.Contains(out, want) {
			t.Fatalf("/usage lacks %q:\n%s", want, out)
		}
	}
	for _, line := range strings.Split(out, "\n") {
		if len(line) > 79 {
			t.Fatalf("a /usage line is %d columns: %q", len(line), line)
		}
	}
	u := ws.Usage()
	if u.Total.Turns != 1 || u.Total.TypedChars != len("hello there") || u.Total.FilesSent != 1 || u.Total.Edits != 1 || u.Total.Calls != 1 {
		t.Fatalf("totals: %+v", u.Total)
	}
	// A project's row: its files, its builds, and the turns that touched it.
	ws.AddUsage("SNAKE", func(s *Stats) { s.Builds++ })
	ws.AddProjectUsage("SNAKE", func(s *Stats) { s.Turns++ })
	if u = ws.Usage(); u.Projects["SNAKE"].Builds != 1 || u.Projects["SNAKE"].Turns != 1 || u.Total.Turns != 1 || u.Total.Builds != 1 {
		t.Fatalf("project: %+v total %+v", u.Projects["SNAKE"], u.Total)
	}
	if txt := ws.UsageText("snake", nil); !strings.Contains(txt, "SNAKE") || !strings.Contains(txt, "Builds") {
		t.Fatalf("project text:\n%s", txt)
	}
	if txt := ws.UsageText("NOPE", nil); !strings.Contains(txt, "No usage recorded for NOPE") {
		t.Fatalf("unknown project:\n%s", txt)
	}
}

// A run's key script always ends with the way out: Esc three times once
// the limit has passed.
func TestWithLimit(t *testing.T) {
	esc := namedKey["ESC"][0]
	total := func(ks []scriptKey) (units int) {
		for _, k := range ks[:len(ks)-2] {
			units += int(k.delay)
		}
		return units
	}
	// No keys of its own: pauses up to 180 s, then Esc, Esc, Esc.
	ks, err := withLimit(nil, 0)
	if err != nil || len(ks) != 9 || ks[6].scan != esc || ks[7].scan != esc || ks[8].scan != esc || ks[7].delay != 9 {
		t.Fatalf("default: %v %+v", err, ks)
	}
	if got := total(ks); got != 1638 { // 180 s in units of 2 ticks
		t.Fatalf("default limit comes to %d units", got)
	}
	// After a script of its own, the rest of the time.
	own, _ := parseKeys("{4}{SPACE}{2}q")
	ks, err = withLimit(own, 30)
	if err != nil || len(ks) != 5 || ks[0] != own[0] || ks[1] != own[1] || total(ks) != 273 {
		t.Fatalf("after a script: %v %+v", err, ks)
	}
	// A script longer than the limit still gets its 2 seconds.
	long, _ := parseKeys("{60}x")
	if ks, _ = withLimit(long, 10); ks[len(ks)-3].delay != 18 {
		t.Fatalf("past the limit: %+v", ks)
	}
	// Too much for the client's table.
	many, _ := parseKeys("abcdefghijklmnopqrstuvwxyz")
	if _, err = withLimit(many, 180); err == nil {
		t.Fatal("26 keys and a 180 s limit fit?")
	}
}
