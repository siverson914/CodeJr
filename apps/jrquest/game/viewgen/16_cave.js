// Room 16, the Dragon's Cave: the dragon (breathing), its fire, the
// Mirror (asleep and awake) and the glow of the Shield.
// The big ones are painted with shapes, not typed: a grid, ellipses,
// thick lines, then a black edge all round.
export default function (kit) {
  const grid = (w, h) => Array.from({ length: h }, () => Array(w).fill('.'));
  const done = (g) => g.map((r) => r.join(''));
  const put = (g, x, y, c) => { x = Math.round(x); y = Math.round(y); if (g[y] && x >= 0 && x < g[0].length) g[y][x] = c; };
  const ell = (g, cx, cy, rx, ry, c) => {
    for (let y = Math.floor(cy - ry); y <= Math.ceil(cy + ry); y++)
      for (let x = Math.floor(cx - rx); x <= Math.ceil(cx + rx); x++)
        if (((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1) put(g, x, y, c);
  };
  const thick = (g, pts, r0, r1, c) => {
    for (let i = 0; i + 1 < pts.length; i++) {
      const [x1, y1] = pts[i], [x2, y2] = pts[i + 1];
      for (let t = 0; t <= 1; t += 0.04) {
        const k = (i + t) / (pts.length - 1), r = r0 + (r1 - r0) * k;
        ell(g, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, r, r * 0.8, c);
      }
    }
  };
  const tri = (g, a, b, c2, col) => {
    const s = (p, q, r) => (p[0] - r[0]) * (q[1] - r[1]) - (q[0] - r[0]) * (p[1] - r[1]);
    for (let y = 0; y < g.length; y++) for (let x = 0; x < g[0].length; x++) {
      const p = [x, y], d1 = s(p, a, b), d2 = s(p, b, c2), d3 = s(p, c2, a);
      if (!((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0))) put(g, x, y, col);
    }
  };
  const edge = (g, c) => {
    const out = g.map((r) => [...r]);
    for (let y = 0; y < g.length; y++) for (let x = 0; x < g[0].length; x++) {
      if (g[y][x] !== '.') continue;
      const n = [[1, 0], [-1, 0], [0, 1], [0, -1]].some(([dx, dy]) => g[y + dy] && g[y + dy][x + dx] && g[y + dy][x + dx] !== '.');
      if (n) out[y][x] = c;
    }
    return out;
  };

  // ------------------------------------------------------------ dragon
  // 66 by 46, facing right, head hung over the end of its shelf. b is
  // the breath: 0 out, 1, 2 in. Green hide, yellow belly, red wings.
  function dragon(b, blink) {
    const g = grid(66, 46);
    // tail, curling off to the left
    thick(g, [[18, 34], [8, 38], [3, 33], [6, 27]], 4, 1, '2');
    // far wing
    tri(g, [20, 22], [30, 3 - b], [36, 22], '4');
    tri(g, [30, 22], [42, 7 - b], [42, 24], '4');
    // body
    ell(g, 24, 31, 16, 9 + b * 0.6, '2');
    ell(g, 25, 36 + b * 0.3, 12, 4.5, 'E');
    ell(g, 20, 27 - b * 0.4, 9, 3, 'A');
    // near wing, folded
    tri(g, [10, 24], [17, 5 - b], [25, 23], 'C');
    tri(g, [17, 24], [27, 9 - b], [31, 24], 'C');
    for (let y = 6 - b; y < 24; y++) put(g, 17 + (y - 5 + b) * 0.42, y, '2');
    for (let y = 10 - b; y < 24; y++) put(g, 27 + (y - 9 + b) * 0.2, y, '2');
    // neck: up, over, and down to the head
    thick(g, [[36, 27], [43, 17], [50, 14], [55, 20], [56, 29]], 5, 3.4, '2');
    thick(g, [[38, 30], [44, 21], [50, 18]], 1.4, 1.2, 'E');
    // spines along the neck
    [[41, 13], [45, 10], [50, 8], [55, 11], [59, 16]].forEach(([x, y]) => { put(g, x, y, 'A'); put(g, x, y + 1, 'A'); });
    // head: skull, snout pointing down and right, open jaw
    ell(g, 56, 32, 6, 4.5, '2');
    tri(g, [53, 33], [64, 39], [57, 39], '2');
    tri(g, [52, 37], [60, 45], [53, 42], '2');
    // mouth: a hot throat between the jaws
    tri(g, [56, 39], [63, 41], [59, 44], b === 2 ? 'C' : 'E');
    // horns, eye, nostril, teeth
    put(g, 52, 26, 'F'); put(g, 51, 25, 'F'); put(g, 50, 24, 'F');
    put(g, 56, 26, 'F'); put(g, 56, 25, 'F'); put(g, 55, 24, 'F');
    if (blink) { put(g, 57, 31, '0'); put(g, 58, 31, '0'); }
    else { put(g, 57, 31, 'E'); put(g, 58, 31, 'C'); put(g, 57, 30, 'E'); }
    put(g, 63, 38, 'C');
    put(g, 58, 40, 'F'); put(g, 61, 41, 'F'); put(g, 56, 43, 'F');
    // legs and claws on the shelf
    ell(g, 16, 40, 4, 3, '2'); ell(g, 33, 40, 4, 3, '2');
    [13, 16, 19].forEach((x) => put(g, x, 43, 'F'));
    [30, 33, 36].forEach((x) => put(g, x, 43, 'F'));
    // belly bands
    for (let x = 16; x < 36; x += 4) { put(g, x, 37, '6'); put(g, x + 1, 38, '6'); }
    return done(edge(g, '0'));
  }
  kit.view('180_dragon', 'The dragon', [[dragon(0), dragon(1), dragon(2), dragon(2, true), dragon(1), dragon(0)]]);

  // -------------------------------------------------------------- fire
  // 30 by 66: a stream from the mouth (top) spreading to the path and on
  // into the lava. Four cels of the same stream with different edges.
  function fire(seed) {
    let s = seed * 7919 + 13;
    const rnd = () => { s = (s * 25173 + 13849) & 0xffff; return s / 65536; };
    const g = grid(30, 66);
    for (let y = 0; y < 66; y++) {
      const k = y / 65;
      const cx = 12 + k * 3 + Math.sin(y / 5 + seed * 1.7) * (0.5 + k * 1.6);
      const hw = 1.6 + k * 9.5 + (rnd() - 0.5) * (1 + k * 3);
      for (let x = 0; x < 30; x++) {
        const d = Math.abs(x - cx) / hw;
        if (d > 1) continue;
        let c = d < 0.3 ? 'F' : d < 0.55 ? 'E' : d < 0.82 ? 'C' : '4';
        if (d < 0.3 && k > 0.35) c = 'E';
        if (d > 0.6 && rnd() < 0.22) continue;        // ragged edges
        if (d > 0.3 && d < 0.6 && rnd() < 0.12) c = 'C';
        put(g, x, y, c);
      }
    }
    // sparks beside the stream
    for (let i = 0; i < 9; i++) put(g, 3 + rnd() * 24, 18 + rnd() * 46, rnd() < 0.5 ? 'E' : 'C');
    return done(g);
  }
  kit.view('181_fire', 'Dragon fire', [[fire(1), fire(2), fire(3), fire(4)]]);

  // ------------------------------------------------------------ mirror
  // 13 by 23: an oval glass in a gold frame on a short stem. Asleep the
  // glass is dark. Awake it is lit and a cursor blinks in it.
  function mirror(glass, extra) {
    const g = grid(13, 23);
    ell(g, 6, 9, 6, 9, 'E');
    ell(g, 6, 9, 4.3, 7.3, glass);
    extra(g);
    // a shadow on the frame's right side, stem and foot
    for (let y = 4; y < 15; y++) if (g[y][12] === 'E') g[y][12] = '6';
    for (let y = 19; y < 21; y++) { put(g, 5, y, '6'); put(g, 6, y, 'E'); put(g, 7, y, '6'); }
    for (let x = 3; x < 10; x++) { put(g, x, 21, 'E'); put(g, x, 22, '6'); }
    return done(g);
  }
  const asleep = (n) => mirror('1', (g) => {
    // a dull reflection that crawls a little, and nothing else
    put(g, 4, 5 + n, '9'); put(g, 4, 6 + n, '9'); put(g, 5, 4 + n, '9');
  });
  const awake = (on) => mirror('B', (g) => {
    put(g, 4, 5, 'F'); put(g, 5, 4, 'F'); put(g, 4, 6, 'F');
    put(g, 8, 13, '9'); put(g, 8, 12, '9');
    if (on) { put(g, 5, 11, '1'); put(g, 6, 11, '1'); put(g, 7, 11, '1'); }
  });
  kit.view('182_mirror', 'The Mirror', [[asleep(0), asleep(1), asleep(2), asleep(1)], [awake(true), awake(false)]]);

  // ------------------------------------------------------- shield glow
  // 16 by 22, drawn over Sir Junior while he is in the fire: the flames
  // part round it.
  function glow(n) {
    const g = grid(16, 22);
    for (let a = 0; a < 64; a++) {
      const t = Math.PI * (a / 63) * 1.25 - Math.PI * 1.125;
      const x = 7.5 + Math.cos(t) * 7.4, y = 10 + Math.sin(t) * 9.6;
      if ((a + n) % 3 === 0) continue;
      put(g, x, y, (a + n) % 2 ? 'B' : 'F');
    }
    return done(g);
  }
  kit.view('183_glow', 'The Shield, held up', [[glow(0), glow(1), glow(2)]]);
}
