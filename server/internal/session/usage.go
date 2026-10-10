package session

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Usage: counts the server has anyway, kept in .codejr/usage.json for the
// /usage command. All of it is by-product: nothing is measured for its
// own sake. One file on the laptop, not one per project folder, because
// everything in a project folder is sent to the PCjr.

// Stats is one row of counts: all time, one day, one project or one call.
type Stats struct {
	Calls        int     `json:"calls,omitempty"`
	ConnectedSec int     `json:"connected_s,omitempty"`
	Turns        int     `json:"turns,omitempty"`
	TypedChars   int     `json:"typed,omitempty"`
	ReplyChars   int     `json:"reply,omitempty"`
	TurnSec      int     `json:"turn_s,omitempty"`
	OutTokens    int     `json:"out_tokens,omitempty"`
	CostUSD      float64 `json:"cost,omitempty"`
	Builds       int     `json:"builds,omitempty"`
	EmuRuns      int     `json:"emu_runs,omitempty"`
	PCjrRuns     int     `json:"pcjr_runs,omitempty"`
	Edits        int     `json:"edits,omitempty"`
	Web          int     `json:"web,omitempty"`
	FilesSent    int     `json:"files,omitempty"`
	FileBytes    int64   `json:"file_bytes,omitempty"`
	LineBytes    int64   `json:"line_bytes,omitempty"`
	SendSec      int     `json:"send_s,omitempty"`
	Resent       int     `json:"resent,omitempty"`
	First        string  `json:"first,omitempty"` // dates, 2006-01-02
	Last         string  `json:"last,omitempty"`
}

// Usage is the whole record.
type Usage struct {
	Since    string            `json:"since"`  // the day counting began
	Seeded   bool              `json:"seeded"` // the server log's past was added once
	Total    Stats             `json:"total"`
	Days     map[string]*Stats `json:"days"`
	Projects map[string]*Stats `json:"projects"`
}

const usageDays = 90 // days kept one by one

func (w *Workspace) usagePath() string { return w.state("usage.json") }

// Usage reads the record (an empty one if there is none yet).
func (w *Workspace) Usage() *Usage {
	u := &Usage{}
	if b, err := os.ReadFile(w.usagePath()); err == nil {
		json.Unmarshal(b, u)
	}
	if u.Days == nil {
		u.Days = map[string]*Stats{}
	}
	if u.Projects == nil {
		u.Projects = map[string]*Stats{}
	}
	if u.Since == "" {
		u.Since = time.Now().Format("2006-01-02")
	}
	if !u.Seeded {
		u.Seeded = true
		w.seedUsage(u)
	}
	return u
}

// AddProjectUsage applies add to one project's row only: for a turn,
// which is counted once overall and once for each project it touched.
func (w *Workspace) AddProjectUsage(project string, add func(*Stats)) error {
	return w.addUsage(project, add, false)
}

// AddUsage applies add to the all-time row, today's row and, when project
// isn't empty, that project's row. Errors only cost statistics, so they
// are returned for the log and nothing else.
func (w *Workspace) AddUsage(project string, add func(*Stats)) error {
	return w.addUsage(project, add, true)
}

func (w *Workspace) addUsage(project string, add func(*Stats), overall bool) error {
	u := w.Usage()
	day := time.Now().Format("2006-01-02")
	var rows []*Stats
	if overall {
		if u.Days[day] == nil {
			u.Days[day] = &Stats{}
		}
		rows = append(rows, &u.Total, u.Days[day])
	}
	if project = strings.ToUpper(strings.TrimSpace(project)); project != "" {
		if u.Projects[project] == nil {
			u.Projects[project] = &Stats{}
		}
		rows = append(rows, u.Projects[project])
	}
	for _, r := range rows {
		add(r)
		if r.First == "" {
			r.First = day
		}
		r.Last = day
	}
	if len(u.Days) > usageDays {
		var days []string
		for d := range u.Days {
			days = append(days, d)
		}
		sort.Strings(days)
		for _, d := range days[:len(days)-usageDays] {
			delete(u.Days, d)
		}
	}
	b, err := json.MarshalIndent(u, "", " ")
	if err != nil {
		return err
	}
	tmp := w.usagePath() + ".new"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, w.usagePath())
}

