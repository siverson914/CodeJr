// Headless runner: plays a built game from a script of keys and typed
// lines, writes PNG frames and prints state hashes.
//
//   node tools/play.js build --script test/x.txt --png-dir out [--every N]
//
// Script lines (see FORMATS.md): a number (cycles to run), `key NAME`,
// `type TEXT`, `shot NAME`, `hash`, `expect TEXT`. `#` starts a comment.

import { readFileSync, existsSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Interpreter, KEY, PALETTE } from '../js/agi.js';
import { renderFrame, screenText, FRAME_W, FRAME_H } from '../js/render.js';

const KEY_NAMES = {
  up: KEY.UP, down: KEY.DOWN, left: KEY.LEFT, right: KEY.RIGHT,
  home: KEY.HOME, end: KEY.END, pgup: KEY.PGUP, pgdn: KEY.PGDN, center: KEY.CENTER,
  enter: KEY.ENTER, esc: KEY.ESC, tab: KEY.TAB, backspace: KEY.BACKSPACE, space: 32,
};
for (let i = 1; i <= 10; i++) KEY_NAMES[`f${i}`] = KEY[`F${i}`];

// The built files as the interpreter wants them.
export function loadResources(dir) {
  const read = (name) => (existsSync(join(dir, name)) ? new Uint8Array(readFileSync(join(dir, name))) : null);
  const vols = [];
  for (let n = 0; n < 16 && existsSync(join(dir, `VOL.${n}`)); n++) vols.push(read(`VOL.${n}`));
  let game = {};
  try { game = JSON.parse(readFileSync(join(dir, 'game.json'), 'utf8')); } catch { /* optional */ }
  return {
    logdir: read('LOGDIR'), picdir: read('PICDIR'), viewdir: read('VIEWDIR'), snddir: read('SNDDIR'),
    vols, words: read('WORDS.TOK'), object: read('OBJECT'), game,
  };
}

export const hex = (n) => n.toString(16).padStart(8, '0');

// Run script text against an interpreter. opts: writePng(path, w, h,
// pixels, palette), pngDir, every, out(line). Returns { cycles, hashes,
// shots }. Throws on a failed `expect` or a bad line.
export async function runScript(agi, text, opts = {}) {
  const out = opts.out || (() => {});
  const result = { cycles: 0, hashes: [], shots: [] };
  const frame = new Uint8Array(FRAME_W * FRAME_H);

  const shot = (name) => {
    if (!opts.pngDir || !opts.writePng) return;
    const path = join(opts.pngDir, `${name}.png`);
    opts.writePng(path, FRAME_W, FRAME_H, renderFrame(agi, frame), PALETTE);
    result.shots.push(path);
  };
  const cycle = async () => {
    agi.cycle();
    // The game waits on a promise from the host: let it arrive.
    if (agi.pending) await new Promise((r) => setImmediate(r));
    result.cycles++;
    if (opts.every && result.cycles % opts.every === 0) {
      const h = hex(agi.hash());
      result.hashes.push(h);
      out(`${result.cycles} ${h}`);
      shot(`frame-${String(result.cycles).padStart(6, '0')}`);
    }
  };

  const lines = text.split('\n');
  for (let n = 0; n < lines.length; n++) {
    const line = lines[n].replace(/\r$/, '');
    const trimmed = line.trim();
    if (trimmed === '' || trimmed.startsWith('#')) continue;
    const space = trimmed.indexOf(' ');
    const cmd = space < 0 ? trimmed : trimmed.slice(0, space);
    const arg = space < 0 ? '' : trimmed.slice(space + 1);
    if (/^\d+$/.test(trimmed)) {
      for (let i = Number(trimmed); i > 0; i--) await cycle();
    } else if (cmd === 'key') {
      const code = KEY_NAMES[arg.trim().toLowerCase()];
      if (code === undefined) throw new Error(`script line ${n + 1}: no key called "${arg}"`);
      agi.keyDown(code);
      agi.keyUp(code);
    } else if (cmd === 'type') {
      for (const ch of arg) agi.typeChar(ch);
      agi.enter();
    } else if (cmd === 'char') {
      // One key with no Enter, for "press R" screens.
      for (const ch of arg.trim()) agi.typeChar(ch);
    } else if (cmd === 'shot') {
      shot(arg.trim() || `shot-${result.cycles}`);
    } else if (cmd === 'hash') {
      const h = hex(agi.hash());
      result.hashes.push(h);
      out(`hash ${result.cycles} ${h}`);
    } else if (cmd === 'expect') {
      // Message windows wrap, so compare with all runs of spaces as one.
      const flat = (s) => s.replace(/\s+/g, ' ').trim();
      const rows = screenText(agi);
      const all = flat(rows.map((r) => r.trim()).join(' '));
      if (!all.includes(flat(arg)) && !rows.some((r) => r.includes(arg))) {
        throw new Error(`script line ${n + 1}: expected "${arg}" on screen at cycle ${result.cycles}. Screen:\n`
          + rows.map((r) => r.replace(/\s+$/, '')).filter((r) => r).join('\n'));
      }
    } else {
      throw new Error(`script line ${n + 1}: don't understand "${trimmed}"`);
    }
  }
  return result;
}

async function main(args) {
  const opt = { build: null, script: null, pngDir: null, every: 0 };
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--script') opt.script = args[++i];
    else if (args[i] === '--png-dir') opt.pngDir = args[++i];
    else if (args[i] === '--every') opt.every = Number(args[++i]) || 0;
    else if (!opt.build) opt.build = args[i];
  }
  if (!opt.build || !opt.script) {
    console.error('usage: node tools/play.js build --script test/x.txt [--png-dir out] [--every N]');
    return 2;
  }
  let writePng = null;
  if (opt.pngDir) {
    mkdirSync(opt.pngDir, { recursive: true });
    ({ writePng } = await import('./png.js'));
  }
  const agi = new Interpreter(loadResources(opt.build), { log: (t) => console.error(`log: ${t}`) });
  try {
    const r = await runScript(agi, readFileSync(opt.script, 'utf8'), {
      writePng, pngDir: opt.pngDir, every: opt.every, out: (l) => console.log(l),
    });
    console.log(`done: ${r.cycles} cycles, hash ${hex(agi.hash())}, ${r.shots.length} frames`);
    return 0;
  } catch (e) {
    console.error(e.message);
    return 1;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  main(process.argv.slice(2)).then((code) => { process.exitCode = code; });
}
