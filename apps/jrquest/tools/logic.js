// .lgc source -> LOGIC resource, with a decoder and disassembler.

import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, basename } from 'node:path';
import {
  SourceError, Bytes, asciiBytes, avisDurgan, bytesToText, word, resourceNumber,
  isMain, runMain, options,
} from './util.js';
import { TEST_BY_NAME, ACTION_BY_NAME, TEST_BY_OP, ACTION_BY_OP, TYPE_NAMES } from './argtypes.js';
import { parseWords } from './words.js';
import { parseObjects } from './objects.js';

const SAID = TEST_BY_NAME.get('said').op;
const OPS = ['&&', '||', '==', '!=', '<=', '>=', '+=', '-=', '*=', '/=', '++', '--',
  '(', ')', '{', '}', ',', ';', ':', '!', '<', '>', '='];

// ---- Lexer ----

// Tokens: { t: 'id' | 'num' | 'str' | 'op' | 'dir', v, file, line }.
function lex(src, file) {
  const toks = [];
  let line = 1;
  let lineStart = true;   // only blanks so far on this line: a # is a directive
  let i = 0;
  const fail = (msg) => { throw new SourceError(file, line, msg); };
  while (i < src.length) {
    const c = src[i];
    if (c === '\n') { line++; lineStart = true; i++; continue; }
    if (c === ' ' || c === '\t' || c === '\r') { i++; continue; }
    if (src.startsWith('//', i)) { while (i < src.length && src[i] !== '\n') i++; continue; }
    if (src.startsWith('/*', i)) {
      const end = src.indexOf('*/', i + 2);
      if (end < 0) fail('comment is never closed');
      for (let k = i; k < end; k++) if (src[k] === '\n') line++;
      i = end + 2;
      continue;
    }
    if (c === '#') {
      if (!lineStart) fail('# must start its line');
      const m = /^#([a-z]+)/.exec(src.slice(i, i + 16));
      if (!m || !['define', 'include', 'message'].includes(m[1])) fail('expected #define, #include or #message');
      toks.push({ t: 'dir', v: m[1], file, line });
      i += m[0].length;
      lineStart = false;
      continue;
    }
    lineStart = false;
    if (c === '"') {
      let s = '';
      i++;
      for (;;) {
        if (i >= src.length || src[i] === '\n') fail('string is never closed');
        const ch = src[i++];
        if (ch === '"') break;
        if (ch !== '\\') { s += ch; continue; }
        const e = src[i++];
        if (e === 'n') s += '\n';
        else if (e === '"' || e === '\\') s += e;
        else fail(`unknown escape \\${e ?? ''} in a string (use \\", \\n or \\\\)`);
      }
      // "one" "two" is one string, so a long message can be split up.
      if (toks.length && toks.at(-1).t === 'str') toks.at(-1).v += s;
      else toks.push({ t: 'str', v: s, file, line });
      continue;
    }
    let m = /^\d+/.exec(src.slice(i, i + 12));
    if (m) {
      if (/[A-Za-z_]/.test(src[i + m[0].length] ?? '')) fail(`bad number ${m[0]}${src[i + m[0].length]}`);
      toks.push({ t: 'num', v: Number(m[0]), file, line });
      i += m[0].length;
      continue;
    }
    // Command names have dots in them (set.view), so names may too.
    m = /^[A-Za-z_][A-Za-z0-9_.]*/.exec(src.slice(i, i + 80));
    if (m) {
      toks.push({ t: 'id', v: m[0], file, line });
      i += m[0].length;
      continue;
    }
    const op = OPS.find((o) => src.startsWith(o, i));
    if (!op) fail(`unexpected character ${JSON.stringify(c)}`);
    toks.push({ t: 'op', v: op, file, line });
    i += op.length;
  }
  return toks;
}

const TYPED = /^([vfoiswcm])(\d+)$/;

