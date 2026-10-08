// The checkpoint-2 views: Sir Junior, King Steve, the castle's people, the
// gate and the three things in the guest room.
export default function (kit) {
  // ---------------------------------------------------------------- JR
  // A PCjr monitor for a head, the system unit for a body, short legs.
  // 7 light gray case, 8 outline, 0 screen, A green face, 4 the red dot.
  const jrFrontTop = [
    '.88888888.',
    '8777777778',
    '8700000078',
    '870A00A078',
    '8700000078',
    '8700AA0078',
    '8777777778',
    '.88888888.',
    '...8778...',
    '8888888888',
    '87777777F8',
    '8700070008',
    '8777777748',
    '8888888888',
  ];
  const jrBackTop = [
    '.88888888.',
    '8777777778',
    '8788888878',
    '8777777778',
    '8788888878',
    '8777777778',
    '8777777778',
    '.88888888.',
    '...8778...',
    '8888888888',
    '8777777778',
    '8778787878',
    '8777777778',
    '8888888888',
  ];
  const jrSideTop = [
    '.8888888',
    '87777778',
    '87777008',
    '877770A8',
    '87777008',
    '877770A8',
    '87777778',
    '.8888888',
    '...8778.',
    '88888888',
    '8777777F',
    '87777008',
    '87777748',
    '88888888',
  ];
  // legs for front and back: stand, left step, stand, right step
  const legsFB = [
    ['..88..88..', '..88..88..', '..88..88..', '.000..000.'],
    ['..88..88..', '..88..88..', '.000..88..', '......000.'],
    ['..88..88..', '..88..88..', '..88..88..', '.000..000.'],
    ['..88..88..', '..88..88..', '..88..000.', '.000......'],
  ];
  const legsSide = [
    ['...88...', '...88...', '...88...', '...000..'],
    ['..88.88.', '..88.88.', '.88...88', '.000..00'],
    ['...88...', '...88...', '...88...', '...000..'],
    ['...888..', '..88.88.', '..88.88.', '..00.000'],
  ];
  kit.view('000_jr', 'Sir Junior', [
    legsSide.map((l) => [...jrSideTop, ...l]),
    { mirror: 0 },
    legsFB.map((l) => [...jrFrontTop, ...l]),
    legsFB.map((l) => [...jrBackTop, ...l]),
  ]);

  // ------------------------------------------------------------ people
  // One body, recolored. H hair, S skin, T top, P pants, B shoes,
  // C hat or crown, M mustache, e eye.
  function person(c, extra = {}) {
    const map = { H: c.hair, S: c.skin, T: c.top, P: c.pants, B: c.shoes, C: c.hat, M: c.mustache || c.skin, e: '0', ...extra };
    const paint = (rows) => rows.map((r) => [...r].map((ch) => map[ch] ?? ch).join(''));
    const top = [
      c.hat ? '.C.C.C.C.' : '.........',
      c.hat ? '.CCCCCCC.' : '..HHHHH..',
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
      ['..PP.PP..', '..PP.PP..', '..SS.SS..', '..SS.SS..', '..SS.SS..', '.BBB.BBB.'],
      ['..PP.PP..', '..PP.PP..', '..SS.SS..', '..SS.SS..', '.BBB.SS..', '.....BBB.'],
    ];
    return legs.map((l) => paint([...top, ...l]));
  }
  const steve = kit.person({ hair: '7', skin: 'C', top: '9', pants: '3', shoes: '6', hat: 'E' });
  kit.view('001_steve', 'King Steve', [steve, { mirror: 0 }, steve, steve]);
  const squire = kit.person({ hair: '6', skin: 'C', top: '2', pants: '6', shoes: '0' });
  kit.view('002_squire', 'The Squire', [squire]);
  const prime = kit.person({ hair: '0', skin: 'C', top: '8', pants: '1', shoes: '0', mustache: '0' });
  kit.view('003_primeagen', 'ThePrimeagen', [prime]);

  // -------------------------------------------------------------- gate
  // The gate is a game cartridge standing in the slot. Cel 0 shut, cel 1
  // half way up, cel 2 gone (one transparent pixel).
  const gateShut = [];
  for (let y = 0; y < 30; y++) {
    if (y === 0 || y === 29) gateShut.push('8'.repeat(23));
    else if (y >= 4 && y <= 12) gateShut.push('8' + '7'.repeat(3) + (y === 4 || y === 12 ? '4'.repeat(15) : '4' + 'F'.repeat(13) + '4') + '7'.repeat(3) + '8');
    else if (y >= 22 && y <= 26) gateShut.push('8' + '7'.repeat(2) + (y % 2 ? '8'.repeat(17) : '7'.repeat(17)) + '7'.repeat(2) + '8');
    else gateShut.push('8' + '7'.repeat(21) + '8');
  }
  kit.view('004_gate', 'The cartridge gate', [[gateShut, gateShut.slice(0, 15), ['.']]]);

  // ------------------------------------------------------------- items
  kit.view('005_book', 'The Technical Reference', [[[
    '888888',
    '811118',
    '81FF18',
    '811118',
    '888888',
  ]]]);
  kit.view('006_bucket', 'A bucket', [[[
    '.8...8.',
    '8.888.8',
    '7777777',
    '7777777',
    '.77777.',
    '.77777.',
    '..888..',
  ]]]);
  kit.view('007_byte', 'A Byte magazine', [[[
    'CCCCCC',
    'CFFFFC',
    'CCCCCC',
  ]]]);
}
