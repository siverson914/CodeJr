// "Let the AI play": the showcase script, the demo file the game plays,
// and the player the browser shell uses (js/demo.js), without a browser.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Interpreter } from '../js/agi.js';
import { screenText } from '../js/render.js';
import { DemoPlayer, parseDemo } from '../js/demo.js';
import { loadResources, hex } from '../tools/play.js';
import { inlineResources } from '../tools/bundle.js';
import { checkDemo } from '../tools/pack.js';

const build = mkdtempSync(join(tmpdir(), 'jrq-show-'));
execFileSync('node', ['tools/pack.js', 'game', build]);
const showcase = readFileSync('test/showcase.txt', 'utf8');
const demo = readFileSync('game/demo/1.txt', 'utf8');

test('the showcase plays the whole game, twice, with the same hash', () => {
  const run = () => execFileSync('node', ['tools/play.js', build, '--script', 'test/showcase.txt'], { encoding: 'utf8' });
  const a = run(), b = run();
  assert.match(a, /done: \d+ cycles/);
  assert.equal(a, b);
});

test('the showcase ends on 158 of 158 and 9 of 9, honestly', () => {
  // These are expect lines: the run above fails if the screen disagrees.
  assert.match(showcase, /^expect You have 158 of 158 points\.$/m);
  assert.match(showcase, /^expect found: 9 of 9\.$/m);
  assert.match(showcase, /^expect THE END Score: 158 of 158/m);
  assert.match(showcase, /^char 1\n1\nchar 0\n1\nchar -\n/m);         // a date typed a key at a time
  assert.doesNotMatch(showcase, /^type /m);                           // nothing is typed all at once
  // Two deaths, each with a Retry, and Abort and Fail tried once each.
  assert.equal((showcase.match(/^expect Abort, Retry, Fail\?$/gm) || []).length, 2);
  assert.match(showcase, /^expect There are no dead ends\nchar r$/m);          // Fail, then Retry
  assert.match(showcase, /^expect You waited 42 years for this\nchar r$/m);    // Abort, then Retry
});

test('game/demo/1.txt is the showcase without its shot, expect and hash lines', () => {
  const stripped = showcase.split('\n').filter((l) => !/^(shot|expect|hash)( |$)/.test(l)).join('\n');
  assert.equal(demo, stripped);
  // And it holds nothing a player could not do.
  assert.equal(checkDemo(demo, 'game/demo/1.txt'), demo);
  assert.throws(() => checkDemo('5\nexpect hello\n', 'x.txt'), /x\.txt:2/);
});

test('pack copies the demo and the one-file bundle carries it', () => {
  assert.equal(readFileSync(join(build, 'demo', '1.txt'), 'utf8'), demo);
  const inline = inlineResources(build);
  assert.equal(Buffer.from(inline['demo/1.txt'], 'base64').toString('utf8'), demo);
});

test('the shell\'s player (js/demo.js) reaches the same end as tools/play.js', () => {
  const out = execFileSync('node', ['tools/play.js', build, '--script', 'game/demo/1.txt'], { encoding: 'utf8' });
  const [, cycles, hash] = /done: (\d+) cycles, hash ([0-9a-f]{8})/.exec(out);
  // No host at all: the seeded generator, the cycle clock, nobody to ask.
  const agi = new Interpreter(loadResources(build), {});
  const player = new DemoPlayer(demo);
  assert.equal(player.total, Number(cycles));
  while (player.step(agi)) { /* one cycle at a time, as the shell does */ }
  assert.ok(player.done);
  assert.equal(player.cycles, Number(cycles));
  assert.equal(hex(agi.hash()), hash);
  assert.equal(agi.vars[3], 158);                    // score
  assert.equal(agi.vars[46], 9);                     // parts that didn't change
  assert.equal(agi.vars[0], 1);                      // home, in the guest room
  assert.match(screenText(agi).join('\n'), /Score:158 of 158/);
});

test('a demo script parses to runs, keys, chars and lines', () => {
  const ops = parseDemo('# c\n3\nkey enter\nchar p\ntype look\nexpect x\n0\nkey space\n');
  assert.deepEqual(ops.map((o) => o.op), ['run', 'key', 'char', 'type', 'key']);
  assert.equal(ops[4].code, 32);
  assert.throws(() => parseDemo('key banana\n'), /line 1/);
  assert.throws(() => parseDemo('5\nwalk left\n'), /line 2/);
});
