// Tests for the JRQ interpreter (js/agi.js). Run: node --test test/
//
// The fixtures are built here with small encoders of our own, so these
// tests do not depend on the tools. One test at the end does use the
// tools, to run a game packed from test/fixture-interp/.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { Interpreter, KEY, missingCommands } from '../js/agi.js';
import { TESTS, ACTIONS, EXT_ACTIONS } from '../js/commands.js';
import { screenText, renderFrame } from '../js/render.js';
import { FONT } from '../js/font.js';
import { runScript } from '../tools/play.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const KEYTEXT = 'Avis Durgan';

// ---- minimal encoders ----

const actionOp = (name) => {
  const i = ACTIONS.findIndex((a) => a[0] === name);
  if (i >= 0) return i;
  const ext = Object.keys(EXT_ACTIONS).find((k) => EXT_ACTIONS[k][0] === name);
  if (ext === undefined) throw new Error(`no action ${name}`);
  return Number(ext);
};
const testOp = (name) => {
  const i = TESTS.findIndex((t) => t && t[0] === name);
  if (i < 0) throw new Error(`no test ${name}`);
  return i;
};
const w16 = (n) => [n & 255, (n >> 8) & 255];

const A = (name, ...args) => [actionOp(name), ...args];
const T = (name, ...args) => [testOp(name), ...args];
const SAID = (...groups) => [14, groups.length, ...groups.flatMap(w16)];
const NOT = (t) => [0xfd, ...t];
const OR = (...ts) => [0xfc, ...ts.flat(), 0xfc];
const GOTO = (offset) => [0xfe, ...w16(offset & 0xffff)];
function IF(tests, then, otherwise) {
  const t = then.flat();
  const e = otherwise ? otherwise.flat() : null;
  const body = e ? [...t, ...GOTO(e.length)] : t;
  return [0xff, ...tests.flat(), 0xff, ...w16(body.length), ...body, ...(e || [])];
}
const RETURN = [0];

function encodeLogic(code, messages = []) {
  const bytes = code.flat();
  const head = [...w16(bytes.length), ...bytes, messages.length];
  const offsets = [], text = [];
  const textStart = 2 + messages.length * 2;
  for (const m of messages) {
    offsets.push(textStart + text.length);
    for (const ch of m) text.push(ch.charCodeAt(0));
    text.push(0);
  }
  const plain = [...text];
  for (let i = 0; i < text.length; i++) text[i] = plain[i] ^ KEYTEXT.charCodeAt(i % KEYTEXT.length);
  return Uint8Array.from([...head, ...w16(textStart + text.length), ...offsets.flatMap(w16), ...text]);
}

// loops: [[{ w, h, color }]], every cel a solid block.
function encodeView(loops, desc = '') {
  const out = [1, 1, loops.length, 0, 0];
  for (let i = 0; i < loops.length; i++) out.push(0, 0);
  loops.forEach((cels, l) => {
    const at = out.length;
    out[5 + l * 2] = at & 255;
    out[6 + l * 2] = at >> 8;
    const blobs = cels.map((c) => {
      const b = [c.w, c.h, 0];
      for (let y = 0; y < c.h; y++) b.push((c.color << 4) | c.w, 0);
      return b;
    });
    out.push(cels.length);
    let off = 1 + cels.length * 2;
    for (const b of blobs) { out.push(...w16(off)); off += b.length; }
    for (const b of blobs) out.push(...b);
  });
  if (desc) {
    out[3] = out.length & 255;
    out[4] = out.length >> 8;
    for (const ch of desc) out.push(ch.charCodeAt(0));
    out.push(0);
  }
  return Uint8Array.from(out);
}

function encodeWords(groups) {
  const list = [];
  for (const g of Object.keys(groups)) for (const w of groups[g]) list.push([w, Number(g)]);
  list.sort((a, b) => (a[0] < b[0] ? -1 : 1));
  const out = new Array(52).fill(0);
  for (const [w, g] of list) {
    out.push(0);
    for (let i = 0; i < w.length; i++) out.push((w.charCodeAt(i) ^ 0x7f) | (i === w.length - 1 ? 0x80 : 0));
    out.push(g >> 8, g & 255);
  }
  out.push(0);
  return Uint8Array.from(out);
}

function encodeObject(items, max = 16) {
  const table = items.length * 3;
  const out = [...w16(table), max];
  const names = [];
  for (const it of items) {
    out.push(...w16(table + names.length), it.room);
    for (const ch of it.name) names.push(ch.charCodeAt(0));
    names.push(0);
  }
  const all = [...out, ...names];
  return Uint8Array.from(all.map((b, i) => b ^ KEYTEXT.charCodeAt(i % KEYTEXT.length)));
}

