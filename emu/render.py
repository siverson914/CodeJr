#!/usr/bin/env python3
"""Render a codejr -trace file as the 80x25 text screen a VT100 would show.

Kermit APC commands and Kermit packets are removed first, since MS-Kermit
consumes those instead of displaying them. Needs pyte (pip install pyte).
"""
import re
import sys

import pyte

data = open(sys.argv[1], "rb").read()
data = re.sub(rb"\x1b_[^\x1b]*\x1b\\\\", b"", data)  # APC
data = re.sub(rb"\x01[^\r\x01]*\r", b"", data)  # Kermit packets
screen = pyte.Screen(80, 25)
stream = pyte.ByteStream(screen)
stream.feed(data)
print("+" + "-" * 80 + "+")
for row in screen.display:
    print("|" + row + "|")
print("+" + "-" * 80 + "+")
print(f"cursor at row {screen.cursor.y + 1}, col {screen.cursor.x + 1}")
