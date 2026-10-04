package kermit

import (
	"fmt"
	"strings"
	"time"
)

// Receive accepts a batch of files from a Kermit that is sending.
func Receive(port Port, opt Options, progress Progress) ([]File, error) {
	opt = opt.withDefaults()

	// Wait for Send-Init, nudging the sender with NAKs.
	var init packet
	deadline := time.Now().Add(opt.InitialWait)
	for {
		p, err := readPacket(port, '1', opt.Timeout)
		if err == nil && p.typ == 'S' {
			init = p
			break
		}
		if err != nil && err != ErrTimeout && err != errBadPacket {
			return nil, err
		}
		if err == nil && p.typ == 'E' {
			return nil, &RemoteError{Msg: strings.TrimSpace(string(p.data))}
		}
		if time.Now().After(deadline) {
			return nil, fmt.Errorf("kermit: no Send-Init within %v", opt.InitialWait)
		}
		port.Write(encodePacket(packet{seq: 0, typ: 'N'}, '1'))
	}

	theirs := parseParams(init.data)
	ours := params{maxl: maxLen, time: int(opt.Timeout.Seconds()), eol: eol, qctl: '#', chkt: '1'}
	switch {
	case isPrefixChar(theirs.qbin):
		ours.qbin = 'Y'
	case theirs.qbin == 'Y':
		ours.qbin = '&'
	default:
		ours.qbin = 'N'
	}
	if theirs.chkt == '3' && opt.BlockCheck == '3' {
		ours.chkt = '3'
	}
	qbin := negotiateQbin(ours.qbin, theirs.qbin)
	chk := ours.chkt

	initAck := encodePacket(packet{seq: init.seq, typ: 'Y', data: ours.encode()}, '1')
	port.Write(initAck)
	lastAck := initAck
	expect := (init.seq + 1) % 64

	var files []File
	var cur *File
	retries := 0
	for {
		p, err := readPacket(port, chk, opt.Timeout)
		if err == ErrTimeout || err == errBadPacket {
			if retries++; retries > opt.Retries {
				return files, fmt.Errorf("kermit: gave up waiting for packet %d", expect)
			}
			port.Write(encodePacket(packet{seq: expect, typ: 'N'}, chk))
			continue
		}
		if err != nil {
			return files, err
		}
		retries = 0

		if p.seq != expect {
			if p.seq == (expect+63)%64 {
				port.Write(lastAck) // our ACK was lost; repeat it
			} else {
				port.Write(encodePacket(packet{seq: expect, typ: 'N'}, chk))
			}
			continue
		}

		ack := packet{seq: p.seq, typ: 'Y'}
		done := false
		switch p.typ {
		case 'F':
			files = append(files, File{Name: string(decodeData(p.data, qbin))})
			cur = &files[len(files)-1]
			if progress != nil {
				progress(cur.Name, 0, -1)
			}
		case 'A', 'X':
			// Attributes and screen text: accept and ignore.
		case 'D':
			if cur != nil {
				cur.Data = append(cur.Data, decodeData(p.data, qbin)...)
				if progress != nil {
					progress(cur.Name, len(cur.Data), -1)
				}
			}
		case 'Z':
			if cur != nil && string(p.data) == "D" {
				files = files[:len(files)-1] // sender discarded it
			}
			cur = nil
		case 'B':
			done = true
		case 'E':
			return files, &RemoteError{Msg: strings.TrimSpace(string(decodeData(p.data, qbin)))}
		default:
			port.Write(encodePacket(packet{seq: p.seq, typ: 'E', data: []byte("unexpected packet")}, chk))
			return files, fmt.Errorf("kermit: unexpected %c packet", p.typ)
		}
		lastAck = encodePacket(ack, chk)
		port.Write(lastAck)
		expect = (expect + 1) % 64
		if done {
			// If this last ACK is lost the sender may report an error, but
			// every file is already here.
			return files, nil
		}
	}
}
