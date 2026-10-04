package session

import "io"

// telnetFilter removes telnet negotiation from what the modem sends. Most
// WiFi modems speak raw TCP, but some negotiate telnet options; the PCjr
// never sends byte 255 on its own (Kermit packets are 7-bit here, and the
// native protocol escapes it).
type telnetFilter struct {
	r     io.Reader
	state int // 0 data, 1 after IAC, 2 option byte, 3 in subnegotiation, 4 IAC in subnegotiation
}

const iac = 255

func (t *telnetFilter) Read(p []byte) (int, error) {
	for {
		n, err := t.r.Read(p)
		j := 0
		for _, b := range p[:n] {
			switch t.state {
			case 0:
				if b == iac {
					t.state = 1
				} else {
					// NUL bytes pass: the native protocol uses them.
					p[j] = b
					j++
				}
			case 1:
				switch {
				case b == iac:
					p[j] = b
					j++
					t.state = 0
				case b >= 251 && b <= 254: // WILL WONT DO DONT
					t.state = 2
				case b == 250: // SB
					t.state = 3
				default:
					t.state = 0
				}
			case 2:
				t.state = 0
			case 3:
				if b == iac {
					t.state = 4
				}
			case 4:
				if b == 240 { // SE
					t.state = 0
				} else {
					t.state = 3
				}
			}
		}
		if j > 0 || err != nil {
			return j, err
		}
	}
}
