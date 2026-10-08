// objects.txt -> OBJECT, and back.

import { readFileSync, writeFileSync } from 'node:fs';
import { SourceError, Bytes, asciiBytes, avisDurgan, bytesToText, word, isMain, runMain } from './util.js';

// Returns { max, items: [{ name, room }] }.
export function parseObjects(text, file = 'objects.txt') {
  let max = 16;
  const items = [];
  text.split('\n').forEach((raw, i) => {
    // A comment needs the # at the start of the line or after a space, so
    // a name may still hold one ("key #2").
    const line = raw.replace(/(^|\s)#.*/, '').trim();
    if (!line) return;
    const n = i + 1;
    let m = /^max\s*:\s*(\d+)$/.exec(line);
    if (m) {
      if (items.length) throw new SourceError(file, n, '"max:" must come before the items');
      max = Number(m[1]);
      if (max < 1 || max > 255) throw new SourceError(file, n, 'max must be 1 to 255');
      return;
    }
    m = /^(\d+)\s*:\s*(.+)$/.exec(line);
    if (!m) throw new SourceError(file, n, 'expected "room: name"');
    const room = Number(m[1]);
    if (room > 255) throw new SourceError(file, n, `room ${room} is above 255`);
    if (items.length === 0 && (room !== 0 || m[2] !== '?')) {
      throw new SourceError(file, n, 'the first item must be "0: ?"');
    }
    if (items.length === 256) throw new SourceError(file, n, 'more than 256 items');
    asciiBytes(m[2], file, n);
    items.push({ name: m[2], room });
  });
  if (!items.length) throw new SourceError(file, 1, 'no items: the first must be "0: ?"');
  return { max, items };
}

export function encodeObjects({ max = 16, items }) {
  const out = new Bytes();
  const tableSize = items.length * 3;
  out.word(tableSize).byte(max);
  // Equal names share one copy, as Sierra's files do for "?".
  const names = new Bytes();
  const where = new Map();
  for (const it of items) {
    if (!where.has(it.name)) {
      where.set(it.name, tableSize + names.length);
      names.put(asciiBytes(it.name, 'objects', 0)).byte(0);
    }
    out.word(where.get(it.name)).byte(it.room);
  }
  out.put(names.done());
  return avisDurgan(out.done());
}

export function decodeObjects(bytes) {
  const b = avisDurgan(Uint8Array.from(bytes));
  const count = word(b, 0) / 3;
  const items = [];
  for (let i = 0; i < count; i++) {
    const at = 3 + i * 3;
    const start = 3 + word(b, at);
    let end = start;
    while (end < b.length && b[end] !== 0) end++;
    items.push({ name: bytesToText(b, start, end), room: b[at + 2] });
  }
  return { max: b[2], items };
}

if (isMain(import.meta)) {
  runMain('objects.js game/objects.txt build/OBJECT', 2, ([src, dst]) => {
    const parsed = parseObjects(readFileSync(src, 'utf8'), src);
    const bytes = encodeObjects(parsed);
    writeFileSync(dst, bytes);
    console.log(`${dst}: ${parsed.items.length} items, ${bytes.length} bytes`);
  });
}
