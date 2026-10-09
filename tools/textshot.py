#!/usr/bin/env python3
"""Makes a PNG of CodeJr's own text screen on the PCjr from the file the
client sends for `just pcjr-screen` (server op "screen"): 4000 bytes of
video memory, then the ROM's 8x8 characters 0-127 and 128-255. The picture
is 640x400 (each row of dots doubled) in the 16 standard colors, drawn
with the PCjr's own character shapes.

    tools/textshot.py ~/.local/share/codejr/workspace/.codejr/screen.bin out.png
"""
import struct
import sys
import zlib

PAL = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170),
       (170, 85, 0), (170, 170, 170), (85, 85, 85), (85, 85, 255), (85, 255, 85),
       (85, 255, 255), (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]


def main():
    data = open(sys.argv[1], "rb").read()
    if len(data) < 4000 + 2048:
        sys.exit("not a text screen file: %d bytes" % len(data))
    vid, font = data[:4000], data[4000:4000 + 2048]
    rows = []
    for r in range(25):
        for y in range(8):
            line = bytearray()
            for c in range(80):
                ch, attr = vid[(r * 80 + c) * 2], vid[(r * 80 + c) * 2 + 1]
                bits = font[ch * 8 + y]
                fg, bg = PAL[attr & 15], PAL[attr >> 4 & 7]
                for x in range(8):
                    line += bytes(fg if bits & (0x80 >> x) else bg)
            rows += [line, line]

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    raw = b"".join(b"\0" + bytes(row) for row in rows)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 640, 400, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(sys.argv[2], "wb").write(png)


if __name__ == "__main__":
    main()
