// Room 10, inside the All-In Tavern. Views 120 to 126: the four hosts,
// the tailor and the term sheet.
//
// Each host has three loops: 0 walking (and standing), 1 its mirror, and
// 2 standing at his chair: cel 0 still, cel 1 his own small movement.
export default function (kit) {
  const host = (file, desc, c, rows, alt) => {
    const walk = kit.person(c, rows);
    const still = walk[0];
    const moved = still.map((row, i) => (alt[i] !== undefined ? alt[i] : row));
    kit.view(file, desc, [walk, { mirror: 0 }, [still, moved]]);
  };
  const talk = { 6: '.CC000CC.' };

  // Chamath Palihapitiya, cold: a thin gray shirt, arms wrapped round
  // himself. His second cel is a shiver.
  host('120_chamath', 'Chamath, cold',
    { hair: '0', skin: '6', top: '7', pants: '1', shoes: '0', legs: '1' },
    { 13: '776777677', 14: '766666667' },
    { 9: '..77777..', 10: '.7777777.', 13: '.7677767.', 14: '.6666666.', 15: '.6777776.', 16: '.6777776.' });
  // The same man in a ribbed cashmere sweater with a roll neck.
  host('121_chamath_warm', 'Chamath, in a sweater',
    { hair: '0', skin: '6', top: 'E', pants: '1', shoes: '0', legs: '1' },
    { 8: '..EEEEE..', 9: '.E7E7E7E.', 10: 'E7E7E7E7E', 11: 'E7E7E7E7E', 12: 'E7E7E7E7E', 13: 'E7E7E7E7E', 14: 'E7E7E7E7E', 15: '66E7E7E66', 16: '66E7E7E66' },
    { 6: '.6600066.' });
  // Jason Calacanis, the moderator: a shaved head, a black shirt and a
  // headset with the microphone already at his mouth.
  host('122_jason', 'Jason, moderating',
    { hair: 'C', skin: 'C', top: '0', pants: '8', shoes: '7', legs: '8' },
    { 0: '.........', 1: '..88888..', 2: '.8CCCCC8.', 3: '8CCCCCCC8', 4: '8C0CCC0C8', 5: '8CCCCCCC.', 6: '87CCCCCC.', 7: '.77CCCC..' },
    { 6: '87CFFFCC.', 7: '.77C0CC..' });
  // David Sacks: brown hair with a parting, a gray suit, a blue tie.
  host('123_sacks', 'Sacks, reviewing',
    { hair: '6', skin: 'C', top: '8', pants: '8', shoes: '0', legs: '8' },
    { 1: '..66666..', 2: '.6666666.', 3: '.66CCCC6.', 9: '.88F1F88.', 10: '888F1F888', 11: '888818888', 12: '888818888', 13: '888818888' },
    talk);
  // David Friedberg: black hair, glasses, a white lab coat over green.
  host('124_friedberg', 'Friedberg, in the corner',
    { hair: '0', skin: 'C', top: 'F', pants: '1', shoes: '0', legs: '1' },
    { 4: '.808C808.', 9: '.FF222FF.', 10: 'FFF222FFF', 11: 'FFFF2FFFF', 12: 'FFFF2FFFF', 13: 'FFFF8FFFF', 14: 'FFFF8FFFF' },
    talk);

  // The tailor: gray hair, a purple waistcoat, a yellow tape measure.
  const tailor = kit.person(
    { hair: '7', skin: 'C', top: 'D', pants: '8', shoes: '0', legs: '8' },
    { 4: '.808C808.', 9: '.DDEDDDD.', 10: 'DDDEDDDDD', 11: 'DDDEDDDDD', 12: 'DDDDEDDDD', 13: 'DDDDEDDDD', 14: 'DDDDDEDDD' },
  );
  kit.view('125_tailor', 'A tailor', [tailor, { mirror: 0 }]);

  // The term sheet on the table: four lines, and a signature on each as
  // the hosts sign. Cel n has n signatures.
  const sheet = (n) => {
    const s = (k) => (n >= k ? '111' : '777');
    return [
      '888888888',
      '8FFFFFFF8',
      '8F44444F8',
      '8FFFFFFF8',
      `8${s(1)}F${s(2)}8`,
      '8FFFFFFF8',
      `8${s(3)}F${s(4)}8`,
      '8FFFFFFF8',
      '888888888',
    ];
  };
  kit.view('126_termsheet', 'The term sheet', [[sheet(0), sheet(1), sheet(2), sheet(3), sheet(4)]]);
}
