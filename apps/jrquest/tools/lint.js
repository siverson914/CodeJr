#!/usr/bin/env node
// Picture linter: finds the mistakes that are easy to make in a .pic and
// hard to see in a preview. A fill that leaked over the whole picture,
// a fill that did nothing, white left behind, a trigger line the hero
// can step over, points off the picture.
//
//   node tools/lint.js game/pic/*.pic [--pri-fill 45] [--vis-fill 60] [--white 3]
//
// Exits 1 if any file has an error. Warnings do not change the exit code.

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { drawPicture, PIC_W, PIC_H } from '../js/pic.js';
import { compilePicMap } from './pic.js';

const AREA = PIC_W * PIC_H;
const TRIGGER = 2;        // the priority that sets "ego on a trigger line"
const MAX_LISTED = 12;    // of one kind, so one bad picture does not scroll away

export const DEFAULTS = {
  priFill: 45,   // percent of the picture one priority fill may change
  visFill: 60,   // the same for the visual layer
  white: 3,      // white regions of more pixels than this are reported
  thinRun: 3,    // one-row trigger runs at least this wide are reported
};

const pct = (n) => (Math.round((n * 1000) / AREA) / 10).toFixed(1);

// Walk the opcodes the way js/pic.js does and list what the linter needs:
// where each op ends, each fill with the pens at that moment, and every
// point that is off the picture.
export function walk(bytes) {
  const steps = [], bad = [];
  let visOn = false, priOn = false, vis = 0, pri = 0, splat = false;
  let p = 0;
  const arg = () => p < bytes.length && bytes[p] < 0xf0;
  const check = (at, x, y) => {
    if (x < 0 || x >= PIC_W || y < 0 || y >= PIC_H) bad.push({ at, x, y });
  };
  while (p < bytes.length) {
    const op = bytes[p++];
    if (op === 0xff) break;
    switch (op) {
      case 0xf0: vis = bytes[p++] & 15; visOn = true; break;
      case 0xf1: visOn = false; break;
      case 0xf2: pri = bytes[p++] & 15; priOn = true; break;
      case 0xf3: priOn = false; break;
      case 0xf4: case 0xf5: {
        if (!arg()) break;
        const at = p;
        let x = bytes[p++], y = bytes[p++];
        check(at, x, y);
        let vertical = op === 0xf4;
        while (arg()) {
          const here = p, v = bytes[p++];
          if (vertical) y = v; else x = v;
          check(here, x, y);
          vertical = !vertical;
        }
        break;
      }
      case 0xf6:
        while (p + 1 < bytes.length && arg()) { check(p, bytes[p], bytes[p + 1]); p += 2; }
        break;
      case 0xf7: {
        if (!arg()) break;
        let x = bytes[p], y = bytes[p + 1];
        check(p, x, y);
        p += 2;
        while (arg()) {
          const here = p, d = bytes[p++];
          x += (d & 0x80 ? -1 : 1) * ((d >> 4) & 7);
          y += (d & 0x08 ? -1 : 1) * (d & 7);
          check(here, x, y);
        }
        break;
      }
      case 0xf8:
        while (p + 1 < bytes.length && arg()) {
          const x = bytes[p], y = bytes[p + 1];
          check(p, x, y);
          steps.push({ fill: true, at: p, end: p + 2, x, y, visOn, priOn, vis, pri });
          p += 2;
        }
        break;
      case 0xf9: splat = !!(bytes[p++] & 0x20); break;
      case 0xfa:
        while (arg()) {
          if (splat) p++;
          check(p, bytes[p], bytes[p + 1]);
          p += 2;
        }
        break;
      // Arcs may hang off the picture on purpose (a hill, a sun): not checked.
      case 0xfb: while (p + 4 < bytes.length && arg()) p += 5; break;
      case 0xfc: case 0xfd: p++; break;
      default: break;
    }
    steps.push({ fill: false, at: p - 1, end: Math.min(p, bytes.length) });
  }
  return { steps, bad };
}

// Why a fill at this pixel cannot start, in the rules' own words.
function whyNothing(s, before) {
  if (s.x >= PIC_W || s.y >= PIC_H) return 'the start point is off the picture';
  const b = before[s.y * PIC_W + s.x];
  if (s.visOn) {
    if (s.vis === 15) return 'the pen is white, and a fill replaces white';
    return `the start pixel is color ${b & 15}, not white`;
  }
  if (s.priOn) {
    if (s.pri === 4) return 'the priority pen is 4, and a priority fill replaces 4';
    return `the start pixel is priority ${b >> 4}, not 4`;
  }
  return 'both pens are off';
}

