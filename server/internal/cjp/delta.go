package cjp

import (
	"encoding/binary"
	"errors"
)

// Deltas: when the PCjr already has an older version of a file, the
// server sends an edit script instead of the whole file. The server keeps
// a copy of what it last sent, so it knows the old version exactly and
// can diff on its side; the PCjr only replays the script:
//
//	0x01 offset(4 LE) length(2 LE)   copy bytes from the old file
//	0x02 length(2 LE) bytes...       new bytes
//	0x00                             end
//
// The script travels like any file (pulled, compressed chunk by chunk).
// Copies find moved code too, so it works for binaries, where a small
// change shifts everything after it.

const (
	DeltaEnd  = 0x00
	DeltaCopy = 0x01
	DeltaData = 0x02

	deltaBlock = 16     // the old file is indexed in blocks this size
	maxRun     = 0xFFFF // one op's length field
)

// Delta returns the script that turns old into new.
func Delta(old, new []byte) []byte {
	index := map[uint32][]int{}
	for off := 0; off+deltaBlock <= len(old); off += deltaBlock {
		h := blockHash(old[off : off+deltaBlock])
		if len(index[h]) < 8 { // a few candidates per hash is plenty
			index[h] = append(index[h], off)
		}
	}
	var out, lit []byte
	flush := func() {
		for len(lit) > 0 {
			n := min(len(lit), maxRun)
			out = append(out, DeltaData, byte(n), byte(n>>8))
			out = append(out, lit[:n]...)
			lit = lit[n:]
		}
	}
	copyOp := func(off, n int) {
		for n > 0 {
			k := min(n, maxRun)
			out = append(out, DeltaCopy, 0, 0, 0, 0, byte(k), byte(k>>8))
			binary.LittleEndian.PutUint32(out[len(out)-6:], uint32(off))
			off += k
			n -= k
		}
	}
	i := 0
	var h uint32
	hashed := -1 // the position h belongs to
	for i < len(new) {
		if i+deltaBlock <= len(new) {
			if hashed != i {
				h = blockHash(new[i : i+deltaBlock])
				hashed = i
			}
			best, bestLen := -1, 0
			for _, off := range index[h] {
				n := 0
				for off+n < len(old) && i+n < len(new) && old[off+n] == new[i+n] {
					n++
				}
				if n > bestLen {
					best, bestLen = off, n
				}
			}
			// A copy op costs 7 bytes; only worth it past that.
			if bestLen >= deltaBlock {
				flush()
				copyOp(best, bestLen)
				i += bestLen
				continue
			}
		}
		lit = append(lit, new[i])
		if i+deltaBlock < len(new) && hashed == i {
			h = rollHash(h, new[i], new[i+deltaBlock])
			hashed = i + 1
		}
		i++
	}
	flush()
	return append(out, DeltaEnd)
}

// A rolling hash (Adler-style sums) over deltaBlock bytes.
func blockHash(b []byte) uint32 {
	var a, s uint32
	for _, c := range b {
		a += uint32(c)
		s += a
	}
	return s<<16 | a&0xFFFF
}

func rollHash(h uint32, out, in byte) uint32 {
	a, s := h&0xFFFF, h>>16
	a = (a - uint32(out) + uint32(in)) & 0xFFFF
	s = (s - deltaBlock*uint32(out) + a) & 0xFFFF
	return s<<16 | a
}

// ApplyDelta replays a script against old (the client does the same in C).
func ApplyDelta(old, script []byte) ([]byte, error) {
	var out []byte
	for p := 0; ; {
		if p >= len(script) {
			return nil, errors.New("delta: no end")
		}
		switch script[p] {
		case DeltaEnd:
			return out, nil
		case DeltaCopy:
			if p+7 > len(script) {
				return nil, errors.New("delta: short copy")
			}
			off := int(binary.LittleEndian.Uint32(script[p+1:]))
			n := int(binary.LittleEndian.Uint16(script[p+5:]))
			if off+n > len(old) {
				return nil, errors.New("delta: copy past the old file")
			}
			out = append(out, old[off:off+n]...)
			p += 7
		case DeltaData:
			if p+3 > len(script) {
				return nil, errors.New("delta: short data")
			}
			n := int(binary.LittleEndian.Uint16(script[p+1:]))
			if p+3+n > len(script) {
				return nil, errors.New("delta: data past the end")
			}
			out = append(out, script[p+3:p+3+n]...)
			p += 3 + n
		default:
			return nil, errors.New("delta: bad op")
		}
	}
}
