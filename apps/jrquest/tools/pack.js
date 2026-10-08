// Build a whole game: game/ source -> build/ (LOGDIR, PICDIR, VIEWDIR,
// SNDDIR, VOL.0, WORDS.TOK, OBJECT, game.json, said/, demo/).

import { readFileSync, writeFileSync, mkdirSync, readdirSync, existsSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { SourceError, resourceNumber, isMain, runMain } from './util.js';
import { parseWords, encodeWords } from './words.js';
import { parseObjects, encodeObjects } from './objects.js';
import { parseView, encodeView } from './view.js';
import { parseSound, encodeSound } from './snd.js';
import { compileLogic, saidJson } from './logic.js';

// The source files of one kind, as [{ number, name, path }] in number order.
function sources(gameDir, folder, ext, errors) {
  const dir = join(gameDir, folder);
  if (!existsSync(dir)) return [];
  const list = [];
  for (const name of readdirSync(dir).sort()) {
    if (!name.endsWith(ext)) continue;
    const path = join(dir, name);
    const number = resourceNumber(name);
    const twin = list.find((s) => s.number === number);
    if (number === null) errors.push(new SourceError(path, 1, `the file name must start with a number 0 to 255, as in 012_name${ext}`));
    else if (twin) errors.push(new SourceError(path, 1, `${folder} ${number} is also ${twin.name}`));
    else list.push({ number, name, path });
  }
  return list.sort((a, b) => a.number - b.number);
}

// A recorded play (game/demo/N.txt) may hold only what a player could do:
// cycle counts, key, char and type lines, and comments. See FORMATS.md.
export function checkDemo(text, path) {
  const lines = text.split('\n');
  for (let n = 0; n < lines.length; n++) {
    const t = lines[n].replace(/\r$/, '').trim();
    if (t === '' || t.startsWith('#') || /^\d+$/.test(t) || /^(key|char|type) \S/.test(t)) continue;
    throw new SourceError(path, n + 1, `a recorded play has only cycle counts, key, char and type lines: ${t}`);
  }
  return text;
}

// Returns { files: { name: bytes }, said: { number: json text }, demos:
// { 'N.txt': text }, counts, warnings }. Throws an AggregateError holding every source error found.
// opts.compilePic replaces tools/pic.js (tests use it).
export async function buildGame(gameDir, opts = {}) {
  const errors = [];
  const warnings = [];
  const files = {};
  const said = {};
  const attempt = (fn) => {
    try { return fn(); } catch (e) {
      if (!(e instanceof SourceError)) throw e;
      errors.push(e);
      return undefined;
    }
  };
  const read = (path) => readFileSync(path, 'utf8');

  const gamePath = join(gameDir, 'game.json');
  files['game.json'] = attempt(() => {
    let text;
    try { text = read(gamePath); } catch { throw new SourceError(gamePath, 1, 'missing'); }
    try { JSON.parse(text); } catch (e) { throw new SourceError(gamePath, 1, `not JSON: ${e.message}`); }
    return new TextEncoder().encode(text);
  });

  const wordsPath = join(gameDir, 'words.txt');
  const objectsPath = join(gameDir, 'objects.txt');
  const words = attempt(() => {
    if (!existsSync(wordsPath)) throw new SourceError(wordsPath, 1, 'missing');
    // Rooms keep their own vocabulary in game/words/*.txt. Lines are joined
    // into one file for the parser; blank padding keeps line numbers right
    // only for words.txt, so extra files report their own name in the text.
    let text = read(wordsPath);
    const extraDir = join(gameDir, 'words');
    if (existsSync(extraDir)) {
      for (const name of readdirSync(extraDir).sort()) {
        if (name.endsWith('.txt')) text += `\n# ---- words/${name} ----\n` + read(join(extraDir, name));
      }
    }
    return parseWords(text, wordsPath);
  });
  const objects = attempt(() => {
    if (!existsSync(objectsPath)) throw new SourceError(objectsPath, 1, 'missing');
    return parseObjects(read(objectsPath), objectsPath);
  });
  if (words) files['WORDS.TOK'] = encodeWords(words);
  if (objects) files['OBJECT'] = encodeObjects(objects);

  // Pictures belong to tools/pic.js, which may not be written yet.
  let compilePic = opts.compilePic || null;
  const pics = sources(gameDir, 'pic', '.pic', errors);
  if (pics.length && !compilePic) {
    try {
      ({ compilePic } = await import('./pic.js'));
      if (typeof compilePic !== 'function') throw new Error('it does not export compilePic');
    } catch (e) {
      if (e.code !== 'ERR_MODULE_NOT_FOUND' && !/compilePic/.test(e.message)) throw e;
      compilePic = null;
      warnings.push(`tools/pic.js is not usable (${e.message.split('\n')[0]}): ${pics.length} picture${pics.length === 1 ? '' : 's'} skipped, PICDIR is empty`);
    }
  }

  const kinds = [
    { dir: 'LOGDIR', label: 'logic', list: sources(gameDir, 'logic', '.lgc', errors),
      make: (s) => {
        // With a broken words or objects file its errors are already
        // listed; logic errors on top would only be noise.
        if (!words || !objects) return undefined;
        const r = compileLogic(read(s.path), { file: s.path, words, objects });
        said[s.number] = saidJson(r.said);
        return r.bytes;
      } },
    { dir: 'PICDIR', label: 'picture', list: compilePic ? pics : [],
      make: (s) => {
        try { return compilePic(read(s.path), s.path); } catch (e) {
          // pic.js does not know the file name, so put it in front here.
          if (e instanceof SourceError) throw e;
          const m = /^(?:.*?:)?(\d+): (.*)$/s.exec(e.message);
          throw new SourceError(s.path, m ? Number(m[1]) : 1, m ? m[2] : e.message);
        }
      } },
    { dir: 'VIEWDIR', label: 'view', list: sources(gameDir, 'view', '.view', errors),
      make: (s) => encodeView(parseView(read(s.path), s.path)) },
    { dir: 'SNDDIR', label: 'sound', list: sources(gameDir, 'sound', '.snd', errors),
      make: (s) => encodeSound(parseSound(read(s.path), s.path)) },
  ];

  const vol = [];
  const counts = {};
  for (const kind of kinds) {
    const dir = new Uint8Array(256 * 3).fill(0xFF);
    counts[kind.label] = 0;
    for (const s of kind.list) {
      const data = attempt(() => kind.make(s));
      if (!data) continue;
      if (data.length > 0xFFFF) { errors.push(new SourceError(s.path, 1, `${kind.label} ${s.number} is larger than 64 KB`)); continue; }
      const at = vol.length;
      if (at > 0xFFFFF) { errors.push(new SourceError(s.path, 1, 'VOL.0 is full (1 MB)')); break; }
      dir.set([at >> 16, (at >> 8) & 0xFF, at & 0xFF], s.number * 3);   // vol 0 in the high 4 bits
      vol.push(0x12, 0x34, 0, data.length & 0xFF, data.length >> 8);
      for (const b of data) vol.push(b);
      counts[kind.label]++;
    }
    files[kind.dir] = dir;
  }
  files['VOL.0'] = Uint8Array.from(vol);

  // Recorded plays: demo.play(n) in a logic asks the host for demo/n.txt.
  const demos = {};
  const demoDir = join(gameDir, 'demo');
  if (existsSync(demoDir)) {
    for (const name of readdirSync(demoDir).sort()) {
      if (!/^\d+\.txt$/.test(name)) continue;
      const path = join(demoDir, name);
      attempt(() => { demos[name] = checkDemo(read(path), path); });
    }
  }

  if (errors.length) throw new AggregateError(errors, `${errors.length} error${errors.length === 1 ? '' : 's'}`);
  return { files, said, demos, counts, warnings };
}

// Build and write. Returns what buildGame returns plus `total` bytes.
export async function pack(gameDir, buildDir, log = () => {}, opts = {}) {
  const built = await buildGame(gameDir, opts);
  mkdirSync(join(buildDir, 'said'), { recursive: true });
  // Drop said lists of logics that no longer exist.
  for (const name of readdirSync(join(buildDir, 'said'))) {
    if (/^\d{3}\.json$/.test(name)) rmSync(join(buildDir, 'said', name));
  }
  let total = 0;
  for (const [name, bytes] of Object.entries(built.files)) {
    writeFileSync(join(buildDir, name), bytes);
    total += bytes.length;
  }
  for (const [number, text] of Object.entries(built.said)) {
    writeFileSync(join(buildDir, 'said', `${String(number).padStart(3, '0')}.json`), text);
  }
  // Recorded plays, copied as they are. Old ones are dropped first.
  const demoOut = join(buildDir, 'demo');
  if (existsSync(demoOut)) {
    for (const name of readdirSync(demoOut)) if (/^\d+\.txt$/.test(name)) rmSync(join(demoOut, name));
  }
  for (const [name, text] of Object.entries(built.demos)) {
    mkdirSync(demoOut, { recursive: true });
    writeFileSync(join(demoOut, name), text);
  }
  for (const w of built.warnings) log(`warning: ${w}`);
  const c = built.counts;
  log(`${buildDir}: ${c.logic} logics, ${c.picture} pictures, ${c.view} views, ${c.sound} sounds, ` +
    `VOL.0 ${built.files['VOL.0'].length} bytes, ${total} bytes in all`);
  return { ...built, total };
}

if (isMain(import.meta)) {
  runMain('pack.js game build', 2, async ([gameDir, buildDir, ...extra]) => {
    if (extra.length) throw new Error(`unexpected argument ${extra[0]} (the one-page HTML build is tools/bundle.js)`);
    await pack(gameDir, buildDir, (line) => (line.startsWith('warning') ? console.error(line) : console.log(line)));
  });
}
