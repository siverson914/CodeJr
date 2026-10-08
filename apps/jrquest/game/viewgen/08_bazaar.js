// Room 8, the Bazaar: three stallkeepers, the art seller, two shoppers,
// and the Glass half of the Shield.
export default function (kit) {
  const cap = ['.........', '.CCCCCCC.'];
  // DeepSeek's keeper: blue, with a whale-blue cap.
  const deep = kit.person(
    { hair: '0', skin: 'C', top: '9', pants: '1', shoes: '0', hat: '1', hatRows: cap },
    { 2: '111111111' });
  kit.view('101_deepseek', 'The DeepSeek stallkeeper', [deep]);
  // Qwen's keeper, the one who sells the Glass half: purple, a round hat.
  const qwen = kit.person(
    { hair: '0', skin: 'C', top: '5', pants: '8', shoes: '0', hat: 'D', hatRows: ['..CCCCC..', '.CCCCCCC.'] },
    { 11: '5555E5555', 12: '555EEE555' });
  kit.view('100_keeper', 'The stallkeeper', [qwen], '2');
  // Kimi's keeper: dark, with a crescent on the hat.
  const kimi = kit.person(
    { hair: '0', skin: 'C', top: '8', pants: '0', shoes: '0', hat: '0', hatRows: ['..CCCCC..', '.CCEECCC.'] },
    { 2: '.000E000.' });
  kit.view('102_kimi', 'The Kimi stallkeeper', [kimi]);
  // The art seller: a red beret, a smock with paint on it, a small beard.
  const art = kit.person(
    { hair: '6', skin: 'C', top: '7', pants: '1', legs: '1', shoes: '6', hat: '4', hatRows: ['..CCCCC..', 'CCCCCCC..'], mustache: '6' },
    { 7: '..C666C..', 11: '77C777977', 13: '7777A7777', 14: '77E777777' });
  kit.view('103_artseller', 'The art seller', [art]);
  // The Glass half: half a shield you can see through.
  kit.view('104_glass', 'The Glass half', [[[
    '9999999',
    '9BFBBB9',
    '9BFBBB9',
    '9BBFBB9',
    '9BBBBB9',
    '.9BBBB9',
    '.9BBBB9',
    '..9BBB9',
    '...9BB9',
    '....999',
  ]]]);
  // Shoppers.
  const s1 = kit.person({ hair: '6', skin: 'C', top: 'D', pants: '5', shoes: '0', hat: 'E', hatRows: ['.........', '.CCCCCCC.'] },
    { 2: 'EEEEEEEEE' });
  kit.view('105_shopper', 'A shopper', [s1, { mirror: 0 }], '2');
  const s2 = kit.person({ hair: '8', skin: 'C', top: '3', pants: '6', legs: '6', shoes: '0' });
  kit.view('106_shopper2', 'Another shopper', [s2, { mirror: 0 }]);
}