// Lex a file and carry out its directives. Returns the tokens that are
// left (the statements), with included files spliced in place.
function load(src, file, st, depth) {
  const raw = lex(src, file);
  const out = [];
  for (let k = 0; k < raw.length; k++) {
    const tok = raw[k];
    if (tok.t !== 'dir') { out.push(tok); continue; }
    const fail = (msg) => { throw new SourceError(file, tok.line, msg); };
    const args = [];
    while (k + 1 < raw.length && raw[k + 1].line === tok.line && raw[k + 1].t !== 'dir') args.push(raw[++k]);
    if (tok.v === 'define') {
      if (args.length !== 2 || args[0].t !== 'id' || args[1].t === 'op') fail('expected #define name value');
      const name = args[0].v;
      if (TYPED.test(name)) fail(`"${name}" already means ${TYPE_NAMES[name[0]]} ${name.slice(1)}`);
      if (['if', 'else', 'goto'].includes(name)) fail(`"${name}" is a keyword`);
      if (st.defines.has(name)) fail(`"${name}" is already defined`);
      st.defines.set(name, args[1]);
    } else if (tok.v === 'message') {
      if (args.length !== 2 || args[0].t !== 'num' || args[1].t !== 'str') fail('expected #message number "text"');
      const n = args[0].v;
      if (n < 1 || n > 255) fail('message numbers are 1 to 255');
      if (st.messages.has(n)) fail(`message ${n} is already defined`);
      asciiBytes(args[1].v, file, tok.line);
      st.messages.set(n, args[1].v);
    } else {
      if (args.length !== 1 || args[0].t !== 'str') fail('expected #include "file"');
      if (depth >= 16) fail('#include nested too deep (a file including itself?)');
      const path = join(dirname(file), args[0].v);
      let text;
      try { text = st.readFile(path); } catch { fail(`cannot read ${path}`); }
      out.push(...load(text, path, st, depth + 1));
    }
  }
  return out;
}

// ---- Compiler ----

