// An 8x8 IBM-style font as data: 256 glyphs of 8 rows, bit 0 = left pixel.
// ASCII 32 to 126 is typed in below; the code page 437 line and block
// characters that message windows and menus use are built from rules.

const ASCII = [
  '0000000000000000', '183c3c1818001800', '3636000000000000', '36367f367f363600',
  '0c3e031e301f0c00', '006333180c666300', '1c361c6e3b336e00', '0606030000000000',
  '180c0606060c1800', '060c1818180c0600', '00663cff3c660000', '000c0c3f0c0c0000',
  '00000000000c0c06', '0000003f00000000', '00000000000c0c00', '6030180c06030100',
  '3e63737b6f673e00', '0c0e0c0c0c0c3f00', '1e33301c06333f00', '1e33301c30331e00',
  '383c36337f307800', '3f031f3030331e00', '1c06031f33331e00', '3f3330180c0c0c00',
  '1e33331e33331e00', '1e33333e30180e00', '000c0c00000c0c00', '000c0c00000c0c06',
  '180c0603060c1800', '00003f00003f0000', '060c1830180c0600', '1e3330180c000c00',
  '3e637b7b7b031e00', '0c1e33333f333300', '3f66663e66663f00', '3c66030303663c00',
  '1f36666666361f00', '7f46161e16467f00', '7f46161e16060f00', '3c66030373667c00',
  '3333333f33333300', '1e0c0c0c0c0c1e00', '7830303033331e00', '6766361e36666700',
  '0f06060646667f00', '63777f7f6b636300', '63676f7b73636300', '1c36636363361c00',
  '3f66663e06060f00', '1e3333333b1e3800', '3f66663e36666700', '1e33070e38331e00',
  '3f2d0c0c0c0c1e00', '3333333333333f00', '33333333331e0c00', '6363636b7f776300',
  '6363361c1c366300', '3333331e0c0c1e00', '7f6331184c667f00', '1e06060606061e00',
  '03060c1830604000', '1e18181818181e00', '081c366300000000', '00000000000000ff',
  '0c0c180000000000', '00001e303e336e00', '0706063e66663b00', '00001e3303331e00',
  '3830303e33336e00', '00001e333f031e00', '1c36060f06060f00', '00006e33333e301f',
  '0706366e66666700', '0c000e0c0c0c1e00', '300030303033331e', '070666361e366700',
  '0e0c0c0c0c0c1e00', '0000337f7f6b6300', '00001f3333333300', '00001e3333331e00',
  '00003b66663e060f', '00006e33333e3078', '00003b6e66060f00', '00003e031e301f00',
  '080c3e0c0c2c1800', '0000333333336e00', '00003333331e0c00', '0000636b7f7f3600',
  '000063361c366300', '00003333333e301f', '00003f190c263f00', '380c0c070c0c3800',
  '1818180018181800', '070c0c380c0c0700', '6e3b000000000000',
];

export const FONT = new Uint8Array(256 * 8);

for (let i = 0; i < ASCII.length; i++) {
  for (let r = 0; r < 8; r++) {
    FONT[(32 + i) * 8 + r] = parseInt(ASCII[i].slice(r * 2, r * 2 + 2), 16);
  }
}

// Single-line box pieces: which arms (up, right, down, left) a glyph has.
// The line is two pixels wide through columns 3-4 and rows 3-4.
const U = 1, R = 2, D = 4, L = 8;
const BOX = {
  179: U | D, 180: U | D | L, 191: D | L, 192: U | R, 193: U | R | L,
  194: R | D | L, 195: U | R | D, 196: R | L, 197: U | R | D | L,
  217: U | L, 218: R | D,
};
for (const code of Object.keys(BOX)) {
  const arms = BOX[code];
  for (let r = 0; r < 8; r++) {
    let bits = 0;
    for (let c = 0; c < 8; c++) {
      const midC = c === 3 || c === 4, midR = r === 3 || r === 4;
      const on = (midC && midR)
        || (midC && r < 3 && (arms & U)) || (midC && r > 4 && (arms & D))
        || (midR && c < 3 && (arms & L)) || (midR && c > 4 && (arms & R));
      if (on) bits |= 1 << c;
    }
    FONT[code * 8 + r] = bits;
  }
}

// Shades and blocks.
for (let r = 0; r < 8; r++) {
  FONT[176 * 8 + r] = r & 1 ? 0x44 : 0x11;
  FONT[177 * 8 + r] = r & 1 ? 0xaa : 0x55;
  FONT[178 * 8 + r] = r & 1 ? 0xbb : 0xee;
  FONT[219 * 8 + r] = 0xff;
  FONT[220 * 8 + r] = r >= 4 ? 0xff : 0;
  FONT[223 * 8 + r] = r < 4 ? 0xff : 0;
  FONT[221 * 8 + r] = 0x0f;
  FONT[222 * 8 + r] = 0xf0;
}

// Arrows for menus and the inventory screen (codes 16, 17, 24, 25, 26, 27).
const EXTRA = {
  16: '0103070f07030100', 17: '080c0e0f0e0c0800',
  24: '0c1e3f0c0c0c0c00', 25: '0c0c0c0c3f1e0c00',
  26: '0018307f30180000', 27: '000c067f060c0000',
};
for (const code of Object.keys(EXTRA)) {
  for (let r = 0; r < 8; r++) {
    FONT[code * 8 + r] = parseInt(EXTRA[code].slice(r * 2, r * 2 + 2), 16);
  }
}

// The IBM 16-color palette as [r, g, b].
export const PALETTE = [
  [0x00, 0x00, 0x00], [0x00, 0x00, 0xaa], [0x00, 0xaa, 0x00], [0x00, 0xaa, 0xaa],
  [0xaa, 0x00, 0x00], [0xaa, 0x00, 0xaa], [0xaa, 0x55, 0x00], [0xaa, 0xaa, 0xaa],
  [0x55, 0x55, 0x55], [0x55, 0x55, 0xff], [0x55, 0xff, 0x55], [0x55, 0xff, 0xff],
  [0xff, 0x55, 0x55], [0xff, 0x55, 0xff], [0xff, 0xff, 0x55], [0xff, 0xff, 0xff],
];
