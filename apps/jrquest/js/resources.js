// Resource loading and decoding: directories, VOL files, LOGIC, VIEW,
// WORDS.TOK, OBJECT, SOUND. See FORMATS.md. No DOM, no Node.

import { TESTS, actionInfo } from './commands.js';
import { bytesToString } from './util.js';

const KEY = 'Avis Durgan';

function word(b, at) { return b[at] | (b[at + 1] << 8); }

// Raw bytes of resource n from a directory file, or null.
export function loadResource(dir, vols, n) {
  if (!dir || n * 3 + 2 >= dir.length) return null;
  const a = dir[n * 3], b = dir[n * 3 + 1], c = dir[n * 3 + 2];
  if (a === 0xff && b === 0xff && c === 0xff) return null;
  const vol = vols[a >> 4];
  const at = ((a & 15) << 16) | (b << 8) | c;
  if (!vol || at + 5 > vol.length) return null;
  if (vol[at] !== 0x12 || vol[at + 1] !== 0x34) return null;
  const len = word(vol, at + 3);
  return vol.subarray(at + 5, at + 5 + len);
}

export function decodeLogic(bytes) {
  const msgAt = 2 + word(bytes, 0);
  const code = bytes.subarray(2, msgAt);
  const messages = [null];
  if (msgAt < bytes.length) {
    const count = bytes[msgAt];
    const base = msgAt + 1;
    const textStart = base + 2 + count * 2;
    // Only the texts are encrypted, and the key runs across all of them.
    const plain = bytes.slice(textStart);
    for (let i = 0; i < plain.length; i++) plain[i] ^= KEY.charCodeAt(i % KEY.length);
    for (let m = 1; m <= count; m++) {
      const off = word(bytes, base + m * 2);
      if (!off) { messages.push(null); continue; }
      let s = base + off - textStart, e = s;
      while (e < plain.length && plain[e] !== 0) e++;
      messages.push(bytesToString(plain, s, e));
    }
  }
  return { code, messages, said: null };
}

// Every distinct said() pattern in a logic, for the AI parser's list.
export function saidPatterns(code) {
  const out = [], seen = new Set();
  let ip = 0, inTest = false;
  while (ip < code.length) {
    const op = code[ip++];
    if (inTest) {
      if (op === 0xff) { inTest = false; ip += 2; continue; }
      if (op === 0xfc || op === 0xfd) continue;
      if (op === 14) {
        const n = code[ip++], groups = [];
        for (let i = 0; i < n; i++, ip += 2) groups.push(word(code, ip));
        const k = groups.join(',');
        if (!seen.has(k)) { seen.add(k); out.push(groups); }
        continue;
      }
      ip += TESTS[op] ? TESTS[op][1] : 0;
    } else {
      if (op === 0xff) { inTest = true; continue; }
      if (op === 0xfe) { ip += 2; continue; }
      const info = actionInfo(op);
      if (!info) break;
      ip += info[1];
    }
  }
  return out;
}

// A view as loops of cels, each cel a width*height array of colors with
// 255 for clear. Mirrored loops are flipped here, once.
export function decodeView(bytes) {
  const nLoops = bytes[2];
  const descAt = word(bytes, 3);
  let desc = '';
  if (descAt) {
    let e = descAt;
    while (e < bytes.length && bytes[e] !== 0) e++;
    desc = bytesToString(bytes, descAt, e);
  }
  const loops = [];
  for (let l = 0; l < nLoops; l++) {
    const loopAt = word(bytes, 5 + l * 2);
    const nCels = bytes[loopAt];
    const cels = [];
    for (let c = 0; c < nCels; c++) {
      let p = loopAt + word(bytes, loopAt + 1 + c * 2);
      const w = bytes[p], h = bytes[p + 1], tm = bytes[p + 2];
      p += 3;
      const trans = tm & 15;
      const flip = (tm & 0x80) !== 0 && ((tm >> 4) & 7) !== l;
      const pix = new Uint8Array(w * h).fill(255);
      for (let y = 0; y < h; y++) {
        let x = 0;
        for (;;) {
          const b = bytes[p++];
          if (!b || b === undefined) break;
          const color = b >> 4;
          for (let n = b & 15; n > 0 && x < w; n--, x++) {
            if (color !== trans) pix[y * w + (flip ? w - 1 - x : x)] = color;
          }
        }
      }
      cels.push({ w, h, pix });
    }
    loops.push(cels);
  }
  return { loops, desc };
}

// WORDS.TOK as a plain word -> group table.
export function decodeWords(bytes) {
  const dict = Object.create(null);
  let p = 52, prev = '', maxParts = 1;
  // The last byte is the end mark, so stop by position.
  while (p < bytes.length - 1) {
    const keep = bytes[p++];
    let w = prev.slice(0, keep);
    for (;;) {
      const b = bytes[p++];
      if (b === undefined) return { dict, maxParts };
      w += String.fromCharCode((b ^ 0x7f) & 0x7f);
      if (b & 0x80) break;
    }
    if (p + 1 >= bytes.length) break;
    dict[w] = (bytes[p] << 8) | bytes[p + 1];
    p += 2;
    prev = w;
    const parts = w.split(' ').length;
    if (parts > maxParts) maxParts = parts;
  }
  return { dict, maxParts };
}

// Group number -> the alphabetically first word in it, for readable patterns.
export function groupNames(dict) {
  const names = Object.create(null);
  for (const w of Object.keys(dict).sort()) {
    if (!(dict[w] in names)) names[dict[w]] = w;
  }
  names[1] = 'anyword';
  names[9999] = 'rol';
  return names;
}

export function decodeObject(bytes) {
  const b = bytes.slice();
  for (let i = 0; i < b.length; i++) b[i] ^= KEY.charCodeAt(i % KEY.length);
  const namesAt = word(b, 0);
  const items = [];
  for (let p = 3; p + 2 < 3 + namesAt; p += 3) {
    const s = 3 + word(b, p);
    let e = s;
    while (e < b.length && b[e] !== 0) e++;
    items.push({ name: bytesToString(b, s, e), room: b[p + 2] });
  }
  return { max: b[2], items };
}

// A sound as one list of note events sorted by start time:
// { t, voice, dur, div, att } with t and dur in 1/60 s, div the chip's
// 10-bit tone divisor (voice 3: the 3-bit noise control), att 0 loud to
// 15 silent.
export function decodeSound(bytes) {
  const events = [];
  let duration = 0;
  for (let v = 0; v < 4; v++) {
    let p = word(bytes, v * 2), t = 0;
    while (p + 1 < bytes.length) {
      const dur = word(bytes, p);
      if (dur === 0xffff) break;
      const div = v < 3 ? ((bytes[p + 2] & 0x3f) << 4) | (bytes[p + 3] & 15) : bytes[p + 3] & 7;
      events.push({ t, voice: v, dur, div, att: bytes[p + 4] & 15 });
      t += dur;
      p += 5;
    }
    if (t > duration) duration = t;
  }
  events.sort((a, b) => a.t - b.t || a.voice - b.voice);
  return { events, duration };
}
