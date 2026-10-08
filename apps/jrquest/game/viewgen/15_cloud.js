// Room 15, the Cloud: the giant (Jensen Huang, in a black leather jacket,
// more than twice the height of anyone else), three merchants, the token
// they pass round, the Chest, and the mainframe's lights and tape reels.
import { Grid } from './06_datacenter.js';

export default function (kit) {
  // ------------------------------------------------------ the giant
  // 28 by 60. Loop 0: holding the Chest, looking down at it. Loop 1: the
  // Chest is on the ground and he is watching the ring, to his right
  // (screen left). Two cels each.
  const giant = (hold, cel) => {
    const g = new Grid(28, 60);
    // hair: silver, swept back
    g.rect(9, 0, 18, 1, '7').rect(8, 1, 19, 4, '7').rect(7, 3, 7, 7, '7').rect(20, 3, 20, 7, '7');
    g.rect(10, 0, 14, 0, 'F').rect(9, 1, 12, 1, 'F');
    // face
    g.rect(8, 4, 19, 12, 'C').rect(9, 13, 18, 13, 'C').rect(10, 14, 17, 14, 'C');
    g.rect(8, 4, 19, 4, '7');
    // glasses, and eyes that look where the money is
    g.rect(9, 6, 12, 8, '0').rect(15, 6, 18, 8, '0').rect(13, 7, 14, 7, '0');
    g.rect(10, 7, 11, 7, 'F').rect(16, 7, 17, 7, 'F');
    if (hold) { g.set(cel ? 11 : 10, 7, '0').set(cel ? 17 : 16, 7, '0'); }
    else { g.set(10, 7, '0').set(16, 7, '0'); if (cel) { g.set(10, 7, 'F').set(11, 7, '0').set(16, 7, 'F').set(17, 7, '0'); } }
    // mouth: a smile when the number goes up
    g.rect(12, 11, 15, 11, '4');
    if (!hold) g.set(11, 10, '4').set(16, 10, '4');
    // neck and a black shirt
    g.rect(12, 15, 15, 16, 'C');
    // the jacket: black leather, a gray shine on the shoulders and zip
    g.rect(5, 16, 22, 17, '0').rect(3, 17, 24, 40, '0');
    g.rect(12, 16, 15, 17, 'C').rect(13, 18, 14, 19, '8');
    g.rect(4, 17, 9, 17, '8').rect(18, 17, 23, 17, '8');
    g.line(11, 18, 13, 24, '8').line(16, 18, 14, 24, '8');
    g.rect(13, 24, 14, 40, '8');
    g.rect(13, 28, 13, 28, '7').rect(13, 34, 13, 34, '7');
    g.rect(3, 39, 24, 40, '8');
    // sleeves
    g.rect(0, 18, 3, 34, '0').rect(24, 18, 27, 34, '0');
    g.rect(0, 18, 0, 30, '8').rect(27, 18, 27, 30, '8');
    if (hold) {
      // both arms round the Chest
      g.rect(0, 30, 5, 35, '0').rect(22, 30, 27, 35, '0');
      chest(g, 7, 25);
      g.rect(5, 30, 7, 33, 'C').rect(20, 30, 22, 33, 'C');
    } else {
      // one hand on his hip, one pointing at the ring
      g.rect(24, 34, 26, 37, 'C');
      g.rect(0, 26, 3, 34, '.');
      g.rect(0, 22, 3, 25, '0').rect(0, 22, 0, 25, '8');
      g.rect(0, 26, 1, 27, 'C');
      if (cel) { g.rect(0, 26, 1, 27, '.'); g.rect(0, 20, 1, 21, 'C'); }
    }
    // trousers and boots
    g.rect(5, 41, 12, 56, '0').rect(15, 41, 22, 56, '0');
    g.rect(5, 41, 5, 56, '8').rect(22, 41, 22, 56, '8');
    g.rect(3, 57, 12, 59, '0').rect(15, 57, 24, 59, '0');
    g.rect(3, 59, 12, 59, '8').rect(15, 59, 24, 59, '8');
    return g.rows();
  };
  // The Chest of Tokens: wood, gold bands, a glow under the lid.
  function chest(g, x, y) {
    g.rect(x, y, x + 13, y + 9, '6');
    g.rect(x, y, x + 13, y, 'E').rect(x, y + 4, x + 13, y + 4, 'E').rect(x, y + 9, x + 13, y + 9, '4');
    g.rect(x, y, x, y + 9, '4').rect(x + 13, y, x + 13, y + 9, '4');
    g.rect(x + 1, y + 3, x + 12, y + 3, 'E');
    g.rect(x + 6, y + 4, x + 7, y + 6, 'E').set(x + 6, y + 5, '0');
    g.set(x + 3, y + 2, 'F').set(x + 9, y + 2, 'F').set(x + 5, y + 1, 'E').set(x + 10, y + 1, 'E');
    g.rect(x + 3, y, x + 3, y + 9, 'E').rect(x + 10, y, x + 10, y + 9, 'E');
  }
  kit.view('170_giant', 'Jensen Huang, a giant', [[giant(true, 0), giant(true, 1)], [giant(false, 0), giant(false, 1)]]);

  const chestCel = new Grid(14, 10);
  chest(chestCel, 0, 0);
  kit.view('173_chest', 'The Chest of Tokens', [[chestCel.rows()]]);

  // -------------------------------------------------- the merchants
  // One view, a loop each. Cel 0 stands; cel 1 has a hand out, passing.
  const pass = (p, hand, sleeve) => [p[0], p[0].map((r, i) => (i === 13 || i === 14 ? r.slice(0, 7) + hand + hand : i === 15 || i === 16 ? sleeve + sleeve + r.slice(2) : r))];
  // the chip maker: a green visor and a green apron, like a circuit board
  const chip = kit.person(
    { hair: '0', skin: 'C', top: '2', pants: '8', shoes: '0', legs: '8', hat: 'A', hatRows: ['.........', '.CCCCCCC.'] },
    { 2: 'AAAAAAAAA', 11: '22E222E22', 13: '222E2E222' },
  );
  // the model maker: a pointed hat and a long robe with stars on it
  const model = kit.person(
    { hair: '7', skin: 'C', top: 'D', pants: 'D', shoes: '4', legs: 'D', hat: 'D', hatRows: ['....C....', '...CCC...'] },
    { 2: '..DDDDD..', 11: 'DDEDDDDDD', 14: 'DDDDDEDDD', 19: '..DDDDD..', 20: '..DDDDD..', 21: '..DEDDD..', 22: '..DDDDD..', 23: '.DDDDDDD.', 24: '.DDDDEDD.', 25: '.DDDDDDD.' },
  );
  // the cloud landlord: a top hat, a blue coat, a ring of keys
  const lord = kit.person(
    { hair: '6', skin: 'C', top: '1', pants: '1', shoes: '0', legs: '8', hat: '0', hatRows: ['..CCCCC..', '..CCCCC..'] },
    { 2: '.0000000.', 10: '1119E9111', 11: '1119E9111', 12: '111999111', 17: '.E11111..', 18: '.E11111..' },
  );
  kit.view('171_merchants', 'Three merchants', [pass(chip, 'C', '2'), pass(model, 'C', 'D'), pass(lord, 'C', '1')]);

  // ------------------------------------------------------ the token
  kit.view('172_token', 'One token', [[
    ['.666.', '6EEE6', '6EFE6', '6EEE6', '.666.'],
    ['..6..', '.6E6.', '.6F6.', '.6E6.', '..6..'],
  ]]);

  // ------------------------------------- the mainframe's lights
  // 23 by 21, over the black panel. Unlit lamps are left clear.
  const lamps = (seed) => {
    const g = new Grid(23, 21);
    let s = seed;
    for (let y = 1; y < 21; y += 3) for (let x = 1; x < 23; x += 3) {
      s = (s * 25173 + 13849) & 0xffff;
      const r = (s >> 8) % 7;
      g.set(x, y, r < 2 ? 'C' : r < 4 ? 'E' : r < 5 ? 'A' : '8');
      g.set(x + 1, y, g.get(x, y));
    }
    return g.rows();
  };
  kit.view('174_lights', 'A panel of lights', [[lamps(7), lamps(99), lamps(1234)]]);

  // ---------------------------------------- the tape reels
  // One view across all four tape cabinets, so they turn together.
  const reels = (step) => {
    const g = new Grid(113, 19);
    for (const x0 of [0, 22, 76, 98]) {
      for (const cx of [x0 + 3, x0 + 11]) {
        g.ell(cx, 5, 3, 4, '7', '8');
        if ((step + (cx > x0 + 3 ? 1 : 0)) % 2) { g.line(cx - 2, 5, cx + 2, 5, '0'); g.line(cx, 2, cx, 8, '0'); }
        else { g.line(cx - 2, 3, cx + 2, 7, '0'); g.line(cx + 2, 3, cx - 2, 7, '0'); }
        g.set(cx, 5, 'F');
      }
      // the tape runs down to the head
      g.line(x0 + 1, 9, x0 + 5, 14, '6').line(x0 + 13, 9, x0 + 9, 14, '6');
      g.rect(x0 + 5, 14, x0 + 9, 16, '8');
      g.set(x0 + 7, 15, step % 2 ? 'C' : 'A');
    }
    return g.rows();
  };
  kit.view('175_reels', 'Tape reels', [[reels(0), reels(1)]]);
}
