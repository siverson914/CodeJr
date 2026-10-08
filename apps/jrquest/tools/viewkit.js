// Helpers for drawing views as text grids. See tools/mkviews.js.
import { writeFileSync } from 'node:fs';

export function makeKit(outDir) {
  const sheet = [];   // [label, grid] of every cel, for previews

  // loops: an array of loops; a loop is an array of cels (arrays of
  // equal-length strings, hex digit = color, '.' = transparent) or
  // { mirror: n }. trans is a color no cel uses.
  function view(file, desc, loops, trans = '5') {
    let t = `desc "${desc}"\n`;
    loops.forEach((loop, li) => {
      if (loop.mirror !== undefined) { t += `loop ${li} mirror ${loop.mirror}\n`; return; }
      t += `loop ${li}\n`;
      loop.forEach((cel, ci) => {
        const w = cel[0].length;
        for (const row of cel) {
          if (row.length !== w) throw new Error(`${file} loop ${li} cel ${ci}: ragged row "${row}"`);
          if (row.toUpperCase().includes(trans.toUpperCase())) throw new Error(`${file} loop ${li} cel ${ci}: uses the transparent color ${trans}`);
        }
        t += `cel ${w} ${cel.length} trans=${trans}\n${cel.join('\n')}\n`;
        sheet.push([`${file} ${li}/${ci}`, cel]);
      });
    });
    writeFileSync(`${outDir}/${file}.view`, t);
  }

  // A standing or walking person, 9 wide and 27 tall, from one body
  // template. c: { hair, skin, top, pants, shoes, hat, mustache, legs }
  // as hex digits. hat: a color gives a crown; hatRows replaces the top
  // two rows with your own (use C for the hat color). legs: a color for
  // long trousers (default bare legs in the skin color). rows: { index:
  // '9 chars' } overrides any finished row, for a tie, a jacket, a lute.
  // Returns two cels: standing, and one step.
  function person(c, rows = {}) {
    const map = { H: c.hair, S: c.skin, T: c.top, P: c.pants, B: c.shoes, C: c.hat, M: c.mustache || c.skin, L: c.legs || c.skin, e: '0' };
    const paint = (grid) => grid.map((r, i) => rows[i] || [...r].map((ch) => map[ch] ?? ch).join(''));
    const hat = c.hatRows || (c.hat ? ['.C.C.C.C.', '.CCCCCCC.'] : ['.........', '..HHHHH..']);
    const top = [
      ...hat,
      '.HHHHHHH.',
      '.HSSSSSH.',
      '.SeSSSeS.',
      '.SSSSSSS.',
      '.SMMMMMS.',
      '..SSSSS..',
      '...SSS...',
      '.TTTTTTT.',
      'TTTTTTTTT',
      'TTTTTTTTT',
      'TTTTTTTTT',
      'TTTTTTTTT',
      'TTTTTTTTT',
      'SSTTTTTSS',
      'SSTTTTTSS',
      '..PPPPP..',
      '..PPPPP..',
      '..PP.PP..',
      '..PP.PP..',
    ];
    const legs = [
      ['..PP.PP..', '..PP.PP..', '..LL.LL..', '..LL.LL..', '..LL.LL..', '.BBB.BBB.'],
      ['..PP.PP..', '..PP.PP..', '..LL.LL..', '..LL.LL..', '.BBB.LL..', '.....BBB.'],
    ];
    return legs.map((l) => paint([...top, ...l]));
  }

  return { view, person, sheet };
}