function encodeSound(voices) {
  const out = [0, 0, 0, 0, 0, 0, 0, 0];
  for (let v = 0; v < 4; v++) {
    out[v * 2] = out.length & 255;
    out[v * 2 + 1] = out.length >> 8;
    for (const n of voices[v] || []) out.push(...w16(n.dur), (n.div >> 4) & 0x3f, 0x80 | (n.div & 15), 0x90 | n.att);
    out.push(0xff, 0xff);
  }
  return Uint8Array.from(out);
}

const WORDS = { 0: ['the', 'a', 'at'], 1: ['anyword'], 9999: ['rol'], 20: ['look', 'examine'], 21: ['door'], 22: ['take', 'pick up'], 23: ['key'], 24: ['save'], 25: ['restore'] };

// { logics: { n: bytes }, pics, views, sounds } -> resources.
function makeGame({ logics = {}, pics = {}, views = {}, sounds = {}, items = [{ name: '?', room: 0 }, { name: 'key', room: 1 }], game = {} }) {
  const vol = [];
  const dir = (table) => {
    const d = new Uint8Array(768).fill(0xff);
    for (const n of Object.keys(table)) {
      const at = vol.length;
      d.set([at >> 16, (at >> 8) & 255, at & 255], n * 3);
      vol.push(0x12, 0x34, 0, ...w16(table[n].length), ...table[n]);
    }
    return d;
  };
  const logdir = dir(logics), picdir = dir(pics), viewdir = dir(views), snddir = dir(sounds);
  return {
    logdir, picdir, viewdir, snddir, vols: [Uint8Array.from(vol)],
    words: encodeWords(WORDS), object: encodeObject(items), game: { id: 'T', ...game },
  };
}

const EGO_VIEW = encodeView([[{ w: 6, h: 10, color: 12 }]]);
// A green wall line nothing may cross (priority 0) at y = 100, and a blue
// slab of priority 14 over rows 140 to 150, x 60 to 100.
const PIC = (() => {
  const b = [0xf0, 2, 0xf2, 0, 0xf6, 0, 100, 159, 100, 0xf0, 1, 0xf2, 14];
  for (let y = 140; y <= 150; y++) b.push(0xf6, 60, y, 100, y);
  b.push(0xff);
  return Uint8Array.from(b);
})();

// Logic 0 that sets up a room with ego at (x, y) on the first cycle.
function roomSetup(x, y, extra = []) {
  return IF([T('isset', 5)], [
    A('assignn', 60, 1), A('load.pic', 60), A('draw.pic', 60),
    A('animate.obj', 0), A('set.view', 0, 0), A('position', 0, x, y), A('draw', 0),
    A('show.pic'), ...extra,
  ]);
}

function boot(spec, host = {}) {
  return new Interpreter(makeGame(spec), host);
}

const run = (agi, n = 1) => { for (let i = 0; i < n; i++) agi.cycle(); };
const typeLine = (agi, text) => { for (const ch of text) agi.typeChar(ch); agi.enter(); };
const onScreen = (agi) => screenText(agi).join('\n');
const settle = () => new Promise((r) => setImmediate(r));
const pixel = (agi, x, y) => agi.screen[(y + 8) * 160 + x];

// ---- tests ----

test('every command in commands.js is implemented', () => {
  assert.deepEqual(missingCommands(), []);
});

test('the font covers ASCII 32 to 126 and the box characters', () => {
  for (let c = 33; c <= 126; c++) {
    let any = 0;
    for (let r = 0; r < 8; r++) any |= FONT[c * 8 + r];
    assert.ok(any, `glyph ${c} is blank`);
  }
  for (const c of [179, 191, 192, 196, 217, 218]) {
    let any = 0;
    for (let r = 0; r < 8; r++) any |= FONT[c * 8 + r];
    assert.ok(any, `box glyph ${c} is blank`);
  }
});

test('logic control flow: if, else, or, not, goto', () => {
  const loop = [...A('increment', 129), ...IF([T('lessn', 129, 3)], [GOTO(-12)])];
  assert.equal(loop.length, 12);
  const agi = boot({ logics: { 0: encodeLogic([
    A('assignn', 10, 5),
    IF([T('equaln', 10, 5)], [A('assignn', 121, 1)], [A('assignn', 121, 2)]),
    IF([T('equaln', 10, 6)], [A('assignn', 122, 1)], [A('assignn', 122, 2)]),
    IF([OR(T('equaln', 10, 9), T('equaln', 10, 5))], [A('assignn', 123, 1)]),
    IF([OR(T('equaln', 10, 9), T('equaln', 10, 8))], [A('assignn', 124, 1)]),
    IF([NOT(T('isset', 50))], [A('assignn', 125, 1)]),
    IF([T('equaln', 10, 5), NOT(T('greatern', 10, 7)), OR(T('isset', 50), T('lessn', 10, 6))], [A('assignn', 126, 1)]),
    IF([T('equaln', 10, 5), T('isset', 50)], [A('assignn', 130, 1)]),
    GOTO(3), A('assignn', 127, 9), A('assignn', 128, 1),
    loop,
    IF([T('equaln', 10, 5)], [IF([T('equaln', 121, 1)], [A('assignn', 131, 7)], [A('assignn', 131, 8)])]),
    RETURN,
    A('assignn', 132, 1),
  ]) } });
  run(agi);
  const v = agi.vars;
  assert.deepEqual(Array.from(v.slice(121, 133)),
    [1, 2, 1, 0, 1, 1, 0, 1, 3, 0, 7, 0]);
});

