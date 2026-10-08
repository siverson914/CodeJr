// words.txt -> WORDS.TOK, and back.

import { readFileSync, writeFileSync } from 'node:fs';
import { SourceError, isMain, runMain } from './util.js';

// Parse words.txt. Returns { groups: Map(number -> [words in file order]),
// words: Map(word -> number) }.
export function parseWords(text, file = 'words.txt') {
  const groups = new Map();
  const words = new Map();
  text.split('\n').forEach((raw, i) => {
    const line = raw.replace(/#.*/, '').trim();
    if (!line) return;
    const m = /^(\d+)\s*:(.*)$/.exec(line);
    if (!m) throw new SourceError(file, i + 1, 'expected "number: word, word"');
    const group = Number(m[1]);
    if (group > 65535) throw new SourceError(file, i + 1, `group ${group} is above 65535`);
    for (const part of m[2].split(',')) {
      const w = part.trim().replace(/\s+/g, ' ');
      if (!w) continue;
      // The file stores each character XOR 0x7F with bit 7 as the end
      // mark, so only 7-bit characters fit. The parser lowercases input.
      if (!/^[a-z0-9 '.\-]+$/.test(w)) {
        throw new SourceError(file, i + 1, `word "${w}" must be lowercase letters, digits, spaces, ' . or -`);
      }
      if (words.has(w)) {
        throw new SourceError(file, i + 1, `word "${w}" is already in group ${words.get(w)}`);
      }
      words.set(w, group);
      if (!groups.has(group)) groups.set(group, []);
      groups.get(group).push(w);
    }
  });
  return { groups, words };
}

// words: Map(word -> group) or the object parseWords returns.
export function encodeWords(words) {
  if (!(words instanceof Map)) words = words.words;
  const sorted = [...words.keys()].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  const out = new Array(52).fill(0);
  let prev = '';
  for (const w of sorted) {
    const letter = w.charCodeAt(0) - 97;
    if (letter >= 0 && letter < 26 && out[letter * 2] === 0 && out[letter * 2 + 1] === 0) {
      out[letter * 2] = out.length >> 8;      // big-endian
      out[letter * 2 + 1] = out.length & 0xFF;
      prev = '';   // the reader starts each letter with no previous word
    }
    let same = 0;
    while (same < prev.length && same < w.length - 1 && prev[same] === w[same]) same++;
    out.push(same);
    for (let i = same; i < w.length; i++) {
      const c = w.charCodeAt(i) ^ 0x7F;
      out.push(i === w.length - 1 ? c | 0x80 : c);
    }
    const group = words.get(w);
    out.push(group >> 8, group & 0xFF);
    prev = w;
  }
  out.push(0);
  return Uint8Array.from(out);
}

// Returns [{ word, group }] in file order.
export function decodeWords(bytes) {
  const list = [];
  let p = 52;
  let prev = '';
  // The last byte is the end mark; a prefix count of 0 looks the same, so
  // stop by position, as Sierra's reader does.
  while (p < bytes.length - 1) {
    let w = prev.slice(0, bytes[p++]);
    for (;;) {
      const c = bytes[p++];
      w += String.fromCharCode((c & 0x7F) ^ 0x7F);
      if (c & 0x80) break;
      if (p >= bytes.length) throw new Error('WORDS.TOK: word runs off the end');
    }
    list.push({ word: w, group: (bytes[p] << 8) | bytes[p + 1] });
    p += 2;
    prev = w;
  }
  return list;
}

// The 26 letter offsets from the header.
export function wordIndex(bytes) {
  const index = [];
  for (let i = 0; i < 26; i++) index.push((bytes[i * 2] << 8) | bytes[i * 2 + 1]);
  return index;
}

if (isMain(import.meta)) {
  runMain('words.js game/words.txt build/WORDS.TOK', 2, ([src, dst]) => {
    const parsed = parseWords(readFileSync(src, 'utf8'), src);
    const bytes = encodeWords(parsed);
    writeFileSync(dst, bytes);
    console.log(`${dst}: ${parsed.words.size} words in ${parsed.groups.size} groups, ${bytes.length} bytes`);
  });
}
