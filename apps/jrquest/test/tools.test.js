// Round-trip tests for the tools: node --test test/

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, existsSync, readdirSync, writeFileSync, mkdirSync, cpSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

import { parseWords, encodeWords, decodeWords, wordIndex } from '../tools/words.js';
import { parseObjects, encodeObjects, decodeObjects } from '../tools/objects.js';
import { parseView, encodeView, decodeView, viewToText, contactSheet } from '../tools/view.js';
import { parseSound, encodeSound, decodeSound } from '../tools/snd.js';
import { compileLogic, decodeLogic, disassemble, encodeLogic } from '../tools/logic.js';
import { buildGame, pack } from '../tools/pack.js';
import { TEST_BY_NAME, ACTION_BY_NAME } from '../tools/argtypes.js';
import { TESTS, ACTIONS, EXT_ACTIONS } from '../js/commands.js';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const game = join(here, 'fixture-game');
const read = (...p) => readFileSync(join(here, ...p), 'utf8');
const words = parseWords(read('fixture-game', 'words.txt'));
const objects = parseObjects(read('fixture-game', 'objects.txt'));
const compile = (src, opts = {}) => compileLogic(src, { file: 'x.lgc', words, objects, ...opts });
const listing = (src, opts) => disassemble(compile(src, opts).bytes).lines;

// ---- words ----

test('words: every word comes back with its group', () => {
  const back = decodeWords(encodeWords(words));
  assert.equal(back.length, words.words.size);
  for (const { word, group } of back) assert.equal(words.words.get(word), group, word);
  const sorted = back.map((w) => w.word);
  assert.deepEqual(sorted, [...sorted].sort());
  assert.ok(sorted.includes('pick up'));
});

test('words: prefix compression and the letter index', () => {
  const bytes = encodeWords(parseWords('5: gate, gates, game\n300: zap\n'));
  // game, gate, gates, zap
  const x = (s) => [...s].map((c, i) => (c.charCodeAt(0) ^ 0x7F) | (i === s.length - 1 ? 0x80 : 0));
  assert.deepEqual([...bytes.slice(52)], [
    0, ...x('game'), 0, 5,
    2, ...x('te'), 0, 5,
    4, ...x('s'), 0, 5,
    0, ...x('zap'), 1, 44,     // 300, big-endian
    0,
  ]);
  const index = wordIndex(bytes);
  assert.equal(index['g'.charCodeAt(0) - 97], 52);
  assert.equal(index['z'.charCodeAt(0) - 97], 52 + 7 + 5 + 4);
  assert.equal(index.filter((o) => o !== 0).length, 2);
  assert.deepEqual(decodeWords(bytes).map((w) => w.word), ['game', 'gate', 'gates', 'zap']);
});

test('words: errors name the line', () => {
  assert.throws(() => parseWords('1: a\n2: b, a\n', 'w.txt'), /^Error: w\.txt:2: word "a" is already in group 1/);
  assert.throws(() => parseWords('1: Look\n', 'w.txt'), /w\.txt:1: /);
  assert.throws(() => parseWords('look\n', 'w.txt'), /w\.txt:1: /);
});

// ---- objects ----

test('objects: round trip, encrypted, shared names', () => {
  const bytes = encodeObjects(objects);
  assert.deepEqual(decodeObjects(bytes), {
    max: 12,
    items: [
      { name: '?', room: 0 }, { name: 'brass key', room: 3 },
      { name: 'floppy disk', room: 255 }, { name: '?', room: 0 },
    ],
  });
  // First word is the table size (4 items of 3 bytes), XORed with "Av".
  assert.equal(bytes[0], 12 ^ 0x41);
  assert.equal(bytes[1], 0 ^ 0x76);
  assert.equal(bytes[2], 12 ^ 0x69);
  // "?" is stored once: 3 header + 12 table + 2 + 10 + 12.
  assert.equal(bytes.length, 3 + 12 + 2 + 10 + 12);
  assert.equal(parseObjects('0: ?\n').max, 16);
});

test('objects: the first item must be "0: ?"', () => {
  assert.throws(() => parseObjects('3: key\n', 'o.txt'), /o\.txt:1: the first item/);
  assert.throws(() => parseObjects('0: ?\nmax: 3\n', 'o.txt'), /o\.txt:2: /);
});

// ---- view ----