test('variables and flags', () => {
  const agi = boot({ logics: { 0: encodeLogic([
    IF([NOT(T('isset', 40))], [
      A('set', 40),
      A('assignn', 30, 250), A('addn', 30, 10),          // wraps to 4
      A('assignn', 31, 3), A('subn', 31, 5),             // wraps to 254
      A('assignn', 32, 255), A('increment', 32),         // stays 255
      A('assignn', 33, 0), A('decrement', 33),           // stays 0
      A('assignn', 34, 7), A('mul.n', 34, 6),            // 42
      A('assignn', 35, 47), A('div.n', 35, 5),           // 9
      A('assignn', 36, 90), A('lindirectn', 36, 77),     // v90 = 77
      A('rindirect', 37, 36),                            // v37 = v[v36] = 77
      A('assignv', 38, 37), A('addv', 38, 34),           // 119
      A('set', 41), A('toggle', 41), A('toggle', 42),
      A('assignn', 39, 43), A('set.v', 39),
      A('random', 10, 20, 44),
      A('div.n', 45, 0),
    ]),
    RETURN,
  ]) } });
  run(agi, 2);
  const v = agi.vars, f = agi.flags;
  assert.deepEqual([v[30], v[31], v[32], v[33], v[34], v[35], v[90], v[37], v[38]], [4, 254, 255, 0, 42, 9, 77, 77, 119]);
  assert.deepEqual([f[40], f[41], f[42], f[43]], [1, 0, 1, 1]);
  assert.ok(v[44] >= 10 && v[44] <= 20);
});

test('said matching, word groups, anyword, rest of line, unknown words', () => {
  const agi = boot({ logics: { 0: encodeLogic([
    IF([T('greatern', 9, 0)], [A('assignv', 70, 9)]),
    IF([SAID(20, 21)], [A('increment', 71)]),
    IF([SAID(22, 1)], [A('increment', 72)]),
    IF([SAID(20, 9999)], [A('increment', 73)]),
    IF([SAID(20)], [A('increment', 74)]),
    IF([T('isset', 2), NOT(T('isset', 4))], [A('increment', 75)]),
    RETURN,
  ]) } });
  const counts = () => Array.from(agi.vars.slice(70, 76));
  run(agi);
  typeLine(agi, 'Look at the DOOR!');
  run(agi);
  assert.deepEqual(counts(), [0, 1, 0, 0, 0, 0]);
  typeLine(agi, 'examine door');            // a synonym: same group
  run(agi);
  assert.deepEqual(counts(), [0, 2, 0, 0, 0, 0]);
  typeLine(agi, 'pick up the key');         // a two-word entry, then anyword
  run(agi);
  assert.deepEqual(counts(), [0, 2, 1, 0, 0, 0]);
  typeLine(agi, 'look key door key');       // only "rest of line" takes this
  run(agi);
  assert.deepEqual(counts(), [0, 2, 1, 1, 0, 0]);
  typeLine(agi, 'look');
  run(agi);
  // said(look, rol) comes first in the logic and also accepts a bare "look".
  assert.deepEqual(counts(), [0, 2, 1, 2, 0, 0]);
  typeLine(agi, 'door');                    // known word, nothing accepts it
  run(agi);
  assert.deepEqual(counts(), [0, 2, 1, 2, 0, 1]);
  typeLine(agi, 'look zebra');              // v9 = position of the unknown word
  run(agi);
  assert.equal(agi.vars[70], 2);
  run(agi);
  assert.equal(agi.flags[2], 0, 'the line is offered for one cycle only');
});

