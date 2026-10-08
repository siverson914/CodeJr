#!/usr/bin/env node
// The review page: the whole game on one page for the owner to read and
// comment on. Every room as the player sees it, its sprites, and every
// line of text with what triggers it.
//
//   node tools/review.js build build/review.html [--game game]
//
// One HTML file, nothing fetched. tools/serve.js serves it at /admin and
// keeps the comments; opened from disk it keeps them in the browser.
// See FORMATS.md, "The review page".

import { readFileSync, writeFileSync, readdirSync, existsSync, statSync } from 'node:fs';
import { join, dirname, resolve, basename } from 'node:path';
import { createHash } from 'node:crypto';
import { crc32 } from 'node:zlib';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { Interpreter, KEY } from '../js/agi.js';
import { renderFrame, FRAME_W, FRAME_H } from '../js/render.js';
import { drawPicture, PIC_W, PIC_H } from '../js/pic.js';
import { loadResource, decodeView, decodeWords } from '../js/resources.js';
import { loadResources } from './play.js';
import { encodePng, IBM_PALETTE } from './png.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const TITLE_ROOM = 98;

// Logics that belong to no one room, in the words the page uses.
const SHARED = {
  0: 'Global replies (they work on every screen)',
  80: 'Zuckerberg, who wanders',
  83: 'The context window (what JR forgets)',
  84: 'The agent swarm',
  90: 'The boot screen and the date prompt',
  99: 'The death screen',
  104: 'The hint ladder',
};
// Plumbing with no text worth reading.
const SKIPPED = new Set([91, 95]);

// ---- small helpers ----

export const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const hash8 = (s) => createHash('sha1').update(s).digest('hex').slice(0, 8);
const number = (name) => { const m = /^(\d+)/.exec(name); return m ? Number(m[1]) : -1; };

