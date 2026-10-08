// Room 12, the Golden Palace. Views 140 to 147.
export default function (kit) {
  // The King of the Palace, on his balcony. Three things carry it at nine
  // pixels: yellow hair swept forward past the forehead, a navy suit, and
  // a red tie that goes all the way down. Cel 1 raises a hand.
  const king = kit.person(
    { hair: 'E', skin: 'C', top: '1', pants: '1', shoes: '0', legs: '1' },
    {
      0: '..EEEEE..',
      1: 'EEEEEEEE.',
      2: 'EEEEEEEEE',
      3: '.EECCCCE.',
      6: '.CC444CC.',
      9: '.11F4F11.',
      10: '111F4F111',
      11: '111141111',
      12: '111141111',
      13: '111141111',
      14: '111141111',
      15: 'CC11411CC',
      16: 'CC11411CC',
    },
  )[0];
  const wave = king.map((row, i) => ({ 7: '..CCCCC.C', 8: '...CCC..C', 9: '.11F4F111', 10: '111F4F11.', 11: '11114111.', 12: '11114111.', 13: '11114111.', 14: '11114111.', 15: 'CC11411..', 16: 'CC11411..' }[i] || row));
  kit.view('140_trump', 'The King of the Palace', [[king, wave]]);

  // A palace guard: gold helmet, red coat, and a halberd taller than he is.
  const guard = kit.person(
    { hair: '6', skin: 'C', top: '4', pants: '1', shoes: '0', legs: '1' },
    { 0: '...EEE...', 1: '..EEEEE..', 2: '.EEEEEEE.', 3: '.ECCCCCE.', 9: '.44E4E44.', 11: '444E4E444', 13: '444E4E444', 14: 'EEEEEEEEE' },
  )[0];
  const halberd = (row, i) => `${row}${i === 0 ? '.7' : i === 1 || i === 2 ? '77' : i === 3 ? '.7' : i === 15 || i === 16 ? 'C6' : '.6'}`;
  kit.view('141_guard', 'A palace guard', [[guard.map(halberd)], { mirror: 0 }]);

  // The sign painter: a white cap and overalls with every color on them.
  // Cel 0 brush down, cel 1 brush up at the sign on his left.
  const painter = kit.person(
    { hair: '6', skin: 'C', top: 'F', pants: 'F', shoes: '0', legs: 'F' },
    { 0: '..FFFFF..', 1: '.FFFFFFF.', 2: 'FFFFFFF..', 10: 'FFF4FFFFF', 12: 'FFFFFEFFF', 13: 'F9FFFFFFF', 18: '..FF2FF..' },
  )[0];
  const down = painter.map((row, i) => (i === 17 ? `E.${row}` : i === 16 ? `6C${row.slice(1)}.`.slice(0, 11) : `..${row}`));
  const up = painter.map((row, i) => (i === 8 ? `E.${row}` : i === 9 ? `6.${row}` : i === 10 ? `CC${row}` : i >= 15 && i <= 16 ? `...${row.slice(1)}` : `..${row}`));
  kit.view('142_painter', 'The sign painter', [[down, up]]);

  // The sign he is working on. Cel 0 says AI in old black letters, cel 1
  // has the A painted out, cels 2 and 3 say SI with the S still wet.
  const board = (first, ink) => {
    const A = ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'];
    const S = ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'];
    const NONE = ['.....', '.....', '.....', '.....', '.....', '.....', '.....'];
    const I = ['###', '.#.', '.#.', '.#.', '.#.', '.#.', '###'];
    const g = first === 'A' ? A : first === 'S' ? S : NONE;
    const bg = first === 'A' ? 'F' : 'E';
    const rows = ['66666666666666666', `6${'F'.repeat(15)}6`];
    for (let r = 0; r < 7; r++) {
      const a = [...g[r]].map((ch) => (ch === '#' ? ink : bg)).join('');
      const i = [...I[r]].map((ch) => (ch === '#' ? '0' : 'F')).join('');
      rows.push(`6F${bg}${a}${bg}FF${i}FF6`);
    }
    rows.push(`6${'F'.repeat(15)}6`, '66666666666666666');
    for (let r = 0; r < 9; r++) rows.push('.......666.......');
    return rows;
  };
  kit.view('143_sign', 'A sign, being corrected', [[board('A', '0'), board(' ', '0'), board('S', '4'), board('S', '4')]]);

  // The groundskeeper: a straw hat, green overalls, a rake.
  const keeper = kit.person(
    { hair: '7', skin: 'C', top: '2', pants: '2', shoes: '6', legs: '2' },
    { 0: '..EEEEE..', 1: '..EEEEE..', 2: 'EEEEEEEEE', 9: '.2F222F2.', 10: '22F222F22' },
  )[0];
  kit.view('144_groundskeeper', 'The groundskeeper', [[keeper.map((row, i) => `${i === 2 ? '878' : i === 3 ? '8.8' : i > 3 ? '.6.' : '...'}${i === 15 || i === 16 ? row : row}`)]]);

  // The Accord, hanging on the palace wall: parchment, a ribbon, a seal,
  // and nothing written under "penalties".
  kit.view('145_accord', 'The Accord', [[[
    '66666666',
    '6EEEEEE6',
    '.EFFFFE.',
    '.E8888E.',
    '.EFFFFE.',
    '.E8888E.',
    '.EFFFFE.',
    '.E88FFE.',
    '.EFFFFE.',
    '.EFFFFE.',
    '.EFF44E.',
    '.EFF44E.',
    '6EEEEEE6',
    '66666666',
  ]]]);

  // The water ledger, on a nail by the shed door.
  kit.view('146_ledger', 'The golf ledger', [[[
    '..8..',
    '11111',
    '1FFF1',
    '1F8F1',
    '1FFF1',
    '1F8F1',
    '11111',
  ]]]);

  // A sprinkler on the eighteenth green, at noon.
  const spr = (a) => [
    a ? '..B...B.....B...B..' : '.B...B.......B...B.',
    a ? '.B..B..B...B..B..B.' : 'B..B..B.....B..B..B',
    a ? 'B..B..B..8..B..B..B' : '..B..B...8...B..B..',
    a ? '.....B...8...B.....' : '....B..B.8.B..B....',
    '.........8.........',
    '........888........',
  ];
  kit.view('147_sprinkler', 'A sprinkler', [[spr(true), spr(false)]]);
}
