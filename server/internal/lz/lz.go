// Package lz compresses data in the LZ4 block format, for sending files to
// the PCjr in small chunks. The format was chosen because its decoder is a
// few dozen lines of C with no tables (client/lz.c), which matters on an
// 8088; the PCjr spends far longer receiving a byte than decoding one.
package lz

import "errors"

const (
	minMatch  = 4
	lastLits  = 5  // the last 5 bytes are always literals
	matchSafe = 12 // no match starts within 12 bytes of the end
	hashBits  = 13
	maxChain  = 64
)

func hash4(b []byte) uint32 {
	v := uint32(b[0]) | uint32(b[1])<<8 | uint32(b[2])<<16 | uint32(b[3])<<24
	return (v * 2654435761) >> (32 - hashBits)
}

// Compress returns src in LZ4 block format. Inputs are expected to be
// small (a few KB), so the match search favors ratio over speed.
func Compress(src []byte) []byte {
	n := len(src)
	out := make([]byte, 0, n+n/255+16)
	head := make([]int32, 1<<hashBits)
	for i := range head {
		head[i] = -1
	}
	prev := make([]int32, n)
	insert := func(i int) {
		if i+minMatch <= n {
			h := hash4(src[i:])
			prev[i] = head[h]
			head[h] = int32(i)
		}
	}
	// longest finds the best match for position i among earlier positions.
	longest := func(i int) (length, offset int) {
		if i > n-matchSafe {
			return 0, 0
		}
		limit := n - lastLits
		tries := maxChain
		for c := head[hash4(src[i:])]; c >= 0 && tries > 0; c, tries = prev[c], tries-1 {
			j := int(c)
			if i-j > 65535 {
				break
			}
			l := 0
			for i+l < limit && src[j+l] == src[i+l] {
				l++
			}
			if l > length {
				length, offset = l, i-j
			}
		}
		if length < minMatch {
			return 0, 0
		}
		return length, offset
	}
	emitLen := func(l int) {
		for ; l >= 255; l -= 255 {
			out = append(out, 255)
		}
		out = append(out, byte(l))
	}
	emit := func(lits []byte, mlen, off int) {
		tok := byte(0)
		if len(lits) >= 15 {
			tok = 15 << 4
		} else {
			tok = byte(len(lits)) << 4
		}
		if mlen > 0 {
			if mlen-minMatch >= 15 {
				tok |= 15
			} else {
				tok |= byte(mlen - minMatch)
			}
		}
		out = append(out, tok)
		if len(lits) >= 15 {
			emitLen(len(lits) - 15)
		}
		out = append(out, lits...)
		if mlen > 0 {
			out = append(out, byte(off), byte(off>>8))
			if mlen-minMatch >= 15 {
				emitLen(mlen - minMatch - 15)
			}
		}
	}

	anchor, i := 0, 0
	for i < n {
		l, off := longest(i)
		if l == 0 {
			insert(i)
			i++
			continue
		}
		// Lazy matching: if the next position has a longer match, take a
		// literal here instead.
		insert(i)
		if l2, _ := longest(i + 1); l2 > l+1 {
			i++
			continue
		}
		emit(src[anchor:i], l, off)
		for k := i + 1; k < i+l; k++ {
			insert(k)
		}
		i += l
		anchor = i
	}
	emit(src[anchor:], 0, 0)
	return out
}

var ErrCorrupt = errors.New("lz: corrupt data")

// Decompress reverses Compress. max bounds the output size.
func Decompress(src []byte, max int) ([]byte, error) {
	var dst []byte
	s := 0
	readLen := func(l int) (int, bool) {
		if l != 15 {
			return l, true
		}
		for {
			if s >= len(src) {
				return 0, false
			}
			b := src[s]
			s++
			l += int(b)
			if b != 255 {
				return l, true
			}
		}
	}
	for s < len(src) {
		tok := src[s]
		s++
		l, ok := readLen(int(tok >> 4))
		if !ok || len(src)-s < l || len(dst)+l > max {
			return nil, ErrCorrupt
		}
		dst = append(dst, src[s:s+l]...)
		s += l
		if s >= len(src) {
			break // the last sequence has no match
		}
		if len(src)-s < 2 {
			return nil, ErrCorrupt
		}
		off := int(src[s]) | int(src[s+1])<<8
		s += 2
		l, ok = readLen(int(tok & 15))
		l += minMatch
		if !ok || off == 0 || off > len(dst) || len(dst)+l > max {
			return nil, ErrCorrupt
		}
		for ; l > 0; l-- {
			dst = append(dst, dst[len(dst)-off])
		}
	}
	return dst, nil
}