// Returns { bytes, said, messages }. `said` is the list for
// build/said/NNN.json; `messages` is indexed by message number.
// opts: file (for error text and to find includes), words and objects
// (the parsed files, or their text), readFile(path) for includes.
export function compileLogic(source, opts = {}) {
  const file = opts.file || 'logic';
  const words = typeof opts.words === 'string' ? parseWords(opts.words) : opts.words;
  const objects = typeof opts.objects === 'string' ? parseObjects(opts.objects) : opts.objects;
  const st = {
    defines: new Map(),
    messages: new Map(),
    readFile: opts.readFile || ((p) => readFileSync(p, 'utf8')),
  };
  const toks = load(source, file, st, 0);
  const code = new Bytes();
  const labels = new Map();
  const gotos = [];
  const said = [];
  let pos = 0;
  let last = toks[0] || { file, line: 1 };

  const err = (tok, msg) => { throw new SourceError(tok.file, tok.line, msg); };
  const peek = (n = 0) => toks[pos + n];
  const isOp = (tok, v) => tok !== undefined && tok.t === 'op' && tok.v === v;
  const show = (tok) => (tok.t === 'str' ? JSON.stringify(tok.v) : String(tok.v));
  function next() {
    if (pos >= toks.length) err(last, 'the file ends in the middle of a statement');
    return (last = toks[pos++]);
  }
  function expect(v) {
    const tok = peek();
    if (!isOp(tok, v)) err(tok || last, `expected "${v}"${tok ? `, found ${show(tok)}` : ' before the end of the file'}`);
    return next();
  }

  // Follow #define names to what they stand for.
  function resolve(tok) {
    for (let hops = 0; tok.t === 'id' && st.defines.has(tok.v); hops++) {
      if (hops > 32) err(tok, `#define ${tok.v} goes in a circle`);
      tok = { ...st.defines.get(tok.v), file: tok.file, line: tok.line, from: tok.from || tok.v };
    }
    return tok;
  }

  // An argument as { type, value, text }: type is a letter from
  // argtypes.js, 'n' for a plain number, or 'str' for a quoted string.
  function argument() {
    const src = next();
    const tok = resolve(src);
    const text = tok.from ? `${tok.from} (${show(tok)})` : show(tok);
    if (tok.t === 'num') return { type: 'n', value: tok.v, text, tok: src };
    if (tok.t === 'str') return { type: 'str', value: tok.v, text, tok: src };
    const m = tok.t === 'id' && TYPED.exec(tok.v);
    if (!m) {
      if (tok.t === 'id') err(src, `unknown name "${tok.v}" (no #define for it)`);
      err(src, `expected an argument, found ${show(tok)}`);
    }
    return { type: m[1], value: Number(m[2]), text, tok: src };
  }

  function messageNumber(text, tok) {
    for (const [n, t] of st.messages) if (t === text) return n;
    asciiBytes(text, tok.file, tok.line);
    for (let n = 1; n <= 255; n++) {
      if (!st.messages.has(n)) { st.messages.set(n, text); return n; }
    }
    err(tok, 'more than 255 messages in one logic');
  }

  // Check an argument against the type a command wants; returns its byte.
  function coerce(arg, want, what) {
    let value = arg.value;
    if (arg.type === 'str') {
      if (want === 'm') {
        value = messageNumber(arg.value, arg.tok);
      } else if (want === 'i') {
        if (!objects) err(arg.tok, `${what}: an item name needs the objects file`);
        value = objects.items.findIndex((it, n) => n > 0 && it.name === arg.value);
        if (value < 0) err(arg.tok, `${what}: no inventory item named ${arg.text}`);
      } else {
        err(arg.tok, `${what} must be a ${TYPE_NAMES[want]}, got the string ${arg.text}`);
      }
    } else if (arg.type !== want) {
      const got = arg.type === 'n' ? `the number ${arg.text}` : `a ${TYPE_NAMES[arg.type]}, ${arg.text}`;
      const hint = arg.type === 'n' && want !== 'n' ? ` (write ${want}${arg.value})` : '';
      err(arg.tok, `${what} must be a ${TYPE_NAMES[want]}, got ${got}${hint}`);
    } else if (want === 'm' && !st.messages.has(value)) {
      err(arg.tok, `${what}: there is no message ${value}`);
    }
    if (value > 255) err(arg.tok, `${what}: ${value} does not fit in a byte`);
    return value;
  }

  // The arguments of a command call, after its name: "(a, b, c)".
  function callArgs() {
    const args = [];
    expect('(');
    if (!isOp(peek(), ')')) {
      do args.push(argument()); while (isOp(peek(), ',') && next());
    }
    expect(')');
    return args;
  }

  function command(info, nameTok, kind) {
    const args = callArgs();
    if (info.types === null) return saidTest(args, nameTok);
    if (args.length !== info.types.length) {
      err(nameTok, `${info.name} takes ${info.types.length} argument${info.types.length === 1 ? '' : 's'}, got ${args.length}`);
    }
    const bytes = [info.op];
    args.forEach((a, n) => bytes.push(coerce(a, info.types[n], `argument ${n + 1} of ${info.name}`)));
    return bytes;
  }

  function saidTest(args, nameTok) {
    if (!args.length) err(nameTok, 'said needs at least one word');
    if (args.length > 255) err(nameTok, 'said takes at most 255 words');
    const groups = args.map((a) => {
      if (a.type === 'n') {
        if (a.value > 65535) err(a.tok, `word group ${a.value} is above 65535`);
        return a.value;
      }
      if (a.type !== 'str') err(a.tok, `said takes words in quotes or group numbers, got ${a.text}`);
      if (!words) err(a.tok, 'said with a quoted word needs the words file');
      const w = a.value.trim().toLowerCase().replace(/\s+/g, ' ');
      if (!words.words.has(w)) err(a.tok, `"${w}" is not in the words file`);
      return words.words.get(w);
    });
    if (!said.some((p) => p.groups.join() === groups.join())) {
      said.push({
        groups,
        words: groups.map((g) => (words && words.groups.has(g) ? words.groups.get(g)[0] : String(g))),
      });
    }
    const bytes = [SAID, groups.length];
    for (const g of groups) bytes.push(g & 0xFF, g >> 8);
    return bytes;
  }

  // ---- Tests ----
  // Parsed as an ordinary boolean expression, then pushed into the one
  // shape AGI can store: tests ANDed together, where a test may be an OR
  // group of single tests.

  const COMPARE = {
    '==': ['equal', false], '!=': ['equal', true], '<': ['less', false],
    '>': ['greater', false], '<=': ['greater', true], '>=': ['less', true],
  };

  function parseOr() {
    const items = [parseAnd()];
    while (isOp(peek(), '||')) { next(); items.push(parseAnd()); }
    return items.length === 1 ? items[0] : { k: 'or', items };
  }
  function parseAnd() {
    const items = [parseUnary()];
    while (isOp(peek(), '&&')) { next(); items.push(parseUnary()); }
    return items.length === 1 ? items[0] : { k: 'and', items };
  }
  function parseUnary() {
    if (isOp(peek(), '!')) { next(); return { k: 'not', x: parseUnary() }; }
    if (isOp(peek(), '(')) {
      next();
      const x = parseOr();
      expect(')');
      return x;
    }
    return parseTest();
  }
  function parseTest() {
    const tok = peek();
    if (!tok) err(last, 'the file ends in the middle of a test');
    if (tok.t === 'id' && isOp(peek(1), '(')) {
      const info = TEST_BY_NAME.get(tok.v);
      if (!info) {
        err(tok, ACTION_BY_NAME.has(tok.v) ? `${tok.v} is an action, not a test` : `unknown test "${tok.v}"`);
      }
      next();
      return { k: 'test', bytes: command(info, tok, 'test'), neg: false };
    }
    const a = argument();
    const opTok = peek();
    const cmp = opTok && opTok.t === 'op' ? COMPARE[opTok.v] : undefined;
    if (!cmp) {
      if (isOp(opTok, '=')) err(opTok, 'use == to compare');
      if (a.type !== 'f') err(a.tok, `${a.text} alone is not a test: only a flag is (compare it with ==, != and so on)`);
      return { k: 'test', bytes: [TEST_BY_NAME.get('isset').op, coerce(a, 'f', 'the flag')], neg: false };
    }
    next();
    const b = argument();
    const left = coerce(a, 'v', `the left side of ${opTok.v}`);
    if (b.type !== 'n' && b.type !== 'v') {
      err(b.tok, `the right side of ${opTok.v} must be a number or a variable, got ${b.text}`);
    }
    const info = TEST_BY_NAME.get(cmp[0] + b.type);
    const right = coerce(b, b.type, `the right side of ${opTok.v}`);
    return { k: 'test', bytes: [info.op, left, right], neg: cmp[1] };
  }

  // Push NOT down to the single tests (De Morgan) and merge nested groups
  // of the same kind.
  function normal(node, neg) {
    if (node.k === 'test') return { ...node, neg: node.neg !== neg };
    if (node.k === 'not') return normal(node.x, !neg);
    const k = (node.k === 'and') !== neg ? 'and' : 'or';
    const items = [];
    for (const item of node.items) {
      const n = normal(item, neg);
      if (n.k === k) items.push(...n.items); else items.push(n);
    }
    return { k, items };
  }

  function condition(ifTok) {
    expect('(');
    const tree = normal(parseOr(), false);
    expect(')');
    const list = tree.k === 'and' ? tree.items : [tree];
    code.byte(0xFF);
    const one = (t) => { if (t.neg) code.byte(0xFD); code.put(t.bytes); };
    for (const item of list) {
      if (item.k === 'test') { one(item); continue; }
      code.byte(0xFC);
      for (const t of item.items) {
        if (t.k !== 'test') {
          err(ifTok, 'this test has && inside ||, which AGI cannot store: split it into two ifs');
        }
        one(t);
      }
      code.byte(0xFC);
    }
    code.byte(0xFF);
  }

  // ---- Statements ----

  // A forward jump: write a placeholder word, return a function that
  // points it at wherever the code has got to by then.
  function jumpWord(tok) {
    const at = code.length;
    code.word(0);
    return () => {
      const d = code.length - (at + 2);
      if (d > 0x7FFF) err(tok, 'this block is too long to jump over (32 KB)');
      code.setWord(at, d);
    };
  }

  function block() {
    if (!isOp(peek(), '{')) { statement(); return; }
    const open = next();
    while (!isOp(peek(), '}')) {
      if (!peek()) err(open, 'this { is never closed');
      statement();
    }
    next();
  }

  const ASSIGN = {
    '=': 'assign', '+=': 'add', '-=': 'sub', '*=': 'mul.', '/=': 'div.',
  };

  // Returns true if the statement was return().
  function statement() {
    const tok = peek();
    if (tok.t === 'id' && isOp(peek(1), ':') && !TYPED.test(tok.v)) {
      if (labels.has(tok.v)) err(tok, `label "${tok.v}" is already used`);
      labels.set(tok.v, code.length);
      pos += 2;
      return false;
    }
    if (tok.t === 'id' && tok.v === 'if') {
      next();
      condition(tok);
      const skip = jumpWord(tok);
      block();
      if (peek() && peek().t === 'id' && peek().v === 'else') {
        const elseTok = next();
        code.byte(0xFE);
        const over = jumpWord(elseTok);
        skip();
        block();
        over();
      } else {
        skip();
      }
      return false;
    }
    if (tok.t === 'id' && tok.v === 'else') err(tok, 'else without an if before it');
    if (tok.t === 'id' && tok.v === 'goto') {
      next();
      const paren = isOp(peek(), '(');
      if (paren) next();
      const label = next();
      if (label.t !== 'id') err(label, 'goto needs a label name');
      if (paren) expect(')');
      expect(';');
      code.byte(0xFE);
      gotos.push({ at: code.length, label });
      code.word(0);
      return false;
    }
    if (tok.t === 'id' && tok.v === 'return' && isOp(peek(1), ';')) {
      pos += 2;
      code.byte(0);
      return true;
    }
    if (tok.t === 'id' && isOp(peek(1), '(')) {
      const info = ACTION_BY_NAME.get(tok.v);
      if (!info) {
        err(tok, TEST_BY_NAME.has(tok.v) ? `${tok.v} is a test: it belongs inside if (...)` : `unknown command "${tok.v}"`);
      }
      next();
      code.put(command(info, tok, 'action'));
      expect(';');
      return info.op === 0;
    }
    if (tok.t !== 'id') err(tok, `expected a statement, found ${show(tok)}`);
    const a = argument();
    const opTok = next();
    if (opTok.t !== 'op' || !(opTok.v in ASSIGN || opTok.v === '++' || opTok.v === '--')) {
      err(opTok, `expected a statement: found ${show(opTok)} after ${a.text}`);
    }
    const left = coerce(a, 'v', `the left side of ${opTok.v}`);
    if (opTok.v === '++' || opTok.v === '--') {
      code.byte(ACTION_BY_NAME.get(opTok.v === '++' ? 'increment' : 'decrement').op).byte(left);
    } else {
      const b = argument();
      if (b.type !== 'n' && b.type !== 'v') {
        err(b.tok, `the right side of ${opTok.v} must be a number or a variable, got ${b.text}`);
      }
      const info = ACTION_BY_NAME.get(ASSIGN[opTok.v] + b.type);
      code.byte(info.op).byte(left).byte(coerce(b, b.type, `the right side of ${opTok.v}`));
    }
    expect(';');
    return false;
  }

  let returned = false;
  while (pos < toks.length) {
    if (isOp(peek(), '}')) err(peek(), '} without a { before it');
    returned = statement();
  }
  if (!returned) code.byte(0);

  for (const g of gotos) {
    if (!labels.has(g.label.v)) err(g.label, `no label "${g.label.v}"`);
    code.setWord(g.at, labels.get(g.label.v) - (g.at + 2));
  }

  const count = Math.max(0, ...st.messages.keys());
  const messages = [];
  for (const [n, text] of st.messages) messages[n] = text;
  const bytes = encodeLogic(code.done(), messages, count);
  if (bytes.length > 65535) throw new SourceError(file, 1, 'the logic is larger than 64 KB');
  return { bytes, said, messages };
}

