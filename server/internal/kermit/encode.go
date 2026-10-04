package kermit

// encodeData encodes as much of src as fits in max bytes of packet data,
// using control prefix '#' and 8th-bit prefix qbin (0 for none). It returns
// the encoded bytes and how many bytes of src they cover.
func encodeData(src []byte, qbin byte, max int) ([]byte, int) {
	out := make([]byte, 0, max)
	var tmp [4]byte
	for i, b := range src {
		e := tmp[:0]
		if qbin != 0 && b&0x80 != 0 {
			e = append(e, qbin)
			b &= 0x7F
		}
		switch lo := b & 0x7F; {
		case lo < 32 || lo == 127: // C0 and, without qbin, C1 controls
			e = append(e, '#', ctl(b))
		case b == '#' || (qbin != 0 && b == qbin):
			e = append(e, '#', b)
		default:
			e = append(e, b)
		}
		if len(out)+len(e) > max {
			return out, i
		}
		out = append(out, e...)
	}
	return out, len(src)
}

// decodeData reverses encodeData.
func decodeData(d []byte, qbin byte) []byte {
	out := make([]byte, 0, len(d))
	for i := 0; i < len(d); i++ {
		c := d[i]
		var hi byte
		if qbin != 0 && c == qbin && i+1 < len(d) {
			hi = 0x80
			i++
			c = d[i]
		}
		if c == '#' && i+1 < len(d) {
			i++
			c = d[i]
			if lo := c & 0x7F; lo >= 63 && lo <= 95 {
				c = ctl(c)
			}
		}
		out = append(out, c|hi)
	}
	return out
}
