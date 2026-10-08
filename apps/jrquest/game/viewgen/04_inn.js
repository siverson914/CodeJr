// Room 4, the Godfathers' Inn: Geoffrey Hinton, Yann LeCun, Gary Marcus,
// Sir Tandy, the cards on the table, the answer key, the inn sign and the
// steam off the soup. Views 60 to 66.
export default function (kit) {
  const FONT = {
    E: ['###', '#..', '##.', '#..', '###'], I: ['###', '.#.', '.#.', '.#.', '###'],
    K: ['#.#', '#.#', '##.', '#.#', '#.#'], N: ['##.', '#.#', '#.#', '#.#', '#.#'],
    Y: ['#.#', '#.#', '.#.', '.#.', '.#.'],
    0: ['###', '#.#', '#.#', '#.#', '###'], 1: ['.#.', '##.', '.#.', '.#.', '###'],
    '%': ['#.#', '..#', '.#.', '#..', '#.#'],
  };
  function label(text, ink, paper) {
    const rows = ['', '', '', '', ''];
    [...text].forEach((ch, i) => {
      FONT[ch].forEach((r, y) => { rows[y] += (i ? paper : '') + [...r].map((c) => (c === '#' ? ink : paper)).join(''); });
    });
    return rows;
  }
  const framed = (rows, edge, paper) => {
    const w = rows[0].length + 4;
    return [edge.repeat(w), ...rows.map((r) => edge + paper + r + paper + edge), edge.repeat(w)];
  };

  // ------------------------------------------------------------ Hinton
  // White hair, a dark blue jacket over a light shirt.
  const hinton = kit.person({ hair: 'F', skin: 'C', top: '1', pants: '8', shoes: '0', legs: '8' }, {
    0: '..FFFFF..',
    1: '.FFFFFFF.',
    2: 'FFFFFFFFF',
    3: 'FFCCCCCFF',
    9: '.11F7F11.',
    10: '111F7F111',
    11: '1111F1111',
  });
  kit.view('060_hinton', 'Geoffrey Hinton', [hinton]);

  // ------------------------------------------------------------- LeCun
  // Dark hair going gray, heavy black glasses, a dark red sweater.
  const lecun = kit.person({ hair: '8', skin: 'C', top: '4', pants: '1', shoes: '0', legs: '1' }, {
    1: '..80808..',
    4: '00e0C0e00',
    9: '.44FFF44.',
  });
  kit.view('061_lecun', 'Yann LeCun', [lecun]);

  // ------------------------------------------------------- Gary Marcus
  // Curly dark hair, a short beard, a gray jacket, a red pen held up.
  const marcus = kit.person({ hair: '0', skin: 'C', top: '8', pants: '1', shoes: '0', legs: '1', mustache: '0' }, {
    0: '.0.0.0.0.',
    1: '.0000000.',
    7: '..00000..',
    9: '.88FFF88.',
    10: '888FFF888',
    11: '8888F8888',
    13: '8888888.4',
    14: '88888884.',
    15: 'CC8888C..',
    16: 'CC88888..',
  });
  kit.view('062_marcus', 'Gary Marcus', [marcus]);

  // --------------------------------------------------------- Sir Tandy
  // Sir Junior's exact colors, with a laurel, a sash and a medal: the
  // one who got the credit.
  const tandy = [
    '.A.A..A.A.',
    'A.AEEEEA.A',
    '.88888888.',
    '8777777778',
    '8700000078',
    '870B00B078',
    '8700000078',
    '870BBBB078',
    '8777777778',
    '.88888888.',
    '...8778...',
    '8888888844',
    '8777777448',
    '8700074408',
    '87EE744778',
    '88EE448888',
    '..88..88..',
    '..88..88..',
    '..88..88..',
    '.000..000.',
  ];
  kit.view('063_tandy', 'Sir Tandy', [[tandy]]);

  // ------------------------------------------- cards and the answer key
  kit.view('064_cards', 'Two forecasts and an answer key', [
    [framed(label('10%', '0', 'F'), '7', 'F'), framed(label('0%', '0', 'F'), '7', 'F')],
    [[
      '777777777777777',
      '7FFFFFFFFFFFFF7',
      ...label('KEY', '4', 'F').map((r) => '7F' + r + 'F7'),
      '7FFFFFFFFFFFFF7',
      '777777777777777',
    ]],
  ]);

  // ---------------------------------------------------------- inn sign
  const inn = label('INN', 'E', '4');
  kit.view('065_innsign', 'The inn sign', [[[
    '6666666666666',
    ...inn.map((r) => '64' + r + '46'),
    '6444444444446',
    '6666666666666',
  ].map((r) => r.padEnd(15, '6'))]]);

  // -------------------------------------------------------------- steam
  kit.view('066_steam', 'Steam off the soup', [[
    ['.7...', '..7..', '.7...', '...7.', '..7..'],
    ['..7..', '.7...', '..7..', '..7..', '...7.'],
    ['...7.', '..7..', '.7...', '..7..', '.7...'],
  ]]);
}
