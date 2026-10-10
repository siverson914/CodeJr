// Package pcjr gives the PCjr's Claude two narrow tools, served over MCP:
// build a project folder with Open Watcom, and run a program from it in a
// headless emulated PCjr. Never a shell: anyone on the LAN who dials in
// gets these, so the tools take a project name and nothing that reaches
// outside the workspace.
package pcjr

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"
)

// Env is where things are.
type Env struct {
	Workspace string // Claude's folder; projects are its subfolders
	Repo      string // the CodeJr checkout: lib/ (JRGFX) and client/SCRDUMP.EXE
	Watcom    string // Open Watcom install (default ~/.local/opt/openwatcom)
}

var dosName = regexp.MustCompile(`^[A-Za-z0-9_$~!#%&@^'()-]{1,8}$`)
var fileName = regexp.MustCompile(`^[A-Za-z0-9_$~!#%&@^'()-]{1,8}(\.[A-Za-z0-9_$~!#%&@^'()-]{1,3})?$`)

// project finds a project folder by name, whatever its case on disk.
func (e Env) project(name string) (string, error) {
	if !dosName.MatchString(name) {
		return "", fmt.Errorf("%q is not a project folder name (up to 8 letters, no extension)", name)
	}
	des, err := os.ReadDir(e.Workspace)
	if err != nil {
		return "", err
	}
	for _, de := range des {
		if de.IsDir() && strings.EqualFold(de.Name(), name) {
			return filepath.Join(e.Workspace, de.Name()), nil
		}
	}
	return "", fmt.Errorf("no project folder %s in the workspace", strings.ToUpper(name))
}

func (e Env) watcom() string {
	if e.Watcom != "" {
		return e.Watcom
	}
	return filepath.Join(os.Getenv("HOME"), ".local/opt/openwatcom")
}

func (e Env) toolEnv() []string {
	w := e.watcom()
	return append(os.Environ(),
		"WATCOM="+w,
		"PATH="+filepath.Join(w, "binl64")+":"+filepath.Join(w, "binl")+":"+os.Getenv("PATH"),
		"INCLUDE="+filepath.Join(w, "h"))
}

// SharedDir is the workspace folder that holds the library's headers
// (kept there by the server) and whatever Claude adds to the library:
// every .C and .ASM in it is built into the library for every project.
const SharedDir = "JRGFX"

// libFile is one of the library's files: its lower-case name and where
// it is.
type libFile struct{ name, path string }

// library lists the JRGFX library as a build sees it: the repo's lib/jr*
// files, then lib/shared (additions saved from a workspace), then the
// workspace's shared folder, a later one replacing an earlier one of the
// same name. The repo's own modules can't be replaced from the workspace.
func (e Env) library() (srcs, heads []libFile) {
	byName := map[string]string{}
	core := map[string]bool{}
	add := func(dir string, only func(string) bool, isCore bool) {
		des, _ := os.ReadDir(dir)
		for _, de := range des {
			n := strings.ToLower(de.Name())
			if !de.Type().IsRegular() || (only != nil && !only(n)) || (core[n] && !isCore) {
				continue
			}
			switch filepath.Ext(n) {
			case ".c", ".asm", ".h", ".inc":
				byName[n] = filepath.Join(dir, de.Name())
				core[n] = core[n] || isCore
			}
		}
	}
	add(filepath.Join(e.Repo, "lib"), func(n string) bool { return strings.HasPrefix(n, "jr") }, true)
	add(filepath.Join(e.Repo, "lib", "shared"), nil, false)
	if des, err := os.ReadDir(e.Workspace); err == nil {
		for _, de := range des {
			if de.IsDir() && strings.EqualFold(de.Name(), SharedDir) {
				add(filepath.Join(e.Workspace, de.Name()), nil, false)
			}
		}
	}
	var names []string
	for n := range byName {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		f := libFile{n, byName[n]}
		switch filepath.Ext(n) {
		case ".c", ".asm":
			srcs = append(srcs, f)
		default:
			heads = append(heads, f)
		}
	}
	return srcs, heads
}

var includeRE = regexp.MustCompile(`(?im)^\s*#\s*include\s*"([^"]+)"`)