test('a message window blocks the logic and Enter resumes it', () => {
  const agi = boot({ logics: { 0: encodeLogic([
    IF([NOT(T('isset', 40))], [
      A('set', 40), A('assignn', 3, 12),
      A('print', 1), A('assignn', 50, 1), A('print', 2), A('assignn', 50, 2),
    ]),
    A('increment', 51),
    RETURN,
  ], ['Score %v3. This is a long enough line that it has to wrap to fit.', 'Second %m3.', 'box']) } });
  run(agi);
  assert.equal(agi.vars[50], 0);
  assert.equal(agi.vars[51], 0, 'the rest of the cycle waits too');
  const text = onScreen(agi);
  assert.match(text, /Score 12\. This is a long/);
  assert.ok(screenText(agi).filter((r) => r.trim()).length >= 3, 'wrapped onto several rows');
  // The border is drawn in box characters around white cells.
  const row = agi.text.findIndex((r) => r.some((c) => c.ch === 218));
  assert.ok(row >= 0);
  assert.equal(agi.text[row + 1].find((c) => c.ch === 83).bg, 15);
  run(agi, 5);
  assert.equal(agi.vars[50], 0, 'cycles do nothing while it waits');
  const h = agi.hash();
  run(agi, 3);
  assert.equal(agi.hash(), h, 'and the state does not drift');
  agi.keyDown(KEY.UP);                       // not a key that closes it
  run(agi);
  assert.equal(agi.vars[50], 0);
  agi.keyDown(KEY.ENTER);
  run(agi);
  assert.equal(agi.vars[50], 1);
  assert.match(onScreen(agi), /Second box\./);
  agi.keyDown(KEY.ESC);
  run(agi);
  assert.equal(agi.vars[50], 2);
  assert.equal(agi.vars[51], 1);
  assert.doesNotMatch(onScreen(agi), /Second/);
  assert.ok(!agi.text.some((r) => r.some((c) => c.ch === 218)), 'the window is gone');
});

test('typing shows at once and waits its turn while a window is up', () => {
  const agi = boot({ logics: { 0: encodeLogic([
    IF([NOT(T('isset', 40))], [A('set', 40), A('print', 1)]),
    IF([SAID(20, 21)], [A('increment', 71)]),
    RETURN,
  ], ['Hello.']) } });
  run(agi);
  for (const ch of 'look door') agi.typeChar(ch);
  assert.match(screenText(agi)[22], /look door_/, 'shown before any cycle runs');
  agi.enter();                               // this Enter closes the window
  run(agi);
  assert.equal(agi.vars[71], 0);
  assert.match(screenText(agi)[22], /look door_/, 'the typed line is still there');
  agi.enter();
  assert.doesNotMatch(screenText(agi)[22], /look/, 'Enter is acknowledged at once');
  run(agi);
  assert.equal(agi.vars[71], 1);
  // Two lines sent between cycles are both parsed, one a cycle.
  typeLine(agi, 'look door');
  typeLine(agi, 'look door');
  run(agi, 2);
  assert.equal(agi.vars[71], 3);
});

test('ego walks on an arrow key, stops at a block line, stops on the same key', () => {
  const agi = boot({ logics: { 0: encodeLogic([roomSetup(80, 120), RETURN]) }, pics: { 1: PIC }, views: { 0: EGO_VIEW } });
  run(agi, 2);
  const ego = agi.objs[0];
  assert.deepEqual([ego.x, ego.y], [80, 120]);
  assert.equal(pixel(agi, 82, 118), 12, 'ego is drawn');
  agi.keyDown(KEY.UP);
  run(agi, 5);
  assert.equal(ego.y, 115);
  assert.equal(agi.vars[6], 1);
  run(agi, 40);
  assert.equal(ego.y, 101, 'stopped just below the priority 0 line');
  assert.equal(pixel(agi, 10, 100), 2, 'the line is still on screen');
  agi.keyDown(KEY.UP);
  run(agi);
  assert.equal(agi.vars[6], 0, 'the same arrow again stops ego');
  agi.keyDown(KEY.RIGHT);
  run(agi, 3);
  assert.deepEqual([ego.x, ego.y], [83, 101]);
  agi.keyDown(KEY.DOWN);                     // a different arrow turns
  run(agi, 4);
  assert.deepEqual([ego.x, ego.y], [83, 105]);
  // The old spot was restored from the save-under.
  assert.equal(pixel(agi, 82, 98), 15);
  // The right edge sets v2.
  agi.keyDown(KEY.RIGHT);
  run(agi, 100);
  assert.equal(ego.x, 154);
  assert.equal(agi.vars[2], 2);
});

test('ego is hidden behind a higher priority band and shows above it', () => {
  const agi = boot({ logics: { 0: encodeLogic([roomSetup(70, 145), RETURN]) }, pics: { 1: PIC }, views: { 0: EGO_VIEW } });
  run(agi, 2);
  const ego = agi.objs[0];
  assert.equal(ego.priority, 13, 'band from y');
  assert.equal(pixel(agi, 72, 143), 1, 'the slab (priority 14) covers ego');
  assert.equal(pixel(agi, 72, 138), 12, 'ego shows above the slab');
  assert.equal(pixel(agi, 72, 152), 15);
});