// `code` and **bold**, the only markdown the two documents use inline.
export function inlineMd(s) {
  return esc(s).replace(/`([^`]+)`/g, '<code>$1</code>').replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
}

// ---- reading .lgc source ----

// Tokens with line numbers, plus the "// ---- heading ----" comments,
// which become headings in the script.
export function tokenize(src) {
  const tokens = [], banners = [];
  let i = 0, line = 1;
  const n = src.length;
  while (i < n) {
    const c = src[i];
    if (c === '\n') { line++; i++; continue; }
    if (c === ' ' || c === '\t' || c === '\r') { i++; continue; }
    if (c === '/' && src[i + 1] === '/') {
      let e = src.indexOf('\n', i);
      if (e < 0) e = n;
      const m = /^\/\/\s*-{2,}\s*(.+?)\s*-{2,}\s*$/.exec(src.slice(i, e));
      if (m) banners.push({ line, text: m[1] });
      i = e;
      continue;
    }
    if (c === '/' && src[i + 1] === '*') {
      let e = src.indexOf('*/', i + 2);
      if (e < 0) e = n;
      for (let k = i; k < e; k++) if (src[k] === '\n') line++;
      i = e + 2;
      continue;
    }
    if (c === '#') {   // #include, #define: read separately
      let e = src.indexOf('\n', i);
      if (e < 0) e = n;
      i = e;
      continue;
    }
    if (c === '"') {
      let s = '', k = i + 1;
      for (; k < n && src[k] !== '"'; k++) {
        if (src[k] === '\\' && k + 1 < n) {
          k++;
          s += src[k] === 'n' ? '\n' : src[k];
        } else {
          if (src[k] === '\n') line++;
          s += src[k];
        }
      }
      tokens.push({ t: 'str', v: s, line });
      i = k + 1;
      continue;
    }
    if (/[0-9]/.test(c)) {
      let k = i;
      while (k < n && /[0-9]/.test(src[k])) k++;
      tokens.push({ t: 'num', v: src.slice(i, k), line });
      i = k;
      continue;
    }
    if (/[A-Za-z_]/.test(c)) {
      let k = i;
      while (k < n && /[\w.]/.test(src[k])) k++;
      tokens.push({ t: 'id', v: src.slice(i, k), line });
      i = k;
      continue;
    }
    const two = src.slice(i, i + 2);
    if (['==', '!=', '<=', '>=', '&&', '||', '++', '--', '+=', '-=', '*=', '/='].includes(two)) {
      tokens.push({ t: 'op', v: two, line });
      i += 2;
      continue;
    }
    tokens.push({ t: 'op', v: c, line });
    i++;
  }
  return { tokens, banners };
}

// A small recursive-descent parser: enough of the language to know which
// condition every statement sits under. Throws with a line number.
export function parseLogic(src) {
  const { tokens, banners } = tokenize(src);
  let p = 0;
  const peek = (k = 0) => tokens[p + k];
  const is = (v, k = 0) => peek(k) && peek(k).t !== 'str' && peek(k).v === v;
  const fail = (msg) => { throw new Error(`line ${peek() ? peek().line : 'end'}: ${msg}`); };
  const take = (v) => { if (!is(v)) fail(`expected "${v}"`); return tokens[p++]; };

  const operand = () => {
    const t = tokens[p++];
    if (!t) fail('unexpected end');
    if (t.t === 'op' && t.v === '-' && peek() && peek().t === 'num') return { kind: 'num', v: `-${tokens[p++].v}` };
    return { kind: t.t, v: t.v };
  };
  const args = () => {
    take('(');
    const out = [];
    while (!is(')')) {
      if (!peek()) fail('unclosed (');
      out.push(operand());
      if (is(',')) p++;
    }
    take(')');
    return out;
  };
  const unary = () => {
    if (is('!')) { p++; return { type: 'not', x: unary() }; }
    if (is('(')) { p++; const e = or(); take(')'); return e; }
    const t = peek();
    if (!t) fail('unexpected end of condition');
    if (t.t === 'id' && is('(', 1)) { p++; return { type: 'call', name: t.v, args: args() }; }
    const left = operand();
    if (peek() && peek().t === 'op' && ['==', '!=', '<', '>', '<=', '>='].includes(peek().v)) {
      const op = tokens[p++].v;
      return { type: 'cmp', left, op, right: operand() };
    }
    return { type: 'cmp', left, op: '!=', right: { kind: 'num', v: '0' } };
  };
  const and = () => {
    const items = [unary()];
    while (is('&&')) { p++; items.push(unary()); }
    return items.length === 1 ? items[0] : { type: 'and', items };
  };
  function or() {
    const items = [and()];
    while (is('||')) { p++; items.push(and()); }
    return items.length === 1 ? items[0] : { type: 'or', items };
  }
  const body = () => {
    if (is('{')) { p++; const b = block(true); take('}'); return b; }
    const s = statement();
    return s ? [s] : [];
  };
  function statement() {
    const t = peek();
    if (is(';')) { p++; return null; }
    if (t.t !== 'id') fail(`unexpected "${t.v}"`);
    if (t.v === 'if') {
      p++;
      take('(');
      const cond = or();
      take(')');
      const then = body();
      let els = null;
      if (is('else')) { p++; els = body(); }
      return { type: 'if', cond, then, els, line: t.line };
    }
    if (t.v === 'goto') {
      p++;
      let label;
      if (is('(')) { p++; label = tokens[p++].v; take(')'); } else label = tokens[p++].v;
      if (is(';')) p++;
      return { type: 'goto', label, line: t.line };
    }
    if (is(':', 1)) { p += 2; return { type: 'label', name: t.v, line: t.line }; }
    if (is('(', 1)) {
      p++;
      const a = args();
      if (is(';')) p++;
      return { type: 'call', name: t.v, args: a, line: t.line };
    }
    // An assignment: keep the operator and what follows it.
    p++;
    const rest = [];
    while (peek() && !is(';')) rest.push(tokens[p++]);
    if (is(';')) p++;
    return { type: 'assign', target: t.v, op: rest[0] ? rest[0].v : '', value: rest.slice(1).map((r) => r.v).join(' '), line: t.line };
  }
  function block(inner) {
    const out = [];
    while (peek() && !(inner && is('}'))) {
      const s = statement();
      if (s) out.push(s);
    }
    return out;
  }
  const stmts = block(false);
  return { stmts, banners };
}

// #define lines of a file and of the headers it includes: name -> { value, note }.
export function readDefines(file, seen = new Set()) {
  const out = new Map();
  if (seen.has(file) || !existsSync(file)) return out;
  seen.add(file);
  for (const line of readFileSync(file, 'utf8').split('\n')) {
    let m = /^\s*#include\s+"([^"]+)"/.exec(line);
    if (m) {
      for (const [k, v] of readDefines(join(dirname(file), m[1]), seen)) out.set(k, v);
      continue;
    }
    m = /^\s*#define\s+(\w+)\s+(\S+)\s*(?:\/\/\s*(.*))?$/.exec(line);
    if (m) out.set(m[1], { value: m[2], note: (m[3] || '').trim() });
  }
  return out;
}

const EDGE = { 1: 'north', 2: 'east', 3: 'south', 4: 'west' };
const ARRIVE = {
  0: 'arriving by teleport (tests)', 1: 'arriving from the south (he walked north)',
  2: 'arriving from the west (he walked east)', 3: 'arriving from the north (he walked south)',
  4: 'arriving from the east (he walked west)', 5: 'arriving through a door',
};
const FLIP = { '==': '!=', '!=': '==', '<': '>=', '>': '<=', '<=': '>', '>=': '<' };
const CMP = { '==': 'is', '!=': 'is not', '<': 'is under', '>': 'is over', '<=': 'is at most', '>=': 'is at least' };
// Engine flags, said the way a player would.
const FLAG_WORDS = {
  f0: ['in the water', 'not in the water'],
  f2: ['the player typed a command', 'no command typed'],
  f3: ['standing on a trigger line', 'not on a trigger'],
  f4: ['a command was accepted', 'no command was accepted'],
  f5: ['on entering', 'after entering'],
};

// Turns conditions into plain words. `defs` is readDefines' map;
// options.vars renames variables, options.cmp(name, op, value) may
// answer for a comparison.
export function makeDescriber(defs = new Map(), options = {}) {
  const raw = (id) => (defs.has(id) ? defs.get(id).value : id);
  const byValue = new Map();
  for (const [k, v] of defs) if (!byValue.has(v.value)) byValue.set(v.value, k);
  const name = (id) => {
    if (options.vars && options.vars[id]) return options.vars[id];
    return /^[fvso]\d+$/.test(id) && byValue.has(id) ? byValue.get(id) : id;
  };
  const words = (a) => a.map((x) => String(x.v).toUpperCase().replace(/^ANYWORD$/, 'ANYTHING').replace(/^ROL$/, '...')).join(' ');

  // An atom as [text, isCommand]. `neg` asks for its opposite.
  function atom(e, neg) {
    if (e.type === 'call') {
      const a = e.args;
      if (e.name === 'said') return neg ? `not ${words(a)}` : words(a);
      if (e.name === 'isset' || e.name === 'issetv') {
        const id = a[0] ? a[0].v : '?';
        const special = FLAG_WORDS[raw(id)];
        if (special) return special[neg ? 1 : 0];
        return neg ? `not ${name(id)}` : name(id);
      }
      if (e.name === 'has') return `${neg ? 'not holding' : 'holding'} ${a[0] ? a[0].v : '?'}`;
      if (e.name === 'posn' || e.name === 'obj.in.box' || e.name === 'center.posn' || e.name === 'right.posn') {
        const who = a[0] && raw(a[0].v) !== 'o0' ? `${name(a[0].v)} ` : '';
        return `${who}${neg ? 'not standing' : 'standing'} in the right place`;
      }
      if (e.name === 'compare.strings') return neg ? 'the typed text does not match' : 'the typed text matches';
      if (e.name === 'have.key') return neg ? 'no key pressed' : 'a key is pressed';
      if (e.name === 'controller') return `${neg ? 'not ' : ''}menu or key ${a[0] ? a[0].v : ''}`;
      return `${neg ? 'not ' : ''}${e.name}(${a.map((x) => x.v).join(', ')})`;
    }
    if (e.type === 'cmp') {
      const op = neg ? FLIP[e.op] : e.op;
      const l = e.left.v, r = e.right.v, lr = raw(l);
      if (options.cmp) {
        const said = options.cmp(l, op, r);
        if (said) return said;
      }
      if (lr === 'v2' && op === '==' && EDGE[r]) return `walking off the ${EDGE[r]} edge`;
      if (l === 'arriveFrom' && op === '==' && ARRIVE[r]) return ARRIVE[r];
      if (lr === 'v0' && (op === '==' || op === '!=')) return `${op === '==' ? 'in' : 'not in'} room ${r}`;
      if (lr === 'v9' && op === '>' && r === '0') return 'the player typed a word the game does not know';
      const rv = e.right.kind === 'id' ? name(r) : r;
      return `${name(l)} ${CMP[op]} ${rv}`;
    }
    return null;
  }

  function describe(e) {
    if (e.type === 'not') {
      const inner = atom(e.x, true);
      return inner !== null ? inner : `not (${describe(e.x)})`;
    }
    if (e.type === 'and' || e.type === 'or') {
      const parts = e.items.map((x) => ((x.type === 'and' || x.type === 'or') ? `(${describe(x)})` : describe(x)));
      return parts.join(e.type === 'and' ? ' and ' : ' or ');
    }
    return atom(e, false);
  }

  const isSaid = (e) => (e.type === 'call' && e.name === 'said') || (e.type === 'or' && e.items.every(isSaid));
  const hasSaid = (e) => isSaid(e) || ((e.type === 'and' || e.type === 'or') && e.items.some(hasSaid));
  const SPECIAL = /^(on entering|walking off|arriving|standing on a trigger line|in the water|the player typed)/;

  return {
    name,
    // "LOOK BED or LOOK ROOM", "when gateOpen", "on entering".
    when(e) {
      const text = describe(e);
      if (isSaid(e)) return text;
      if (e.type === 'and' && e.items.some(hasSaid)) {
        // A command with a proviso: the command first, then "when ...".
        const side = (list) => list.map((x) => ((x.type === 'or' && !isSaid(x)) ? `(${describe(x)})` : describe(x))).join(' and ');
        const rest = e.items.filter((x) => !hasSaid(x));
        return `${side(e.items.filter(hasSaid))}${rest.length ? `, when ${side(rest)}` : ''}`;
      }
      return SPECIAL.test(text) ? text : `when ${text}`;
    },
    // The else branch: "otherwise", with the opposite spelled out when
    // the condition is simple enough for that to help.
    otherwise(e) {
      let opposite = null;
      if (e.type === 'not') opposite = atom(e.x, false);
      else if (e.type === 'call' && e.name !== 'said') opposite = atom(e, true);
      else if (e.type === 'cmp') opposite = atom(e, true);
      return opposite ? `otherwise (${opposite})` : 'otherwise';
    },
  };
}

// %-codes as words, so "%v40 tokens" reads "[tokens] tokens".
function codes(text, d) {
  return text.replace(/%([vsmgow])(\d+)(?:\|\d+)?/g, (all, kind, n) => {
    if (kind === 'v') return `[${d.name(`v${n}`)}]`;
    if (kind === 's') return n === '5' ? '[the death line]' : `[text ${d.name(`s${n}`)}]`;
    if (kind === 'w') return `[word ${n} the player typed]`;
    if (kind === 'm') return `[message ${n}]`;
    return all;
  });
}

// The script of one logic: every text in source order with the path of
// conditions it sits under. Returns { cards, lines, error }.
//   card:  { heading, trigger, groups }
//   group: { trigger (below the card's), items }
//   item:  { type: 'text', kind, text, id, line } | { type: 'points', n }
//        | { type: 'note', text } | { type: 'fx', text }
// options: logic (number, for ids), defs, roomName(n), vars, cmp.
export function extractScript(source, options = {}) {
  const logic = options.logic === undefined ? 0 : options.logic;
  let ast;
  try {
    ast = parseLogic(source);
  } catch (e) {
    // Still show every string, so nothing is hidden by a parse problem.
    const items = [...source.matchAll(/\b(?:print|print\.at|display|set\.string)\([^"\n]*"((?:[^"\\]|\\.)*)"/g)].map((m) => {
      const text = m[1].replace(/\\n/g, '\n').replace(/\\(.)/g, '$1');
      return { type: 'text', kind: 'print', text, id: `L${logic}:${hash8(text)}`, line: 0 };
    });
    return { cards: [{ heading: null, trigger: 'trigger unknown', groups: [{ trigger: '', items }] }], lines: items.length, error: e.message };
  }
  const d = makeDescriber(options.defs, options);
  const roomName = options.roomName || (() => '');
  const entries = [];
  const strings = new Map();   // sN -> the item last set, for "%s6%s7"
  let top = -1, topIf = false;

  // Where each goto comes from, to say how a label is reached.
  const jumps = new Map();
  const findJumps = (stmts, first) => {
    for (const s of stmts) {
      if (s.type === 'if') {
        const f = first || d.when(s.cond);
        findJumps(s.then, f);
        if (s.els) findJumps(s.els, f);
      } else if (s.type === 'goto' && first) {
        if (!jumps.has(s.label)) jumps.set(s.label, []);
        if (!jumps.get(s.label).includes(first)) jumps.get(s.label).push(first);
      }
    }
  };
  findJumps(ast.stmts, null);
  const labelPart = (label) => {
    const from = (jumps.get(label) || []).map((f) => (f.length > 40 ? `${f.slice(0, 40).replace(/\s+\S*$/, '')}...` : f));
    return `at "${label}"${from.length ? ` (jumped to from: ${from.join('; ')})` : ''}`;
  };

  const emit = (path, item) => entries.push({ top, topIf, path: path.length ? path : ['every turn'], item });
  const str = (a, i) => (a[i] && a[i].kind === 'str' ? a[i].v : null);

  const call = (s, path) => {
    const a = s.args;
    const text = (kind, t, extra = {}) => {
      if (t === null) return null;
      const item = { type: 'text', kind, raw: t, line: s.line, ...extra };
      emit(path, item);
      return item;
    };
    switch (s.name) {
      case 'print': case 'print.at': text('print', str(a, 0)); break;
      case 'display': text('display', str(a, 2)); break;
      case 'get.string': text('prompt', str(a, 1)); break;
      case 'get.num': text('prompt', str(a, 0)); break;
      case 'set.string': {
        const v = a[0] ? a[0].v : '';
        let t = str(a, 1);
        if (t === null) break;
        // "%s6\n%s7" is put together from the pieces set just above.
        t = t.replace(/%s(\d+)/g, (all, n) => {
          const piece = strings.get(`s${n}`);
          if (!piece) return all;
          piece.used = true;
          return piece.raw;
        });
        const real = options.defs && options.defs.has(v) ? options.defs.get(v).value : v;
        const item = text(real === 's5' ? 'death' : 'string', t);
        strings.set(v, item);
        break;
      }
      case 'get': if (str(a, 0) !== null) emit(path, { type: 'note', text: `JR gets: ${str(a, 0)}` }); break;
      case 'drop': if (str(a, 0) !== null) emit(path, { type: 'note', text: `JR no longer has: ${str(a, 0)}` }); break;
      case 'new.room': {
        const n = a[0] ? a[0].v : '?';
        emit(path, { type: 'fx', text: `goes to room ${n}${roomName(Number(n)) ? ` (${roomName(Number(n))})` : ''}` });
        break;
      }
      case 'set': case 'reset': {
        const id = a[0] ? a[0].v : '?';
        if (s.name === 'set' && id === 'dead') emit(path, { type: 'note', text: 'JR dies here' });
        else emit(path, { type: 'fx', text: `${s.name === 'set' ? 'sets' : 'clears'} ${d.name(id)}` });
        break;
      }
      default: break;
    }
  };

  const assign = (s, path) => {
    const n = s.op === '++' ? 1 : Number(s.value);
    const target = options.defs && options.defs.has(s.target) ? options.defs.get(s.target).value : s.target;
    if (target === 'v3' && (s.op === '+=' || s.op === '++') && n > 0) emit(path, { type: 'points', n });
    else if (s.target === 'unchanged' && (s.op === '++' || s.op === '+=')) emit(path, { type: 'note', text: 'counts as a "part that didn\'t change"' });
    else if (s.target === 'tokens' && s.op === '-=' && n > 0) emit(path, { type: 'note', text: `costs ${n} token${n === 1 ? '' : 's'}` });
    else if (s.target === 'tokens' && s.op === '+=' && n > 0) emit(path, { type: 'note', text: `gives ${n} token${n === 1 ? '' : 's'}` });
  };

  const saysSomething = (e) => (e.type === 'call' && e.name === 'said')
    || ((e.type === 'and' || e.type === 'or') && e.items.some(saysSomething)) || (e.type === 'not' && saysSomething(e.x));
  const walk = (stmts, base, depth) => {
    let path = base, labeled = false;
    for (const s of stmts) {
      if (depth === 0) { top++; topIf = s.type === 'if'; }
      if (s.type === 'label') {
        path = [...base, labelPart(s.name)];
        labeled = true;
        // The code under a label is not one "if": let it share a card.
        continue;
      }
      // A label's stretch ends where the next command handler begins.
      if (labeled && s.type === 'if' && saysSomething(s.cond)) { path = base; labeled = false; }
      if (labeled && depth === 0) topIf = false;
      if (s.type === 'if') {
        walk(s.then, [...path, d.when(s.cond)], depth + 1);
        if (s.els) walk(s.els, [...path, d.otherwise(s.cond)], depth + 1);
        // "if (x) { ...; goto y; }" with no else: what follows only
        // happens when x was false.
        const last = s.then[s.then.length - 1];
        if (!s.els && last && last.type === 'goto') path = [...path, d.otherwise(s.cond)];
      } else if (s.type === 'call') call(s, path);
      else if (s.type === 'assign') assign(s, path);
      else if (s.type === 'goto') {
        emit(path, { type: 'jump', text: `continues at "${s.label}", further down` });
        if (labeled) { path = base; labeled = false; }
      }
    }
  };
  // The line of the top-level statement each entry belongs to, for headings.
  const topLines = ast.stmts.map((s) => s.line);
  walk(ast.stmts, [], 0);

  // Finish the texts: drop pieces that went into a longer string, name the rest.
  const kept = entries.filter((en) => !(en.item.type === 'text' && en.item.used));
  let lines = 0;
  for (const en of kept) {
    if (en.item.type !== 'text') continue;
    en.item.text = codes(en.item.raw, d);
    en.item.id = `L${logic}:${hash8(en.item.raw)}`;
    lines++;
  }

  // Cards: one per top-level statement that says something.
  const cards = [];
  const banners = ast.banners.slice();
  let card = null, group = null;
  for (const en of kept) {
    // Each "if" at the top is its own card; loose statements in a row share one.
    const key0 = en.topIf ? `${en.top}|${en.path[0]}` : `loose|${en.path[0]}`;
    if (!card || card.key !== key0) {
      let heading = null;
      const at = topLines[en.top] || 0;
      // The last heading comment above this statement, said once.
      while (banners.length && banners[0].line <= at) heading = banners.shift().text;
      card = { key: key0, heading, trigger: en.path[0], groups: [] };
      cards.push(card);
      group = null;
    }
    const sub = en.path.slice(1).join(' \u203a ');
    if (!group || group.trigger !== sub) {
      group = { trigger: sub, items: [] };
      card.groups.push(group);
    }
    const { used, raw, ...item } = en.item;
    group.items.push(item);
  }
  // A card with nothing to read (only "goes to room 3") is plumbing.
  const worth = (c) => c.groups.some((g) => g.items.some((i) => i.type === 'text' || i.type === 'points' || i.type === 'note'));
  let carried = null;
  const shown = [];
  for (const c of cards) {
    if (!worth(c)) { if (c.heading) carried = c.heading; continue; }
    if (!c.heading && carried) c.heading = carried;
    carried = null;
    delete c.key;
    shown.push(c);
  }
  return { cards: shown, lines, error: null };
}

// ---- the two documents ----

// GAMEPLAN.md: the map grid, the interiors, the exits table, the briefs.
export function parseGameplan(md) {
  const out = { grid: [], interiors: [], exits: {}, briefs: {}, names: {}, problems: [] };
  const lines = md.split('\n');
  for (const line of lines) {
    const m = /^\s*(north|middle|south)\s+(\d.*)$/.exec(line);
    if (!m) continue;
    const row = [...m[2].matchAll(/(\d+) (.*?)(?=\s{2,}|$)/g)].map((c) => ({ n: Number(c[1]), name: c[2].trim() }));
    if (row.length) out.grid.push({ label: m[1], rooms: row });
  }
  const inter = /Interiors:([\s\S]*?)\n\s*\n/.exec(md);
  if (inter) {
    for (const m of inter[1].replace(/\n/g, ' ').matchAll(/(\d+) ([^(,]+?) \(([^)]+)\)/g)) {
      out.interiors.push({ n: Number(m[1]), name: m[2].trim(), how: m[3].trim() });
    }
  }
  for (const line of lines) {
    const m = /^\|\s*(\d+)\s*\|([^|]*)\|([^|]*)\|([^|]*)\|([^|]*)\|\s*$/.exec(line);
    if (m) out.exits[Number(m[1])] = { north: m[2].trim(), east: m[3].trim(), south: m[4].trim(), west: m[5].trim() };
  }
  const rooms = /^## The rooms\s*$/m.exec(md);
  if (rooms) {
    const parts = md.slice(rooms.index).split(/^### /m).slice(1);
    for (const part of parts) {
      const nl = part.indexOf('\n');
      const heading = part.slice(0, nl).trim();
      const text = part.slice(nl + 1).split(/^## /m)[0].trim();
      for (const m of heading.matchAll(/(?:^|and )(\d+) /g)) out.briefs[Number(m[1])] = { heading, text };
    }
  }
  for (const row of out.grid) for (const r of row.rooms) out.names[r.n] = r.name;
  for (const r of out.interiors) out.names[r.n] = r.name;
  if (out.grid.length !== 3) out.problems.push('the map in GAMEPLAN.md ("north / middle / south" rows) could not be read');
  if (!Object.keys(out.briefs).length) out.problems.push('the room briefs in GAMEPLAN.md ("## The rooms") could not be read');
  return out;
}

// DESIGN.md's puzzle chain table. { rows, total } or { error }.
export function parsePuzzles(md) {
  const at = /^## The puzzle chain\s*$/m.exec(md);
  if (!at) return { error: 'DESIGN.md has no "## The puzzle chain" heading' };
  const lines = md.slice(at.index).split('\n').slice(1);
  let i = lines.findIndex((l) => l.trim().startsWith('|'));
  const stop = lines.findIndex((l) => /^## /.test(l));
  if (i < 0 || (stop >= 0 && i > stop)) return { error: 'no table under "## The puzzle chain" in DESIGN.md' };
  const cells = (l) => l.trim().replace(/^\|/, '').replace(/\|$/, '').split(/(?<!\\)\|/).map((c) => c.trim());
  const head = cells(lines[i]).map((h) => h.toLowerCase());
  const col = (word) => head.findIndex((h) => h.includes(word));
  const cn = col('#'), cw = col('where'), cp = col('problem'), ca = col('answer'), cs = col('points');
  if ([cn, cw, cp, ca, cs].some((c) => c < 0)) return { error: `the puzzle chain table has other columns than expected (${head.join(', ')})` };
  const rows = [];
  for (i += 2; i < lines.length && lines[i].trim().startsWith('|'); i++) {
    const c = cells(lines[i]);
    if (c.length < head.length) continue;
    rows.push({ n: c[cn], where: c[cw], problem: c[cp], answer: c[ca], points: c[cs] });
  }
  if (!rows.length) return { error: 'the puzzle chain table in DESIGN.md has no rows' };
  const total = /^That is (\d+)\./m.exec(lines.slice(i).join('\n'));
  return { rows, total: total ? Number(total[1]) : null };
}

// ---- pictures ----

// A PNG whose last palette entry is clear, for sprites on any background.
function clearPng(w, h, pixels, scaleX = 1, scaleY = 1) {
  const palette = [...IBM_PALETTE, [0, 0, 0]];
  const png = encodePng(w, h, pixels, palette, scaleX, scaleY);
  // The tRNS chunk goes right after PLTE: signature 8, IHDR 25, PLTE 12 + 3 a color.
  const at = 8 + 25 + 12 + palette.length * 3;
  const data = Buffer.alloc(palette.length, 255);
  data[palette.length - 1] = 0;
  const chunk = Buffer.alloc(12 + data.length);
  chunk.writeUInt32BE(data.length, 0);
  chunk.write('tRNS', 4, 'latin1');
  data.copy(chunk, 8);
  chunk.writeUInt32BE(crc32(chunk.subarray(4, 8 + data.length)), 8 + data.length);
  return Buffer.concat([png.subarray(0, at), chunk, png.subarray(at)]);
}

const dataUri = (png) => `data:image/png;base64,${png.toString('base64')}`;

// Every cel of a view on one sheet: a loop a row, wrapped so the sheet
// stays narrow enough for a phone. Pixels are 2 wide, as on screen.
export function viewSheet(view, maxWidth = 150) {
  const GAP = 3, CLEAR = 16;
  const rows = [];
  for (const loop of view.loops) {
    let row = { cels: [], w: 0, h: 0 };
    for (const cel of loop) {
      if (row.cels.length && row.w + GAP + cel.w > maxWidth) {
        rows.push(row);
        row = { cels: [], w: 0, h: 0 };
      }
      row.w += (row.cels.length ? GAP : 0) + cel.w;
      row.h = Math.max(row.h, cel.h);
      row.cels.push(cel);
    }
    if (row.cels.length) rows.push(row);
  }
  const w = Math.max(1, ...rows.map((r) => r.w));
  const h = Math.max(1, rows.reduce((a, r) => a + r.h, 0) + GAP * Math.max(0, rows.length - 1));
  const pix = new Uint8Array(w * h).fill(CLEAR);
  let y = 0;
  for (const row of rows) {
    let x = 0;
    for (const cel of row.cels) {
      const dy = y + row.h - cel.h;   // stand every cel on the row's floor
      for (let cy = 0; cy < cel.h; cy++) {
        for (let cx = 0; cx < cel.w; cx++) {
          const c = cel.pix[cy * cel.w + cx];
          if (c !== 255) pix[(dy + cy) * w + x + cx] = c;
        }
      }
      x += cel.w + GAP;
    }
    y += row.h + GAP;
  }
  return { png: clearPng(w, h, pix, 2, 1), w: w * 2, h };
}

// Play to a room the way the test scripts do and take the frame the
// player would see, greeting boxes closed.
export async function captureRoom(resources, room) {
  const agi = new Interpreter(resources, { log: () => {} });
  const cycle = async (n = 1) => {
    for (let i = 0; i < n; i++) {
      agi.cycle();
      if (agi.pending) await new Promise((r) => setImmediate(r));
    }
  };
  const type = (text) => { for (const ch of text) agi.typeChar(ch); agi.enter(); };
  await cycle(5);
  if (room === TITLE_ROOM) {
    // The title is what a real date leads to; nothing to dismiss.
    type('10-05-2026');
    await cycle(30);
  } else {
    type('debug');
    await cycle(5);
    if (room !== 1) {
      type('tp');
      await cycle(2);
      type(String(room));
    }
    for (let i = 0; i < 40; i++) {
      await cycle(1);
      if (agi.win && agi.wait) {
        agi.keyDown(KEY.ENTER);
        agi.keyUp(KEY.ENTER);
        await cycle(2);
      }
    }
  }
  return { frame: renderFrame(agi, new Uint8Array(FRAME_W * FRAME_H)), room: agi.vars[0] };
}

// ---- gathering everything ----

export async function gather(buildDir, gameDir, docsDir) {
  const resources = loadResources(buildDir);
  if (!resources.logdir) throw new Error(`${buildDir} has no built game (run tools/pack.js first)`);
  const read = (p) => (existsSync(p) ? readFileSync(p, 'utf8') : '');
  const plan = parseGameplan(read(join(docsDir, 'GAMEPLAN.md')));
  const puzzles = parsePuzzles(read(join(docsDir, 'DESIGN.md')));
  const logicDir = join(gameDir, 'logic');
  const files = existsSync(logicDir) ? readdirSync(logicDir).filter((f) => f.endsWith('.lgc')).sort() : [];
  const roomName = (n) => (n === TITLE_ROOM ? 'Title screen' : plan.names[n] || '');
  const images = {};

  // Views: one sheet each, described by the view itself.
  const views = [];
  for (let n = 0; n < 256; n++) {
    const bytes = loadResource(resources.viewdir, resources.vols, n);
    if (!bytes) continue;
    let view;
    try { view = decodeView(bytes); } catch (e) { continue; }
    const sheet = viewSheet(view);
    images[`v${n}`] = dataUri(sheet.png);
    views.push({ n, desc: view.desc || '', w: sheet.w, h: sheet.h, loops: view.loops.length, cels: view.loops.reduce((a, l) => a + l.length, 0) });
  }
  const viewNumbers = new Set(views.map((v) => v.n));

  // The hint ladder numbers its steps; say what each step is waiting for.
  const hintOptions = (source) => {
    const steps = {};
    for (const m of source.matchAll(/if \(work == 0(?: && ([^{]*?))?\) \{ work = (\d+); \}/g)) steps[m[2]] = m[1] || '';
    const nth = { 1: 'the first time it is asked', 2: 'the second time', 3: 'the third time' };
    return {
      cmp(name, op, value) {
        if (op !== '==') return null;
        if (name === 'work' && value in steps) {
          const need = steps[value].replace(/!isset\((\w+)\)/g, 'not $1').replace(/!has\("([^"]+)"\)/g, 'no $1').replace(/&&/g, 'and');
          return `hint step ${value}${need ? ` (still ${need})` : ' (everything is done)'}`;
        }
        if (name === 'hintLevel' && nth[value]) return nth[value];
        return null;
      },
    };
  };

  const scripts = {};
  let lineCount = 0;
  for (const f of files) {
    const n = number(f);
    if (n < 0 || SKIPPED.has(n)) continue;
    const source = read(join(logicDir, f));
    const defs = readDefines(join(logicDir, f));
    const extra = n === 104 ? hintOptions(source) : {};
    const script = extractScript(source, { logic: n, defs, roomName, ...extra });
    lineCount += script.lines;
    // Views the logic loads or stamps, beside its own range.
    const used = new Set();
    for (const m of source.matchAll(/\bload\.view\(\s*(\d+)\s*\)|\bset\.view\(\s*\w+\s*,\s*(\d+)\s*\)|\badd\.to\.pic\(\s*(\d+)/g)) {
      const v = Number(m[1] || m[2] || m[3]);
      if (viewNumbers.has(v)) used.add(v);
    }
    scripts[n] = { n, file: f, script, used };
  }

  // Rooms: the title, then 1 to 16.
  const numbers = Object.keys(scripts).map(Number).filter((n) => (n >= 1 && n <= 79) || n === TITLE_ROOM);
  const order = [...numbers.filter((n) => n === TITLE_ROOM), ...numbers.filter((n) => n !== TITLE_ROOM).sort((a, b) => a - b)];
  const rooms = [];
  for (const n of order) {
    const shot = await captureRoom(resources, n);
    images[`s${n}`] = dataUri(encodePng(FRAME_W, FRAME_H, shot.frame));
    let hasPic = false;
    const picBytes = loadResource(resources.picdir, resources.vols, n);
    if (picBytes) {
      const buf = drawPicture(picBytes, new Uint8Array(PIC_W * PIC_H));
      images[`p${n}`] = dataUri(encodePng(PIC_W, PIC_H, buf.map((b) => b & 15), IBM_PALETTE, 2, 1));
      images[`q${n}`] = dataUri(encodePng(PIC_W, PIC_H, buf.map((b) => b >> 4), IBM_PALETTE, 2, 1));
      hasPic = true;
    }
    const own = views.filter((v) => n !== TITLE_ROOM && v.n >= n * 10 + 20 && v.n <= n * 10 + 29).map((v) => v.n);
    const helper = scripts[100 + n] && !(100 + n in SHARED) ? scripts[100 + n] : null;
    const all = new Set([...own, ...scripts[n].used, ...(helper ? helper.used : [])]);
    rooms.push({
      n,
      name: roomName(n) || basename(scripts[n].file, '.lgc').replace(/^\d+_/, ''),
      arrived: shot.room === n,
      hasPic,
      views: [...all].sort((a, b) => a - b),
      script: scripts[n].script,
      helper: helper ? { n: 100 + n, script: helper.script } : null,
      brief: plan.briefs[n] || null,
      exits: plan.exits[n] || null,
      interior: plan.interiors.find((r) => r.n === n) || null,
    });
  }
  const roomSet = new Set(order);
  const shared = Object.values(scripts)
    .filter((s) => !roomSet.has(s.n) && !(s.n > 100 && roomSet.has(s.n - 100) && !(s.n in SHARED)))
    .sort((a, b) => a.n - b.n)
    .map((s) => ({ n: s.n, name: SHARED[s.n] || `Logic ${s.n} (${basename(s.file, '.lgc').replace(/^\d+_/, '')})`, script: s.script }));

  // Vocabulary and sizes, from the built files.
  const groups = new Map();
  if (resources.words) {
    for (const [w, g] of Object.entries(decodeWords(resources.words).dict)) {
      if (!groups.has(g)) groups.set(g, []);
      groups.get(g).push(w);
    }
  }
  let bytes = 0;
  for (const name of readdirSync(buildDir)) {
    if (/^(LOGDIR|PICDIR|VIEWDIR|SNDDIR|WORDS\.TOK|OBJECT|VOL\.\d+)$/.test(name)) bytes += statSync(join(buildDir, name)).size;
  }
  const flags = [...readDefines(join(logicDir, 'defines.h'))].filter(([, v]) => v.note).map(([k, v]) => ({ name: k, value: v.value, note: v.note }));

  return {
    game: resources.game || {},
    plan, puzzles, rooms, shared, views, images, flags,
    vocabulary: [...groups].sort((a, b) => a[0] - b[0]).map(([g, w]) => ({ group: g, words: w.sort() })),
    totals: {
      rooms: rooms.filter((r) => r.n !== TITLE_ROOM).length,
      sprites: views.length,
      cels: views.reduce((a, v) => a + v.cels, 0),
      lines: lineCount,
      words: [...groups.values()].reduce((a, w) => a + w.length, 0),
      groups: groups.size,
      bytes,
    },
  };
}

// ---- the page ----

const SPEAKER = /^([A-Z][A-Z0-9 .'-]{1,24}):\s+(.*)$/;

// Game text as HTML: "NAME: line" reads as dialogue, blank lines as gaps.
export function textHtml(text) {
  const out = [];
  for (const line of text.split('\n')) {
    if (line.trim() === '') { out.push('<span class="gap"></span>'); continue; }
    const m = SPEAKER.exec(line);
    const body = (s) => esc(s).replace(/\[([^\]\n]{1,40})\]/g, '<span class="code">[$1]</span>');
    out.push(m ? `<span class="l"><b class="sp">${esc(m[1])}:</b> ${body(m[2])}</span>` : `<span class="l">${body(line)}</span>`);
  }
  return out.join('');
}

const KIND = { display: 'on the screen', prompt: 'asks for input', death: 'death line', string: 'text kept for later' };

function scriptHtml(script) {
  const out = [];
  if (script.error) out.push(`<p class="warn">This file could not be read as a program (${esc(script.error)}), so the lines below have no triggers.</p>`);
  for (const card of script.cards) {
    if (card.heading) out.push(`<h4 class="bh">${esc(card.heading)}</h4>`);
    out.push(`<div class="card"><div class="trig">${esc(card.trigger)}</div>`);
    for (const g of card.groups) {
      const quiet = g.items.every((i) => i.type === 'fx');
      out.push(`<div class="grp${quiet ? ' fxonly' : ''}"${g.trigger ? ` data-trig="${esc(g.trigger)}"` : ''}>`);
      if (g.trigger) out.push(`<div class="sub">${esc(g.trigger)}</div>`);
      for (const it of g.items) {
        if (it.type === 'text') {
          const tag = KIND[it.kind] ? `<span class="tag">${KIND[it.kind]}</span>` : '';
          out.push(`<div class="ln" data-t="${esc(it.id)}"><div class="tx">${tag}${textHtml(it.text)}</div><button class="cb" type="button" aria-label="Comment on this line">+</button></div>`);
        } else if (it.type === 'points') {
          out.push(`<div class="pts">+${it.n} point${it.n === 1 ? '' : 's'}</div>`);
        } else {
          out.push(`<div class="${it.type === 'fx' ? 'fx' : 'nt'}">${esc(it.text)}</div>`);   // note, jump
        }
      }
      out.push('</div>');
    }
    out.push('</div>');
  }
  if (!script.cards.length) out.push('<p class="quiet">No text in this one.</p>');
  return out.join('\n');
}

const img = (key, w, h, alt, cls = 'px') => `<img class="${cls}" data-img="${key}" data-w="${w}" width="${w}" height="${h}" alt="${esc(alt)}">`;
const commentBox = (target, kind, label, text = 'Comment') => `<span class="ct" data-t="${esc(target)}" data-kind="${kind}" data-label="${esc(label)}"><button class="cb wide" type="button">${text}</button></span>`;

function spriteHtml(v) {
  const label = `View ${v.n}${v.desc ? `: ${v.desc}` : ''}`;
  return `<figure class="spr ct" data-t="view:${v.n}" data-kind="sprite" data-label="${esc(label)}">
