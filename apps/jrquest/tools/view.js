// .view text -> VIEW resource, and back, plus a contact sheet PNG.

import { readFileSync, writeFileSync } from 'node:fs';
import { SourceError, Bytes, asciiBytes, bytesToText, word, isMain, runMain, options } from './util.js';

export const PALETTE = [
  [0x00, 0x00, 0x00], [0x00, 0x00, 0xAA], [0x00, 0xAA, 0x00], [0x00, 0xAA, 0xAA],
  [0xAA, 0x00, 0x00], [0xAA, 0x00, 0xAA], [0xAA, 0x55, 0x00], [0xAA, 0xAA, 0xAA],
  [0x55, 0x55, 0x55], [0x55, 0x55, 0xFF], [0x55, 0xFF, 0x55], [0x55, 0xFF, 0xFF],
  [0xFF, 0x55, 0x55], [0xFF, 0x55, 0xFF], [0xFF, 0xFF, 0x55], [0xFF, 0xFF, 0xFF],
];

// Returns { desc, loops: [{ mirrorOf } or { cels: [{ width, height, trans,
// pixels }] }] }. pixels is one color index a pixel, transparent pixels
// holding the transparent color.
export function parseView(text, file = 'view') {
  const lines = text.split('\n');
  const view = { desc: '', loops: [] };
  let loop = null;
  const fail = (i, msg) => { throw new SourceError(file, i + 1, msg); };

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].replace(/#.*/, '').trim();
    if (!line) continue;
    let m;
    if ((m = /^desc\s+"(.*)"$/.exec(lines[i].trim().replace(/\s+#[^"]*$/, '')))) {
      view.desc = m[1].replace(/\\n/g, '\n');
      asciiBytes(view.desc, file, i + 1);
    } else if ((m = /^loop\s+(\d+)(?:\s+mirror\s+(\d+))?$/.exec(line))) {
      const n = Number(m[1]);
      if (n !== view.loops.length) fail(i, `expected loop ${view.loops.length}, got loop ${n}`);
      if (loop && loop.cels && !loop.cels.length) fail(i, `loop ${n - 1} has no cels`);
      if (m[2] === undefined) {
        loop = { cels: [] };
      } else {
        const of = Number(m[2]);
        const src = view.loops[of];
        if (!src) fail(i, `loop ${n} mirrors loop ${of}, which is not defined above it`);
        if (src.mirrorOf !== undefined) fail(i, `loop ${of} is itself a mirror`);
        if (src.mirrored) fail(i, `loop ${of} already has a mirror (a cel can name only one)`);
        // The cel header has 3 bits for the loop the data belongs to.
        if (of > 7) fail(i, 'only loops 0 to 7 can be mirrored');
        src.mirrored = true;
        loop = { mirrorOf: of };
      }
      view.loops.push(loop);
    } else if ((m = /^cel\s+(\d+)\s+(\d+)(?:\s+trans=([0-9a-fA-F]))?$/.exec(line))) {
      if (!loop) fail(i, 'cel before any loop');
      if (!loop.cels) fail(i, 'a mirror loop has no cels of its own');
      const width = Number(m[1]);
      const height = Number(m[2]);
      const trans = m[3] === undefined ? 0 : parseInt(m[3], 16);
      if (width < 1 || width > 160) fail(i, 'cel width must be 1 to 160');
      if (height < 1 || height > 168) fail(i, 'cel height must be 1 to 168');
      const pixels = new Uint8Array(width * height);
      for (let y = 0; y < height; y++) {
        i++;
        const row = (lines[i] ?? '').trim();
        if (row.length !== width || !/^[0-9a-fA-F.]*$/.test(row)) {
          fail(Math.min(i, lines.length - 1),
            `cel row ${y} must be exactly ${width} characters of 0-9, A-F or "."`);
        }
        for (let x = 0; x < width; x++) {
          const c = row[x] === '.' ? trans : parseInt(row[x], 16);
          // A digit equal to the transparent color would silently vanish.
          if (row[x] !== '.' && c === trans) {
            fail(i, `color ${row[x]} is this cel's transparent color: use "." or another trans=`);
          }
          pixels[y * width + x] = c;
        }
      }
      if (loop.cels.length === 255) fail(i, 'more than 255 cels in a loop');
      loop.cels.push({ width, height, trans, pixels });
    } else {
      fail(i, `not a view line: ${line}`);
    }
  }
  if (!view.loops.length) throw new SourceError(file, 1, 'a view needs at least one loop');
  if (loop.cels && !loop.cels.length) {
    throw new SourceError(file, lines.length, `loop ${view.loops.length - 1} has no cels`);
  }
  return view;
}

function encodeCel(cel, mirrorBits) {
  const out = [cel.width, cel.height, (mirrorBits << 4) | cel.trans];
  for (let y = 0; y < cel.height; y++) {
    let end = cel.width;
    while (end > 0 && cel.pixels[y * cel.width + end - 1] === cel.trans) end--;
    let x = 0;
    while (x < end) {
      const c = cel.pixels[y * cel.width + x];
      let run = 1;
      while (x + run < end && run < 15 && cel.pixels[y * cel.width + x + run] === c) run++;
      out.push((c << 4) | run);
      x += run;
    }
    out.push(0);
  }
  return out;
}

export function encodeView(view) {
  const count = view.loops.length;
  const out = new Bytes();
  out.byte(1).byte(1).byte(count).word(0);
  for (let i = 0; i < count; i++) out.word(0);
  const at = [];
  view.loops.forEach((loop, n) => {
    if (loop.mirrorOf !== undefined) return;
    at[n] = out.length;
    const mirrored = view.loops.some((l) => l.mirrorOf === n);
    const cels = loop.cels.map((c) => encodeCel(c, mirrored ? 8 | n : 0));
    let offset = 1 + cels.length * 2;
    out.byte(cels.length);
    for (const c of cels) { out.word(offset); offset += c.length; }
    for (const c of cels) out.put(c);
  });
  view.loops.forEach((loop, n) => {
    out.setWord(5 + n * 2, at[loop.mirrorOf ?? n]);
  });
  if (view.desc) {
    out.setWord(3, out.length);
    out.put(asciiBytes(view.desc, 'view', 0)).byte(0);
  }
  if (out.length > 65535) throw new Error('view is larger than 64 KB');
  return out.done();
}

// The same shape parseView returns. A mirror loop comes back with both
// `mirrorOf` and its own (flipped) `cels`, ready to draw.
export function decodeView(bytes) {
  const count = bytes[2];
  const descAt = word(bytes, 3);
  let desc = '';
  if (descAt) {
    let end = descAt;
    while (end < bytes.length && bytes[end] !== 0) end++;
    desc = bytesToText(bytes, descAt, end);
  }
  const offsets = [];
  const loops = [];
  for (let n = 0; n < count; n++) {
    const loopAt = word(bytes, 5 + n * 2);
    const loop = { cels: [] };
    const first = offsets.indexOf(loopAt);
    if (first >= 0) loop.mirrorOf = first;
    offsets.push(loopAt);
    for (let c = 0; c < bytes[loopAt]; c++) {
      let p = loopAt + word(bytes, loopAt + 1 + c * 2);
      const width = bytes[p];
      const height = bytes[p + 1];
      const info = bytes[p + 2];
      const trans = info & 0x0F;
      const flip = (info & 0x80) !== 0 && ((info >> 4) & 7) !== n;
      p += 3;
      const pixels = new Uint8Array(width * height).fill(trans);
      for (let y = 0; y < height; y++) {
        let x = 0;
        for (let b = bytes[p++]; b !== 0; b = bytes[p++]) {
          for (let run = b & 0x0F; run > 0 && x < width; run--, x++) {
            pixels[y * width + (flip ? width - 1 - x : x)] = b >> 4;
          }
        }
      }
      loop.cels.push({ width, height, trans, pixels });
    }
    loops.push(loop);
  }
  return { desc, loops };
}

// Back to source text, for tests and for reading a built view.
export function viewToText(view) {
  const lines = [];
  if (view.desc) lines.push(`desc "${view.desc.replace(/\n/g, '\\n')}"`);
  view.loops.forEach((loop, n) => {
    if (loop.mirrorOf !== undefined) { lines.push(`loop ${n} mirror ${loop.mirrorOf}`); return; }
    lines.push(`loop ${n}`);
    for (const cel of loop.cels) {
      lines.push(`cel ${cel.width} ${cel.height} trans=${cel.trans.toString(16).toUpperCase()}`);
      for (let y = 0; y < cel.height; y++) {
        let row = '';
        for (let x = 0; x < cel.width; x++) {
          const c = cel.pixels[y * cel.width + x];
          row += c === cel.trans ? '.' : c.toString(16).toUpperCase();
        }
        lines.push(row);
      }
    }
  });
  return lines.join('\n') + '\n';
}

// One row a loop (mirrors drawn flipped), cels left to right. Each cel
// pixel is 4x2 so the sheet keeps AGI's double-wide pixels. Transparent
// pixels show as a grey checker.
export function contactSheet(view) {
  const SX = 4, SY = 2, GAP = 4;
  const loops = view.loops.map((loop) => {
    if (loop.cels) return loop.cels;
    return view.loops[loop.mirrorOf].cels.map((c) => {
      const pixels = new Uint8Array(c.pixels.length);
      for (let y = 0; y < c.height; y++) {
        for (let x = 0; x < c.width; x++) pixels[y * c.width + x] = c.pixels[y * c.width + c.width - 1 - x];
      }
      return { ...c, pixels };
    });
  });
  let width = GAP;
  let height = GAP;
  for (const cels of loops) {
    width = Math.max(width, cels.reduce((w, c) => w + c.width * SX + GAP, GAP));
    height += Math.max(...cels.map((c) => c.height)) * SY + GAP;
  }
  const pixels = new Uint8Array(width * height);
  let top = GAP;
  for (const cels of loops) {
    let left = GAP;
    for (const c of cels) {
      for (let y = 0; y < c.height * SY; y++) {
        for (let x = 0; x < c.width * SX; x++) {
          const cx = Math.floor(x / SX);
          const cy = Math.floor(y / SY);
          let color = c.pixels[cy * c.width + cx];
          if (color === c.trans) color = (cx + cy) & 1 ? 7 : 8;
          pixels[(top + y) * width + left + x] = color;
        }
      }
      left += c.width * SX + GAP;
    }
    top += Math.max(...cels.map((c) => c.height)) * SY + GAP;
  }
  return { width, height, pixels };
}

if (isMain(import.meta)) {
  runMain('view.js in.view out.bin [--png sheet.png]', 2, async (args) => {
    const { rest: [src, dst], opts } = options(args);
    const view = parseView(readFileSync(src, 'utf8'), src);
    const bytes = encodeView(view);
    writeFileSync(dst, bytes);
    console.log(`${dst}: ${view.loops.length} loops, ${bytes.length} bytes`);
    if (opts.png) {
      const { writePng } = await import('./png.js');
      const sheet = contactSheet(view);
      writePng(opts.png, sheet.width, sheet.height, sheet.pixels, PALETTE);
      console.log(`${opts.png}: ${sheet.width}x${sheet.height}`);
    }
  });
}