// Put code and messages together. messages[n] is the text of message n,
// with holes for numbers that are not used.
export function encodeLogic(codeBytes, messages = [], count = Math.max(0, messages.length - 1)) {
  const out = new Bytes();
  out.word(codeBytes.length).put(codeBytes).byte(count);
  const origin = out.length;   // every message offset counts from here
  out.word(0);
  for (let n = 1; n <= count; n++) out.word(0);
  const textAt = out.length;
  for (let n = 1; n <= count; n++) {
    if (messages[n] === undefined) continue;
    out.setWord(origin + n * 2, out.length - origin);
    out.put(asciiBytes(messages[n], 'message', n)).byte(0);
  }
  out.setWord(origin, out.length - origin);
  return avisDurgan(out.done(), textAt);
}

// ---- Decoder ----

// Returns { code: Uint8Array, messages } with messages[n] the text of
// message n (undefined where there is none).
export function decodeLogic(bytes) {
  const codeLength = word(bytes, 0);
  const code = bytes.slice(2, 2 + codeLength);
  const count = bytes[2 + codeLength];
  const origin = 3 + codeLength;
  const textAt = origin + 2 + count * 2;
  const plain = avisDurgan(Uint8Array.from(bytes), textAt, origin + word(bytes, origin));
  const messages = [];
  for (let n = 1; n <= count; n++) {
    const offset = word(bytes, origin + n * 2);
    if (!offset) continue;
    let end = origin + offset;
    while (end < plain.length && plain[end] !== 0) end++;
    messages[n] = bytesToText(plain, origin + offset, end);
  }
  return { code, messages };
}

