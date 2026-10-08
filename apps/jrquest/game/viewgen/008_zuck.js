// View 8: Mark Zuckerberg and his wheelbarrow of gold. He wanders the
// outdoor screens the way King's Quest 1's dwarf did, except he is hiring.
export default function (kit) {
  const body = kit.person({ hair: '6', skin: 'C', top: '8', pants: '1', shoes: '7', legs: '1' });
  // The barrow sits to his right: gold heaped in a gray tray, one wheel.
  const barrow = [
    '.........', '.........', '.........', '.........', '.........', '.........',
    '.........', '.........', '.........', '.........', '.........', '.........',
    '.........', '.........', '...E.E...', '..EEEEE..', '.EEEEEEE.', '888888888',
    '.8777778.', '..87778..', '...888...', '....8....', '...000...', '..07770..',
    '..07770..', '...000...', '.........',
  ];
  const cels = body.map((cel) => cel.map((row, i) => row + barrow[i]));
  kit.view('008_zuck', 'Mark Zuckerberg, hiring', [cels, { mirror: 0 }]);
}
