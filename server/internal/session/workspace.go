package session

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"codejr/internal/kermit"
)

// Workspace is Claude's working folder. It mirrors C:\CODEJR\PROGS on the PCjr:
// files keep Unix line endings here and get DOS line endings on the way
// over. State lives in .codejr/ inside it.
type Workspace struct {
	Dir string
}

// maxFile keeps one runaway file from tying up the line for an hour.
const maxFile = 256 << 10

// DOS 8.3 names: letters, digits and the punctuation DOS allows. No
// spaces, dots beyond the one separator, commas or braces, which also
// keeps names safe to put inside a Kermit APC command.
var dosName = regexp.MustCompile(`^[A-Za-z0-9_$~!#%&@^'()-]{1,8}(\.[A-Za-z0-9_$~!#%&@^'()-]{1,3})?$`)

// ValidName reports whether name is a legal DOS 8.3 file name.
func ValidName(name string) bool { return dosName.MatchString(name) }

var textExt = map[string]bool{
	".BAS": true, ".TXT": true, ".BAT": true, ".C": true, ".H": true, ".ASM": true,
	".INC": true, ".INI": true, ".DOC": true, ".CFG": true, ".LOG": true, ".ME": true,
	".PAS": true, ".FTH": true, ".LGO": true,
}

func isText(name string) bool { return textExt[strings.ToUpper(filepath.Ext(name))] }

func toDOS(data []byte) []byte {
	data = bytes.ReplaceAll(data, []byte("\r\n"), []byte("\n"))
	return bytes.ReplaceAll(data, []byte("\n"), []byte("\r\n"))
}

func fromDOS(data []byte) []byte {
	data = bytes.TrimRight(data, "\x1a") // old DOS end-of-file marker
	return bytes.ReplaceAll(data, []byte("\r\n"), []byte("\n"))
}

func hash(b []byte) string {
	h := sha256.Sum256(b)
	return hex.EncodeToString(h[:8])
}

func (w *Workspace) state(name string) string { return filepath.Join(w.Dir, ".codejr", name) }

func (w *Workspace) Init() error { return os.MkdirAll(filepath.Join(w.Dir, ".codejr"), 0o755) }

func (w *Workspace) loadSynced() map[string]string {
	m := map[string]string{}
	if b, err := os.ReadFile(w.state("synced.json")); err == nil {
		json.Unmarshal(b, &m)
	}
	return m
}

func (w *Workspace) saveSynced(m map[string]string) error {
	b, _ := json.MarshalIndent(m, "", "  ")
	return os.WriteFile(w.state("synced.json"), b, 0o644)
}

// Entry is one file in the workspace.
type Entry struct {
	Name    string // upper-case DOS name
	Size    int
	OnPCjr  bool // the PCjr has this exact version
	Problem string
}

