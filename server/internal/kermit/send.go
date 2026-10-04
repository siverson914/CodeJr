package kermit

import (
	"fmt"
	"strings"
)

// File is one file in a transfer.
type File struct {
	Name string
	Data []byte
}

type sender struct {
	port Port
	opt  Options
	seq  int
	chk  byte // block check in use
	qbin byte // 8th-bit prefix in use, 0 for none
	maxl int  // longest packet the receiver accepts
}

// Send transfers files as one batch to a Kermit that is in RECEIVE.
func Send(port Port, files []File, opt Options, progress Progress) error {
	s := &sender{port: port, opt: opt.withDefaults(), chk: '1'}
	if err := s.sendInit(); err != nil {
		return err
	}
	for _, f := range files {
		if _, err := s.sendFile(f, progress); err != nil {
			return err
		}
	}
	// Every file is already confirmed by its Z ACK. If only the ACK to B is
	// lost, the receiver has finished and won't answer again: that's success.
	if _, err := s.exchange('B', nil); err != nil {
		if _, remote := err.(*RemoteError); remote {
			return err
		}
	}
	return nil
}

func (s *sender) sendInit() error {
	ours := params{maxl: maxLen, time: int(s.opt.Timeout.Seconds()), eol: eol, qctl: '#', qbin: '&', chkt: s.opt.BlockCheck}
	ack, err := s.exchange('S', ours.encode())
	if err != nil {
		return err
	}
	theirs := parseParams(ack.data)
	s.maxl = theirs.maxl
	s.qbin = negotiateQbin(ours.qbin, theirs.qbin)
	if theirs.chkt == ours.chkt {
		s.chk = ours.chkt
	}
	return nil
}

// sendFile sends F, D... and Z for one file. It reports whether the
// receiver skipped the file.
func (s *sender) sendFile(f File, progress Progress) (bool, error) {
	name, _ := encodeData([]byte(f.Name), s.qbin, s.maxData())
	if _, err := s.exchange('F', name); err != nil {
		return false, err
	}
	if progress != nil {
		progress(f.Name, 0, len(f.Data))
	}
	for off := 0; off < len(f.Data); {
		enc, n := encodeData(f.Data[off:], s.qbin, s.maxData())
		ack, err := s.exchange('D', enc)
		if err != nil {
			return false, err
		}
		off += n
		if progress != nil {
			progress(f.Name, off, len(f.Data))
		}
		if len(ack.data) > 0 {
			switch ack.data[0] {
			case 'X': // skip this file
				_, err := s.exchange('Z', []byte("D"))
				return true, err
			case 'Z': // cancel the batch
				s.exchange('Z', []byte("D"))
				s.exchange('B', nil)
				return true, ErrCanceled
			}
		}
	}
	_, err := s.exchange('Z', nil)
	return false, err
}

func (s *sender) maxData() int { return s.maxl - 2 - checkLen(s.chk) }

// exchange sends one packet and waits for its ACK, retrying on timeouts,
// NAKs and garbage. The S packet goes out with block check 1.
func (s *sender) exchange(typ byte, data []byte) (packet, error) {
	p := packet{seq: s.seq, typ: typ, data: data}
	chk := s.chk
	if typ == 'S' {
		chk = '1'
	}
	wire := encodePacket(p, chk)
	for try := 0; try <= s.opt.Retries; try++ {
		if _, err := s.port.Write(wire); err != nil {
			return packet{}, err
		}
		resp, err := readPacket(s.port, chk, s.opt.Timeout)
		switch {
		case err == ErrTimeout || err == errBadPacket:
			continue
		case err != nil:
			return packet{}, err
		}
		switch {
		case resp.typ == 'Y' && resp.seq == s.seq:
			s.seq = (s.seq + 1) % 64
			return resp, nil
		case resp.typ == 'N' && resp.seq == (s.seq+1)%64:
			// NAK for the next packet means this one arrived.
			s.seq = (s.seq + 1) % 64
			return packet{typ: 'Y'}, nil
		case resp.typ == 'E':
			return packet{}, &RemoteError{Msg: strings.TrimSpace(string(decodeData(resp.data, s.qbin)))}
		}
		// NAK for this packet, or a stale ACK: send it again.
	}
	return packet{}, fmt.Errorf("kermit: no response to %c packet after %d tries", typ, s.opt.Retries+1)
}

// Abort sends an error packet, which makes any Kermit stop a transfer.
func Abort(port Port, msg string) {
	port.Write(encodePacket(packet{seq: 0, typ: 'E', data: []byte(msg)}, '1'))
}
