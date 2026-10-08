// Every room's own script, against a fresh pack. They were written by the
// rooms' authors and run by hand until a change to the opening broke two
// of them without any test noticing.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, readdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const build = mkdtempSync(join(tmpdir(), 'jrq-rooms-'));
execFileSync('node', ['tools/pack.js', 'game', build]);

for (const name of readdirSync('test/rooms').filter((n) => n.endsWith('.txt')).sort()) {
  test(`room script ${name}`, () => {
    const r = spawnSync('node', ['tools/play.js', build, '--script', join('test/rooms', name)], { encoding: 'utf8' });
    assert.match(r.stdout + r.stderr, /done: \d+ cycles/, (r.stdout + r.stderr).slice(-600));
  });
}