<div class="chk">${img(`v${v.n}`, v.w, v.h, label)}</div>
<figcaption><b>${v.n}</b> ${esc(v.desc || '(no description)')} <span class="quiet">${v.loops} loop${v.loops === 1 ? '' : 's'}, ${v.cels} cel${v.cels === 1 ? '' : 's'}</span> <button class="cb" type="button" aria-label="Comment on this sprite">+</button></figcaption>
</figure>`;
}

function exitsHtml(room, names) {
  if (room.exits) {
    const one = (dir) => {
      const v = room.exits[dir];
      const n = /^\d+$/.test(v) ? Number(v) : null;
      return `<li><b>${dir}</b> ${n ? `<a href="#room-${n}">${n} ${esc(names(n))}</a>` : esc(v)}</li>`;
    };
    return `<ul class="exits">${['north', 'east', 'south', 'west'].map(one).join('')}</ul>`;
  }
  if (room.interior) return `<p class="quiet">An interior: ${esc(room.interior.how)}.</p>`;
  return '';
}

function briefHtml(room) {
  if (!room.brief) return '<p class="quiet">GAMEPLAN.md has no brief for this one (rooms 1 and 2 were built first, as the house style).</p>';
  const paras = room.brief.text.split(/\n\s*\n/).map((p) => `<p>${inlineMd(p.replace(/\n/g, ' '))}</p>`).join('');
  return `<details class="brief"><summary>The brief: ${esc(room.brief.heading)}</summary>${paras}</details>`;
}

function roomHtml(room, data) {
  const names = (n) => (data.rooms.find((r) => r.n === n) || {}).name || '';
  const title = `${room.n === TITLE_ROOM ? '' : `${room.n} `}${room.name}`;
  const group = room.n === TITLE_ROOM ? 'Title screen' : `Room ${room.n}: ${room.name}`;
  const sprites = room.views.map((n) => data.views.find((v) => v.n === n)).filter(Boolean);
  const layers = room.hasPic ? `<div class="tabs" role="group" aria-label="Which picture">
