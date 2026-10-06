// Package cjp is the CodeJr protocol: what the native PCjr client
// (CODEJR.EXE) and the server speak instead of Kermit and escape codes.
//
// A frame on the wire is
//
//	FEND  type seq ack len payload... crc-hi crc-lo  FEND
//
// with every byte between the FENDs escaped if it is FEND, FESC, XON,
// XOFF, 0xFF or CR: those become FESC, byte^0x20. XON/XOFF are escaped because
// the WiFi modem uses XON/XOFF flow control, and 0xFF because some modems
// speak telnet. The CRC is CRC-16/CCITT-FALSE over type..payload. The
// length byte means a dropped or extra byte (the PCjr's most common error)
// is always caught, not just with the CRC's 1-in-65536 odds.
//
// Reliability: data frames carry a sequence number (mod 256); at most
// Window of them are unacknowledged at a time; every frame's ack field is
// the next sequence number its sender expects (cumulative). Receivers keep
// frames that arrive after a gap and send a Nak naming the missing one,
// which is resent on its own (selective repeat); a timer covers lost acks.
// Ack and Nak frames carry no sequence number and are never acknowledged.
//
// The C side is client/cjp.c; keep the two in step.
package cjp

import "errors"

const (
	FEND       = 0xC0
	FESC       = 0xDB
	MaxPayload = 128
	// Window is how many frames this side may have unacknowledged. Each
	// sender picks its own; the PCjr client uses 4 to save memory.
	Window = 8
)

// Message types. Keep in sync with client/cjp.h.
const (
	Ack        byte = 0x00 // acknowledgement only
	Hello      byte = 0x01 // both ways: "CODEJR/1 <who> <version>"
	Text       byte = 0x02 // server: style byte, then text (\n = new line)
	Status     byte = 0x03 // server: status line text
	Input      byte = 0x04 // client: a line the user typed
	Key        byte = 0x05 // client: control key (KeyCtrlC, ...)
	FileBegin  byte = 0x06 // server: size (4 bytes LE), then 8.3 name
	FileData   byte = 0x07 // server: file bytes
	FileEnd    byte = 0x08 // server: end of file
	FileResult byte = 0x09 // client: 0 ok / 1 error, then name
	Bye        byte = 0x0A // both ways: hang up
	Clear      byte = 0x0B // server: clear the transcript
	Nak        byte = 0x0C // "I'm missing this one": ack field = missing seq
	FileOffer  byte = 0x0D // server: size (4 LE), chunk count (2 LE), 8.3 name; the client pulls it
	Ready      byte = 0x0E // server: the user can type now (clients that said ui=1)
	Temp       byte = 0x0F // server: a transient transcript line, erased by the next text (ui=1)
	Result     byte = 0x10 // client: how a /run went: 'B' name, 'D' text..., 'E' (server said "results")
	DeltaOffer byte = 0x11 // server: like FileOffer, but the chunks are an edit script for the PCjr's copy (delta=1)
	Play       byte = 0x12 // server: play this clip with SAY.EXE once the reply is done (voice=1)
	Voice      byte = 0x13 // server: 1 voice on, 0 off, so the client can say goodbye (voice=1)

	// Types from DatagramBase up are datagrams: no sequence number, no
	// ack, delivered as they arrive. The file pull protocol uses them
	// (see pull.go).
	DatagramBase byte = 0x40
	Pull         byte = 0x40 // client: chunk index (2 LE), bitmap of wanted blocks (4 LE)
	Block        byte = 0x41 // server: seq = block index, ack = chunk index mod 256
)

// ChunkSize is how much data senders put in each frame. Smaller than
// MaxPayload on purpose: the PCjr loses about one received byte in a
// hundred, and a shorter frame is less likely to be hit.
const ChunkSize = 64

// Text styles (first payload byte of a Text message).
const (
	StylePlain  byte = 0
	StyleUser   byte = 1
	StyleNote   byte = 2
	StyleWarn   byte = 3
	StyleBanner byte = 4
)

