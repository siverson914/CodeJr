#!/usr/bin/env node
// Picture compiler: .pic text to an AGI PICTURE resource, plus PNG previews.
// node tools/pic.js in.pic out.bin [--png out.png] [--pri pri.png] [--lint]
import { readFileSync, writeFileSync } from 'node:fs';
import { drawPicture, PIC_W, PIC_H } from '../js/pic.js';
import { writePng } from './png.js';

// Pixels are 2 wide and about 1.2 tall on a 4:3 screen, so a shape that
// should look round needs rx = 0.6 * ry.
const roundRx = (r) => Math.max(1, Math.round(r * 0.6));

export function compilePic(text, name = 'pic') {
  return compilePicMap(text, name).bytes;
}

// The same bytes plus, for the linter, the .pic line each byte came from
// (lines[i] is 1-based; 0 for the closing FF).
export function compilePicMap(text, name = 'pic') {
  const out = [];
  const from = [];
  const lines = text.split('\n');
  const fail = (n, msg) => { throw new Error(`${name}:${n + 1}: ${msg}`); };
  let visOn = false;

  lines.forEach((raw, n) => {
    const src = raw.replace(/#.*/, '').trim();
    if (!src) return;
    const [cmd, ...rest] = src.split(/\s+/);
    const nums = () => rest.map((t) => {
      const v = Number(t);
      if (!Number.isInteger(v)) fail(n, `"${t}" is not a whole number`);
      return v;
    });
    const xy = (a) => {
      if (a.length % 2) fail(n, 'needs x y pairs');
      for (let i = 0; i < a.length; i += 2) {
        if (a[i] < 0 || a[i] >= PIC_W) fail(n, `x ${a[i]} is off the picture (0 to 159)`);
        if (a[i + 1] < 0 || a[i + 1] >= PIC_H) fail(n, `y ${a[i + 1]} is off the picture (0 to 167)`);
      }
      return a;
    };
    const color = (t) => {
      const v = Number(t);
      if (!Number.isInteger(v) || v < 0 || v > 15) fail(n, `color "${t}" must be 0 to 15`);
      return v;
    };
    switch (cmd) {
      case 'color':
        if (rest[0] === 'off') { out.push(0xf1); visOn = false; }
        else { out.push(0xf0, color(rest[0])); visOn = true; }
        break;
      case 'pri':
        if (rest[0] === 'off') out.push(0xf3);
        else out.push(0xf2, color(rest[0]));
        break;
      case 'line': { const a = xy(nums()); if (a.length < 2) fail(n, 'line needs points'); out.push(0xf6, ...a); break; }
      case 'poly': {
        const a = xy(nums());
        if (a.length < 6) fail(n, 'poly needs three points');
        out.push(0xf6, ...a, a[0], a[1]);
        break;
      }
      case 'rect': {
        const [x1, y1, x2, y2] = xy(nums());
        out.push(0xf6, x1, y1, x2, y1, x2, y2, x1, y2, x1, y1);
        break;
      }
      case 'box': {
        const [x1, y1, x2, y2] = xy(nums());
        out.push(0xf6, x1, y1, x2, y1, x2, y2, x1, y2, x1, y1);
        if (x2 - x1 > 1 && y2 - y1 > 1) out.push(0xf8, (x1 + x2) >> 1, (y1 + y2) >> 1);
        break;
      }
      case 'rel': {
        const a = nums();
        xy(a.slice(0, 2));
        out.push(0xf7, a[0], a[1]);
        for (let i = 2; i < a.length; i += 2) {
          const dx = a[i], dy = a[i + 1];
          if (Math.abs(dx) > 7 || Math.abs(dy) > 7 || dy === undefined) fail(n, 'rel steps are -7 to 7, in pairs');
          out.push((dx < 0 ? 0x80 : 0) | (Math.abs(dx) << 4) | (dy < 0 ? 0x08 : 0) | Math.abs(dy));
        }
        break;
      }
      case 'ycorner': out.push(0xf4, ...nums()); break;
      case 'xcorner': out.push(0xf5, ...nums()); break;
      case 'fill': out.push(0xf8, ...xy(nums())); break;
      case 'pen': {
        const size = Number(rest[1]);
        if (!(size >= 0 && size <= 7)) fail(n, 'pen size is 0 to 7');
        out.push(0xf9, (rest[2] === 'splatter' ? 0x20 : 0) | (rest[0] === 'rect' ? 0x10 : 0) | size);
        break;
      }
      case 'plot': out.push(0xfa, ...xy(nums())); break;
      case 'arc': {
        const a = nums();
        if (a.length < 4) fail(n, 'arc needs cx cy rx ry [mask]');
        const mask = a[4] === undefined ? 255 : a[4];
        if (a[2] > 119 || a[3] > 119 || a[2] < 0 || a[3] < 0) fail(n, 'arc radius is 0 to 119');
        if (a[0] > 239 || a[1] > 239 || mask > 255) fail(n, 'arc center or mask out of range');
        out.push(0xfb, a[0], a[1], a[2], a[3], mask);
        break;
      }
      case 'circle': {   // sugar: looks round on screen. r is the vertical radius
        const [cx, cy, r, mask = 255] = nums();
        out.push(0xfb, cx, cy, roundRx(r), r, mask);
        break;
      }
      case 'pattern': {
        const pn = Number(rest[0]);
        if (!(pn >= 0 && pn <= 15)) fail(n, 'pattern is 0 to 15');
        out.push(0xfc, pn);
        if (rest[1] !== undefined) {
          const c2 = color(rest[1]);
          if (pn && c2 === 15) fail(n, 'a pattern cannot use white (15): later fills would leak into it');
          out.push(0xfd, c2);
        }
        break;
      }
      default: fail(n, `unknown command "${cmd}"`);
    }
    // Argument bytes of 0xF0 and up would be read as opcodes.
    // (Arc masks are the exception the format has to live with: see below.)
    while (from.length < out.length) from.push(n + 1);
  });
  out.push(0xff);
  from.push(0);
  const done = escapeCheck(out, from);
  return { bytes: Uint8Array.from(done.bytes), lines: done.lines };
}

// Every argument byte must be below 0xF0. Coordinates always are. An arc
// mask of 0xF0 or more is not, so the compiler splits such an arc into two
// arcs whose masks are each below 0xF0.
function escapeCheck(bytes, from) {
  const out = [], lines = [];
  const put = (line, ...b) => { for (const v of b) { out.push(v); lines.push(line); } };
  let i = 0;
  while (i < bytes.length) {
    const op = bytes[i], line = from[i];
    if (op === 0xfb) {
      const [cx, cy, rx, ry, mask] = bytes.slice(i + 1, i + 6);
      if (mask >= 0xf0) put(line, 0xfb, cx, cy, rx, ry, mask & 0x0f, 0xfb, cx, cy, rx, ry, mask & 0xf0 & 0x7f, 0xfb, cx, cy, rx, ry, 0x80);
      else put(line, 0xfb, cx, cy, rx, ry, mask);
      i += 6;
      continue;
    }
    put(line, op);
    i++;
    while (i < bytes.length && bytes[i] < 0xf0) { put(from[i], bytes[i]); i++; }
  }
  return { bytes: out, lines };
}

export function renderPic(bytes) {
  return drawPicture(bytes, new Uint8Array(PIC_W * PIC_H));
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const args = process.argv.slice(2);
  const opt = (k) => { const i = args.indexOf(k); return i < 0 ? null : args[i + 1]; };
  const [inFile, outFile] = args;
  if (!inFile) { console.error('usage: pic.js in.pic [out.bin] [--png out.png] [--pri pri.png] [--lint]'); process.exit(2); }
  try {
    const text = readFileSync(inFile, 'utf8');
    const { bytes, lines } = compilePicMap(text, inFile);
    if (outFile && !outFile.startsWith('--')) writeFileSync(outFile, bytes);
    const buf = renderPic(bytes);
    // Previews at 640x403: 4x wide, 2.4x tall is the real shape; 4 by 2 rows
    // doubled plus a repeat every fifth row keeps it integer.
    const png = opt('--png'), pri = opt('--pri');
    if (png) writePng(png, PIC_W, PIC_H, buf.map((b) => b & 15), undefined, 4, 2);
    if (pri) writePng(pri, PIC_W, PIC_H, buf.map((b) => b >> 4), undefined, 4, 2);
    console.log(`${inFile}: ${bytes.length} bytes`);
    if (args.includes('--lint')) {
      // Loaded late and without await: lint.js imports this file, and a
      // top-level await here would leave each waiting for the other.
      import('./lint.js').then(({ lintPic, lintText, formatIssue }) => {
        const issues = [...lintText(text), ...lintPic(bytes, lines)];
        for (const it of issues) console.log(formatIssue(inFile, it));
        if (issues.some((it) => it.level === 'error')) process.exitCode = 1;
      });
    }
  } catch (e) { console.error(e.message); process.exit(1); }
}