var (
	logTurn = regexp.MustCompile(`: turn: "(.*)"$`)
	logSent = regexp.MustCompile(`: native: sent (\S+) \((\d+) bytes\) in `)
	logLine = regexp.MustCompile(`: native: \S+: \d+ requests for \d+ chunks, (\d+) bytes on the line`)
	logCall = regexp.MustCompile(`: started \(native client\)$`)
)

// seedUsage adds, once, what the server's log still says about the time
// before counting began: calls, messages and files. The log has no dates
// and no token counts, so those start from today.
func (w *Workspace) seedUsage(u *Usage) {
	f, err := os.Open(filepath.Join(filepath.Dir(w.Dir), "logs", "server.log"))
	if err != nil {
		return
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 4<<20)
	for sc.Scan() {
		line := sc.Text()
		if m := logTurn.FindStringSubmatch(line); m != nil {
			if text, err := strconv.Unquote(`"` + m[1] + `"`); err == nil && text != compactPrompt {
				u.Total.Turns++
				if i := strings.LastIndex(text, "\n]\n\n"); strings.HasPrefix(text, "[The user ran") && i >= 0 {
					text = text[i+4:] // without the run report CodeJr put in front
				}
				u.Total.TypedChars += len(text)
			}
		} else if m := logSent.FindStringSubmatch(line); m != nil {
			n, _ := strconv.ParseInt(m[2], 10, 64)
			u.Total.FilesSent++
			u.Total.FileBytes += n
			if p := projectOf(m[1]); p != "" {
				if u.Projects[p] == nil {
					u.Projects[p] = &Stats{}
				}
				u.Projects[p].FilesSent++
				u.Projects[p].FileBytes += n
			}
		} else if m := logLine.FindStringSubmatch(line); m != nil {
			n, _ := strconv.ParseInt(m[1], 10, 64)
			u.Total.LineBytes += n
		} else if logCall.MatchString(line) {
			u.Total.Calls++
		}
	}
}

