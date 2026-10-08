// Packs the real game and plays checkpoint 2 from the date prompt to the
// edge of the map. The script's own "expect" lines are the assertions.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

test('checkpoint 2 can be won, twice, with the same hash', () => {
  const build = mkdtempSync(join(tmpdir(), 'jrq-'));
  execFileSync('node', ['tools/pack.js', 'game', build]);
  const run = () => execFileSync('node', ['tools/play.js', build, '--script', 'test/checkpoint2.txt'], { encoding: 'utf8' });
  const a = run(), b = run();
  assert.match(a, /done: \d+ cycles/);
  assert.equal(a, b);
});