<button type="button" class="on" data-show="s${room.n}">As played</button><button type="button" data-show="p${room.n}">Bare picture</button><button type="button" data-show="q${room.n}">Priority and control</button></div>` : '';
  return `<section class="room" id="room-${room.n}" data-group="${esc(group)}" data-order="${room.n === TITLE_ROOM ? 0 : room.n}">
<h2>${esc(title)} ${commentBox(`room:${room.n}`, 'room', group, 'Comment on this room')}</h2>
${room.arrived ? '' : '<p class="warn">The screenshot run did not end in this room; the picture below may be of another.</p>'}
<figure class="shot ct" data-t="shot:${room.n}" data-kind="screenshot" data-label="${esc(`${group}, the screenshot`)}">
${img(`s${room.n}`, FRAME_W, FRAME_H, `${title} as the player sees it`)}
${layers}
<figcaption><span class="cap">As the player sees it.</span> <button class="cb wide" type="button">Comment on the picture</button></figcaption>
</figure>
${exitsHtml(room, names)}
${briefHtml(room)}
<h3>Sprites here</h3>
<div class="sprites">${sprites.map(spriteHtml).join('\n') || '<p class="quiet">None.</p>'}</div>
<h3>Everything this room says <span class="quiet">${room.script.lines} line${room.script.lines === 1 ? '' : 's'}</span></h3>
<div class="script">${scriptHtml(room.script)}</div>
${room.helper ? `<h3>Its helper, logic ${room.helper.n}</h3><div class="script">${scriptHtml(room.helper.script)}</div>` : ''}
</section>`;
}

function overviewHtml(data) {
  const t = data.totals;
  const thumb = (n, name, note = '') => {
    const have = data.rooms.some((r) => r.n === n);
    return `<a class="cell" href="#room-${n}">${have ? img(`s${n}`, FRAME_W, FRAME_H, name, 'th') : ''}<span>${n === TITLE_ROOM ? '' : `${n} `}${esc(name)}</span>${note ? `<small>${esc(note)}</small>` : ''}</a>`;
  };
  const grid = data.plan.grid.map((row) => row.rooms.map((r) => thumb(r.n, r.name)).join('')).join('');
  const inter = data.plan.interiors.map((r) => thumb(r.n, r.name, r.how)).join('');
  const title = data.rooms.some((r) => r.n === TITLE_ROOM) ? thumb(TITLE_ROOM, 'Title screen') : '';
  let puzzles;
  if (data.puzzles.error) {
    puzzles = `<p class="warn">The puzzle chain could not be read: ${esc(data.puzzles.error)}.</p>`;
  } else {
    const rows = data.puzzles.rows.map((r) => `<tr class="ct" data-t="puzzle:${esc(r.n)}" data-kind="puzzle" data-label="${esc(`Puzzle ${r.n}: ${r.where}`)}">
