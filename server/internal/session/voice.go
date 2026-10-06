package session

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

// Voice: with /voice on, every reply is also spoken on the PCjr. The
// server turns the reply into a speech clip with the speech project's
// converter (speech/mksnd.py: a neural voice through gipity, reshaped for
// the PCjr's sound chip), sends it like any file, and tells the client to
// play it with SAY.EXE once the reply is done. Clips go in the workspace's
// VOICE folder (VOICE\REPLY.SND on the PCjr); short fixed ones (voice on,
// voice off, goodbye) are made once and kept there.

// VoiceDir is the workspace folder for speech clips.
const VoiceDir = "VOICE"

// voiceNote is added to Claude's instructions while voice is on.
const voiceNote = `

Voice is on: your reply is also spoken aloud on the PCjr, and audio is
slow to send over its modem. Answer in one or two short sentences (under
25 words) unless the user asks for more. Plain words only: no lists,
symbols or code in the reply.`

// cannedClips are made once and kept; the client plays them from disk.
var cannedClips = map[string]string{
	"ON":  "Voice on.",
	"OFF": "Voice off.",
	"BYE": "Goodbye. See you next time.",
}

// MakeClip runs the converter: text in, NAME.SND out in the workspace's
// VOICE folder, at the converter's own default quality, so the speech
// project picks the quality in one place (medium: 4-bit ADPCM at 8 kHz,
// about 4 KB per second of speech). tool is speech/mksnd.py.
func MakeClip(ctx context.Context, tool, wsDir, name, text string) error {
	if tool == "" {
		return fmt.Errorf("no speech converter configured")
	}
	tmp, err := os.MkdirTemp("", "codejr-voice-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	ctx, cancel := context.WithTimeout(ctx, 60*time.Second)
	defer cancel()
	out := filepath.Join(tmp, name)
	cmd := exec.CommandContext(ctx, tool, "--text", text, "-o", out)
	if b, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("%v: %s", err, strings.TrimSpace(string(b)))
	}
	b, err := os.ReadFile(out + ".SND")
	if err != nil {
		return err
	}
	dir := filepath.Join(wsDir, VoiceDir)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(dir, name+".SND"), b, 0o644)
}

// EnsureCannedClips makes the fixed clips that are missing. It is slow
// (the voice service), so the server runs it in the background.
func EnsureCannedClips(tool, wsDir string, logf func(string, ...any)) {
	for name, text := range cannedClips {
		if _, err := os.Stat(filepath.Join(wsDir, VoiceDir, name+".SND")); err == nil {
			continue
		}
		if err := MakeClip(context.Background(), tool, wsDir, name, text); err != nil {
			logf("voice: making %s: %v", name, err)
			return
		}
		logf("voice: made %s\\%s.SND", VoiceDir, name)
	}
}

// spokenText trims a reply to what is worth saying: the first few
// sentences, at most about 300 characters.
func spokenText(reply string) string {
	t := strings.Join(strings.Fields(reply), " ")
	if len(t) <= 300 {
		return t
	}
	cut := t[:300]
	if i := strings.LastIndexAny(cut, ".!?"); i > 80 {
		return cut[:i+1]
	}
	if i := strings.LastIndex(cut, " "); i > 0 {
		return cut[:i] + "..."
	}
	return cut
}
