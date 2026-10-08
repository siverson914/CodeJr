// Room 13, the Cathedral: the two wizards, their apprentices (each one a
// little newer and a little smaller than the one in front), and the
// Confident Door.
export default function (kit) {
  const hat = ['....C....', '...CCC...'];
  // Sam Altman: a green robe, fair hair, no glasses.
  const sam = kit.person(
    { hair: '6', skin: 'C', top: '2', pants: '2', legs: '2', shoes: '0', hat: '2', hatRows: hat },
    { 2: '..22222..', 3: '.6CCCCC6.', 12: '222E2E222', 13: '2222E2222' });
  // Dario Amodei: a brown robe, dark curls, glasses.
  const dario = kit.person(
    { hair: '0', skin: 'C', top: '6', pants: '6', legs: '6', shoes: '0', hat: '6', hatRows: hat },
    { 2: '..66666..', 3: '.00C0C00.', 4: '.0F0C0F0.', 12: '6666E6666', 13: '666EEE666' });
  kit.view('150_sam', 'Sam Altman, wizard', [sam]);
  kit.view('151_dario', 'Dario Amodei, wizard', [dario]);

  // An apprentice is the wizard again with rows taken out and a brighter robe.
  const shrink = (cel, drop, from, to) => cel
    .filter((_, i) => !drop.includes(i))
    .map((r) => r.replaceAll(from, to));
  const dropA = [12, 13, 22];
  const dropB = [11, 12, 13, 14, 21, 22];
  kit.view('152_sam2', 'A newer Sam', [sam.map((c) => shrink(c, dropA, '2', 'A'))]);
  kit.view('153_sam3', 'A newer one yet', [sam.map((c) => shrink(c, dropB, '2', 'B'))]);
  kit.view('154_dario2', 'A newer Dario', [dario.map((c) => shrink(c, dropA, '6', 'E'))]);
  kit.view('155_dario3', 'A newer one yet', [dario.map((c) => shrink(c, dropB, '6', 'D'))]);

  // The Confident Door: oak, arched, with a face that has never once
  // been unsure. Cel 0 shut, 1 ajar, 2 open (the leaf folded to the left).
  const W = 19, H = 31;
  const inArch = (x, y) => {
    if (y >= 8) return true;
    const dx = (x - 9) / 9.6, dy = (8 - y) / 8.4;
    return dx * dx + dy * dy <= 1;
  };
  const face = {
    11: '......0.....0......',
    12: '....000.....000....',
    13: '...0FF0.....0FF0...',
    14: '...0F00.....00F0...',
    15: '....00.......00....',
    19: '....0.........0....',
    20: '.....0.......00....',
    21: '......0000000......',
  };
  const shut = [];
  for (let y = 0; y < H; y++) {
    let row = '';
    for (let x = 0; x < W; x++) {
      if (!inArch(x, y)) { row += '.'; continue; }
      const edge = !inArch(x - 1, y) || !inArch(x + 1, y) || !inArch(x, y - 1) || x === 0 || x === W - 1 || y === H - 1;
      let c = edge ? '4' : (x % 5 === 2 ? '4' : '6');
      if (y === 25 && !edge) c = 'E';
      if (y >= 16 && y <= 17 && x >= 15 && x <= 16) c = 'E';
      const f = face[y];
      if (f && f[x] !== '.' && !edge) c = f[x];
      row += c;
    }
    shut.push(row);
  }
  const ajar = shut.map((r) => {
    // squeeze the leaf to 11 columns; the rest is the dark inside
    let out = '';
    for (let x = 0; x < W; x++) {
      const src = Math.floor(x * W / 11);
      const c = x < 11 ? r[src] : (r[x] === '.' ? '.' : '0');
      out += r[x] === '.' ? '.' : (c === '.' ? '4' : c);
    }
    return out;
  });
  const open = shut.map((r) => [...r].map((c, x) => (c === '.' ? '.' : (x < 3 ? (x === 2 ? '4' : '6') : '0'))).join(''));
  kit.view('156_door', 'The Confident Door', [[shut, ajar, open]]);
}