// 4-connected regions of pixels that pass the test, as bounding boxes.
function regions(buf, test) {
  const seen = new Uint8Array(AREA), out = [];
  for (let start = 0; start < AREA; start++) {
    if (seen[start] || !test(buf[start])) continue;
    const r = { size: 0, x1: PIC_W, y1: PIC_H, x2: 0, y2: 0 };
    const stack = [start];
    seen[start] = 1;
    while (stack.length) {
      const i = stack.pop(), x = i % PIC_W, y = (i / PIC_W) | 0;
      r.size++;
      if (x < r.x1) r.x1 = x;
      if (x > r.x2) r.x2 = x;
      if (y < r.y1) r.y1 = y;
      if (y > r.y2) r.y2 = y;
      const next = [];
      if (x > 0) next.push(i - 1);
      if (x < PIC_W - 1) next.push(i + 1);
      if (y > 0) next.push(i - PIC_W);
      if (y < PIC_H - 1) next.push(i + PIC_W);
      for (const j of next) if (!seen[j] && test(buf[j])) { seen[j] = 1; stack.push(j); }
    }
    out.push(r);
  }
  return out;
}

// Lint compiled bytes. `lines` is compilePicMap's map from byte offset to
// source line; without it the issues carry line 0.
export function lintPic(bytes, lines = null, options = {}) {
  const opt = { ...DEFAULTS, ...options };
  const lineOf = (at) => (lines && lines[at]) || 0;
  const issues = [];
  const add = (level, code, line, msg) => issues.push({ level, code, line, msg });

  const { steps, bad } = walk(bytes);
  for (const b of bad) {
    add('error', 'range', lineOf(b.at), `point ${b.x},${b.y} is off the picture (x 0 to ${PIC_W - 1}, y 0 to ${PIC_H - 1})`);
  }

  // Draw the picture again up to the end of each step and compare with
  // the step before: slow, but it asks the real rasterizer, not a copy.
  let prev = new Uint8Array(AREA).fill(0x4f);
  const priOwner = new Int32Array(AREA);
  for (const s of steps) {
    const cur = drawPicture(bytes.subarray(0, s.end), new Uint8Array(AREA));
    let dv = 0, dp = 0;
    for (let i = 0; i < AREA; i++) {
      const d = cur[i] ^ prev[i];
      if (d & 0x0f) dv++;
      if (d & 0xf0) { dp++; priOwner[i] = lineOf(s.at); }
    }
    if (s.fill) {
      const line = lineOf(s.at), where = `fill at ${s.x},${s.y}`;
      if (dv === 0 && dp === 0) {
        add('warning', 'fill-nothing', line, `${where} does nothing: ${whyNothing(s, prev)}`);
      }
      if (dp * 100 > opt.priFill * AREA) {
        add('error', 'fill-big', line, `${where} changes ${pct(dp)}% of the priority layer (limit ${opt.priFill}%): a leak?`);
      }
      if (dv * 100 > opt.visFill * AREA) {
        add('error', 'fill-big', line, `${where} changes ${pct(dv)}% of the visual layer (limit ${opt.visFill}%): a leak?`);
      }
    }
    prev = cur;
  }
  const final = prev;

  // White left behind. Small specks are often meant (stars, glints).
  const white = regions(final, (b) => (b & 15) === 15).filter((r) => r.size > opt.white)
    .sort((a, b) => b.size - a.size);
  white.slice(0, MAX_LISTED).forEach((r) => {
    add('warning', 'white', 0, `white left on the visual layer: ${r.size} pixels in ${r.x1},${r.y1} to ${r.x2},${r.y2}`);
  });
  if (white.length > MAX_LISTED) {
    add('warning', 'white', 0, `and ${white.length - MAX_LISTED} more white regions`);
  }

  // A trigger pixel with no trigger above or below it is one row thick.
  // The hero moves 2 rows a step, so a run of those can be stepped over.
  const priAt = (x, y) => final[y * PIC_W + x] >> 4;
  const thin = [];
  for (let y = 0; y < PIC_H; y++) {
    for (let x = 0; x < PIC_W; x++) {
      const start = x;
      while (x < PIC_W && priAt(x, y) === TRIGGER
        && (y === 0 || priAt(x, y - 1) !== TRIGGER)
        && (y === PIC_H - 1 || priAt(x, y + 1) !== TRIGGER)) x++;
      if (x - start >= opt.thinRun) thin.push({ y, x1: start, x2: x - 1 });
    }
  }
  thin.slice(0, MAX_LISTED).forEach((t) => {
    add('warning', 'thin-trigger', priOwner[t.y * PIC_W + t.x1],
      `trigger line (priority ${TRIGGER}) is one row thick at y ${t.y}, x ${t.x1} to ${t.x2}: the hero moves 2 rows a step and can step over it`);
  });
  if (thin.length > MAX_LISTED) {
    add('warning', 'thin-trigger', 0, `and ${thin.length - MAX_LISTED} more one-row trigger runs`);
  }
  return issues;
}

