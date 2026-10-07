package lz

import (
	"bytes"
	"math/rand"
	"os"
	"testing"
)

func roundTrip(t *testing.T, name string, src []byte) int {
	t.Helper()
	c := Compress(src)
	d, err := Decompress(c, len(src))
	if err != nil {
		t.Fatalf("%s: %v", name, err)
	}
	if !bytes.Equal(d, src) {
		t.Fatalf("%s: round trip mismatch (%d bytes in, %d out)", name, len(src), len(d))
	}
	return len(c)
}

func TestRoundTrip(t *testing.T) {
	rng := rand.New(rand.NewSource(1))
	random := make([]byte, 4089)
	rng.Read(random)
	cases := map[string][]byte{
		"empty":  nil,
		"one":    {7},
		"short":  []byte("hello, world"),
		"zeros":  make([]byte, 4089),
		"random": random,
		"text":   bytes.Repeat([]byte("10 PRINT \"HELLO\"\r\n20 GOTO 10\r\n"), 100),
		"runs":   bytes.Repeat([]byte{1, 1, 1, 1, 1, 1, 1, 1, 2, 3}, 300),
	}
	for name, src := range cases {
		n := roundTrip(t, name, src)
		t.Logf("%-7s %5d -> %5d", name, len(src), n)
	}
	// Every length from 0 to 300, to cover the end-of-block rules.
	for n := 0; n <= 300; n++ {
		roundTrip(t, "len", bytes.Repeat([]byte("abcabcabd"), 40)[:n])
	}
}

// Real programs, in the 4 KB chunks the transfer uses.
func TestRatioOnRealFiles(t *testing.T) {
	for _, path := range []string{"../../../apps/jrdoom/JRDOOM.EXE", "../../../client/CODEJR.EXE", "../../../workspace/MATH.BAS"} {
		src, err := os.ReadFile(path)
		if err != nil {
			t.Logf("skip %s: %v", path, err)
			continue
		}
		total := 0
		for off := 0; off < len(src); off += 4089 {
			end := min(off+4089, len(src))
			total += min(roundTrip(t, path, src[off:end]), end-off)
		}
		t.Logf("%s: %d -> %d (%d%%)", path, len(src), total, total*100/len(src))
	}
}

func TestCorruptInputIsRejectedNotCrashed(t *testing.T) {
	src := bytes.Repeat([]byte("the quick brown fox "), 100)
	c := Compress(src)
	rng := rand.New(rand.NewSource(2))
	for i := 0; i < 2000; i++ {
		bad := append([]byte(nil), c...)
		bad[rng.Intn(len(bad))] ^= byte(1 << rng.Intn(8))
		if rng.Intn(4) == 0 {
			bad = bad[:rng.Intn(len(bad))]
		}
		Decompress(bad, len(src)) // must not panic or over-allocate
	}
}