const hex4 = (n) => n.toString(16).toUpperCase().padStart(4, '0');

function showCommand(info, code, p) {
  if (info.types === null) {
    const groups = [];
    for (let k = 0; k < code[p + 1]; k++) groups.push(word(code, p + 2 + k * 2));
    return { text: `said(${groups.join(', ')})`, size: 2 + groups.length * 2 };
  }
  const args = [...info.types].map((t, k) => (t === 'n' ? '' : t) + code[p + 1 + k]);
  return { text: `${info.name}(${args.join(', ')})`, size: 1 + info.types.length };
}

// One line an instruction: "0012 set.view(o0, 3)". Jumps show the
// address they go to. Returns { lines, messages }.
export function disassemble(bytes) {
  const { code, messages } = decodeLogic(bytes);
  const lines = [];
  let p = 0;
  const bad = (what) => { throw new Error(`logic: ${what} at ${hex4(p)}`); };
  while (p < code.length) {
    const at = p;
    const op = code[p];
    if (op === 0xFF) {
      p++;
      const parts = [];
      let group = null;
      let not = '';
      while (code[p] !== 0xFF) {
        if (p >= code.length) bad('test list runs off the end');
        if (code[p] === 0xFD) { not = '!'; p++; continue; }
        if (code[p] === 0xFC) {
          if (group) { parts.push(`(${group.join(' || ')})`); group = null; } else group = [];
          p++;
          continue;
        }
        const info = TEST_BY_OP.get(code[p]);
        if (!info) bad(`unknown test ${code[p]}`);
        const c = showCommand(info, code, p);
        (group || parts).push(not + c.text);
        not = '';
        p += c.size;
      }
      p += 3;
      lines.push(`${hex4(at)} if (${parts.join(' && ')}) else ${hex4(p + word(code, p - 2))}`);
    } else if (op === 0xFE) {
      p += 3;
      const d = word(code, p - 2);
      lines.push(`${hex4(at)} goto ${hex4(p + (d > 0x7FFF ? d - 0x10000 : d))}`);
    } else {
      const info = ACTION_BY_OP.get(op);
      if (!info) bad(`unknown action ${op}`);
      const c = showCommand(info, code, p);
      lines.push(`${hex4(at)} ${c.text}`);
      p += c.size;
    }
  }
  return { lines, messages };
}

