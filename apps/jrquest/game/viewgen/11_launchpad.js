// Room 11, the Launch Pad: Elon Musk, the test-fire flame, and view 9,
// the escaped agents (shared with logic 84: one loop, 12 by 8).
export default function (kit) {
  // Black shirt with an X on it, dark trousers.
  const musk = kit.person(
    { hair: '6', skin: 'C', top: '0', pants: '8', legs: '8', shoes: '0' },
    { 11: '000F0F000', 12: '0000F0000', 13: '000F0F000' });
  kit.view('130_musk', 'Elon Musk', [musk]);

  // The flame under the rocket: three cels of the same fire.
  const flame = [
    [
      '..EEEEEEEEE..',
      '..EFFFFFFFE..',
      '.EEFFFFFFFEE.',
      '.CEEFFFFFEEC.',
      '.CEEEFFFEEEC.',
      'CCCEEEFEEECCC',
      'C.CCEEEEECC.C',
      '..CCCEEECCC..',
      '.C.CCEEECC.C.',
      '...CCCECCC...',
      '..C.CCCCC.C..',
      '....C4C4C....',
      '.4...C4C...4.',
      '.....4C4.....',
      '..4...4...4..',
      '......4......',
    ],
    [
      '..EEEEEEEEE..',
      '..EFFFFFFFE..',
      '..EEFFFFFEE..',
      '.CCEFFFFFECC.',
      'CCEEEFFFEEECC',
      '.CCEEEFEEECC.',
      'C.CEEEEEEEC.C',
      '.CCCCEEECCCC.',
      '..CCCEEECCC..',
      '.C..CCECC..C.',
      '...CCCCCCC...',
      '.4..CC4CC..4.',
      '....4C4C4....',
      '..4..C4C..4..',
      '......4......',
      '.4....4....4.',
    ],
    [
      '..EEEEEEEEE..',
      '..EFFFFFFFE..',
      '.CEFFFFFFFEC.',
      '.CEEFFFFFEEC.',
      '.CCEEFFFEECC.',
      'C.CEEEFEEEC.C',
      '.CCCEEEEECCC.',
      'C.CCEEEEECC.C',
      '..CCCCECCCC..',
      '...CCEEECC...',
      '.C..CCCCC..C.',
      '...4CC4CC4...',
      '..4..4C4..4..',
      '....4.4.4....',
      '.4....4....4.',
      '......4......',
    ],
  ];
  kit.view('131_flame', 'The test fire', [flame]);

  // ---- view 9: the escaped agents ----
  // Five tiny figures, each three wide and four tall: a head, a body,
  // legs. Each cel moves some of them a pixel and swaps their feet.
  function swarm(figs) {
    const g = Array.from({ length: 8 }, () => Array(12).fill('.'));
    for (const [x, y, color, step] of figs) {
      g[y][x + 1] = 'F';
      g[y + 1][x] = color; g[y + 1][x + 1] = color; g[y + 1][x + 2] = color;
      g[y + 2][x + 1] = color;
      if (step) { g[y + 3][x] = '8'; g[y + 3][x + 2] = '8'; }
      else { g[y + 3][x + 1] = '8'; }
    }
    return g.map((r) => r.join(''));
  }
  kit.view('009_swarm', 'The escaped agents', [[
    swarm([[0, 0, '9', 1], [4, 1, 'C', 0], [9, 0, 'A', 1], [2, 4, 'E', 0], [7, 4, 'D', 1]]),
    swarm([[0, 1, '9', 0], [4, 0, 'C', 1], [8, 0, 'A', 0], [1, 4, 'E', 1], [7, 4, 'D', 0]]),
    swarm([[1, 0, '9', 1], [5, 1, 'C', 0], [9, 1, 'A', 1], [2, 4, 'E', 0], [6, 4, 'D', 1]]),
  ]]);
}
