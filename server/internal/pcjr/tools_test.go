package pcjr

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// Builds and runs a small JRGFX program through the tools: needs Open
// Watcom, DOSBox and ImageMagick, so it is skipped without them.
func TestBuildAndRun(t *testing.T) {
	e := Env{Workspace: t.TempDir()}
	e.Repo, _ = filepath.Abs("../../..")
	if !fileExists(filepath.Join(e.watcom(), "binl64", "wcl")) {
		t.Skip("no Open Watcom")
	}
	if _, err := exec.LookPath("dosbox"); err != nil {
		t.Skip("no dosbox")
	}
	if !fileExists(filepath.Join(e.Repo, "client", "SCRDUMP.EXE")) {
		t.Skip("client/SCRDUMP.EXE not built (client/build.sh)")
	}
	dir := filepath.Join(e.Workspace, "boxes")
	os.MkdirAll(dir, 0o755)
	os.WriteFile(filepath.Join(dir, "boxes.c"), []byte(`#include <stdio.h>
#include "jrgfx.h"
int main(void)
{
	FILE *f = fopen("LOG.TXT", "w");
	if (gfx_init(GFX_160x200) != 0)
		return 1;
	gfx_clear(1);
	gfx_rect(10, 10, 100, 100, 14);
	gfx_present();
	gfx_screenshot("SHOT.PPM");
	gfx_done();
	printf("drawn\n");
	if (f) { fprintf(f, "frame ok\n"); fclose(f); }
	return 0;
}
`), 0o644)
	ctx := context.Background()

	out, ok, err := e.Build(ctx, "BOXES")
	if err != nil || !ok {
		t.Fatalf("build: %v %v\n%s", ok, err, out)
	}
	if !fileExists(filepath.Join(dir, "BOXES.EXE")) {
		t.Fatal("no BOXES.EXE in the project")
	}
	if ents, _ := os.ReadDir(dir); len(ents) != 2 {
		t.Fatalf("the build left extra files: %v", ents)
	}

	r, err := e.Run(ctx, "boxes", "boxes.exe", "", 60)
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"ran and ended", "drawn", "frame ok"} {
		if !strings.Contains(r.Text, want) {
			t.Fatalf("run output lacks %q:\n%s", want, r.Text)
		}
	}
	if len(r.Images["SHOT.PPM"]) == 0 {
		t.Fatal("no screenshot")
	}

	// Broken code comes back as a failed build with the compiler's words.
	os.WriteFile(filepath.Join(dir, "boxes.c"), []byte("int main(void) { return x; }\n"), 0o644)
	out, ok, _ = e.Build(ctx, "boxes")
	if ok || !strings.Contains(out, "'x'") {
		t.Fatalf("expected a failed build naming x, got ok=%v:\n%s", ok, out)
	}

	// Nothing outside the workspace.
	for _, bad := range []string{"..", "../x", "a/b", "TOOLONGNAME"} {
		if _, _, err := e.Build(ctx, bad); err == nil {
			t.Fatalf("Build(%q) was allowed", bad)
		}
	}
}
