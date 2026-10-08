// Packs the real game and plays it from the date prompt to the ending:
// a real date, every room on foot, every puzzle, two deaths and two
// Retries, 158 of 158. The script's own "expect" lines are the assertions.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

test('the whole game can be won on foot, twice, with the same hash', () => {
  const build = mkdtempSync(join(tmpdir(), 'jrq-'));
  execFileSync('node', ['tools/pack.js', 'game', build]);
  const run = () => execFileSync('node', ['tools/play.js', build, '--script', 'test/walkthrough.txt'], { encoding: 'utf8' });
  const a = run(), b = run();
  assert.match(a, /done: \d+ cycles/);
  assert.equal(a, b);
});

test('the walkthrough is honest: a real date and no test commands', () => {
  const script = readFileSync('test/walkthrough.txt', 'utf8');
  assert.match(script, /^type 10-05-2026$/m);
  assert.doesNotMatch(script, /^type (debug|tp|gimme|setflag|setvar)\b/m);
  // It ends on the full score and the ending.
  assert.match(script, /^expect THE END Score: 158 of 158/m);
  assert.match(script, /^expect You have 158 of 158 points\./m);
  // The title is a menu now: P plays.
  assert.match(script, /^expect Let the AI play\nshot 00-title\n# the menu: P plays\nchar p$/m);
});
