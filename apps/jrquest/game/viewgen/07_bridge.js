// Room 7, Sand Hill Bridge: the Toll Troll, what floats in the stream of
// money, and the wanted poster. Views 90 to 92.
export default function (kit) {
  const FONT = {
    A: ['.#.', '#.#', '###', '#.#', '#.#'], D: ['##.', '#.#', '#.#', '#.#', '##.'],
    E: ['###', '#..', '##.', '#..', '###'], N: ['##.', '#.#', '#.#', '#.#', '#.#'],
    T: ['###', '.#.', '.#.', '.#.', '.#.'], W: ['#.#', '#.#', '###', '###', '#.#'],
    1: ['.#.', '##.', '.#.', '.#.', '###'], 5: ['###', '#..', '##.', '..#', '##.'],
    '%': ['#.#', '..#', '.#.', '#..', '#.#'],
  };
  function label(text, ink, paper) {
    const rows = ['', '', '', '', ''];
    [...text].forEach((ch, i) => {
      FONT[ch].forEach((r, y) => { rows[y] += (i ? paper : '') + [...r].map((c) => (c === '#' ? ink : paper)).join(''); });
    });
    return rows;
  }
  const pad = (rows, w, ch) => rows.map((r) => {
    const left = (w - r.length) >> 1;
    return ch.repeat(left) + r + ch.repeat(w - r.length - left);
  });

  // ---------------------------------------------------------- the troll
  // Green, tusked, in a toll collector's cap and a vest, holding up his
  // rate on a stick.
  const troll = [
    '...111111111...',
    '..11111E11111..',
    '.1111111111111.',
    '2.22222222222.2',
    '222F022222F0222',
    '222222AAA222222',
    '.22222AAA22222.',
    '.22F2222222F22.',
    '..22F00000F22..',
    '...222222222...',
    '..66222222266..',
    '.6666222226666.',
    '266662222266662',
    '266662222266662',
    '266662E2E266662',
    '2.6662222266622',
    '2.66622222666.2',
    '..66666666666..',
    '..11111111111..',
    '..1111...1111..',
    '..222.....222..',
    '..222.....222..',
    '.2222.....2222.',
    '22222.....22222',
  ];
  const trollTalk = troll.map((r, y) => (y === 8 ? '..22F04440F22..' : y === 9 ? '...220000022...' : r));
  const board = ['8'.repeat(13), ...pad(label('15%', '4', 'F'), 11, 'F').map((r) => '8' + r + '8'), '8'.repeat(13)];
  const stick = (rows) => rows.map((r, y) => r + (y < 7 ? board[y] : y < 17 ? '......6......' : '.............'));
  kit.view('090_troll', 'The Toll Troll', [[stick(troll), stick(trollTalk)]]);

  // -------------------------------------------------- coins and a note
  kit.view('091_money', 'Money in the stream', [
    [
      ['.EEE.', 'EE6EE', 'E6EEE', 'EE6EE', '.EEE.'],
      ['.EEE.', 'EF6EE', 'E6EEE', 'EE6EE', '.EEE.'],
      ['.E6E.', '.E6E.', '.E6E.', '.E6E.', '.E6E.'],
      ['.EEE.', 'EE6FE', 'E6EEE', 'EE6EE', '.EEE.'],
    ],
    [
      ['2222222', '2AAFAA2', '2AFFFA2', '2222222'],
      ['.222222', '2AAFAA2', '2AFFFA2', '222222.'],
    ],
  ]);

  // ---------------------------------------------------------- the poster
  const W = 27;
  const paper = (r) => r.padEnd(W, 'F');
  const poster = [
    'F'.repeat(W),
    ...pad(label('WANTED', '4', 'F'), W, 'F'),
    'F'.repeat(W),
    paper('FFFFFFFFFFFF000FFFFF7'),
    paper('FFFFFFFFFFF00000FFF7F'),
    paper('FFFFFFFFFF0000000F7FF'),
    paper('FFFFFFFFFFFCCCCCF7FFF'),
    paper('FFFFFFFFFFFC0C0C6FFFF'),
    paper('FFFFFFFFFFFCCCCCFFFFF'),
    paper('FFFFFFFFFFFFC8CFFFFFF'),
    paper('FFFFFFFFFF1111111FFFF'),
    'F'.repeat(W),
    paper('FF0000000F0000000F0000000'),
    'F'.repeat(W),
    paper('FFFF7777777F777F77777FF'),
    'F'.repeat(W),
  ];
  kit.view('092_poster', 'A wanted poster', [[poster]], '5');
}