<td data-h="#">${inlineMd(r.n)}</td><td data-h="Where">${inlineMd(r.where)}</td><td data-h="The problem">${inlineMd(r.problem)}</td><td data-h="The answer">${inlineMd(r.answer)}</td><td data-h="Points">${inlineMd(r.points)} <button class="cb" type="button" aria-label="Comment on this puzzle">+</button></td></tr>`).join('\n');
    puzzles = `<table class="puz"><thead><tr><th>#</th><th>Where</th><th>The problem</th><th>The answer</th><th>Points</th></tr></thead><tbody>
${rows}
</tbody></table>${data.puzzles.total ? `<p class="quiet">That is ${data.puzzles.total} points.</p>` : ''}`;
  }
  const problems = data.plan.problems.map((p) => `<p class="warn">${esc(p)}.</p>`).join('');
  return `<section id="overview" data-group="Whole game" data-order="-1">
<h2>Overview ${commentBox('game', 'game', 'The whole game', 'Comment on the whole game')}</h2>
<ul class="totals">
<li><b>${t.rooms}</b> rooms and a title</li><li><b>${t.sprites}</b> sprites (${t.cels} cels)</li><li><b>${t.lines}</b> lines of text</li>
<li><b>${t.words}</b> words in ${t.groups} groups</li><li><b>${t.bytes.toLocaleString('en-US')}</b> bytes of game data</li>
</ul>
${problems}
<h3>The map <span class="quiet">it wraps both ways</span></h3>
<div class="map">${grid}</div>
<h3>Interiors and the title</h3>
<div class="map">${inter}${title}</div>
<h3>The puzzle chain</h3>
${puzzles}
</section>`;
}

export function buildPage(data) {
  const css = readFileSync(join(HERE, 'review', 'page.css'), 'utf8');
  const js = readFileSync(join(HERE, 'review', 'page.js'), 'utf8');
  const safe = (s) => s.replace(/<\/(script|style)/gi, '<\\/$1').replace(/<!--/g, '<\\!--');
  const name = data.game.name || "Jr's Quest";
  const jump = [
    '<option value="">Jump to...</option>', '<option value="overview">Overview</option>',
    ...data.rooms.map((r) => `<option value="room-${r.n}">${r.n === TITLE_ROOM ? '' : `${r.n} `}${esc(r.name)}</option>`),
    ...data.shared.map((s) => `<option value="logic-${s.n}">${esc(s.name)}</option>`),
    '<option value="gallery">All sprites</option>', '<option value="words">Vocabulary</option>', '<option value="feedback">Feedback</option>',
  ].join('');
  const shared = data.shared.map((s) => `<section class="room" id="logic-${s.n}" data-group="${esc(`Shared: ${s.name}`)}" data-order="${200 + s.n}">
