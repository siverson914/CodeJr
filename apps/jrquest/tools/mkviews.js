#!/usr/bin/env node
// Writes every generated .view file. The drawings live in game/viewgen/*.js,
// one module per room or group; each exports default (kit) => void and calls
// kit.view(...). A walk cycle is then one drawing plus leg variants and not
// eight hand-typed grids.
// node tools/mkviews.js game/view [--png sheet.png] [--only 5,6,12]
import { readdirSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import { makeKit } from './viewkit.js';
import { writePng } from './png.js';

const out = process.argv[2] || 'game/view';
const kit = makeKit(out);
const genDir = resolve(out, '..', 'viewgen');
for (const name of readdirSync(genDir).sort()) {
  if (name.endsWith('.js')) (await import(pathToFileURL(resolve(genDir, name)).href)).default(kit);
}
const sheet = kit.sheet;

// ------------------------------------------------------------ preview
const pngAt = process.argv.indexOf('--png');
if (pngAt > 0) {
  const W = 150, H = 50;
  const px = new Uint8Array(W * H).fill(2);
  let x0 = 2;
  const onlyAt = process.argv.indexOf('--only');
  const pick = onlyAt > 0 ? process.argv[onlyAt + 1].split(',').map(Number) : sheet.map((_, i) => i);
  for (const [, cel] of pick.map((i) => sheet[i]).filter(Boolean)) {
    if (x0 + cel[0].length + 2 > W) break;
    cel.forEach((row, y) => [...row].forEach((ch, x) => {
      if (ch !== '.') px[(H - cel.length - 2 + y) * W + x0 + x] = parseInt(ch, 16);
    }));
    x0 += cel[0].length + 3;
  }
  writePng(process.argv[pngAt + 1], W, H, px, undefined, 10, 6);
}
console.log(`${sheet.length} cels`);
