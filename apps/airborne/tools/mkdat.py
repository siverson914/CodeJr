#!/usr/bin/env python3
"""Build AIRBORNE.DAT for the PCjr from the original 1985 disk's files.

The originals stay outside the repo, in ~/.local/share/codejr/airborne
(made by the reverse-engineering tools there); this writes the PCjr's data
file next to AIRBORNE.EXE. Nothing it reads or writes is committed.

Everything is shrunk from the Mac's 512x342 to the PCjr's 320x200 with an
exact area filter (8 Mac columns -> 5, 12 Mac rows -> 7) and 4 greys by how
much of each PCjr pixel was black: index 0 white, 1 light grey, 2 dark grey,
3 black. Pixels are 2 bits, the left pixel in the high bits.

File (little-endian): "AIRB", u16 version, u16 chunks, then per chunk an
id[4], u32 offset, u32 length. Chunks:
  CODE  CODE 4 of the game (the logic reads its sprites and level table)
  BACK  the game screen without sprites: 200 rows of 80 bytes
  IMGS  u16 n, u32 offset[n] (from the chunk start), then images:
        u8 bytes a row, u8 rows, s8 first row, u8 versions (shifted copies
        by 4/versions pixels), then versions x rows x bytes
  SMAP  per Mac sprite: u16 CODE 4 offset, u16 image[16] by x & 15 (FFFF none)
  DIGT  per digit 0-9: u8 Mac advance, s8 Mac origin x, u16 image
  OVER  u16 image: the game-over box
  TITL PRES SPIC INS1..INS5  full screens (16000 bytes)
  SFX   the sound effects for the SN76496 (tools/mksfx.py)

  ~/.local/share/codejr/airborne/.venv/bin/python tools/mkdat.py [OUT]
"""
import json, os, struct, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mksfx  # noqa: E402

SRC = os.path.expanduser('~/.local/share/codejr/airborne')
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'AIRBORNE.DAT')

code = open(os.path.join(SRC, 'res/CODE/4.bin'), 'rb').read()[4:]
LEVELS = (0.12, 0.38, 0.65)


def grey(cov):
    return np.digitize(cov, LEVELS).astype(np.uint8)  # 0..3