// Range checks the compiler does not make, on the source text. A number
// of 240 or more in these commands would become an opcode byte.
export function lintText(text) {
  const issues = [];
  text.split('\n').forEach((raw, n) => {
    const [cmd, ...rest] = raw.replace(/#.*/, '').trim().split(/\s+/);
    const a = rest.map(Number);
    const bad = (msg) => issues.push({ level: 'error', code: 'range', line: n + 1, msg });
    if (cmd === 'ycorner' || cmd === 'xcorner') {
      a.forEach((v, i) => {
        // After the first point the values alternate, starting with y for ycorner.
        const isX = i < 2 ? i === 0 : (i % 2 === 0) === (cmd === 'xcorner');
        const max = isX ? PIC_W - 1 : PIC_H - 1;
        if (!Number.isInteger(v) || v < 0 || v > max) bad(`${cmd}: ${isX ? 'x' : 'y'} ${rest[i]} is off the picture (0 to ${max})`);
      });
    } else if (cmd === 'circle') {
      const [cx, cy, r, mask = 255] = a;
      if (!(cx >= 0 && cx <= 239) || !(cy >= 0 && cy <= 239)) bad('circle: center must be 0 to 239');
      if (!(r >= 0 && r <= 119)) bad('circle: radius must be 0 to 119');
      if (!(mask >= 0 && mask <= 255)) bad('circle: mask must be 0 to 255');
    }
  });
  return issues;
}

// Compile and lint one picture's source. A compile error is one issue.
export function lintSource(text, name = 'pic', options = {}) {
  let map;
  try {
    map = compilePicMap(text, name);
  } catch (e) {
    const m = /:(\d+): (.*)$/s.exec(e.message);
    return [{ level: 'error', code: 'compile', line: m ? Number(m[1]) : 0, msg: m ? m[2] : e.message }];
  }
  const seen = new Set(), out = [];
  for (const it of [...lintText(text), ...lintPic(map.bytes, map.lines, options)]) {
    // The text check and the byte check can both see the same bad point.
    const key = it.code === 'range' ? `range:${it.line}` : null;
    if (key && seen.has(key)) continue;
    if (key) seen.add(key);
    out.push(it);
  }
  return out;
}

export function formatIssue(file, it) {
  return `${file}${it.line ? `:${it.line}` : ''}: ${it.level}: ${it.msg}`;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const args = process.argv.slice(2);
  const options = {};
  const files = [];
  const FLAGS = { '--pri-fill': 'priFill', '--vis-fill': 'visFill', '--white': 'white' };
  for (let i = 0; i < args.length; i++) {
    if (args[i] in FLAGS) options[FLAGS[args[i]]] = Number(args[++i]);
    else files.push(args[i]);
  }
  if (!files.length || Object.values(options).some((v) => !(v >= 0))) {
    console.error('usage: node tools/lint.js file.pic [more.pic ...] [--pri-fill 45] [--vis-fill 60] [--white 3]');
    process.exit(2);
  }
  let errors = 0, warnings = 0;
  for (const file of files) {
    let issues;
    try {
      issues = lintSource(readFileSync(file, 'utf8'), file, options);
    } catch (e) {
      issues = [{ level: 'error', code: 'read', line: 0, msg: e.message }];
    }
    for (const it of issues) {
      console.log(formatIssue(file, it));
      if (it.level === 'error') errors++; else warnings++;
    }
  }
  console.log(`${files.length} picture${files.length === 1 ? '' : 's'}: ${errors} error${errors === 1 ? '' : 's'}, ${warnings} warning${warnings === 1 ? '' : 's'}`);
  process.exit(errors ? 1 : 0);
}
