package cjp

import (
	"encoding/binary"
	"io"
	"math/bits"
	"sync"
	"sync/atomic"

	"codejr/internal/lz"
)

// File transfer by pulling. The PCjr corrupts what it receives while it
// transmits, and its UART has no FIFO, so files don't go over the
// acknowledged link. Instead the client asks for a chunk of the file with
// a Pull datagram and then only listens, polling the UART in a tight loop
// while the server sends the chunk's blocks back to back as one burst.
// Whatever arrives damaged is asked for again. The server keeps no state:
// it answers each Pull with the blocks named in its bitmap.
//
// A file is cut into chunks of ChunkRaw bytes, each compressed on its own
// (LZ4 block format, or stored if that isn't smaller). A chunk travels as
//
//	method (0 stored, 1 LZ)   1 byte
//	raw length                2 bytes LE
//	data length               2 bytes LE
//	CRC-16 of the raw bytes   2 bytes LE
//	data
//
// cut into blocks of BlockSize bytes, at most BlocksPerChunk of them. Each
// block is a Block frame: seq = block index, ack = chunk index mod 256.
//
// The C side is client/pull.c; keep the two in step.
const (
	BlockSize      = MaxPayload
	BlocksPerChunk = 32
	chunkHeader    = 7
	ChunkRaw       = BlocksPerChunk*BlockSize - chunkHeader // 4089
)

// MakeChunks cuts data into chunk payloads ready to send.
func MakeChunks(data []byte) [][]byte {
	var chunks [][]byte
	for off := 0; off < len(data); off += ChunkRaw {
		raw := data[off:min(off+ChunkRaw, len(data))]
		method, body := byte(1), lz.Compress(raw)
		if len(body) >= len(raw) {
			method, body = 0, raw
		}
		c := make([]byte, chunkHeader, chunkHeader+len(body))
		c[0] = method
		binary.LittleEndian.PutUint16(c[1:], uint16(len(raw)))
		binary.LittleEndian.PutUint16(c[3:], uint16(len(body)))
		binary.LittleEndian.PutUint16(c[5:], CRC16(raw))
		chunks = append(chunks, append(c, body...))
	}
	return chunks
}

// Burst returns the bytes answering a Pull for the given chunk: a Block
// frame for every block named in bitmap. A block the chunk doesn't have
// is sent as two bare FENDs, so the burst always holds exactly
// 2*popcount(bitmap) FEND bytes and the client can stop listening by
// counting them, without waiting for the line to go quiet.
func Burst(index int, chunk []byte, bitmap uint32) []byte {
	total := (len(chunk) + BlockSize - 1) / BlockSize
	out := make([]byte, 0, bits.OnesCount32(bitmap)*(BlockSize+12))
	for b := 0; b < BlocksPerChunk; b++ {
		if bitmap&(1<<b) == 0 {
			continue
		}
		if b >= total {
			out = append(out, FEND, FEND)
			continue
		}
		payload := chunk[b*BlockSize : min((b+1)*BlockSize, len(chunk))]
		out = append(out, Encode(Frame{Type: Block, Seq: byte(b), Ack: byte(index), Payload: payload})...)
	}
	return out
}

// ParsePull decodes a Pull datagram.
func ParsePull(p []byte) (index int, bitmap uint32, ok bool) {
	if len(p) != 6 {
		return 0, 0, false
	}
	return int(binary.LittleEndian.Uint16(p)), binary.LittleEndian.Uint32(p[2:]), true
}

// OfferPayload builds a FileOffer message.
func OfferPayload(name string, size, chunks int) []byte {
	p := make([]byte, 6, 6+len(name))
	binary.LittleEndian.PutUint32(p, uint32(size))
	binary.LittleEndian.PutUint16(p[4:], uint16(chunks))
	return append(p, name...)
}

// Burster writes bursts to the line one at a time. A new burst replaces
// the one still being written: when the client asks again (because the
// answer stalled on the way and it gave up waiting), finishing the old
// answer first would leave every later answer queued behind stale data.
type Burster struct {
	out io.Writer
	gen atomic.Int64
	wg  sync.WaitGroup
}

func NewBurster(out io.Writer) *Burster { return &Burster{out: out} }

// Send starts writing data and returns at once.
func (b *Burster) Send(data []byte) {
	g := b.gen.Add(1)
	b.wg.Add(1)
	go func() {
		defer b.wg.Done()
		// Small pieces, so a replaced burst stops within a few dozen bytes.
		for off := 0; off < len(data) && b.gen.Load() == g; off += 64 {
			if _, err := b.out.Write(data[off:min(off+64, len(data))]); err != nil {
				return
			}
		}
	}()
}

// Stop abandons the burst being written and waits for the writer to finish.
func (b *Burster) Stop() {
	b.gen.Add(1)
	b.wg.Wait()
}