if (isMain(import.meta)) {
  runMain('logic.js in.lgc out.bin --words game/words.txt --objects game/objects.txt [--said out.json] [--list]', 2, (args) => {
    const { rest: [src, dst], opts } = options(args, ['list']);
    const result = compileLogic(readFileSync(src, 'utf8'), {
      file: src,
      words: opts.words && parseWords(readFileSync(opts.words, 'utf8'), opts.words),
      objects: opts.objects && parseObjects(readFileSync(opts.objects, 'utf8'), opts.objects),
    });
    writeFileSync(dst, result.bytes);
    if (opts.said) writeFileSync(opts.said, saidJson(result.said));
    if (opts.list) {
      const d = disassemble(result.bytes);
      console.log(d.lines.join('\n'));
      d.messages.forEach((text, n) => console.log(`m${n} ${JSON.stringify(text)}`));
    }
    const n = resourceNumber(basename(src));
    console.log(`${dst}: logic${n === null ? '' : ' ' + n}, ${result.bytes.length} bytes, ${result.said.length} said patterns`);
  });
}

// One pattern a line, so a diff of the file reads well.
export function saidJson(said) {
  return said.length ? `[\n${said.map((p) => '  ' + JSON.stringify(p)).join(',\n')}\n]\n` : '[]\n';
}