test('a fixed priority above the band brings ego in front', () => {
  const agi = boot({
    logics: { 0: encodeLogic([roomSetup(70, 145, [A('set.priority', 0, 15)]), RETURN]) },
    pics: { 1: PIC }, views: { 0: EGO_VIEW },
  });
  run(agi, 2);
  assert.equal(pixel(agi, 72, 143), 12);
});

test('new.room runs logic 0 again in the same cycle', () => {
  const agi = boot({ logics: {
    0: encodeLogic([
      A('increment', 80),
      IF([T('isset', 5)], [A('increment', 81)]),
      IF([T('equaln', 0, 0)], [A('new.room', 2), A('assignn', 82, 9)]),
      A('call.v', 0),
      RETURN,
    ]),
    2: encodeLogic([
      IF([T('isset', 5)], [A('increment', 83)]),
      A('increment', 84),
      IF([T('equaln', 84, 3)], [A('new.room', 3)]),
      RETURN,
    ]),
    3: encodeLogic([A('increment', 85), RETURN]),
  } });
  run(agi);
  const v = agi.vars;
  assert.deepEqual([v[0], v[1], v[80], v[81], v[82], v[83], v[84]], [2, 0, 2, 2, 0, 1, 1]);
  assert.equal(agi.flags[5], 0, 'flag 5 is cleared at the end of the cycle');
  run(agi);
  assert.deepEqual([v[81], v[83], v[84]], [2, 1, 2]);
  run(agi);
  assert.deepEqual([v[0], v[1], v[85]], [3, 2, 1]);
});

test('get.string, str.date and display on a text screen', () => {
  const agi = boot({ logics: { 0: encodeLogic([
    IF([NOT(T('isset', 40))], [
      A('set', 40), A('text.screen'), A('display', 1, 0, 2),
      A('get.string', 1, 1, 3, 0, 12), A('str.date', 1, 100), A('increment', 90),
      A('get.string', 1, 1, 5, 0, 12), A('str.date', 1, 110),
    ]),
    RETURN,
  ], ['Date: ', 'Current date is Tue  1-01-1980']) } });
  run(agi);
  assert.match(screenText(agi)[1], /^Current date is Tue {2}1-01-1980/);
  assert.match(screenText(agi)[3], /^Date: _/);
  for (const ch of '10-05-27') agi.typeChar(ch);
  agi.keyDown(KEY.BACKSPACE);
  agi.typeChar('6');
  assert.match(screenText(agi)[3], /^Date: 10-05-26_/, 'typed characters show without a cycle');
  run(agi, 3);
  assert.equal(agi.vars[90], 0);
  agi.enter();
  run(agi);
  assert.equal(agi.vars[90], 1);
  assert.equal(agi.strings[1], '10-05-26');
  assert.equal(agi.vars[100], 0, 'a two-digit year below 80 is invalid, as on DOS');
  typeLine(agi, ' 10/05/2026 ');
  run(agi);
  assert.deepEqual(Array.from(agi.vars.slice(110, 114)), [1, 10, 5, 46]);
});

test('str.date accepts and rejects what DOS does', () => {
  const cases = [
    ['1-01-80', [1, 1, 1, 0]], ['12.31.99', [1, 12, 31, 19]], ['2-30-2099', [1, 2, 30, 119]],
    ['10-05-26', null], ['13-01-1990', null], ['0-10-1990', null], ['4-32-1990', null],
    ['4-5-1979', null], ['4-5-2100', null], ['4-5', null], ['', null], ['a-b-c', null], ['1-2-3-4', null],
  ];
  for (const [text, want] of cases) {
    const agi = boot({ logics: { 0: encodeLogic([A('set.string', 2, 1), A('str.date', 2, 100), RETURN], [text || ' ']) } });
    agi.vars[101] = 99;
    run(agi);
    assert.deepEqual(Array.from(agi.vars.slice(100, 104)), want || [0, 99, 0, 0], `"${text}"`);
  }
});

