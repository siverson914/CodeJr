// Turns the interpreter's 160x200 screen and 40x25 text grid into one
// 320x200 frame of color indexes. The browser shell and the headless
// runner both use this, so a PNG frame is what the browser shows.

import { FONT } from './font.js';

export const FRAME_W = 320;
export const FRAME_H = 200;

export function renderFrame(agi, out = new Uint8Array(FRAME_W * FRAME_H)) {
  const src = agi.screen;
  for (let y = 0; y < 200; y++) {
    let o = y * 320;
    const s = y * 160;
    for (let x = 0; x < 160; x++) {
      const c = src[s + x];
      out[o++] = c;
      out[o++] = c;
    }
  }
  for (let row = 0; row < 25; row++) {
    const cells = agi.text[row];
    for (let col = 0; col < 40; col++) {
      const cell = cells[col];
      const opaque = cell.bg >= 0;
      if (!opaque && (cell.ch === 32 || cell.ch === 0)) continue;
      const g = (cell.ch & 255) * 8;
      for (let r = 0; r < 8; r++) {
        const bits = FONT[g + r];
        let o = (row * 8 + r) * 320 + col * 8;
        for (let b = 0; b < 8; b++, o++) {
          if (bits & (1 << b)) out[o] = cell.fg;
          else if (opaque) out[o] = cell.bg;
        }
      }
    }
  }
  return out;
}

// The text on screen as 25 strings, for "expect" lines and for tests.
export function screenText(agi) {
  const rows = [];
  for (let row = 0; row < 25; row++) {
    let s = '';
    for (let col = 0; col < 40; col++) {
      const ch = agi.text[row][col].ch;
      s += ch >= 32 && ch < 127 ? String.fromCharCode(ch) : ' ';
    }
    rows.push(s);
  }
  return rows;
}