<h2>${esc(s.name)} <span class="quiet">logic ${s.n}</span> ${commentBox(`logic:${s.n}`, 'shared', `Shared: ${s.name}`, 'Comment on this part')}</h2>
<div class="script">${scriptHtml(s.script)}</div>
</section>`).join('\n');
  const vocab = data.vocabulary.map((g) => `<tr><td>${g.group}</td><td>${esc(g.words.join(', '))}</td></tr>`).join('');
  const flags = data.flags.map((f) => `<tr><td>${esc(f.name)}</td><td>${esc(f.note)}</td></tr>`).join('');
  const images = JSON.stringify(data.images).replace(/</g, '\\u003c');
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(name)}: review</title>
<style>
${safe(css)}
</style>
</head>
<body>
<header id="bar">
<a class="home" href="#overview">Review</a>
<select id="jump" aria-label="Jump to a room">${jump}</select>
<input id="find" type="search" placeholder="Search the text" aria-label="Search all the text" autocomplete="off">
<a class="fb" href="#feedback">Feedback <b id="count">0</b></a>
</header>
<main>
<h1>${esc(name)} <span class="quiet">review page</span></h1>
<p id="where" class="notice" role="status">Loading comments...</p>
<div id="offer" class="notice" hidden></div>
<p id="found" class="quiet" hidden></p>
${overviewHtml(data)}
${data.rooms.map((r) => roomHtml(r, data)).join('\n')}
<h2 class="part">Text shared by every room</h2>
${shared}
<section id="gallery" data-group="Sprites" data-order="900">
<h2>All sprites</h2>
<div class="sprites">${data.views.map(spriteHtml).join('\n')}</div>
</section>
<section id="words" data-group="Vocabulary" data-order="901">
<h2>Vocabulary</h2>
<details><summary>${data.totals.words} words in ${data.totals.groups} groups (every word in a group means the same thing to the game)</summary>
<table class="voc"><tbody>${vocab}</tbody></table></details>
<details><summary>What the flag names in the triggers mean</summary>
<p class="quiet">A trigger like "when gateOpen" names one of these. Tick "Show the plumbing" below to see where each is set.</p>
<table class="voc"><tbody>${flags}</tbody></table></details>
<p><label class="tick"><input type="checkbox" id="plumb"> Show the plumbing (flags set, rooms changed) between the lines</label></p>
</section>
<section id="feedback" data-group="Whole game" data-order="-1">
<h2>Feedback</h2>
<p class="row"><button type="button" id="copy">Copy all as text</button> <button type="button" id="download">Download JSON</button> <label class="tick"><input type="checkbox" id="hidedone"> Hide done</label></p>
<p id="copied" class="quiet" role="status"></p>
<textarea id="report" readonly hidden rows="10" aria-label="All comments as text"></textarea>
<div id="panel"></div>
</section>
</main>
<script type="application/json" id="images">${images}</script>
<script>
${safe(js)}
</script>
</body>
</html>
`;
}

export async function buildReview(buildDir, options = {}) {
  const build = resolve(buildDir);
  const game = resolve(options.game || join(dirname(build), 'game'));
  const docs = resolve(options.docs || dirname(game));
  const data = await gather(build, game, docs);
  return { html: buildPage(data), data };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const args = process.argv.slice(2);
  const at = args.indexOf('--game');
  const game = at >= 0 ? args.splice(at, 2)[1] : undefined;
  const [buildDir, outFile] = args;
  if (!buildDir || !outFile) {
    console.error('usage: node tools/review.js build build/review.html [--game game]');
    process.exit(2);
  }
  try {
    const { html, data } = await buildReview(buildDir, { game });
    writeFileSync(outFile, html);
    const t = data.totals;
    console.log(`${outFile}: ${html.length} bytes, ${t.rooms} rooms, ${t.sprites} sprites, ${t.lines} lines of text, one file, no network requests`);
  } catch (e) {
    console.error(e.message);
    process.exit(1);
  }
}
