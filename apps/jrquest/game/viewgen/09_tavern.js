// Room 9, outside the All-In Tavern. View 110: the bouncer.
export default function (kit) {
  // Bald, sunglasses, an earpiece, a black suit and shoulders two pixels
  // wider than anybody else's in the kingdom.
  const slim = kit.person(
    { hair: 'C', skin: 'C', top: '0', pants: '0', shoes: '8', legs: '0' },
    {
      0: '.........',
      1: '..CCCCC..',
      4: '.0000000.',
      5: '.CCCCCC7.',
      9: '.00F4F00.',
      10: '000F4F000',
      11: '000040000',
      12: '000040000',
    },
  );
  const wide = (cel) => cel.map((row, i) => (i >= 9 && i <= 14 ? `0${row}0` : i === 15 || i === 16 ? `C${row.replace(/C/g, '0')}C` : `.${row}.`));
  // Loop 0: standing guard and one step. Loop 1 mirrors it for walking left.
  kit.view('110_bouncer', 'The bouncer', [slim.map(wide), { mirror: 0 }]);
}