test('view: round trip through the binary, with a mirror loop', () => {
  const src = read('fixture-game', 'view', '000_ego.view');
  const view = parseView(src);
  const bytes = encodeView(view);
  const back = decodeView(bytes);
  assert.equal(back.desc, 'Sir Junior');
  assert.equal(back.loops.length, 2);
  assert.equal(back.loops[1].mirrorOf, 0);
  // Loop 1 decodes as loop 0 flipped left to right.
  assert.deepEqual([...back.loops[1].cels[1].pixels], [3, 3, 2, 1, 1, 2, 3, 3]);
  assert.deepEqual([...back.loops[0].cels[1].pixels], [1, 2, 3, 3, 3, 3, 2, 1]);
  assert.deepEqual([...back.loops[0].cels[0].pixels], [...view.loops[0].cels[0].pixels]);
  // And the text comes back the same (comments aside).
  assert.equal(viewToText(back), src.replace(/\s+#.*/g, ''));
});

test('view: exact bytes', () => {
  const bytes = encodeView(parseView('loop 0\ncel 5 2 trans=A\n00B..\n.....\nloop 1 mirror 0\n'));
  assert.deepEqual([...bytes], [
    1, 1, 2, 0, 0,     // two loops, no description
    9, 0, 9, 0,        // both loops share the data at 9
    1, 3, 0,           // one cel, 3 bytes into the loop
    5, 2, 0x8A,        // mirrored, stored for loop 0, transparent color A
    0x02, 0xB1, 0,     // two black, one B, trailing transparent left out
    0,
  ]);
  // Long runs split at 15.
  const wide = encodeView(parseView(`loop 0\ncel 20 1 trans=0\n${'7'.repeat(20)}\n`));
  assert.deepEqual([...wide.slice(-3)], [0x7F, 0x75, 0]);
});

test('view: errors and the contact sheet', () => {
  assert.throws(() => parseView('loop 0\ncel 3 1\n12\n', 'v.view'), /v\.view:3: cel row 0 must be exactly 3/);
  assert.throws(() => parseView('loop 0\ncel 2 1 trans=1\n12\n', 'v.view'), /v\.view:3: color 1 is this cel's transparent/);
  assert.throws(() => parseView('loop 1\n', 'v.view'), /v\.view:1: expected loop 0/);
  assert.throws(() => parseView('loop 0\ncel 1 1\n5\nloop 1 mirror 3\n', 'v.view'), /v\.view:4: /);
  const sheet = contactSheet(parseView(read('fixture-game', 'view', '000_ego.view')));
  assert.equal(sheet.pixels.length, sheet.width * sheet.height);
  assert.equal(sheet.width, 4 + 8 * 4 + 4 + 4 * 4 + 4);
  assert.equal(sheet.height, 4 + (3 * 2 + 4) * 2);
});

// ---- sound ----

test('sound: round trip and exact bytes', () => {
  const voices = parseSound('tempo 120\nvoice 1\nvol 12 a4/4 r/8 | c5/8.\nvoice noise\nw2/2 p0/4\n');
  const bytes = encodeSound(voices);
  assert.deepEqual(decodeSound(bytes), voices);
  assert.deepEqual(voices[0], [
    { duration: 30, divisor: 254, attenuation: 3 },     // 111861 / 440
    { duration: 15, divisor: 0, attenuation: 15 },
    { duration: 23, divisor: 214, attenuation: 3 },     // ends at 67.5 ticks, rounded to 68
  ]);
  assert.deepEqual(voices[3], [
    { duration: 60, divisor: 6, attenuation: 0 },
    { duration: 30, divisor: 0, attenuation: 0 },
  ]);
  const at = bytes[0] | (bytes[1] << 8);
  assert.equal(at, 8);
  assert.deepEqual([...bytes.slice(8, 13)], [30, 0, 0x0F, 0x8E, 0x93]);
  // Voices 2 and 3 are empty: just the end mark.
  assert.deepEqual([...bytes.slice(bytes[2] | (bytes[3] << 8)).slice(0, 2)], [0xFF, 0xFF]);
  assert.deepEqual([...bytes.slice(-7)], [30, 0, 0, 0xE0, 0xF0, 0xFF, 0xFF]);
});

test('sound: voices stay in step, and errors', () => {
  // Three triplet-ish lengths that do not divide evenly must still add up.
  const v = parseSound('tempo 132\nvoice 1\nc4/16 c4/16 c4/16 c4/16\nvoice 2\nc3/4\n');
  const sum = (notes) => notes.reduce((t, n) => t + n.duration, 0);
  assert.equal(sum(v[0]), sum(v[1]));
  assert.throws(() => parseSound('voice 1\nh4/4\n', 's.snd'), /s\.snd:2: not a note: h4\/4/);
  assert.throws(() => parseSound('c4/4\n', 's.snd'), /s\.snd:1: notes before/);
  assert.throws(() => parseSound('voice 1\nc1/4\n', 's.snd'), /s\.snd:2: c1 is out of/);
  assert.throws(() => parseSound('voice noise\nc4/4\n', 's.snd'), /s\.snd:2: /);
});

// ---- argument types ----

test('argtypes: one entry for every command in commands.js', () => {
  assert.equal(TEST_BY_NAME.size, TESTS.length - 1);
  assert.equal(ACTION_BY_NAME.size, ACTIONS.length + Object.keys(EXT_ACTIONS).length);
  assert.equal(ACTION_BY_NAME.get('speak').op, 200);
  assert.equal(ACTION_BY_NAME.get('move.obj').types, 'onnnf');
});

// ---- logic ----

test('logic: every syntax feature, by disassembly', () => {
  const path = join(here, 'fixture-src', 'every.lgc');
  const r = compileLogic(readFileSync(path, 'utf8'), { file: path, words, objects });
  const d = disassemble(r.bytes);
  assert.deepEqual(d.lines, [
    '0000 if (isset(f5)) else 000B',
    '0006 load.pic(v0)',
    '0008 draw.pic(v0)',
    '000A show.pic()',
    '000B if (said(20, 100) && !isset(f40)) else 001F',
    '0018 print(m1)',
    '001A set(f40)',
    '001C addn(v3, 8)',
    '001F if ((said(20) || said(20, 103))) else 0031',
    '002F print(m3)',
    '0031 if (said(21, 102, 9999) && !has(i1)) else 0045',
    '0040 get(i1)',
    '0042 goto 0047',
    '0045 print(m3)',
    '0047 if (equaln(v1, 3) && !equalv(v1, v2) && lessn(v1, 4) && greaterv(v1, v2) && !greatern(v1, 5) && !lessv(v1, v2)) else 0082',
    '0060 assignn(v1, 3)',
    '0063 assignv(v1, v2)',
    '0066 addn(v1, 2)',
    '0069 subv(v1, v2)',
    '006C mul.n(v1, 2)',
    '006F div.n(v1, 2)',
    '0072 increment(v1)',
    '0074 decrement(v1)',
    '0076 addv(v1, v2)',
    '0079 subn(v1, 1)',
    '007C mul.v(v1, v2)',
    '007F div.v(v1, v2)',
    // !(f1 && f2) becomes an OR group of two negated tests.
    '0082 if ((!isset(f1) || !isset(f2)) && (isset(f3) || !isset(f4) || equaln(v9, 1)) && controller(c7) && obj.in.room(i2, v0)) else 00A0',
    '009D goto 00D1',
    '00A0 if (posn(o0, 1, 2, 3, 4)) else 00B9',
    '00AA if (isset(f7)) else 00B6',
    '00B0 set.view(o0, 2)',
    '00B3 goto 00B9',
    '00B6 goto 00A0',          // a backward goto
    '00B9 print(m2)',
    '00BB print(m4)',
    '00BD display(1, 2, m2)',  // the same text reuses message 2
    '00C1 set.string(s1, m5)',
    '00C4 word.to.string(s1, w1)',
    '00C7 move.obj(o0, 10, 20, 1, f41)',
    '00CD talk(2)',
    '00CF link.status(v20)',
    '00D1 return()',           // added by the compiler
  ]);
  assert.deepEqual(d.messages.slice(1), [
    'You read all of it.',
    'Hello,\n"friend" \\ %v3',
    'The gate is shut.',        // #message 3 keeps its number
    'part one, part two',
    'x',
  ]);
  assert.deepEqual(r.said, [
    { groups: [20, 100], words: ['look', 'code'] },
    { groups: [20], words: ['look'] },
    { groups: [20, 103], words: ['look', 'room'] },
    { groups: [21, 102, 9999], words: ['take', 'key', 'rol'] },
  ]);
});

test('logic: exact bytes of a small program', () => {
  const { bytes } = compile('if (said("look", 9999) || !f3) { print("Hi"); } else { v2++; }');
  const key = 'Avis Durgan';
  const enc = [...'Hi\0'].map((c, i) => c.charCodeAt(0) ^ key.charCodeAt(i));
  assert.deepEqual([...bytes], [
    23, 0,                                  // code length
    0xFF, 0xFC, 14, 2, 20, 0, 0x0F, 0x27, 0xFD, 7, 3, 0xFC, 0xFF, 5, 0,
    101, 1,                                 // print(m1)
    0xFE, 2, 0,                             // jump over the else
    1, 2,                                   // increment(v2)
    0,                                      // return
    1,                                      // one message
    7, 0,                                   // end of the section, from here
    4, 0,                                   // message 1
    ...enc,
  ]);
});

test('logic: messages are encrypted with one running key, gaps are 0', () => {
  const { bytes } = compile('#message 1 "one"\n#message 4 "four"\nprint(m4);');
  const d = decodeLogic(bytes);
  assert.deepEqual(Array.from(d.messages), [undefined, 'one', undefined, undefined, 'four']);
  const origin = 3 + d.code.length;
  assert.equal(bytes[origin - 1], 4);
  assert.equal(bytes[origin + 4] | bytes[origin + 6], 0);   // messages 2 and 3
  const key = 'Avis Durgan';
  const plain = 'one\0four\0';
  const textAt = origin + 2 + 4 * 2;
  for (let i = 0; i < plain.length; i++) {
    assert.equal(bytes[textAt + i], plain.charCodeAt(i) ^ key.charCodeAt(i % key.length));
  }
  assert.equal(bytes.length, textAt + plain.length);
  // encodeLogic and decodeLogic agree on their own too.
  const again = decodeLogic(encodeLogic(d.code, d.messages));
  assert.deepEqual(Array.from(again.messages), Array.from(d.messages));
  assert.deepEqual([...again.code], [...d.code]);
});

test('logic: a logic with no messages and no return', () => {
  const { bytes } = compile('set(f1);');
  assert.deepEqual([...bytes], [3, 0, 12, 1, 0, 0, 2, 0]);
  assert.deepEqual(listing('return();'), ['0000 return()']);
  assert.deepEqual(listing('return;'), ['0000 return()']);
  assert.deepEqual(listing(''), ['0000 return()']);
  // A return inside an if is not the end of the logic.
  assert.deepEqual(listing('if (f1) { return(); }').at(-1), '0007 return()');
});

test('logic: #include reads through readFile, relative to the file', () => {
  const asked = [];
  const lines = listing('#include "inc/a.h"\nset(flag);', {
    file: 'game/logic/x.lgc',
    readFile: (p) => {
      asked.push(p);
      return p.endsWith('a.h') ? '#include "b.h"\n' : '#define flag f9\n';
    },
  });
  assert.deepEqual(asked, ['game/logic/inc/a.h', 'game/logic/inc/b.h']);
  assert.equal(lines[0], '0000 set(f9)');
});

test('logic: compile errors are file:line: message', () => {
  const bad = (src, pattern, opts) => assert.throws(() => compile(src, opts), pattern);
  // Type errors.
  bad('\nset(v5);', /^Error: x\.lgc:2: argument 1 of set must be a flag, got a variable, v5$/);
  bad('#define gate.open f9\nv1 = 2;\nassignn(gate.open, 2);',
    /x\.lgc:3: argument 1 of assignn must be a variable, got a flag, gate\.open \(f9\)/);
  bad('set.view(o0, v3);', /argument 2 of set\.view must be a number, got a variable, v3/);
  bad('set(5);', /must be a flag, got the number 5 \(write f5\)/);
  bad('f5 = 1;', /x\.lgc:1: the left side of = must be a variable/);
  bad('if (v5) { }', /x\.lgc:1: v5 alone is not a test/);
  bad('if (f5 == 1) { }', /the left side of == must be a variable/);
  bad('print(s1);', /must be a message, got a string, s1/);
  bad('print(m9);', /there is no message 9/);
  bad('set.view(o0);', /set\.view takes 2 arguments, got 1/);
  bad('assignn(v1, 300);', /300 does not fit in a byte/);
  // Names.
  bad('if (said("xyzzy")) { }', /x\.lgc:1: "xyzzy" is not in the words file/);
  bad('get("sword");', /no inventory item named "sword"/);
  bad('frob(1);', /unknown command "frob"/);
  bad('if (frob(1)) { }', /unknown test "frob"/);
  bad('isset(f1);', /isset is a test/);
  bad('if (set(f1)) { }', /set is an action, not a test/);
  bad('set(nothing);', /unknown name "nothing"/);
  bad('goto nowhere;', /x\.lgc:1: no label "nowhere"/);
  bad('here:\nhere:', /x\.lgc:2: label "here" is already used/);
  // Shape.
  bad('if (f1 || (f2 && f3)) { }', /&& inside \|\|/);
  bad('if (f1) {\nset(f2);', /x\.lgc:1: this \{ is never closed/);
  bad('set(f1)\nset(f2);', /x\.lgc:2: expected ";", found set/);
  bad('}', /\} without/);
  bad('else { }', /else without an if/);
  bad('print("abc);', /x\.lgc:1: string is never closed/);
  bad('/* never', /comment is never closed/);
  bad('print("a\\q");', /unknown escape/);
  bad('#define v1 v2', /"v1" already means variable 1/);
  bad('#define a f1\n#define a f2', /x\.lgc:2: "a" is already defined/);
  bad('#message 1 "a"\n#message 1 "b"', /x\.lgc:2: message 1 is already defined/);
  bad('#include "gone.h"', /x\.lgc:1: cannot read gone\.h/, { readFile: () => { throw new Error('no'); } });
  // An error inside an include names the include.
  bad('#include "d.h"\nset(f1);', /^Error: d\.h:2: expected #define name value$/,
    { readFile: () => '// defs\n#define broken\n' });
});

test('logic: the command line exits nonzero with file:line', () => {
  const dir = mkdtempSync(join(tmpdir(), 'jrq-'));
  const src = join(dir, '005_bad.lgc');
  writeFileSync(src, 'set(f1);\nset(v2);\n');
  const r = spawnSync(process.execPath, [join(root, 'tools/logic.js'), src, join(dir, 'out.bin')], { encoding: 'utf8' });
  assert.equal(r.status, 1);
  assert.equal(r.stderr, `${src}:2: argument 1 of set must be a flag, got a variable, v2\n`);
  assert.ok(!existsSync(join(dir, 'out.bin')));
});

// ---- pack ----

// Stands in for tools/pic.js, which someone else is writing.
const fakePic = (text) => Uint8Array.from([0xF0, text.includes('color 2') ? 2 : 0, 0xFF]);

function resource(built, dirName, number) {
  const dir = built.files[dirName];
  const vol = built.files['VOL.0'];
  const e = number * 3;
  if (dir[e] === 0xFF && dir[e + 1] === 0xFF && dir[e + 2] === 0xFF) return null;
  assert.equal(dir[e] >> 4, 0, 'vol number');
  const at = ((dir[e] & 0x0F) << 16) | (dir[e + 1] << 8) | dir[e + 2];
  assert.deepEqual([...vol.slice(at, at + 3)], [0x12, 0x34, 0]);
  const length = vol[at + 3] | (vol[at + 4] << 8);
  return vol.slice(at + 5, at + 5 + length);
}

test('pack: the fixture game builds and every resource reads back', async () => {
  const built = await buildGame(game, { compilePic: fakePic });
  assert.deepEqual(built.counts, { logic: 2, picture: 1, view: 1, sound: 1 });
  for (const d of ['LOGDIR', 'PICDIR', 'VIEWDIR', 'SNDDIR']) assert.equal(built.files[d].length, 768);

  assert.deepEqual(disassemble(resource(built, 'LOGDIR', 0)).lines.slice(0, 4), [
    '0000 if (isset(f5)) else 0018',
    '0006 load.pic(v0)',
    '0008 draw.pic(v0)',
    '000A show.pic()',
  ]);
  assert.equal(resource(built, 'LOGDIR', 1), null);
  const gate = disassemble(resource(built, 'LOGDIR', 2));
  assert.deepEqual(gate.messages.slice(1), ['It is stuck.', 'The gate is shut.', 'A gate.']);
  assert.equal(gate.lines.at(-1).slice(5), 'return()');
  assert.deepEqual([...resource(built, 'PICDIR', 1)], [0xF0, 2, 0xFF]);
  assert.equal(decodeView(resource(built, 'VIEWDIR', 0)).desc, 'Sir Junior');
  assert.equal(decodeSound(resource(built, 'SNDDIR', 1))[0].length, 3);
  assert.equal(decodeWords(built.files['WORDS.TOK']).length, words.words.size);
  assert.equal(decodeObjects(built.files['OBJECT']).items[1].name, 'brass key');
  assert.equal(JSON.parse(new TextDecoder().decode(built.files['game.json'])).id, 'FIX');
  assert.deepEqual(JSON.parse(built.said[0]), [
    { groups: [20, 101], words: ['look', 'gate'] },
    { groups: [21, 102], words: ['take', 'key'] },
  ]);
  // Resources sit back to back in VOL.0 with nothing between.
  const sizes = [['LOGDIR', 0], ['LOGDIR', 2], ['PICDIR', 1], ['VIEWDIR', 0], ['SNDDIR', 1]]
    .reduce((t, [d, n]) => t + 5 + resource(built, d, n).length, 0);
  assert.equal(built.files['VOL.0'].length, sizes);
});

test('pack: writes build/ and clears old said lists', async () => {
  const out = mkdtempSync(join(tmpdir(), 'jrq-'));
  mkdirSync(join(out, 'said'));
  writeFileSync(join(out, 'said', '099.json'), '[]\n');
  const lines = [];
  await pack(game, out, (l) => lines.push(l), { compilePic: fakePic });
  assert.deepEqual(readdirSync(out).sort(),
    ['LOGDIR', 'OBJECT', 'PICDIR', 'SNDDIR', 'VIEWDIR', 'VOL.0', 'WORDS.TOK', 'game.json', 'said']);
  assert.deepEqual(readdirSync(join(out, 'said')).sort(), ['000.json', '002.json']);
  assert.equal(readFileSync(join(out, 'game.json'), 'utf8'), read('fixture-game', 'game.json'));
  assert.match(lines.at(-1), /2 logics, 1 pictures, 1 views, 1 sounds/);
});

test('pack: the command line, with or without tools/pic.js', () => {
  const out = mkdtempSync(join(tmpdir(), 'jrq-'));
  const r = spawnSync(process.execPath, [join(root, 'tools/pack.js'), game, out], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  const picdir = readFileSync(join(out, 'PICDIR'));
  if (existsSync(join(root, 'tools/pic.js')) && !/warning/.test(r.stderr)) {
    assert.notEqual(picdir[3], 0xFF);
  } else {
    // No picture compiler yet: a warning, and the rest still builds.
    assert.match(r.stderr, /^warning: tools\/pic\.js is not usable .*1 picture skipped/);
    assert.ok(picdir.every((b) => b === 0xFF));
    assert.match(r.stdout, /2 logics, 0 pictures/);
  }
});

test('pack: lists every error and exits nonzero', () => {
  const dir = mkdtempSync(join(tmpdir(), 'jrq-'));
  const bad = join(dir, 'game');
  cpSync(game, bad, { recursive: true });
  writeFileSync(join(bad, 'logic', '002_gate.lgc'), 'set(f1);\nprint(v1);\n');
  writeFileSync(join(bad, 'view', '003_bad.view'), 'loop 0\ncel 2 1\n123\n');
  writeFileSync(join(bad, 'view', '000_twin.view'), 'loop 0\ncel 1 1\n5\n');
  writeFileSync(join(bad, 'sound', 'beep.snd'), 'voice 1\n');
  const r = spawnSync(process.execPath, [join(root, 'tools/pack.js'), bad, join(dir, 'build')], { encoding: 'utf8' });
  assert.equal(r.status, 1);
  const errors = r.stderr.split('\n').filter((l) => l && !l.startsWith('warning'));
  assert.equal(errors.length, 4, r.stderr);
  assert.ok(errors.includes(`${join(bad, 'logic', '002_gate.lgc')}:2: argument 1 of print must be a message, got a variable, v1`));
  assert.ok(errors.some((l) => l.startsWith(`${join(bad, 'view', '003_bad.view')}:3: `)));
  assert.ok(errors.some((l) => /000_twin\.view:1: view 0 is also 000_ego\.view/.test(l)));
  assert.ok(errors.some((l) => /beep\.snd:1: the file name must start with a number/.test(l)));
  assert.ok(!existsSync(join(dir, 'build')));
});