test('inventory: get, has, drop, put, obj.in.room and the status screen', () => {
  const agi = boot({ logics: { 0: encodeLogic([
    IF([NOT(T('isset', 40))], [
      A('set', 40),
      IF([T('has', 1)], [A('assignn', 90, 9)]),
      A('assignn', 60, 1), IF([T('obj.in.room', 1, 60)], [A('assignn', 91, 1)]),
      A('get', 1), IF([T('has', 1)], [A('assignn', 92, 1)]),
      A('status'), A('assignn', 93, 1),
      A('drop', 1), IF([NOT(T('has', 1))], [A('assignn', 94, 1)]),
      A('assignn', 61, 7), A('put', 1, 61), A('assignn', 62, 1), A('get.room.v', 62, 95),
    ]),
    RETURN,
  ]) } });
  run(agi);
  assert.match(onScreen(agi), /You are carrying:/);
  assert.match(onScreen(agi), /key/);
  assert.equal(agi.vars[93], 0);
  agi.typeChar(' ');
  run(agi);
  assert.deepEqual(Array.from(agi.vars.slice(90, 96)), [0, 1, 1, 1, 1, 7]);
  assert.doesNotMatch(onScreen(agi), /carrying/);
});

test('move.obj sets its flag on arrival and end.of.loop sets its flag', () => {
  const view = encodeView([[{ w: 4, h: 4, color: 9 }, { w: 4, h: 4, color: 10 }, { w: 4, h: 4, color: 11 }]]);
  const agi = boot({ logics: { 0: encodeLogic([
    IF([T('isset', 5)], [
      A('assignn', 60, 1), A('draw.pic', 60),
      A('animate.obj', 1), A('set.view', 1, 0), A('position', 1, 20, 130), A('draw', 1),
      A('move.obj', 1, 50, 140, 2, 100), A('stop.cycling', 1), A('show.pic'),
    ]),
    IF([T('isset', 100), NOT(T('isset', 102))], [A('set', 102), A('set.cel', 1, 0), A('end.of.loop', 1, 101)]),
    RETURN,
  ]) }, pics: { 1: PIC }, views: { 0: view } });
  run(agi, 2);
  const o = agi.objs[1];
  assert.equal(agi.flags[100], 0);
  assert.equal(o.stepSize, 2);
  run(agi, 30);
  assert.equal(agi.flags[100], 1);
  assert.ok(Math.abs(o.x - 50) < 2 && Math.abs(o.y - 140) < 2, `arrived near the target: ${o.x},${o.y}`);
  assert.equal(o.stepSize, 1, 'the old step size comes back');
  run(agi, 10);
  assert.equal(agi.flags[101], 1);
  assert.equal(o.cel, 2, 'stopped on the last cel');
});

test('sound: the host gets note events and the done flag follows the length', () => {
  const played = [];
  const snd = encodeSound([[{ dur: 30, div: 254, att: 2 }, { dur: 30, div: 127, att: 15 }], [], [], [{ dur: 10, div: 4, att: 0 }]]);
  const agi = boot({
    logics: { 0: encodeLogic([IF([NOT(T('isset', 40))], [A('set', 40), A('sound', 1, 99)]), RETURN]) },
    sounds: { 1: snd },
  }, { sound: (events) => played.push(events) });
  run(agi);
  assert.equal(played.length, 1);
  assert.deepEqual(played[0][0], { t: 0, voice: 0, dur: 30, div: 254, att: 2 });
  assert.deepEqual(played[0].map((e) => e.voice), [0, 3, 0]);
  assert.equal(agi.flags[99], 0);
  // 60 ticks at 6 a cycle (v10 = 2): done at the end of the tenth cycle.
  run(agi, 8);
  assert.equal(agi.flags[99], 0);
  run(agi, 1);
  assert.equal(agi.flags[99], 1);
});

test('speak and talk: no host finishes at once, a host is waited for', async () => {
  const code = encodeLogic([
    IF([NOT(T('isset', 40))], [A('set', 40), A('speak', 3), A('assignn', 90, 1), A('talk', 1), A('assignn', 91, 1)]),
    RETURN,
  ], ['You are the Mirror.']);
  const plain = boot({ logics: { 0: code } });
  run(plain);
  assert.deepEqual([plain.vars[90], plain.vars[91], plain.flags[28], plain.flags[29]], [1, 1, 0, 1]);

  let endSpeech, seen;
  const agi = boot({ logics: { 0: code } }, {
    speak: (n) => new Promise((r) => { endSpeech = () => r(n); }),
    talk: async (n, facts, io) => {
      seen = { n, prompt: facts.prompt, score: facts.score };
      await io.print('Hello?');
      seen.answer = await io.ask('>');
      return true;
    },
  });
  run(agi, 3);
  assert.equal(agi.vars[90], 0, 'waiting on the clip');
  assert.ok(agi.pending);
  endSpeech();
  await settle();
  run(agi);
  assert.equal(agi.vars[90], 1);
  assert.match(onScreen(agi), /Hello\?/);
  agi.enter();
  run(agi);
  await settle();
  run(agi);
  typeLine(agi, 'who are you');
  run(agi);
  await settle();
  run(agi);
  assert.deepEqual(seen, { n: 1, prompt: 'You are the Mirror.', score: 0, answer: 'who are you' });
  assert.deepEqual([agi.vars[91], agi.flags[28], agi.flags[29]], [1, 1, 0]);
});

