// Package kermit implements the Kermit file transfer protocol, enough of it
// to talk to MS-DOS Kermit 3.14 on an IBM PCjr: stop-and-wait (window 1),
// short packets, block check types 1 and 3, control prefixing and 8th-bit
// prefixing. No repeat counts, long packets, sliding windows or attributes.
//
// Reference: Frank da Cruz, "Kermit Protocol Manual", 6th edition.
package kermit

import (
	"errors"
	"fmt"
	"time"
)

const (
	mark = 0x01 // SOH, starts every packet
	eol  = 0x0D // CR, ends every packet

	// Our receive limits. 94 is the largest short packet; the PCjr serial
	// port has no FIFO, so we never ask for more.
	maxLen = 94
)

// ErrTimeout is returned by Port.ReadTimeout when no byte arrives in time.
var ErrTimeout = errors.New("kermit: timeout")

// Port is the serial line. ReadTimeout waits at most timeout for one byte.
type Port interface {
	ReadTimeout(timeout time.Duration) (byte, error)
	Write(p []byte) (int, error)
}

// Options tunes a transfer. The zero value is usable.
type Options struct {
	Timeout     time.Duration // wait for one packet before retrying (default 5s)
	Retries     int           // retries per packet before giving up (default 10)
	InitialWait time.Duration // receiver: how long to wait for the first S packet (default 30s)
	BlockCheck  byte          // '1' or '3' (default '3', CRC-16)
}

func (o Options) withDefaults() Options {
	if o.Timeout == 0 {
		o.Timeout = 5 * time.Second
	}
	if o.Retries == 0 {
		o.Retries = 10
	}
	if o.InitialWait == 0 {
		o.InitialWait = 30 * time.Second
	}
	if o.BlockCheck != '1' && o.BlockCheck != '3' {
		o.BlockCheck = '3'
	}
	return o
}

// Progress is called as file data moves. done and total are in bytes of
// file content (total is -1 when receiving, since the size isn't known).
type Progress func(name string, done, total int)

// RemoteError is an E packet from the other side.
type RemoteError struct{ Msg string }

func (e *RemoteError) Error() string { return "kermit: remote error: " + e.Msg }

// Canceled means the other side asked to stop (X or Z in an ACK).
var ErrCanceled = errors.New("kermit: transfer canceled by remote")

func tochar(x int) byte { return byte(x + 32) }
func unchar(c byte) int { return int(c) - 32 }
func ctl(c byte) byte   { return c ^ 64 }

type packet struct {
	seq  int
	typ  byte
	data []byte
}

func (p packet) String() string { return fmt.Sprintf("%c%d[%q]", p.typ, p.seq, p.data) }

func checkLen(chk byte) int {
	if chk == '3' {
		return 3
	}
	return 1
}

// check1 is the type 1 block check: a 6-bit folded sum.
func check1(b []byte) []byte {
	s := 0
	for _, c := range b {
		s += int(c)
	}
	return []byte{tochar((s + ((s & 0xC0) >> 6)) & 0x3F)}
}

// check3 is the type 3 block check: Kermit's CRC-CCITT, sent as three
// printable characters.
func check3(b []byte) []byte {
	crc := crc16(b)
	return []byte{tochar((crc >> 12) & 0x0F), tochar((crc >> 6) & 0x3F), tochar(crc & 0x3F)}
}

// crc16 is CRC-16/KERMIT (CCITT polynomial, reflected, zero init).
func crc16(b []byte) int {
	crc := 0
	for _, c := range b {
		q := (crc ^ int(c)) & 0x0F
		crc = (crc >> 4) ^ (q * 0x1081)
		q = (crc ^ (int(c) >> 4)) & 0x0F
		crc = (crc >> 4) ^ (q * 0x1081)
	}
	return crc
}

func blockCheck(chk byte, b []byte) []byte {
	if chk == '3' {
		return check3(b)
	}
	return check1(b)
}

