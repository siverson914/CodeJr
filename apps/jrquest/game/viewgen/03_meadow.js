// Room 3, Token Meadow: DHH, his fencing apprentices, the race car, the
// cashmere goat, the token flowers and the signpost. Views 50 to 55.
export default function (kit) {
  // A 3 by 5 font for the labels. '#' is ink.
  const FONT = {
    A: ['.#.', '#.#', '###', '#.#', '#.#'], B: ['##.', '#.#', '##.', '#.#', '##.'],
    E: ['###', '#..', '##.', '#..', '###'], G: ['.##', '#..', '#.#', '#.#', '.##'],
    H: ['#.#', '#.#', '###', '#.#', '#.#'], I: ['#', '#', '#', '#', '#'],
    L: ['#..', '#..', '#..', '#..', '###'], M: ['#.#', '###', '###', '#.#', '#.#'],
    N: ['##.', '#.#', '#.#', '#.#', '#.#'], P: ['##.', '#.#', '##.', '#..', '#..'],
    T: ['###', '.#.', '.#.', '.#.', '.#.'], U: ['#.#', '#.#', '#.#', '#.#', '###'],
    V: ['#.#', '#.#', '#.#', '#.#', '.#.'],
  };
  // Five rows of text, ink and paper as color digits, one column between letters.
  function label(text, ink, paper) {
    const rows = ['', '', '', '', ''];
    [...text].forEach((ch, i) => {
      FONT[ch].forEach((r, y) => { rows[y] += (i ? paper : '') + [...r].map((c) => (c === '#' ? ink : paper)).join(''); });
    });
    return rows;
  }
  const pad = (rows, w, ch = '.') => rows.map((r) => {
    const left = (w - r.length) >> 1;
    return ch.repeat(left) + r + ch.repeat(w - r.length - left);
  });

  // ---------------------------------------------------------------- DHH
  // Curly dark hair, a white racing suit with a red stripe, a stopwatch
  // in one hand. His sword is stuck in the ground beside him.
  const dhh = kit.person({ hair: '0', skin: 'C', top: 'F', pants: 'F', shoes: '4', legs: 'F' }, {
    0: '..0.0.0..',
    1: '.0000000.',
    6: '.C8CCC8C.',
    10: 'FFF4FFFFF',
    11: 'FFF4FFFFF',
    12: 'FFF4F1FFF',
    13: 'FFF4FFFFF',
    14: 'FFF4FFFFF',
    15: 'CCF4FFFEE',
    16: 'CCF4FFFEE',
  });
  const sword = [];
  for (let y = 0; y < 27; y++) {
    if (y < 9) sword.push('....');
    else if (y === 9) sword.push('..E.');
    else if (y < 13) sword.push('..6.');
    else if (y === 13) sword.push('.EEE');
    else sword.push('..7.');
  }
  const dhhCels = dhh.map((cel) => cel.map((r, y) => r + sword[y]));
  kit.view('050_dhh', 'DHH', [dhhCels]);

  // -------------------------------------------------------- apprentices
  // Three identical little agents with square heads, fencing in a row.
  const appA = [
    '...7777.....',
    '..777777....',
    '..7B77B7...F',
    '..777777..F.',
    '...3333..F..',
    '..333333F...',
    '.33333336...',
    '.3.3333.....',
    '.7.3333.....',
    '...3333.....',
    '..33..33....',
    '..33..33....',
    '..3....3....',
    '.00...00....',
  ];
  const appB = [
    '............',
    '...7777.....',
    '..777777....',
    '..7B77B7....',
    '..777777....',
    '...3333.....',
    '..3333336FFF',
    '.33333333...',
    '.3.3333.....',
    '.7.3333.....',
    '..333.33....',
    '.33....33...',
    '.3......3...',
    '00......00..',
  ];
  const row3 = (a, b, c) => a.map((r, y) => r + '.' + b[y] + '.' + c[y]);
  kit.view('051_apprentices', 'A row of fencing apprentices', [[
    row3(appA, appB, appA), row3(appB, appA, appB),
  ]]);

  // ----------------------------------------------------------- race car
  kit.view('052_racecar', 'A Le Mans race car', [[[
    '.........................44444.',
    '.........................4...4.',
    '...........0BBBB0........4...4.',
    '.........44BBBBBB4444....4.....',
    '....444444444444444444444444...',
    '.4444444444FFF4444444444444444.',
    '44444000444F0F44444444000444444',
    'EE4407770444444444444077704444C',
    '...007770000000000000077700....',
    '....000................000.....',
  ]]]);

  // --------------------------------------------------------------- goat
  const goatUp = [
    '.............7.7',
    '.............7.7',
    '............FFFF',
    '...........FF0FF',
    '7FFFFFFFFFFFFFFC',
    'FFFFFFFFFFFFF7..',
    'FFFFFFFFFFFFF7..',
    'FFFFFFFFFFFF....',
    '.F7FFFFFF7FF....',
    '.F.F.....F.F....',
    '.F.F.....F.F....',
    '.8.8.....8.8....',
  ];
  const goatDown = [
    '................',
    '................',
    '................',
    '................',
    '7FFFFFFFFFFF....',
    'FFFFFFFFFFFFF7.7',
    'FFFFFFFFFFFFFF7.',
    'FFFFFFFFFFFFFFFF',
    '.F7FFFFFF7FFF0FF',
    '.F.F.....F.FFFFC',
    '.F.F.....F.F..7.',
    '.8.8.....8.8....',
  ];
  kit.view('053_goat', 'A cashmere goat', [[goatUp, goatUp, goatUp, goatDown, goatDown, goatUp, goatDown, goatDown]]);

  // -------------------------------------------------------------- tokens
  // A token is a piece of a word on a stem. Loop 0: un, believ, able,
  // ing, the. Each is one cel.
  function token(text, paper) {
    const text5 = label(text, '0', paper);
    const w = text5[0].length + 2;
    const tag = [paper.repeat(w), ...text5.map((r) => paper + r + paper), paper.repeat(w)];
    const mid = w >> 1;
    const stem = (l, r) => '.'.repeat(mid - 2) + l + '2' + r + '.'.repeat(w - mid - 2);
    return [...tag, stem('..', '.'), stem('A2', '.'), stem('..', 'A'), stem('..', '.'), stem('.A', '.'), stem('..', '.')].map((r) => r.padEnd(w, '.'));
  }
  kit.view('054_tokens', 'Token flowers', [[
    token('UN', 'E'), token('BELIEV', 'B'), token('ABLE', 'D'), token('ING', 'C'), token('THE', 'F'),
  ]]);

  // ------------------------------------------------------------ signpost
  const board = label('MAP', 'E', '6');
  const signTop = ['8'.repeat(15), ...pad(board, 13, '6').map((r) => '8' + r + '8'), '8'.repeat(15)];
  const post = [];
  for (let y = 0; y < 15; y++) {
    if (y === 2) post.push('....87778......');
    else if (y === 3 || y === 5) post.push('....84448......');
    else if (y === 4) post.push('....84048......');
    else if (y === 6) post.push('....87778......');
    else post.push('.....868.......');
  }
  kit.view('055_signpost', 'The signpost', [[[...signTop, ...post]]]);
}
