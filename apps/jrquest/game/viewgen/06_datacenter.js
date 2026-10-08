// Room 6, the Data Center: the Moat Keeper, the well (three water
// levels), the fans in the castle wall and the cursor in its one window.

// A small canvas for drawings too big to type row by row. '.' is clear.
export class Grid {
  constructor(w, h) { this.w = w; this.h = h; this.px = Array.from({ length: h }, () => Array(w).fill('.')); }
  set(x, y, c) { if (x >= 0 && x < this.w && y >= 0 && y < this.h) this.px[y][x] = c; return this; }
  get(x, y) { return x >= 0 && x < this.w && y >= 0 && y < this.h ? this.px[y][x] : '.'; }
  rect(x1, y1, x2, y2, c) { for (let y = y1; y <= y2; y++) for (let x = x1; x <= x2; x++) this.set(x, y, c); return this; }
  line(x1, y1, x2, y2, c) {
    const dx = Math.abs(x2 - x1), dy = Math.abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    let err = dx - dy, x = x1, y = y1;
    for (;;) {
      this.set(x, y, c);
      if (x === x2 && y === y2) break;
      const e2 = 2 * err;
      if (e2 > -dy) { err -= dy; x += sx; }
      if (e2 < dx) { err += dx; y += sy; }
    }
    return this;
  }
  // A filled ellipse; with edge, its outline in that color.
  ell(cx, cy, rx, ry, c, edge) {
    for (let y = -ry; y <= ry; y++) for (let x = -rx; x <= rx; x++) {
      const d = (x * x) / (rx * rx || 1) + (y * y) / (ry * ry || 1);
      if (d <= 1.05) this.set(cx + x, cy + y, c);
    }
    if (edge) {
      for (let y = -ry; y <= ry; y++) for (let x = -rx; x <= rx; x++) {
        const inside = (xx, yy) => (xx * xx) / (rx * rx || 1) + (yy * yy) / (ry * ry || 1) <= 1.05;
        if (inside(x, y) && !(inside(x - 1, y) && inside(x + 1, y) && inside(x, y - 1) && inside(x, y + 1))) this.set(cx + x, cy + y, edge);
      }
    }
    return this;
  }
  // Stamp rows of text (another cel) at x, y. '.' leaves what is there.
  stamp(rows, x0, y0) { rows.forEach((r, y) => [...r].forEach((ch, x) => { if (ch !== '.') this.set(x0 + x, y0 + y, ch); })); return this; }
  rows() { return this.px.map((r) => r.join('')); }
}

export default function (kit) {
  // ------------------------------------------------- the Moat Keeper
  // A straw hat, red hair, a green shirt under blue overalls, waders.
  const keeper = kit.person(
    { hair: '4', skin: 'C', top: '2', pants: '1', shoes: '0', legs: '8', hat: 'E', hatRows: ['..CCCCC..', 'CCCCCCCCC'] },
    {
      7: '.4CCCCC4.',
      8: '.4.CCC.4.',
      10: '221222122',
      11: '221222122',
      12: '221111122',
      13: '2211E1122',
      14: '221111122',
    },
  );
  kit.view('080_keeper', 'The Moat Keeper', [keeper]);

  // ------------------------------------------------------- the well
  // 22 by 28. Cel 0: you can see the bottom. Cel 1: half. Cel 2: full.
  const well = (level) => {
    const g = new Grid(22, 28);
    // roof
    for (let r = 0; r < 6; r++) g.rect(9 - r * 2 < 0 ? 0 : 9 - r * 2, r, 12 + r * 2 > 21 ? 21 : 12 + r * 2, r, r === 5 ? '4' : 'C');
    g.line(10, 0, 0, 5, '4').line(11, 0, 21, 5, '4');
    // posts and the winch
    g.rect(3, 6, 4, 19, '6').rect(17, 6, 18, 19, '6');
    g.rect(5, 9, 16, 9, '6').rect(8, 8, 13, 10, '6');
    g.set(19, 9, '8').set(20, 9, '8').set(20, 10, '8').set(20, 11, '8');
    // rope: short when the water is high
    const rope = level === 0 ? 17 : level === 1 ? 15 : 13;
    g.rect(11, 11, 11, rope, 'F');
    // the stone ring
    g.rect(0, 19, 21, 27, '7');
    for (let y = 21; y <= 27; y += 2) g.rect(0, y, 21, y, '8');
    for (let y = 20; y <= 26; y += 2) for (let x = (y % 4 === 0 ? 3 : 7); x < 21; x += 8) g.set(x, y, '8');
    g.rect(0, 19, 0, 27, '8').rect(21, 19, 21, 27, '8');
    g.set(0, 27, '.').set(21, 27, '.');
    // the mouth, and what is in it
    g.ell(10, 18, 10, 3, '0', '8');
    g.ell(11, 18, 10, 3, '0', '8');
    g.rect(2, 18, 19, 18, '0');
    if (level === 0) { g.rect(8, 20, 13, 20, '1'); g.rect(11, 11, 11, 18, 'F'); }
    if (level === 1) { g.rect(4, 19, 17, 19, '1'); g.rect(6, 20, 15, 20, '1'); g.rect(8, 19, 10, 19, '9'); g.rect(11, 11, 11, 18, 'F'); }
    if (level === 2) {
      g.rect(3, 17, 18, 17, '9'); g.rect(2, 18, 19, 18, '9'); g.rect(4, 19, 17, 19, '9'); g.rect(6, 20, 15, 20, '1');
      g.rect(6, 18, 9, 18, 'B'); g.rect(13, 17, 15, 17, 'B'); g.set(11, 19, 'B');
    }
    return g.rows();
  };
  kit.view('081_well', 'The town well', [[well(0), well(1), well(2)]]);

  // ------------------------------------------------------- the fans
  // Three fans in the wall, one view so they turn together. Four cels.
  const fan = (step) => {
    const g = new Grid(11, 9);
    g.ell(5, 4, 5, 4, '0', '8');
    const blades = [[[1, 4], [9, 4]], [[2, 2], [8, 6]], [[5, 1], [5, 7]], [[8, 2], [2, 6]]][step];
    g.line(blades[0][0], blades[0][1], blades[1][0], blades[1][1], '7');
    const other = [[[5, 1], [5, 7]], [[8, 2], [2, 6]], [[1, 4], [9, 4]], [[2, 2], [8, 6]]][step];
    g.line(other[0][0], other[0][1], other[1][0], other[1][1], '8');
    g.set(5, 4, 'F');
    return g.rows();
  };
  const fans = [0, 1, 2, 3].map((s) => {
    const g = new Grid(47, 9);
    g.stamp(fan(s), 0, 0).stamp(fan((s + 1) % 4), 18, 0).stamp(fan((s + 2) % 4), 36, 0);
    return g.rows();
  });
  kit.view('082_fans', 'Cooling fans', [fans]);

  // ----------------------------------------------------- the cursor
  kit.view('083_cursor', 'One cursor', [[['AAA'], ['...']]]);
}
