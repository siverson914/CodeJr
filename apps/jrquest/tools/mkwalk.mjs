// Expands macros (goto, exit, say, dismiss, until, and for a recorded
// show: pace, slowtype, zuckchat) into plain
// play.js lines by playing the game live. Usage:
//   node tools/mkwalk.mjs build test/walkthrough.src test/walkthrough.txt [--png-dir D] [--stop N]
//   node tools/mkwalk.mjs build test/showcase.src test/showcase.txt --demo game/demo/1.txt [--pace "12 2 1 4"]
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
const Q = new URL('..', import.meta.url).pathname.replace(/\/$/, '');
const { Interpreter, KEY, PALETTE } = await import(Q + '/js/agi.js');
const { renderFrame, screenText, FRAME_W, FRAME_H } = await import(Q + '/js/render.js');
const { loadResources } = await import(Q + '/tools/play.js');
const { writePng } = await import(Q + '/tools/png.js');
const args = process.argv.slice(2);
const [build, src, outPath] = args;
let demoPath = null, paceArg = null;
// Show pacing, set by a `pace BASE PER GAP BEAT` line in the source: a
// message box stays up BASE + (its characters / PER) cycles, commands are
// typed one character at a time GAP cycles apart, BEAT cycles after the
// last thing happened. null: no pacing (a test walkthrough).
let pace = null; let zuckDone = false; let boxCount = 0, boxChars = 0, typed = 0;
let pngDir = null, stopAt = 0, sheet = null; const frames = {};
for (let i = 3; i < args.length; i++) { if (args[i] === '--png-dir') pngDir = args[++i]; if (args[i] === '--stop') stopAt = Number(args[++i]); if (args[i] === '--demo') demoPath = args[++i]; if (args[i] === '--pace') paceArg = args[++i]; if (args[i] === '--sheet') sheet = args[++i].split(','); }
if (pngDir) mkdirSync(pngDir, { recursive: true });
const agi = new Interpreter(loadResources(build), { log: (t) => console.error('log: ' + t) });
const out = []; const log = []; let cycles = 0;
const W = 160, H = 168;
const DX = [0, 0, 1, 1, 1, 0, -1, -1, -1], DY = [0, -1, -1, 0, 1, 1, 1, 0, -1];
const DKEY = { 1: 'up', 2: 'pgup', 3: 'right', 4: 'pgdn', 5: 'down', 6: 'end', 7: 'left', 8: 'home' };
const KN = { space: 32, up: KEY.UP, down: KEY.DOWN, left: KEY.LEFT, right: KEY.RIGHT, home: KEY.HOME, end: KEY.END, pgup: KEY.PGUP, pgdn: KEY.PGDN, enter: KEY.ENTER, esc: KEY.ESC };
const emit = (l) => out.push(l);
const say = (l) => { log.push(l); };
async function cyc(n = 1) { for (let i = 0; i < n; i++) { agi.cycle(); if (agi.pending) await new Promise((r) => setImmediate(r)); cycles++; } }
function runCycles(n) { return cyc(n).then(() => { if (n > 0) { const last = out[out.length - 1]; if (/^\d+$/.test(last || '')) out[out.length - 1] = String(Number(last) + n); else emit(String(n)); } }); }
function key(name) { const c = KN[name]; agi.keyDown(c); agi.keyUp(c); emit('key ' + name); }
const ego = () => agi.objs[0];
const st = () => `room ${agi.vars[0]} ego ${ego().x},${ego().y} score ${agi.vars[3]} tok ${agi.vars[40]} cyc ${cycles}`;
function winText() {
  const w = agi.win; if (!w) return null;
  const rows = [];
  for (let y = 1; y < w.h - 1; y++) { let s = ''; for (let x = 1; x < w.w - 1; x++) { const ch = agi.text[w.r + y][w.c + x].ch; s += ch >= 32 && ch < 127 ? String.fromCharCode(ch) : ' '; } rows.push(s.trim()); }
  return rows.join(' ').replace(/\s+/g, ' ').trim();
}
function shot(name) { if (!pngDir) return; const f = new Uint8Array(FRAME_W * FRAME_H); frames[name] = f; writePng(join(pngDir, name + '.png'), FRAME_W, FRAME_H, renderFrame(agi, f), PALETTE); }
function expectKey(t) { // a short distinctive piece of the window text
  let s = t.slice(0, 44); const sp = s.lastIndexOf(' '); if (t.length > 44 && sp > 12) s = s.slice(0, sp); return s;
}
async function dismiss(auto = true, max = 40) {
  let n = 0;
  while (agi.win && agi.wait && n < max) {
    const t = winText(); say(`   [box] ${t}`); if (agi.win.h - 2 >= 17) say(`   !!! TALL BOX ${agi.win.h - 2} lines`);
    if (auto) emit('expect ' + expectKey(t));
    boxCount++; boxChars += t.length;
    if (pace) await runCycles(pace.base + Math.ceil(t.length / pace.per));
    key('enter'); await runCycles(2); n++;
  }
  return n;
}
// Type a line the way a person would (pace mode), or all at once.
async function typeLine(text, where) {
  if (!pace) { for (const ch of text) agi.typeChar(ch); agi.enter(); emit('type ' + text); return; }
  if (agi.wait && agi.win) throw new Error(`${where}: a box is open before typing "${text}": ${winText()}`);
  await runCycles(pace.beat);
  for (const ch of text) {
    if (agi.wait && agi.win) throw new Error(`${where}: a box opened while typing "${text}": ${winText()}`);
    if (ch === ' ') key('space'); else { agi.typeChar(ch); emit('char ' + ch); }
    typed++;
    await runCycles(pace.gap);
  }
  key('enter');
}
// ---- pathfinding ----
function okAt(x, y, opt) {
  const o = ego();
  if (x < 0 || x + o.xSize > W || y > H - 1 || y <= agi.horizon) return false;
  if (opt.margin && (x < 1 || x + o.xSize > W - 1 || y > H - 2 || y <= agi.horizon + 1)) return false;
  let water = true;
  for (let i = 0; i < o.xSize; i++) {
    const p = agi.gbuf[y * W + x + i] >> 4;
    if (p === 0 || p === 1) return false;
    if (p === 2 && !opt.trig) return false;
    if (p !== 3) water = false; else if (!opt.water) return false;
  }
  for (const u of agi.objs) {
    if (u === o || (u.flags & 0x41) !== 0x41 || (u.flags & 0x200)) continue;
    if (x + o.xSize < u.x - 2 || x > u.x + u.xSize + 2) continue;
    if (Math.abs(y - u.y) <= (opt.objPad ?? 2)) return false;
  }
  if (agi.block.on) { /* handled in stepFrom */ }
  return true;
}
const inBlock = (x, y) => agi.block.on && x > agi.block.x1 && x < agi.block.x2 && y > agi.block.y1 && y < agi.block.y2;
function stepFrom(x, y, d, opt) {
  const ss = ego().stepSize;
  if (inBlock(x, y) !== inBlock(x + ss * DX[d], y + ss * DY[d])) return null;
  for (let s = 0; s < ss; s++) { x += DX[d]; y += DY[d]; if (!okAt(x, y, opt)) return null; }
  return [x, y];
}
function plan(tx, ty, opt) {
  const o = ego(); const sx = o.x, sy = o.y;
  const id = (x, y, d) => (y * W + x) * 9 + d;
  const dist = new Map(), prev = new Map();
  let frontier = [[0, sx, sy, 0]]; dist.set(id(sx, sy, 0), 0);
  const buckets = new Map(); const push = (c, x, y, d) => { if (!buckets.has(c)) buckets.set(c, []); buckets.get(c).push([x, y, d]); };
  push(0, sx, sy, 0); let c = 0, maxC = 0, best = null;
  const tol = opt.tol ?? 1;
  while (c <= maxC + 0 && c < 5000) {
    const b = buckets.get(c); if (!b || !b.length) { c++; if (c > maxC) break; continue; }
    const [x, y, d] = b.pop();
    if (dist.get(id(x, y, d)) !== c) continue;
    if (Math.abs(x - tx) <= tol && Math.abs(y - ty) <= tol) { best = [x, y, d]; break; }
    for (let nd = 1; nd <= 8; nd++) {
      const p = stepFrom(x, y, nd, opt); if (!p) continue;
      const nc = c + 1 + (nd === d ? 0 : 6);
      const k = id(p[0], p[1], nd);
      if (!dist.has(k) || dist.get(k) > nc) { dist.set(k, nc); prev.set(k, [x, y, d]); push(nc, p[0], p[1], nd); if (nc > maxC) maxC = nc; }
    }
  }
  if (!best) return null;
  const path = []; let cur = best;
  while (!(cur[0] === sx && cur[1] === sy && cur[2] === 0)) { path.push(cur); cur = prev.get(id(cur[0], cur[1], cur[2])); }
  path.reverse(); return path;
}
async function goto(tx, ty, opt) {
  for (let attempt = 0; attempt < 12; attempt++) {
    const path = plan(tx, ty, opt);
    if (!path) throw new Error(`goto ${tx},${ty}: no path from ${ego().x},${ego().y} (${st()})`);
    if (path.length === 0) return;
    let curDir = 0, ok = true;
    for (let i = 0; i < path.length && ok; i++) {
      const [x, y, d] = path[i];
      if (d !== curDir) { key(DKEY[d]); curDir = d; }
      const room = agi.vars[0];
      await runCycles(1);
      let tries = 0;
      while ((ego().x !== x || ego().y !== y) && tries < 2 && !agi.wait && agi.vars[0] === room && agi.vars[6] === d) { await runCycles(1); tries++; }
      if (agi.vars[0] !== room) throw new Error(`goto: room changed to ${agi.vars[0]} during walk (${st()})`);
      if (agi.wait) { say(`   goto interrupted by a box at ${st()}: ${winText()}`); if (opt.boxok) { await dismiss(); ok = false; curDir = 0; break; } throw new Error('goto: box opened: ' + winText() + ' ' + st()); }
      if (ego().x !== x || ego().y !== y) { ok = false; say(`   goto: off plan at ${ego().x},${ego().y} wanted ${x},${y}; replanning`); }
    }
    if (agi.vars[6] !== 0) { key(DKEY[agi.vars[6]]); await runCycles(1); }
    if (ok) return;
  }
  throw new Error(`goto ${tx},${ty}: gave up at ${st()}`);
}
// ---- main ----
const lines = readFileSync(src, 'utf8').split('\n');
let err = null;
async function exec(t, where) {
    if (t === '') { return; }
    if (t.startsWith('#')) { emit(t); say(t); return; }
    const sp = t.indexOf(' '); const cmd = sp < 0 ? t : t.slice(0, sp); const arg = sp < 0 ? '' : t.slice(sp + 1);
    if (/^\d+$/.test(t)) { await runCycles(Number(t)); }
    else if (cmd === 'pace') { const a = arg.trim().split(/\s+/).map(Number); if (paceArg) { const b = paceArg.trim().split(/\s+/).map(Number); for (let i = 0; i < b.length; i++) a[i] = b[i]; } pace = { base: a[0], per: a[1], gap: a[2] || 1, beat: a[3] || 4 }; }
    else if (cmd === 'slowtype') { say(`${where} slowtype ${arg}`); await typeLine(arg, where); }
    else if (cmd === 'zuckchat') { // the first time Zuckerberg is on screen: one conversation, ending in no
      if (!zuckDone && agi.flags[67]) {
        zuckDone = true; emit('# Zuckerberg has wandered in with his wheelbarrow');
        await exec('say look at zuckerberg', where); await exec('say talk to zuckerberg', where); await exec('say no', where);
      }
    }
    else if (cmd === 'key') { key(arg.trim()); }
    else if (cmd === 'type') { for (const ch of arg) agi.typeChar(ch); agi.enter(); emit(t); }
    else if (cmd === 'char') { for (const ch of arg.trim()) agi.typeChar(ch); emit(t); }
    else if (cmd === 'shot') { shot(arg.trim()); emit(t); }
    else if (cmd === 'hash') { emit(t); }
    else if (cmd === 'expect') {
      const flat = (s) => s.replace(/\s+/g, ' ').trim(); const rows = screenText(agi); const all = flat(rows.map((r) => r.trim()).join(' '));
      if (!all.includes(flat(arg))) throw new Error(`${where}: expected "${arg}". Screen:\n` + rows.map((r) => r.replace(/\s+$/, '')).filter((r) => r).join('\n'));
      emit(t);
    }
    else if (cmd === 'say') { // type a command, auto-expect and dismiss every box
      say(`${where} > ${arg}   (${st()})`);
      await typeLine(arg, where); await runCycles(3);
      const k = await dismiss(true); if (!k) say('   (no box)');
    }
    else if (cmd === 'dismiss') { say(`${where} dismiss (${st()})`); const k = await dismiss(arg.trim() !== 'quiet'); if (!k) say('   (no box)'); }
    else if (cmd === 'goto') {
      const a = arg.trim().split(/\s+/); const opt = { trig: a.includes('trig'), water: a.includes('water'), boxok: a.includes('boxok'), margin: !a.includes('edge') };
      for (const z of a) { const m = /^tol=(\d+)$/.exec(z); if (m) opt.tol = Number(m[1]); }
      await goto(Number(a[0]), Number(a[1]), opt); say(`${where} goto ${arg} -> ${st()}`);
    }
    else if (cmd === 'exit' || cmd === 'walk') { // walk DIR N? / exit DIR: walk until the room changes or a box opens
      const a = arg.trim().split(/\s+/); const room = agi.vars[0]; const max = Number(a[1]) || 120;
      key(a[0]); let i = 0;
      for (; i < max; i++) { await runCycles(1); if (agi.vars[0] !== room || agi.wait) break; if (cmd === 'walk' && agi.vars[6] === 0) break; }
      if (cmd === 'exit' && agi.vars[0] === room && !agi.wait) throw new Error(`${where}: exit ${a[0]}: still in room ${room} at ${st()}`);
      say(`${where} ${cmd} ${arg} -> ${st()}${agi.wait ? ' [box: ' + winText() + ']' : ''}`);
    }
    else if (cmd === 'until') { // until N text : cycle until text on screen
      const m = /^(\d+)\s+(.*)$/.exec(arg); const max = Number(m[1]); let i = 0; const flat = (s) => s.replace(/\s+/g, ' ').trim();
      for (; i < max; i++) { const all = flat(screenText(agi).map((r) => r.trim()).join(' ')); if (all.includes(flat(m[2]))) break; await runCycles(1); }
      if (i === max) throw new Error(`${where}: until: never saw "${m[2]}" (${st()})\n` + screenText(agi).map((r) => r.replace(/\s+$/, '')).filter((r) => r).join('\n'));
      emit('expect ' + m[2]); say(`${where} until ${m[2]} after ${i} (${st()})`);
    }
    else if (cmd === 'waitbox') { // cycle until a box opens
      const max = Number(arg) || 100; let i = 0; for (; i < max && !agi.wait; i++) await runCycles(1);
      say(`${where} waitbox after ${i}: ${agi.wait ? winText() : '(none)'} (${st()})`);
    }
    else if (cmd === 'pos') { say(`${where} pos ${st()} objs ${agi.objs.filter((o) => (o.flags & 0x41) === 0x41).map((o) => `o${o.n}@${o.x},${o.y}`).join(' ')}`); }
    else if (cmd === 'assertpos') { const a = arg.trim().split(/\s+/).map(Number); if (agi.vars[0] !== a[0]) throw new Error(`${where}: not in room ${a[0]}: ${st()}`); }
    else throw new Error(`${where}: don't understand "${t}"`);
}
try {
  for (let n = 0; n < lines.length; n++) {
    if (stopAt && n + 1 >= stopAt) break;
    await exec(lines[n].trim(), `L${n + 1}`);
  }
  if (pace && !zuckDone) throw new Error('Zuckerberg never turned up where a zuckchat line was waiting for him');
} catch (e) { err = e; }
writeFileSync(outPath, out.join('\n') + '\n');
// The demo the game itself plays: the same lines without the test's own.
if (demoPath && !err) writeFileSync(demoPath, out.filter((l) => !/^(shot|expect|hash)( |$)/.test(l)).join('\n') + '\n');
say(`=== ${cycles} cycles (${(cycles / 600).toFixed(1)} minutes at 10 a second), ${boxCount} boxes, ${boxChars} characters read, ${typed} characters typed`);
console.log(log.join('\n'));
console.log('--- ' + st() + ' flags46-73: ' + Array.from({ length: 28 }, (_, i) => agi.flags[46 + i] ? 46 + i : '').filter((x) => x).join(','));
console.log('inv: ' + agi.items.filter((it, i) => agi.invRoom[i] === 255).map((it) => it.name).join(', '));
console.log(screenText(agi).map((r) => r.replace(/\s+$/, '')).filter((r) => r).join('\n'));
shot('zz-last');
if (sheet) { const names = sheet.filter((n) => frames[n]); const cols = 2, rows = Math.ceil(names.length / cols); const big = new Uint8Array(640 * 200 * rows);
  names.forEach((n, k) => { const ox = (k % cols) * 320, oy = Math.floor(k / cols) * 200; for (let y = 0; y < 200; y++) big.set(frames[n].subarray(y * 320, y * 320 + 320), (oy + y) * 640 + ox); });
  writePng('sheet.png', 640, 200 * rows, big, PALETTE); }
if (err) { console.log('ERROR: ' + err.message); process.exitCode = 1; }
