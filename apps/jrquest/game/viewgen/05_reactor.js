// Room 5, the Cold Reactor: the guard and his clipboard.
export default function (kit) {
  // Navy uniform, cap with a badge, clipboard held in his left hand.
  const rows = {
    2: '.0000000.',            // the cap's visor
    10: '11E111111',           // badge
    12: '111116666',
    13: '111116FF6',
    14: '111116F86',
    15: 'CC1116FF6',
    16: 'CC1116666',
  };
  const c = { hair: '6', skin: 'C', top: '1', pants: '1', shoes: '0', legs: '1', hatRows: ['..11111..', '.11E1111.'] };
  const up = kit.person(c, rows)[0];
  // Second cel: he looks down at the clipboard and ticks nothing.
  const down = kit.person(c, { ...rows, 4: '.CCCCCCC.', 5: '.C0CCC0C.', 14: '111116886' })[0];
  kit.view('070_guard', 'The reactor guard', [[up, down]]);
}