// encodePacket builds the bytes on the wire for p using block check chk.
func encodePacket(p packet, chk byte) []byte {
	n := len(p.data) + 2 + checkLen(chk)
	buf := make([]byte, 0, n+3)
	buf = append(buf, mark, tochar(n), tochar(p.seq%64), p.typ)
	buf = append(buf, p.data...)
	buf = append(buf, blockCheck(chk, buf[1:])...)
	return append(buf, eol)
}

var errBadPacket = errors.New("kermit: bad packet")

// readPacket waits up to timeout for one well-formed packet. It skips line
// noise and resynchronizes on every SOH. A packet that arrives complete but
// fails its block check returns errBadPacket so the caller can NAK it.
func readPacket(port Port, chk byte, timeout time.Duration) (packet, error) {
	deadline := time.Now().Add(timeout)
	next := func() (byte, error) {
		left := time.Until(deadline)
		if left <= 0 {
			return 0, ErrTimeout
		}
		return port.ReadTimeout(left)
	}

	c, err := next()
outer:
	for {
		if err != nil {
			return packet{}, err
		}
		if c != mark {
			c, err = next()
			continue
		}
		// c is SOH: read LEN.
		if c, err = next(); err != nil {
			return packet{}, err
		}
		if c == mark {
			continue
		}
		n := unchar(c)
		if n < 3 || n > maxLen {
			c, err = next()
			continue
		}
		body := []byte{c}
		for i := 0; i < n; i++ {
			if c, err = next(); err != nil {
				return packet{}, err
			}
			if c == mark { // truncated packet, start over at this SOH
				continue outer
			}
			body = append(body, c)
		}
		typ := body[2]
		// S, I and E packets (and their ACKs) are always type 1, and a
		// duplicate S can arrive after we switched block check.
		for _, try := range []byte{chk, '1'} {
			cl := checkLen(try)
			if n-2 < cl {
				continue
			}
			payload, sum := body[:len(body)-cl], body[len(body)-cl:]
			if string(blockCheck(try, payload)) == string(sum) {
				if try != chk && typ != 'S' && typ != 'I' && typ != 'E' && typ != 'Y' && typ != 'N' {
					break
				}
				return packet{seq: unchar(body[1]), typ: typ, data: append([]byte(nil), payload[3:]...)}, nil
			}
		}
		return packet{seq: unchar(body[1])}, errBadPacket
	}
}

// params are the Send-Init fields we use.
type params struct {
	maxl int  // longest packet the sender of these params can receive
	time int  // seconds it wants us to wait before timing out
	eol  byte // packet terminator it wants
	qctl byte // control prefix
	qbin byte // 8th-bit prefix, 'Y' (will if you ask) or 'N' (won't)
	chkt byte // block check type
}

func (p params) encode() []byte {
	return []byte{tochar(p.maxl), tochar(p.time), tochar(0), ctl(0), tochar(int(p.eol)), p.qctl, p.qbin, p.chkt}
}

func parseParams(d []byte) params {
	p := params{maxl: 80, time: 5, eol: eol, qctl: '#', qbin: 'N', chkt: '1'}
	field := func(i int) (byte, bool) {
		if i < len(d) && d[i] != ' ' {
			return d[i], true
		}
		return 0, false
	}
	if c, ok := field(0); ok {
		p.maxl = unchar(c)
		if p.maxl < 10 || p.maxl > maxLen {
			p.maxl = 80
		}
	}
	if c, ok := field(1); ok {
		p.time = unchar(c)
	}
	if c, ok := field(4); ok {
		p.eol = byte(unchar(c))
	}
	if c, ok := field(5); ok {
		p.qctl = c
	}
	if c, ok := field(6); ok {
		p.qbin = c
	}
	if c, ok := field(7); ok {
		p.chkt = c
	}
	return p
}

func isPrefixChar(c byte) bool { return (c >= 33 && c <= 62) || (c >= 96 && c <= 126) }

// negotiateQbin returns the agreed 8th-bit prefix, or 0 for none.
func negotiateQbin(ours, theirs byte) byte {
	switch {
	case isPrefixChar(ours) && (theirs == 'Y' || theirs == ours):
		return ours
	case isPrefixChar(theirs) && ours == 'Y':
		return theirs
	}
	return 0
}