// Build compiles every .C and .ASM file in the project into NAME.EXE
// (NAME = the folder), 8088 only, large model. A program that includes
// one of the library's headers (jrgfx.h, jrkeys.h, ...) is linked with
// the JRGFX library, built fresh as a .lib so only the parts it uses go
// into the program. Objects are built in a scratch folder, so only the
// .EXE lands in the project (and goes to the PCjr). It returns the
// compiler's output; ok is false if it failed.
func (e Env) Build(ctx context.Context, name string) (out string, ok bool, err error) {
	dir, err := e.project(name)
	if err != nil {
		return "", false, err
	}
	des, err := os.ReadDir(dir)
	if err != nil {
		return "", false, err
	}
	tmp, err := os.MkdirTemp("", "pcjr-build-")
	if err != nil {
		return "", false, err
	}
	defer os.RemoveAll(tmp)

	var cs, asms []string
	jrgfx := false
	libSrcs, libHeads := e.library()
	isLibHead := map[string]bool{}
	for _, h := range libHeads {
		isLibHead[h.name] = true
	}
	for _, de := range des {
		n := de.Name()
		if !de.Type().IsRegular() {
			continue
		}
		b, err := os.ReadFile(filepath.Join(dir, n))
		if err != nil {
			return "", false, err
		}
		up := strings.ToUpper(n)
		ext := filepath.Ext(up)
		if ext == ".C" || ext == ".H" || ext == ".ASM" || ext == ".INC" {
			// Watcom on Linux is case sensitive about includes; give it
			// lower-case names, which is how C sources spell them.
			if err := os.WriteFile(filepath.Join(tmp, strings.ToLower(n)), b, 0o644); err != nil {
				return "", false, err
			}
		}
		switch ext {
		case ".C":
			cs = append(cs, strings.ToLower(n))
			for _, m := range includeRE.FindAllSubmatch(b, -1) {
				if isLibHead[strings.ToLower(string(m[1]))] {
					jrgfx = true
				}
			}
		case ".ASM":
			asms = append(asms, strings.ToLower(n))
		}
	}
	if len(cs) == 0 && len(asms) == 0 {
		return "", false, fmt.Errorf("no .C or .ASM files in %s", strings.ToUpper(name))
	}
	sort.Strings(cs)
	sort.Strings(asms)
	exe := strings.ToUpper(filepath.Base(dir)) + ".EXE"

	var log bytes.Buffer
	run := func(args ...string) bool {
		// Resolve the tool in Watcom's folders: exec looks names up in
		// this process's PATH, not the one given to the child.
		bin := args[0]
		for _, d := range []string{"binl64", "binl"} {
			if p := filepath.Join(e.watcom(), d, bin); fileExists(p) {
				bin = p
				break
			}
		}
		c := exec.CommandContext(ctx, bin, args[1:]...)
		c.Dir = tmp
		c.Env = e.toolEnv()
		c.Stdout, c.Stderr = &log, &log
		if err := c.Run(); err != nil {
			if log.Len() == 0 {
				fmt.Fprintf(&log, "%s: %v", args[0], err)
			}
			return false
		}
		return true
	}
	var objs []string
	for _, a := range asms {
		// Its own object name: NAME.ASM beside NAME.C would otherwise
		// both become name.o, and the compiler's replaces the assembler's.
		o := strings.TrimSuffix(a, filepath.Ext(a)) + "_asm.o"
		if !run("wasm", "-q", "-0", "-ml", "-fo="+o, a) {
			return cleanLog(log.String(), tmp), false, nil
		}
		objs = append(objs, o)
	}
	args := []string{"wcl", "-q", "-bt=dos", "-ml", "-0", "-ox", "-w4", "-fe=" + exe}
	if jrgfx {
		// The library from its sources, so a stale jrgfx.lib can't bite,
		// in its own folder: a project's file of the same name is the
		// project's, and the project's headers come first.
		ld := filepath.Join(tmp, "jrlib")
		if err := os.Mkdir(ld, 0o755); err != nil {
			return "", false, err
		}
		for _, f := range append(libHeads, libSrcs...) {
			b, err := os.ReadFile(f.path)
			if err != nil {
				return "", false, err
			}
			if err := os.WriteFile(filepath.Join(ld, f.name), b, 0o644); err != nil {
				return "", false, err
			}
		}
		lib := []string{"wlib", "-q", "-n", "-b", "jrlib/jr.lib"}
		for _, f := range libSrcs {
			o := "jrlib/" + strings.TrimSuffix(f.name, filepath.Ext(f.name)) + ".o"
			var ok bool
			if filepath.Ext(f.name) == ".asm" {
				ok = run("wasm", "-q", "-0", "-ml", "-fo="+o, "jrlib/"+f.name)
			} else {
				ok = run("wcl", "-q", "-bt=dos", "-ml", "-0", "-ox", "-w4", "-c", "-i=jrlib", "-fo="+o, "jrlib/"+f.name)
			}
			if !ok {
				return "The library didn't build (a file in " + SharedDir + "?):\n" + cleanLog(log.String(), tmp), false, nil
			}
			lib = append(lib, "+"+o)
		}
		if !run(lib...) {
			return cleanLog(log.String(), tmp), false, nil
		}
		args = append(args, "-i=jrlib")
		args = append(args, cs...)
		args = append(args, objs...)
		args = append(args, "jrlib/jr.lib")
	} else {
		args = append(args, cs...)
		args = append(args, objs...)
	}
	if !run(args...) {
		return cleanLog(log.String(), tmp), false, nil
	}
	b, err := os.ReadFile(filepath.Join(tmp, exe))
	if err != nil {
		return cleanLog(log.String(), tmp), false, fmt.Errorf("the compiler made no %s", exe)
	}
	if err := os.WriteFile(filepath.Join(dir, exe), b, 0o644); err != nil {
		return "", false, err
	}
	msg := cleanLog(log.String(), tmp)
	if msg == "" {
		msg = "no warnings"
	}
	return fmt.Sprintf("Built %s\\%s (%d bytes). %s", strings.ToUpper(filepath.Base(dir)), exe, len(b), msg), true, nil
}