// Keys sent in Key messages.
const (
	KeyCtrlC byte = 3
)

// CRC16 is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
func CRC16(b []byte) uint16 {
	crc := uint16(0xFFFF)
	for _, c := range b {
		crc ^= uint16(c) << 8
		for i := 0; i < 8; i++ {
			if crc&0x8000 != 0 {
				crc = crc<<1 ^ 0x1021
			} else {
				crc <<= 1
			}
		}
	}
	return crc
}

// needsEscape lists the bytes never sent raw: the frame markers, XON/XOFF
// (the modem's flow control), 0xFF (telnet IAC) and CR. Zimodem treats
// "ATDT" as "dial with TELNET translation", and telnet drops a NUL after
// a CR, which cut bytes out of frames on the real PCjr.
func needsEscape(c byte) bool {
	return c == FEND || c == FESC || c == 0x11 || c == 0x13 || c == 0xFF || c == 0x0D
}

// Frame is one decoded frame.
type Frame struct {
	Type    byte
	Seq     byte
	Ack     byte
	Payload []byte
}

// Encode returns the bytes on the wire for f.
func Encode(f Frame) []byte {
	body := make([]byte, 0, 4+len(f.Payload)+2)
	body = append(body, f.Type, f.Seq, f.Ack, byte(len(f.Payload)))
	body = append(body, f.Payload...)
	crc := CRC16(body)
	body = append(body, byte(crc>>8), byte(crc))

	out := make([]byte, 0, len(body)*2+2)
	out = append(out, FEND)
	for _, c := range body {
		if needsEscape(c) {
			out = append(out, FESC, c^0x20)
		} else {
			out = append(out, c)
		}
	}
	return append(out, FEND)
}

var ErrBadFrame = errors.New("cjp: bad frame")

// Decoder turns a byte stream into frames. Bytes outside frames are
// ignored, so modem chatter and line noise don't matter.
type Decoder struct {
	buf    []byte
	in     bool
	escape bool
}

// Feed adds one byte. It returns a frame when one completes, ErrBadFrame
// for a frame that fails its CRC or is malformed, or nil, nil.
func (d *Decoder) Feed(c byte) (*Frame, error) {
	if c == 0x11 || c == 0x13 {
		// Raw XON/XOFF is flow control (the PCjr pauses the modem around
		// disk writes); inside frames these values are always escaped.
		return nil, nil
	}
	if c == FEND {
		if !d.in || len(d.buf) == 0 {
			// Start of a frame (or two FENDs in a row: an empty frame).
			d.in, d.escape, d.buf = true, false, d.buf[:0]
			return nil, nil
		}
		body := d.buf
		d.buf = nil
		d.escape = false
		// This FEND also opens the next frame.
		if len(body) < 6 || len(body)-6 > MaxPayload || int(body[3]) != len(body)-6 {
			return nil, ErrBadFrame
		}
		n := len(body) - 2
		if CRC16(body[:n]) != uint16(body[n])<<8|uint16(body[n+1]) {
			return nil, ErrBadFrame
		}
		return &Frame{Type: body[0], Seq: body[1], Ack: body[2], Payload: append([]byte(nil), body[4:n]...)}, nil
	}
	if !d.in {
		return nil, nil
	}
	if d.escape {
		d.escape = false
		c ^= 0x20
	} else if c == FESC {
		d.escape = true
		return nil, nil
	}
	if len(d.buf) > MaxPayload+9 {
		// Runaway (a lost FEND): drop it and wait for the next FEND.
		d.in, d.buf = false, d.buf[:0]
		return nil, ErrBadFrame
	}
	d.buf = append(d.buf, c)
	return nil, nil
}

// StartInFrame tells the decoder a FEND was already consumed elsewhere.
func (d *Decoder) StartInFrame() { d.in, d.escape, d.buf = true, false, d.buf[:0] }
