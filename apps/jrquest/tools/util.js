// Small things every tool needs.

import { pathToFileURL } from 'node:url';

// An error in a source file. Tools print it as `file:line: message`.
export class SourceError extends Error {
  constructor(file, line, message) {
    super(`${file}:${line}: ${message}`);
    this.file = file;
    this.line = line;
    this.reason = message;
  }
}

const KEY = new TextEncoder().encode('Avis Durgan');

// XOR bytes[start..end) with the repeating key, in place. The same call
// decrypts.
export function avisDurgan(bytes, start = 0, end = bytes.length) {
  for (let i = start; i < end; i++) bytes[i] ^= KEY[(i - start) % KEY.length];
  return bytes;
}

// Text is stored as one byte a character (the PC's code page 437 for
// anything above ASCII; we only allow ASCII in source).
export function asciiBytes(text, file, line) {
  const out = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i);
    if (c > 126 || (c < 32 && c !== 10)) {
      throw new SourceError(file, line, `character ${JSON.stringify(text[i])} is not plain ASCII`);
    }
    out[i] = c;
  }
  return out;
}

export function bytesToText(bytes, start, end) {
  let s = '';
  for (let i = start; i < end; i++) s += String.fromCharCode(bytes[i]);
  return s;
}

// A growable byte buffer with little-endian words.
export class Bytes {
  constructor() { this.a = []; }
  get length() { return this.a.length; }
  byte(b) { this.a.push(b & 0xFF); return this; }
  word(w) { this.a.push(w & 0xFF, (w >> 8) & 0xFF); return this; }
  put(bytes) { for (const b of bytes) this.a.push(b); return this; }
  setWord(at, w) { this.a[at] = w & 0xFF; this.a[at + 1] = (w >> 8) & 0xFF; }
  done() { return Uint8Array.from(this.a); }
}

export const word = (bytes, at) => bytes[at] | (bytes[at + 1] << 8);

// The resource number at the start of a file name: "012_gate.lgc" is 12.
export function resourceNumber(name) {
  const m = /^(\d{1,3})(?:[_.]|$)/.exec(name);
  if (!m) return null;
  const n = Number(m[1]);
  return n <= 255 ? n : null;
}

export function isMain(meta) {
  return process.argv[1] !== undefined && meta.url === pathToFileURL(process.argv[1]).href;
}

// Run a tool's command line: print source errors plainly, anything else
// with its stack, and exit nonzero either way.
export async function runMain(usage, minArgs, main) {
  const args = process.argv.slice(2);
  if (args.length < minArgs || args.includes('--help')) {
    console.error(`usage: node tools/${usage}`);
    process.exit(2);
  }
  try {
    await main(args);
  } catch (e) {
    if (e instanceof SourceError) console.error(e.message);
    else if (e instanceof AggregateError) for (const x of e.errors) console.error(x.message);
    else console.error(e.code === 'ENOENT' ? e.message : e.stack || String(e));
    process.exit(1);
  }
}

// Pull `--name value` options out of an argument list. Flags in `bare`
// take no value.
export function options(args, bare = []) {
  const rest = [];
  const opts = {};
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (!a.startsWith('--')) { rest.push(a); continue; }
    const name = a.slice(2);
    if (bare.includes(name)) { opts[name] = true; continue; }
    if (i + 1 >= args.length) throw new Error(`--${name} needs a value`);
    opts[name] = args[++i];
  }
  return { rest, opts };
}