test('the AI parser hook: asked only on a miss, its answer is parsed once', async () => {
  const code = encodeLogic([
    IF([T('greatern', 9, 0)], [A('increment', 70)]),
    IF([SAID(20, 21)], [A('increment', 71)]),
    IF([SAID(22, 23)], [A('increment', 72)]),
    A('increment', 73),
    RETURN,
  ]);
  const asked = [];
  let answer = 'look door';
  const agi = boot({ logics: { 0: code } }, {
    resolve: async (text, patterns) => { asked.push({ text, patterns: patterns.map((p) => p.text) }); return answer; },
  });
  run(agi);
  typeLine(agi, 'look door');
  run(agi);
  assert.equal(asked.length, 0, 'a line the classic parser takes never reaches the host');
  assert.equal(agi.vars[71], 1);

  typeLine(agi, 'peruse the entrance');
  const before = agi.vars[73];
  run(agi);
  assert.equal(asked.length, 1);
  assert.deepEqual(asked[0], { text: 'peruse the entrance', patterns: ['examine door', 'pick up key'] });
  assert.match(screenText(agi)[23], /Thinking/);
  for (const ch of 'take') agi.typeChar(ch);
  assert.match(screenText(agi)[22], /take_/, 'typing stays live while it waits');
  run(agi, 2);
  assert.equal(agi.vars[73], before, 'the game holds still while the host thinks');
  await settle();
  run(agi);
  assert.deepEqual([agi.vars[70], agi.vars[71]], [0, 2], 'the answer went through said()');
  assert.doesNotMatch(screenText(agi)[23], /Thinking/);
  assert.equal(agi.aiHelps, 1);

  // Known words that no said() could accept are a miss too.
  answer = null;
  for (let i = 0; i < 4; i++) agi.keyDown(KEY.BACKSPACE);
  typeLine(agi, 'door key');
  run(agi);
  assert.equal(asked.length, 2);
  await settle();
  run(agi);
  assert.equal(agi.flags[2], 1, 'no answer: the line goes to the game as typed');
  assert.equal(asked.length, 2, 'and is not asked about twice');

  // No answer for an unknown word: classic behavior, v9 set.
  typeLine(agi, 'look zebra');
  run(agi);
  await settle();
  run(agi);
  assert.equal(agi.vars[70], 1);

  // Without host.resolve nothing waits.
  const classic = boot({ logics: { 0: code } });
  run(classic);
  typeLine(classic, 'peruse the entrance');
  run(classic);
  assert.equal(classic.vars[70], 1);
  assert.equal(classic.wait, null);
});

test('snapshot and restore give the same hash, and the same future', () => {
  const spec = {
    logics: { 0: encodeLogic([
      roomSetup(80, 130, [A('animate.obj', 1), A('set.view', 1, 0), A('position', 1, 30, 150), A('draw', 1), A('wander', 1), A('status.line.on')]),
      IF([SAID(20, 21)], [A('print', 1), A('addn', 3, 1)]),
      RETURN,
    ], ['A door.']) },
    pics: { 1: PIC }, views: { 0: EGO_VIEW },
  };
  const agi = boot(spec);
  run(agi, 5);
  agi.keyDown(KEY.LEFT);
  run(agi, 10);
  typeLine(agi, 'look door');
  run(agi, 2);                               // a window is up: saved mid-wait
  for (const ch of 'exa') agi.typeChar(ch);
  const snap = JSON.parse(JSON.stringify(agi.snapshot()));
  const h0 = agi.hash();
  const future = () => {
    const hashes = [];
    agi.keyDown(KEY.ENTER);
    for (let i = 0; i < 40; i++) {
      if (i === 5) agi.keyDown(KEY.DOWN);
      agi.cycle();
      hashes.push(agi.hash());
    }
    return hashes;
  };
  const first = future();
  assert.notEqual(agi.hash(), h0);
  agi.restore(snap);
  assert.equal(agi.hash(), h0);
  assert.match(onScreen(agi), /A door\./);
  assert.deepEqual(future(), first);
  // A second interpreter can pick the snapshot up too.
  const other = boot(spec);
  other.restore(snap);
  assert.equal(other.hash(), h0);
});

