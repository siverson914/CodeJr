// The picture linter, on small pictures written here.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compilePic, compilePicMap } from '../tools/pic.js';
import { lintSource, lintPic, formatIssue } from '../tools/lint.js';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const codes = (issues, level) => issues.filter((i) => i.level === level).map((i) => i.code);

test('the line map does not change the bytes and names each byte\'s line', () => {
  const text = '# a comment\ncolor 1\nbox 10 10 30 30\n\narc 80 80 20 10\npri 2\nline 0 100 20 100\n';
  const map = compilePicMap(text);
  assert.deepEqual(map.bytes, compilePic(text));
  assert.equal(map.lines.length, map.bytes.length);
  assert.equal(map.lines[0], 2);                       // F0 from "color 1"
  assert.equal(map.lines[map.bytes.indexOf(0xf8)], 3); // the box's fill
  // A full-circle arc is split in three; all three belong to line 5.
  const arcs = [...map.bytes].map((b, i) => (b === 0xfb ? map.lines[i] : 0)).filter(Boolean);
  assert.deepEqual(arcs, [5, 5, 5]);
  assert.equal(map.lines[map.bytes.length - 1], 0);    // the closing FF
});

test('a fill that floods the visual layer is an error with its place and share', () => {
  const issues = lintSource('color 2\nfill 5 6\n');
  const big = issues.find((i) => i.code === 'fill-big');
  assert.equal(big.level, 'error');
  assert.equal(big.line, 2);
  assert.match(big.msg, /fill at 5,6 changes 100\.0% of the visual layer \(limit 60%\)/);
});

test('a priority fill over 45% is an error, under it is not', () => {
  // Wall off the top 40% of the picture, then fill each side.
  const wall = 'pri 0\nline 0 67 159 67\npri 1\n';
  const top = lintSource(`${wall}fill 5 5\n`);
  assert.deepEqual(codes(top, 'error'), []);
  const bottom = lintSource(`${wall}fill 5 100\n`);
  const big = bottom.find((i) => i.code === 'fill-big');
  assert.equal(big.line, 4);
  assert.match(big.msg, /fill at 5,100 changes 59\.5% of the priority layer \(limit 45%\)/);
  // The limit is an option.
  assert.deepEqual(codes(lintSource(`${wall}fill 5 100\n`, 'pic', { priFill: 60 }), 'error'), []);
});

test('a fill that does nothing is a warning and says why', () => {
  const issues = lintSource('color 1\nbox 10 10 30 30\nfill 20 20\n');
  const dead = issues.filter((i) => i.code === 'fill-nothing');
  assert.equal(dead.length, 1);
  assert.equal(dead[0].line, 3);
  assert.match(dead[0].msg, /fill at 20,20 does nothing: the start pixel is color 1, not white/);
  const pri = lintSource('pri 7\nline 0 50 159 50\nfill 80 50\n').find((i) => i.code === 'fill-nothing');
  assert.match(pri.msg, /priority 7, not 4/);
  const off = lintSource('fill 3 3\n').find((i) => i.code === 'fill-nothing');
  assert.match(off.msg, /both pens are off/);
});

test('white left behind is a warning with a bounding box; specks are let go', () => {
  // Paint everything but the inside of two outlines: one 9x9, one 1x2.
  const text = 'color 1\nrect 10 10 20 20\nrect 50 50 52 53\ncolor 2\nfill 0 0\n';
  const issues = lintSource(text, 'pic', { visFill: 100 });
  assert.deepEqual(codes(issues, 'error'), []);
  const white = issues.filter((i) => i.code === 'white');
  assert.equal(white.length, 1);
  assert.equal(white[0].level, 'warning');
  assert.match(white[0].msg, /81 pixels in 11,11 to 19,19/);
});

test('a one-row trigger line is a warning; two rows are fine', () => {
  const thin = lintSource('pri 2\nline 10 100 60 100\n').filter((i) => i.code === 'thin-trigger');
  assert.equal(thin.length, 1);
  assert.equal(thin[0].level, 'warning');
  assert.equal(thin[0].line, 2);
  assert.match(thin[0].msg, /y 100, x 10 to 60/);
  const thick = lintSource('pri 2\nline 10 100 60 100\nline 10 101 60 101\n');
  assert.equal(thick.filter((i) => i.code === 'thin-trigger').length, 0);
  // Another priority drawn the same way is not a trigger.
  assert.equal(lintSource('pri 3\nline 10 100 60 100\n').filter((i) => i.code === 'thin-trigger').length, 0);
});

test('points off the picture are errors with the source line', () => {
  const rel = lintSource('color 1\nrel 158 10 7 0\n').find((i) => i.code === 'range');
  assert.equal(rel.line, 2);
  assert.match(rel.msg, /165,10 is off the picture/);
  // The compiler does not check corner lines; the linter does, once.
  const corner = lintSource('color 1\n\nycorner 10 10 200 20\n').filter((i) => i.code === 'range');
  assert.equal(corner.length, 1);
  assert.equal(corner[0].line, 3);
  // What the compiler refuses comes back as one issue, not a throw.
  const refused = lintSource('color 1\nline 200 5 1 1\n', 'x.pic');
  assert.equal(refused.length, 1);
  assert.equal(refused[0].line, 2);
  assert.equal(formatIssue('x.pic', refused[0]), 'x.pic:2: error: x 200 is off the picture (0 to 159)');
});

test('compiled bytes with no line map can still be linted', () => {
  const issues = lintPic(compilePic('color 2\nfill 5 5\n'));
  assert.equal(issues.find((i) => i.code === 'fill-big').line, 0);
});

test('the command line exits 1 on errors and 0 on warnings alone', () => {
  const dir = mkdtempSync(join(tmpdir(), 'jrq-lint-'));
  const warn = join(dir, 'warn.pic'), bad = join(dir, 'bad.pic');
  writeFileSync(warn, 'color 1\nbox 10 10 30 30\n');           // white around the box
  writeFileSync(bad, 'pri 7\nfill 2 2\n');                      // floods the priority layer
  const run = (...files) => spawnSync('node', ['tools/lint.js', ...files], { cwd: ROOT, encoding: 'utf8' });
  const ok = run(warn);
  assert.equal(ok.status, 0);
  assert.match(ok.stdout, /warn\.pic: warning: white left/);
  assert.match(ok.stdout, /1 picture: 0 errors, 1 warning/);
  const fail = run(warn, bad);
  assert.equal(fail.status, 1);
  assert.match(fail.stdout, /bad\.pic:2: error: fill at 2,2 changes/);
  // pic.js --lint says the same and fails the same way.
  const pic = spawnSync('node', ['tools/pic.js', bad, '--lint'], { cwd: ROOT, encoding: 'utf8' });
  assert.equal(pic.status, 1);
  assert.match(pic.stdout, /bad\.pic:2: error: fill at 2,2 changes/);
});
