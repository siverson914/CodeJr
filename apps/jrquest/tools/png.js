// Minimal indexed PNG writer (color type 3), no dependencies.
import { deflateSync } from 'node:zlib';
import { writeFileSync } from 'node:fs';

export const IBM_PALETTE = [
  [0, 0, 0], [0, 0, 170], [0, 170, 0], [0, 170, 170],
  [170, 0, 0], [170, 0, 170], [170, 85, 0], [170, 170, 170],
  [85, 85, 85], [85, 85, 255], [85, 255, 85], [85, 255, 255],
  [255, 85, 85], [255, 85, 255], [255, 255, 85], [255, 255, 255],
];

const CRC = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  CRC[n] = c >>> 0;
}
function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC[(c ^ buf[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'latin1');
  Buffer.from(data).copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}

// pixels: one palette index per pixel. scaleX/scaleY repeat pixels, so a
// 160-wide picture can be written at its real 2:1 shape.
export function encodePng(width, height, pixels, palette = IBM_PALETTE, scaleX = 1, scaleY = 1) {
  const w = width * scaleX, h = height * scaleY;
  const raw = Buffer.alloc((w + 1) * h);
  for (let y = 0; y < h; y++) {
    const row = y * (w + 1);
    const src = ((y / scaleY) | 0) * width;
    for (let x = 0; x < w; x++) raw[row + 1 + x] = pixels[src + ((x / scaleX) | 0)];
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 3;
  const plte = Buffer.from(palette.flat());
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('PLTE', plte),
    chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0)),
  ]);
}

export function writePng(path, width, height, pixels, palette = IBM_PALETTE, scaleX = 1, scaleY = 1) {
  writeFileSync(path, encodePng(width, height, pixels, palette, scaleX, scaleY));
}