// cleanLog drops the scratch folder's path from compiler messages.
func cleanLog(s, tmp string) string {
	s = strings.ReplaceAll(s, tmp+"/", "")
	return strings.TrimSpace(s)
}

// RunResult is what a program left behind in the emulator.
type RunResult struct {
	Text   string            // exit, output, LOG.TXT, the text screen
	Images map[string][]byte // PNG screenshots the program wrote (*.PPM)
}

// Run starts program (a .EXE or .COM in the project) with args in a
// headless emulated PCjr, for up to secs seconds, and collects its output:
// what it printed, LOG.TXT, the text screen after it ended, and any PPM
// screenshots it wrote (as PNG).
func (e Env) Run(ctx context.Context, name, program, args string, secs int) (*RunResult, error) {
	dir, err := e.project(name)
	if err != nil {
		return nil, err
	}
	program = strings.ToUpper(program)
	if !fileName.MatchString(program) {
		return nil, fmt.Errorf("%q is not a DOS file name", program)
	}
	if ext := filepath.Ext(program); ext != ".EXE" && ext != ".COM" {
		if ext == ".BAS" {
			return nil, fmt.Errorf("BASIC programs can't run in the emulator here (no GW-BASIC on the laptop); the user can /run it on the PCjr")
		}
		return nil, fmt.Errorf("only .EXE and .COM programs run in the emulator")
	}
	if strings.ContainsAny(args, "\r\n|<>&") || len(args) > 60 {
		return nil, fmt.Errorf("arguments must be one short line without | < > &")
	}
	if secs <= 0 {
		secs = 20
	}
	if secs > 120 {
		secs = 120
	}

	tmp, err := os.MkdirTemp("", "pcjr-run-")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(tmp)
	c := filepath.Join(tmp, "c")
	if err := os.MkdirAll(c, 0o755); err != nil {
		return nil, err
	}
	// The project, upper-cased as DOS would show it.
	before := map[string]time.Time{}
	des, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	found := false
	for _, de := range des {
		if !de.Type().IsRegular() {
			continue
		}
		b, err := os.ReadFile(filepath.Join(dir, de.Name()))
		if err != nil {
			return nil, err
		}
		up := strings.ToUpper(de.Name())
		if up == program {
			found = true
		}
		p := filepath.Join(c, up)
		if err := os.WriteFile(p, b, 0o644); err != nil {
			return nil, err
		}
		if st, err := os.Stat(p); err == nil {
			before[up] = st.ModTime()
		}
	}
	if !found {
		return nil, fmt.Errorf("no %s in %s (build it first)", program, strings.ToUpper(name))
	}
	if sd, err := os.ReadFile(filepath.Join(e.Repo, "client", "SCRDUMP.EXE")); err == nil {
		os.WriteFile(filepath.Join(tmp, "SCRDUMP.EXE"), sd, 0o644)
	}
	// Quiet setup, then a clear screen, so the screen dump shows only
	// what the program drew.
	conf := fmt.Sprintf("[dosbox]\nmachine=pcjr\n[cpu]\ncore=normal\ncycles=fixed 3000\n[autoexec]\n@echo off\nmount c %s > nul\nmount d %s > nul\nc:\ncls\n%s %s > OUT.TXT\nD:\\SCRDUMP > D:\\SCREEN.TXT\nexit\n",
		c, tmp, program, args)
	if err := os.WriteFile(filepath.Join(tmp, "dosbox.conf"), []byte(conf), 0o644); err != nil {
		return nil, err
	}
	rctx, cancel := context.WithTimeout(ctx, time.Duration(secs)*time.Second)
	defer cancel()
	cmd := exec.CommandContext(rctx, "dosbox", "-conf", filepath.Join(tmp, "dosbox.conf"))
	// Never a window on Steve's screen.
	cmd.Env = append(os.Environ(), "SDL_VIDEODRIVER=dummy", "SDL_AUDIODRIVER=dummy")
	start := time.Now()
	cmd.Run()
	took := time.Since(start).Round(100 * time.Millisecond)

	var t strings.Builder
	if rctx.Err() != nil {
		fmt.Fprintf(&t, "%s was still running after %d s and was stopped (a program that waits for keys never ends here: give it a test or demo mode that exits by itself).\n", program, secs)
	} else {
		fmt.Fprintf(&t, "%s ran and ended after %s (emulator time, not the PCjr's: DOSBox's speed and timing differ).\n", program, took)
	}
	section := func(title, path string, max int) {
		b, err := os.ReadFile(path)
		if err != nil {
			return
		}
		var lines []string
		for _, l := range strings.Split(strings.ReplaceAll(string(b), "\r\n", "\n"), "\n") {
			lines = append(lines, strings.TrimRight(l, " "))
		}
		s := strings.Trim(strings.Join(lines, "\n"), "\n")
		if s == "" {
			return
		}
		if len(s) > max {
			s = s[:max] + "\n... (cut)"
		}
		fmt.Fprintf(&t, "\n--- %s\n%s\n", title, s)
	}
	section("what it printed (OUT.TXT)", filepath.Join(c, "OUT.TXT"), 3000)
	section("LOG.TXT", filepath.Join(c, "LOG.TXT"), 4000)
	if rctx.Err() == nil {
		section("text screen when it ended", filepath.Join(tmp, "SCREEN.TXT"), 2200)
	}

	res := &RunResult{Images: map[string][]byte{}}
	des, _ = os.ReadDir(c)
	var made []string
	for _, de := range des {
		n := de.Name()
		st, err := de.Info()
		if err != nil || n == "OUT.TXT" {
			continue
		}
		if old, ok := before[n]; ok && !st.ModTime().After(old) {
			continue
		}
		made = append(made, n)
		if strings.HasSuffix(n, ".PPM") && len(res.Images) < 3 {
			png := filepath.Join(tmp, n+".png")
			if exec.CommandContext(ctx, "magick", filepath.Join(c, n), png).Run() == nil {
				if b, err := os.ReadFile(png); err == nil {
					res.Images[n] = b
				}
			}
		}
	}
	if len(made) > 0 {
		fmt.Fprintf(&t, "\nFiles it wrote (kept in the emulator only): %s\n", strings.Join(made, ", "))
	}
	res.Text = t.String()
	return res, nil
}

func fileExists(p string) bool {
	st, err := os.Stat(p)
	return err == nil && st.Mode().IsRegular()
}