def scale(bits, ax=(8, 5), ay=(12, 7), out_w=None, out_h=None):
    """Area-filter a 0/1 array: ax = (a Mac columns -> b PCjr columns)."""
    (am, bm), (cm, dm) = ax, ay
    h, w = bits.shape
    ow = out_w if out_w is not None else -(-w * bm // am)
    oh = out_h if out_h is not None else -(-h * dm // cm)
    # sub-pixel grid: each source pixel bm x dm cells, each output am x cm
    pad = np.zeros((oh * cm, ow * am), dtype=np.float32)
    up = np.repeat(np.repeat(bits.astype(np.float32), dm, axis=0), bm, axis=1)
    hh, ww = min(up.shape[0], pad.shape[0]), min(up.shape[1], pad.shape[1])
    pad[:hh, :ww] = up[:hh, :ww]
    cov = pad.reshape(oh, cm, ow, am).mean(axis=(1, 3))
    return grey(cov)


def pack_rows(g, shift=0, wb=None):
    """2-bit pixels, 4 to a byte, left in the high bits, shifted right by shift pixels."""
    h, w = g.shape
    wb = wb or -(-(w + shift) // 4)
    out = bytearray(h * wb)
    for y in range(h):
        for x in range(w):
            v = int(g[y, x])
            if v:
                px = x + shift
                out[y * wb + px // 4] |= v << (6 - 2 * (px & 3))
    return bytes(out), wb


def image(g, versions):
    """Crop empty rows; return the IMGS record."""
    rows = np.nonzero(g.any(axis=1))[0]
    if len(rows) == 0:
        g = g[:1]; top = 0
    else:
        top = int(rows[0]); g = g[rows[0]:rows[-1] + 1]
    h, w = g.shape
    wb = -(-(w + 3) // 4)
    data = b''
    for v in range(versions):
        d, _ = pack_rows(g, v * 4 // versions, wb)
        data += d
    return struct.pack('<BBbB', wb, h, top, versions) + data


def screen_bits(rowbytes, height, raw):
    a = np.frombuffer(raw, dtype=np.uint8)[:rowbytes * height].reshape(height, rowbytes)
    return np.unpackbits(a, axis=1)


def png_bits(path, crop=None):
    im = Image.open(os.path.join(SRC, path)).convert('L')
    if crop:
        im = im.crop(crop)
    return (np.asarray(im) < 128).astype(np.uint8)


def full(g):
    out = np.zeros((200, 320), dtype=np.uint8)
    h, w = min(200, g.shape[0]), min(320, g.shape[1])
    out[:h, :w] = g[:h, :w]
    return pack_rows(out)[0]


def main():
    imgs = []
    # ---- sprites: every slot the game can reach ----
    sj = json.load(open(os.path.join(SRC, 're/sprites/sprites.json')))
    smap = b''
    for sp in sj['sprites']:
        off = int(sp['offset'], 16)
        W, H, S = struct.unpack('>hhh', code[off:off + 6])
        width, height = (W + 1) * 32, H + 1
        ids = [0xFFFF] * 16
        for slot in sp['slots']:
            start = off + 6 + slot * S
            raw = code[start:start + (W + 1) * 4 * height]
            bits = screen_bits((W + 1) * 4, height, raw)
            ids[slot] = len(imgs)
            imgs.append(image(scale(bits), 2))
        smap += struct.pack('<H16H', off, *ids)
    # ---- the background: white game area, the status bar ----
    hud = screen_bits(64, 342, open(os.path.join(SRC, 're/hud_init.bin'), 'rb').read()).copy()
    hud[:300, :] = 0
    hud[311:331, 238:300] = 0  # the Score digits (DOSC's rect)
    hud[311:331, 394:466] = 0  # the Hi Score digits
    back = full(scale(hud))
    # ---- score digits: Airborne Scores 12 ----
    font = json.load(open(os.path.join(SRC, 're/pics/json/FONT_AirborneScores12.json')))
    digt = b''
    for d in '0123456789':
        g = font['glyphs'][str(ord(d))]
        rows = [r.replace('#', '1').replace('.', '0') for r in g['rows']]
        iw = g['imageWidth']
        bits = np.array([[int(c) for c in r[:iw]] for r in rows], dtype=np.uint8) if iw else np.zeros((len(rows), 1), np.uint8)
        digt += struct.pack('<BbH', g['width'], g['originX'], len(imgs))
        imgs.append(image(scale(bits), 4))
    # ---- the game-over box: as the original, but "Press Enter" ----
    box = np.zeros((40, 140), dtype=np.uint8)
    box[0:2, 8:132] = 1; box[38:40, 8:132] = 1; box[8:32, 0:2] = 1; box[8:32, 138:140] = 1
    for i in range(8):  # rounded corners (approximately the 16x16 oval)
        k = [8, 5, 4, 3, 2, 1, 1, 0][i]
        for (y, sgn) in ((i, 1), (39 - i, -1)):
            box[y, k:k + 2] = 1; box[y, 138 - k:140 - k] = 1
    chicago = json.load(open(os.path.join(SRC, 're/pics/json/FONT_Chicago12.json')))
    text = 'Press Enter'
    x = 70 - sum(chicago['glyphs'][str(ord(c))]['width'] for c in text) // 2
    for c in text:
        g = chicago['glyphs'][str(ord(c))]
        for r, row in enumerate(g['rows']):
            for i, ch in enumerate(row):
                if ch == '#':
                    yy, xx = 24 - chicago['header']['ascent'] + r, x + g['originX'] + i
                    if 0 <= yy < 40 and 0 <= xx < 140:
                        box[yy, xx] = 1
        x += g['width']
    over = len(imgs)
    imgs.append(image(scale(box), 4))
    # ---- full screens ----
    screens = {
        b'TITL': full(scale(png_bits('re/pics/TitleScreen_composite.png'))),
        b'PRES': full(scale(png_bits('re/pics/StartupScreen.png'))),
        b'SPIC': full(scale(png_bits('re/pics/Air_Startup_Pic.png', (0, 0, 512, 342)))),
    }
    pages = sorted(p for p in os.listdir(os.path.join(SRC, 're/pics')) if p.startswith('PICT_30') and p.endswith('.png'))
    for i, p in enumerate(pages):
        screens[b'INS%d' % (i + 1)] = full(scale(png_bits('re/pics/' + p), (13, 10), (6, 5), 320, 200))
    # ---- write ----
    ihead = struct.pack('<H', len(imgs))
    offs, body, pos = [], b'', 2 + 4 * len(imgs)
    for im in imgs:
        offs.append(pos); body += im; pos += len(im)
    chunks = [(b'CODE', code), (b'BACK', back), (b'IMGS', ihead + struct.pack('<%dI' % len(offs), *offs) + body),
              (b'SMAP', smap), (b'DIGT', digt), (b'OVER', struct.pack('<H', over)), (b'SFX ', mksfx.build()[0])] + list(screens.items())
    head = b'AIRB' + struct.pack('<HH', 1, len(chunks))
    pos = len(head) + 12 * len(chunks)
    dirs, data = b'', b''
    for cid, blob in chunks:
        dirs += cid + struct.pack('<II', pos, len(blob)); data += blob; pos += len(blob)
    open(OUT, 'wb').write(head + dirs + data)
    print('%s: %d bytes, %d images (%d bytes), %d sprites' % (OUT, len(head + dirs + data), len(imgs), len(body), len(sj['sprites'])))
    # previews for a look on the laptop
    prev = os.path.join(HERE, '..', 'build')
    os.makedirs(prev, exist_ok=True)
    pal = np.array([255, 170, 85, 0], dtype=np.uint8)
    for name, blob in [(b'BACK', back)] + list(screens.items()):
        a = np.frombuffer(blob, dtype=np.uint8).reshape(200, 80)
        px = np.stack([(a >> s) & 3 for s in (6, 4, 2, 0)], axis=2).reshape(200, 320)
        Image.fromarray(pal[px]).resize((640, 480), Image.NEAREST).save(os.path.join(prev, name.decode() + '.png'))


if __name__ == '__main__':
    main()