// projectOf is the project folder of a file as the PCjr names it
// (FOLDER\NAME), or "" for a file at the top or one of CodeJr's own.
func projectOf(name string) string {
	name = strings.ReplaceAll(name, "/", `\`)
	i := strings.IndexByte(name, '\\')
	if i <= 0 || strings.HasPrefix(name, "..") || strings.HasPrefix(name, ".") {
		return ""
	}
	return strings.ToUpper(name[:i])
}

// ---- the /usage command's text ----

func commas(n int64) string {
	s := strconv.FormatInt(n, 10)
	for i := len(s) - 3; i > 0; i -= 3 {
		s = s[:i] + "," + s[i:]
	}
	return s
}

func hm(secs int) string {
	if secs < 3600 {
		return fmt.Sprintf("%d min", (secs+30)/60)
	}
	return fmt.Sprintf("%d h %02d min", secs/3600, secs%3600/60)
}

func bytesShort(n int64) string {
	switch {
	case n < 10<<10:
		return commas(n) + " bytes"
	case n < 10<<20:
		return commas(n>>10) + " KB"
	}
	return fmt.Sprintf("%.1f MB", float64(n)/(1<<20))
}

// usageRows is the table's lines: a label and a cell for each column.
func usageRows(cols []*Stats) [][]string {
	row := func(label string, cell func(*Stats) string) []string {
		r := []string{label}
		for _, c := range cols {
			r = append(r, cell(c))
		}
		return r
	}
	return [][]string{
		row("Calls", func(s *Stats) string { return commas(int64(s.Calls)) }),
		row("Time connected", func(s *Stats) string { return hm(s.ConnectedSec) }),
		row("Messages", func(s *Stats) string { return commas(int64(s.Turns)) }),
		row("Typed, characters", func(s *Stats) string { return commas(int64(s.TypedChars)) }),
		row("Replies, characters", func(s *Stats) string { return commas(int64(s.ReplyChars)) }),
		row("CodeJr working", func(s *Stats) string { return hm(s.TurnSec) }),
		row("Tokens written", func(s *Stats) string { return kilo(s.OutTokens) }),
		row("Cost at list price", func(s *Stats) string { return fmt.Sprintf("$%.2f", s.CostUSD) }),
		row("Files changed", func(s *Stats) string { return commas(int64(s.Edits)) }),
		row("Builds", func(s *Stats) string { return commas(int64(s.Builds)) }),
		row("Emulator runs", func(s *Stats) string { return commas(int64(s.EmuRuns)) }),
		row("Runs on the PCjr", func(s *Stats) string { return commas(int64(s.PCjrRuns)) }),
		row("Web lookups", func(s *Stats) string { return commas(int64(s.Web)) }),
		row("Files sent", func(s *Stats) string { return commas(int64(s.FilesSent)) }),
		row("  their size", func(s *Stats) string { return bytesShort(s.FileBytes) }),
		row("  on the line", func(s *Stats) string { return bytesShort(s.LineBytes) }),
		row("  time sending", func(s *Stats) string { return hm(s.SendSec) }),
		row("Frames resent", func(s *Stats) string { return commas(int64(s.Resent)) }),
	}
}

// UsageText is what /usage prints: all time, today and this call side by
// side, or one project's row when project isn't empty. Lines fit 79
// columns.
func (w *Workspace) UsageText(project string, call *Stats) string {
	u := w.Usage()
	var b strings.Builder
	table := func(heads []string, cols []*Stats) {
		fmt.Fprintf(&b, "%-20s", "")
		for _, h := range heads {
			fmt.Fprintf(&b, "%18s", h)
		}
		b.WriteString("\n")
		for _, r := range usageRows(cols) {
			zero := true
			for _, c := range r[1:] {
				if c != "0" && c != "0 min" && c != "$0.00" && c != "0 bytes" {
					zero = false
				}
			}
			if zero {
				continue // nothing to say on this line
			}
			fmt.Fprintf(&b, "%-20s", r[0])
			for _, c := range r[1:] {
				fmt.Fprintf(&b, "%18s", c)
			}
			b.WriteString("\n")
		}
	}
	if project = strings.ToUpper(strings.TrimSpace(project)); project != "" {
		p := u.Projects[project]
		if p == nil {
			var names []string
			for n := range u.Projects {
				names = append(names, n)
			}
			sort.Strings(names)
			return fmt.Sprintf("No usage recorded for %s. Projects with some: %s.\n", project, strings.Join(names, " "))
		}
		table([]string{project}, []*Stats{p})
		if p.First != "" {
			fmt.Fprintf(&b, "First worked on %s, last %s. A message counts for every project it touched.\n", p.First, p.Last)
		}
		return b.String()
	}
	today := u.Days[time.Now().Format("2006-01-02")]
	if today == nil {
		today = &Stats{}
	}
	if call == nil {
		call = &Stats{}
	}
	table([]string{"all time", "today", "this call"}, []*Stats{&u.Total, today, call})
	// The busiest projects, by messages and then files.
	type pr struct {
		name string
		s    *Stats
	}
	var ps []pr
	for n, s := range u.Projects {
		ps = append(ps, pr{n, s})
	}
	sort.Slice(ps, func(i, j int) bool {
		if ps[i].s.Turns != ps[j].s.Turns {
			return ps[i].s.Turns > ps[j].s.Turns
		}
		if ps[i].s.FilesSent != ps[j].s.FilesSent {
			return ps[i].s.FilesSent > ps[j].s.FilesSent
		}
		return ps[i].name < ps[j].name
	})
	if len(ps) > 0 {
		line := "Projects (messages/files):"
		for i, p := range ps {
			item := fmt.Sprintf(" %s %d/%d", p.name, p.s.Turns, p.s.FilesSent)
			if i >= 8 || len(line)+len(item) > 74 {
				line += " ..."
				break
			}
			line += item
		}
		b.WriteString(line + "\n/usage NAME shows one project.\n")
	}
	fmt.Fprintf(&b, "Counted since %s; calls, messages and files from before come from the log.\n", u.Since)
	return b.String()
}
