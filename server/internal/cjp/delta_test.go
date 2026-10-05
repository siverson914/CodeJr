package cjp

import (
	"bytes"
	"math/rand"
	"os"
	"testing"
)

func TestDeltaRoundTrip(t *testing.T) {
	r := rand.New(rand.NewSource(1))
	old := make([]byte, 50000)
	r.Read(old)
	// Insert, delete and change bytes: everything after shifts.
	new := append([]byte{}, old[:1000]...)
	new = append(new, []byte("inserted code here")...)
	new = append(new, old[1000:20000]...)
	new = append(new, old[20100:]...)
	new[30000] ^= 0xFF
	d := Delta(old, new)
	got, err := ApplyDelta(old, d)
	if err != nil || !bytes.Equal(got, new) {
		t.Fatalf("round trip failed: %v", err)
	}
	if len(d) > 500 {
		t.Fatalf("delta is %d bytes for three small edits", len(d))
	}
	for _, c := range [][2][]byte{{nil, new}, {old, nil}, {[]byte("abc"), []byte("abd")}} {
		got, err := ApplyDelta(c[0], Delta(c[0], c[1]))
		if err != nil || !bytes.Equal(got, c[1]) {
			t.Fatalf("edge case failed: %v", err)
		}
	}
}

// DELTA_A=old DELTA_B=new go test -run TestDeltaSize -v: sizes on real files.
func TestDeltaSize(t *testing.T) {
	a, b := os.Getenv("DELTA_A"), os.Getenv("DELTA_B")
	if a == "" {
		t.Skip("set DELTA_A and DELTA_B")
	}
	old, _ := os.ReadFile(a)
	new, _ := os.ReadFile(b)
	d := Delta(old, new)
	sum := func(cs [][]byte) (n int) {
		for _, c := range cs {
			n += len(c)
		}
		return
	}
	t.Logf("new %d bytes, %d compressed; delta %d bytes, %d compressed",
		len(new), sum(MakeChunks(new)), len(d), sum(MakeChunks(d)))
}