test('save.game and restore.game go through the host', () => {
  const slots = new Map();
  const agi = boot({
    logics: { 0: encodeLogic([
      roomSetup(80, 130),
      IF([SAID(24)], [A('save.game')]),
      IF([SAID(25)], [A('restore.game')]),
      IF([T('isset', 12)], [A('increment', 96)]),
      RETURN,
    ]) },
    pics: { 1: PIC }, views: { 0: EGO_VIEW },
  }, { save: (slot, obj) => { slots.set(slot, JSON.stringify(obj)); return true; }, load: (slot) => (slots.has(slot) ? JSON.parse(slots.get(slot)) : null) });
  run(agi, 3);
  typeLine(agi, 'save');
  run(agi);
  assert.match(onScreen(agi), /Save game/);
  agi.typeChar('2');
  run(agi);
  assert.match(onScreen(agi), /Game saved\./);
  agi.enter();
  run(agi);
  assert.ok(slots.has(2));
  agi.keyDown(KEY.DOWN);
  run(agi, 10);
  assert.equal(agi.objs[0].y, 140);
  typeLine(agi, 'restore');
  run(agi);
  agi.typeChar('5');
  run(agi);
  assert.match(onScreen(agi), /no saved game/);
  agi.enter();
  run(agi);
  typeLine(agi, 'restore');
  run(agi);
  agi.typeChar('2');
  run(agi);
  assert.equal(agi.objs[0].y, 130, 'ego is back where the game was saved');
  assert.equal(agi.vars[96], 1, 'flag 12 was set for one cycle');
  assert.equal(pixel(agi, 82, 128), 12);
});

test('determinism: the same script twice gives the same hashes', async () => {
  const spec = {
    logics: { 0: encodeLogic([
      roomSetup(80, 130, [A('animate.obj', 1), A('set.view', 1, 0), A('position', 1, 30, 150), A('draw', 1), A('wander', 1)]),
      IF([SAID(20, 21)], [A('print', 1)]),
      A('random', 0, 255, 97),
      RETURN,
    ], ['A door.']) },
    pics: { 1: PIC }, views: { 0: EGO_VIEW },
  };
  const script = '5\nhash\nkey left\n12\nhash\ntype look door\n2\nexpect A door.\nhash\nkey enter\nkey up\n30\nhash\n';
  const a = await runScript(boot(spec), script, { every: 7 });
  const b = await runScript(boot(spec), script, { every: 7 });
  assert.ok(a.hashes.length >= 10);
  assert.deepEqual(a.hashes, b.hashes);
  assert.ok(new Set(a.hashes).size >= 8, 'and the hash does change as the game moves');
  await assert.rejects(runScript(boot(spec), '2\nexpect No such text\n'), /expected/);
});

test('rendered frames are 320x200 with the text grid on top', () => {
  const agi = boot({ logics: { 0: encodeLogic([roomSetup(80, 130, [A('status.line.on')]), RETURN]) }, pics: { 1: PIC }, views: { 0: EGO_VIEW } });
  run(agi, 2);
  const frame = renderFrame(agi);
  assert.equal(frame.length, 320 * 200);
  assert.equal(frame[(100 + 8) * 320 + 40], 2, 'the picture is doubled across');
  assert.equal(frame[(100 + 8) * 320 + 41], 2);
  const statusRow = Array.from(frame.slice(0, 320 * 8));
  assert.ok(statusRow.includes(15) && statusRow.includes(0), 'the status line is black on white');
  assert.match(screenText(agi)[0], /Score:0 of 0 +Sound:on/);
});

test('a game packed by the tools runs (test/fixture-interp)', async (t) => {
  if (!existsSync(join(HERE, '..', 'tools', 'pack.js'))) { t.skip('tools/pack.js is not there yet'); return; }
  const { buildGame } = await import('../tools/pack.js');
  const built = await buildGame(join(HERE, 'fixture-interp'));
  const f = built.files;
  const resources = () => ({
    logdir: f.LOGDIR, picdir: f.PICDIR, viewdir: f.VIEWDIR, snddir: f.SNDDIR, vols: [f['VOL.0']],
    words: f['WORDS.TOK'], object: f.OBJECT, game: JSON.parse(new TextDecoder().decode(f['game.json'])),
  });
  const script = [
    '3', 'expect Score:0 of 10', 'key up', '40', 'hash',
    'type look at the door', '1', 'expect A plain door. You have 0 of 10 points.', 'key enter', '1',
    'type pick up key', '1', 'expect Taken.', 'key enter', '2', 'expect Score:5 of 10',
    'type take key', '1', 'expect You have it.', 'key enter', '1',
    'type dance', '1', 'expect Bad command or file name.', 'key enter', '2', 'hash',
  ].join('\n');
  const agi = new Interpreter(resources());
  const a = await runScript(agi, script);
  assert.equal(agi.objs[0].y, 101, 'ego walked up to the wall line and stopped');
  assert.equal(agi.vars[3], 5);
  const b = await runScript(new Interpreter(resources()), script);
  assert.deepEqual(a.hashes, b.hashes);
});
