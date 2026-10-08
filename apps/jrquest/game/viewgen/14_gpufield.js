// Room 14, the GPU Field: the beanstalk (a valuation chart that grows up
// and to the right) and the scarecrow.
import { Grid } from './06_datacenter.js';

// The chart, in pixels from the foot of the stalk: x right, y up. It dips
// now and then, as charts do, and it ends steeper than it began.
export const STALK = [[4, 0], [12, 7], [18, 4], [27, 14], [33, 10], [42, 24], [47, 20], [56, 44], [60, 40], [67, 74], [72, 118]];
const SW = 78, SH = 120;

export default function (kit) {
  // -------------------------------------------------- the beanstalk
  // Cel 0 is a sprout. Cels 1 to 8 are the chart, a bit more each time.
  // Every cel is cut to what has grown, so its bottom left corner (where
  // the view is positioned) never moves.
  const total = STALK.slice(1).reduce((n, p, i) => n + Math.hypot(p[0] - STALK[i][0], p[1] - STALK[i][1]), 0);
  const grown = (frac) => {
    const g = new Grid(SW, SH);
    const Y = (y) => SH - 1 - y;
    let left = total * frac, tip = STALK[0], dir = [1, 1];
    const knots = [];
    for (let i = 1; i < STALK.length && left > 0; i++) {
      const [x1, y1] = STALK[i - 1];
      let [x2, y2] = STALK[i];
      const len = Math.hypot(x2 - x1, y2 - y1);
      if (len > left) { x2 = Math.round(x1 + (x2 - x1) * left / len); y2 = Math.round(y1 + (y2 - y1) * left / len); }
      left -= len;
      // a stem three pixels thick, light on its left side
      g.line(x1 + 1, Y(y1), x2 + 1, Y(y2), '2');
      g.line(x1, Y(y1), x2, Y(y2), '2');
      g.line(x1 - 1, Y(y1), x2 - 1, Y(y2), 'A');
      tip = [x2, y2];
      dir = [x2 - x1, y2 - y1];
      if (left > 0) knots.push([x2, y2, i]);
    }
    // a leaf at every turn of the chart
    for (const [x, y, i] of knots) {
      const s = i % 2 ? -1 : 1;
      g.set(x + 2 * s, Y(y) - 1, 'A').set(x + 3 * s, Y(y) - 1, 'A').set(x + 3 * s, Y(y) - 2, 'A').set(x + 4 * s, Y(y) - 2, '2');
    }
    if (frac >= 1) {
      // the arrowhead: up and to the right, into the Cloud
      const [x, y] = tip;
      g.line(x, Y(y), x + 4, Y(y) - 1, '2').line(x, Y(y), x - 3, Y(y) + 6, '2').line(x + 4, Y(y) - 1, x + 5, Y(y) + 7, '2');
      g.line(x + 1, Y(y) + 1, x + 4, Y(y) + 5, 'A').line(x, Y(y) + 2, x - 1, Y(y) + 5, 'A');
      g.set(x + 2, Y(y) - 1, '2').set(x + 3, Y(y) - 1, '2');
    } else {
      // a growing tip: two small leaves
      const [x, y] = tip;
      g.set(x - 2, Y(y) - 1, 'A').set(x + 2, Y(y) - 1, 'A').set(x, Y(y) - 1, 'A').set(x - 3, Y(y) - 2, 'A').set(x + 3, Y(y) - 2, 'A');
    }
    // cut to what is drawn
    const rows = g.rows();
    let top = rows.findIndex((r) => /[^.]/.test(r));
    if (top < 0) top = SH - 1;
    let right = 0;
    rows.forEach((r) => { const m = r.search(/\.*$/); if (m > right) right = m; });
    return rows.slice(top).map((r) => r.slice(0, Math.max(right, 9)));
  };
  const sprout = [
    '..A...A..',
    '.AAA.AAA.',
    '..AA2AA..',
    '....2....',
    '....2....',
  ];
  kit.view('160_beanstalk', 'The beanstalk', [[sprout, ...[0.06, 0.14, 0.24, 0.36, 0.5, 0.66, 0.83, 1].map(grown)]]);

  // -------------------------------------------------- the scarecrow
  // Straw for a face, a red shirt, arms out along a stick, one leg (a pole).
  const crow = kit.person(
    { hair: 'E', skin: 'E', top: '4', pants: '1', shoes: '6', hat: '6', hatRows: ['...CCC...', '.CCCCCCC.'] },
    {
      2: '666666666',
      4: '.E0EEE0E.',
      6: '.E0E0E0E.',
      10: '644444446',
      11: 'EE44C44EE',
      12: '.4444444.',
      13: '.44C4444.',
      14: '.4444C44.',
      15: '.4444444.',
      16: '..44444..',
      17: '..11111..',
      18: '..1E111..',
      19: '...E6E...',
      20: '....6....',
      21: '....6....',
      22: '....6....',
      23: '....6....',
      24: '....6....',
      25: '....6....',
      26: '...666...',
    },
  );
  kit.view('161_scarecrow', 'A scarecrow', [[crow[0]]]);
}