// Scan lists the workspace: its files, and the files one level down in
// project folders (SNAKE/SNAKE.BAS on the laptop is SNAKE\SNAKE.BAS on the
// PCjr). Hidden files are ignored; files that can't go to the PCjr come
// back with Problem set.
func (w *Workspace) Scan() ([]Entry, map[string][]byte, error) {
	des, err := os.ReadDir(w.Dir)
	if err != nil {
		return nil, nil, err
	}
	synced := w.loadSynced()
	seen := map[string]bool{}
	var out []Entry
	data := map[string][]byte{}
	add := func(dir, n string) {
		rel := filepath.Join(dir, n)
		e := Entry{Name: strings.ToUpper(n)}
		if dir != "" {
			e.Name = strings.ToUpper(dir) + `\` + e.Name
		}
		b, err := os.ReadFile(filepath.Join(w.Dir, rel))
		switch {
		case err != nil:
			e.Problem = err.Error()
		case !ValidName(n) || (dir != "" && !validDir(dir)):
			e.Name, e.Problem = rel, "not a DOS 8.3 name"
		case seen[e.Name]:
			e.Problem = "same name as another file in a different case"
		case len(b) > maxFile:
			e.Problem = fmt.Sprintf("too big (%d KB)", len(b)>>10)
		default:
			seen[e.Name] = true
			e.Size = len(b)
			e.OnPCjr = synced[e.Name] == hash(b)
			data[e.Name] = b
		}
		out = append(out, e)
	}
	for _, de := range des {
		n := de.Name()
		if strings.HasPrefix(n, ".") {
			continue
		}
		if de.IsDir() {
			sub, err := os.ReadDir(filepath.Join(w.Dir, n))
			if err != nil {
				continue
			}
			for _, f := range sub {
				switch {
				case strings.HasPrefix(f.Name(), "."):
				case f.IsDir():
					out = append(out, Entry{Name: n + "/" + f.Name(), Problem: "folders inside folders don't go to the PCjr"})
				case f.Type().IsRegular():
					add(n, f.Name())
				}
			}
			continue
		}
		if de.Type().IsRegular() {
			add("", n)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out, data, nil
}

// validDir reports whether name works as a DOS folder name: up to 8
// characters, no extension.
func validDir(name string) bool { return ValidName(name) && !strings.Contains(name, ".") }

// ValidPath accepts NAME.EXT or FOLDER\NAME.EXT (either slash).
func ValidPath(p string) bool {
	dir, name, ok := strings.Cut(strings.ReplaceAll(p, "/", `\`), `\`)
	if !ok {
		return ValidName(p)
	}
	return validDir(dir) && ValidName(name)
}

// InFolder reports whether a workspace entry lives in a project folder.
func InFolder(name string) bool { return strings.Contains(name, `\`) }

// Pending returns the files the PCjr doesn't have yet, ready to send, and
// the hashes to record once they arrive.
func (w *Workspace) Pending() ([]kermit.File, map[string]string, []Entry, error) {
	entries, data, err := w.Scan()
	if err != nil {
		return nil, nil, nil, err
	}
	var files []kermit.File
	hashes := map[string]string{}
	var problems []Entry
	for _, e := range entries {
		if e.Problem != "" {
			problems = append(problems, e)
			continue
		}
		if e.OnPCjr {
			continue
		}
		b := data[e.Name]
		hashes[e.Name] = hash(b)
		if isText(e.Name) {
			b = toDOS(b)
		}
		files = append(files, kermit.File{Name: e.Name, Data: b})
	}
	return files, hashes, problems, nil
}

// MarkSynced records that the PCjr now has these versions.
func (w *Workspace) MarkSynced(hashes map[string]string) error {
	m := w.loadSynced()
	for k, v := range hashes {
		m[k] = v
	}
	return w.saveSynced(m)
}

// Provide writes files the server supplies (launchers, the client) into
// the workspace when they are missing or different, so the next sync sends
// them like any file Claude wrote.
func (w *Workspace) Provide(files map[string][]byte) error {
	for name, b := range files {
		p := filepath.Join(w.Dir, name)
		if old, err := os.ReadFile(p); err == nil && bytes.Equal(old, b) {
			continue
		}
		if b == nil {
			continue
		}
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return err
		}
		if err := os.WriteFile(p, b, 0o644); err != nil {
			return err
		}
	}
	return nil
}

// Forget makes the next sync send name again (NAME or FOLDER\NAME).
func (w *Workspace) Forget(name string) error {
	m := w.loadSynced()
	delete(m, strings.ToUpper(strings.ReplaceAll(name, "/", `\`)))
	return w.saveSynced(m)
}

// SaveFromPCjr stores a file that came from the PCjr and records it as
// already synced, so it isn't sent straight back.
func (w *Workspace) SaveFromPCjr(name string, data []byte) error {
	name = strings.ToUpper(filepath.Base(name))
	if !ValidName(name) {
		return fmt.Errorf("%q is not a DOS name", name)
	}
	if isText(name) {
		data = fromDOS(data)
	}
	// Replace any existing file regardless of case.
	if des, err := os.ReadDir(w.Dir); err == nil {
		for _, de := range des {
			if strings.EqualFold(de.Name(), name) && de.Name() != name {
				os.Remove(filepath.Join(w.Dir, de.Name()))
			}
		}
	}
	if err := os.WriteFile(filepath.Join(w.Dir, name), data, 0o644); err != nil {
		return err
	}
	return w.MarkSynced(map[string]string{name: hash(data)})
}

// The PCjr's copies: what was last sent of each file, exactly as sent,
// so the next version can go as changes (see cjp.Delta).
func (w *Workspace) havePath(name string) string {
	return w.state(filepath.Join("have", filepath.FromSlash(strings.ReplaceAll(strings.ToUpper(name), `\`, "/"))))
}

// Have returns the copy the PCjr has of name, or nil.
func (w *Workspace) Have(name string) []byte {
	b, err := os.ReadFile(w.havePath(name))
	if err != nil {
		return nil
	}
	return b
}

// SetHave records what the PCjr now has of name.
func (w *Workspace) SetHave(name string, data []byte) error {
	p := w.havePath(name)
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	return os.WriteFile(p, data, 0o644)
}

// SeedHave keeps a copy of every file the PCjr is known to have now, so
// its next version can go as changes.
func (w *Workspace) SeedHave() error {
	entries, data, err := w.Scan()
	if err != nil {
		return err
	}
	for _, e := range entries {
		if !e.OnPCjr || w.Have(e.Name) != nil {
			continue
		}
		b := data[e.Name]
		if isText(e.Name) {
			b = toDOS(b)
		}
		if err := w.SetHave(e.Name, b); err != nil {
			return err
		}
	}
	return nil
}

// LastRun is the report of the program the user last ran with /run, if
// Claude hasn't seen it yet.
func (w *Workspace) LastRun() string {
	b, _ := os.ReadFile(w.state("lastrun.txt"))
	return string(b)
}

// SetLastRun keeps a run report for the next turn ("" clears it).
func (w *Workspace) SetLastRun(r string) error {
	if r == "" {
		err := os.Remove(w.state("lastrun.txt"))
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	return os.WriteFile(w.state("lastrun.txt"), []byte(r), 0o644)
}

// Session remembers the Claude conversation between calls.
func (w *Workspace) Session() string {
	b, _ := os.ReadFile(w.state("session"))
	return strings.TrimSpace(string(b))
}

func (w *Workspace) SetSession(id string) error {
	if id == "" {
		err := os.Remove(w.state("session"))
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	return os.WriteFile(w.state("session"), []byte(id+"\n"), 0o644)
}

// Flag reads a yes/no setting kept in the workspace state.
func (w *Workspace) Flag(name string) bool {
	_, err := os.Stat(w.state(name))
	return err == nil
}

// SetFlag sets or clears a yes/no setting.
func (w *Workspace) SetFlag(name string, on bool) error {
	if on {
		return os.WriteFile(w.state(name), nil, 0o644)
	}
	err := os.Remove(w.state(name))
	if os.IsNotExist(err) {
		return nil
	}
	return err
}
