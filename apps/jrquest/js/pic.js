// Picture decoder and rasterizer. Shared by the tools and the interpreter,
// and the reference for the 8088 version: integers only.
// See FORMATS.md, "PICTURE".

export const PIC_W = 160;
export const PIC_H = 168;

// 4x4 fill patterns, bit (y & 3) * 4 + (x & 3), 1 = pen color.
export const PATTERNS = [
  0xffff, 0xa5a5, 0xa0a0, 0xa000, 0xfafa, 0xfffa, 0xa050, 0x5a5a,
  0x0f0f, 0x5555, 0x8421, 0x1248, 0xf000, 0x1111, 0x8fff, 0x9669,
];

export function drawPicture(bytes, buf, clear = true) {
  if (clear) buf.fill(0x4f);
  let visOn = false, priOn = false, vis = 0, pri = 0;
  let pattern = 0, color2 = 0;
  let penSize = 0, penRect = false, penSplat = false;
  let p = 0;

  const plot = (x, y) => {
    if (x < 0 || x >= PIC_W || y < 0 || y >= PIC_H) return;
    const i = y * PIC_W + x;
    if (visOn) buf[i] = (buf[i] & 0xf0) | vis;
    if (priOn) buf[i] = (buf[i] & 0x0f) | (pri << 4);
  };

  const line = (x1, y1, x2, y2) => {
    const dx = Math.abs(x2 - x1), dy = Math.abs(y2 - y1);
    const sx = x2 > x1 ? 1 : -1, sy = y2 > y1 ? 1 : -1;
    let x = x1, y = y1;
    plot(x, y);
    if (dx >= dy) {
      let err = dx >> 1;
      for (let n = dx; n > 0; n--) {
        x += sx; err -= dy;
        if (err < 0) { y += sy; err += dx; }
        plot(x, y);
      }
    } else {
      let err = dy >> 1;
      for (let n = dy; n > 0; n--) {
        y += sy; err -= dx;
        if (err < 0) { x += sx; err += dy; }
        plot(x, y);
      }
    }
  };

  const fill = (x0, y0) => {
    if (x0 >= PIC_W || y0 >= PIC_H) return;
    let test;
    if (visOn) {
      if (vis === 15) return;
      test = (i) => (buf[i] & 0x0f) === 15;
    } else if (priOn) {
      if (pri === 4) return;
      test = (i) => (buf[i] >> 4) === 4;
    } else return;
    // A pattern that paints white would never finish: treat it as solid.
    const pat = visOn && pattern && color2 !== 15 ? PATTERNS[pattern] : 0xffff;
    if (!test(y0 * PIC_W + x0)) return;
    const stack = [x0, y0];
    while (stack.length) {
      const y = stack.pop();
      let x = stack.pop();
      let i = y * PIC_W + x;
      if (!test(i)) continue;
      while (x > 0 && test(i - 1)) { x--; i--; }
      let up = false, down = false;
      for (; x < PIC_W && test(i); x++, i++) {
        if (visOn) {
          const c = (pat >> ((y & 3) * 4 + (x & 3))) & 1 ? vis : color2;
          buf[i] = (buf[i] & 0xf0) | c;
        }
        if (priOn) buf[i] = (buf[i] & 0x0f) | (pri << 4);
        if (y > 0) {
          const t = test(i - PIC_W);
          if (t && !up) stack.push(x, y - 1);
          up = t;
        }
        if (y < PIC_H - 1) {
          const t = test(i + PIC_W);
          if (t && !down) stack.push(x, y + 1);
          down = t;
        }
      }
    }
  };

  // Midpoint ellipse. region 1 is the part nearer north and south (steps
  // in x), region 2 the part nearer east and west (steps in y).
  const arc = (cx, cy, rx, ry, mask) => {
    const put = (dx, dy, r1) => {
      const ne = r1 ? 2 : 1, nw = r1 ? 4 : 8, sw = r1 ? 32 : 16, se = r1 ? 64 : 128;
      if (mask & ne) plot(cx + dx, cy - dy);
      if (mask & nw) plot(cx - dx, cy - dy);
      if (mask & sw) plot(cx - dx, cy + dy);
      if (mask & se) plot(cx + dx, cy + dy);
    };
    if (rx === 0 || ry === 0) {
      for (let d = 0; d <= rx; d++) put(d, 0, false);
      for (let d = 0; d <= ry; d++) put(0, d, true);
      return;
    }
    const a2 = rx * rx, b2 = ry * ry;
    let x = 0, y = ry, dx = 0, dy = 2 * a2 * y;
    let d = 4 * b2 - 4 * a2 * ry + a2;
    while (dx < dy) {
      put(x, y, true);
      x++; dx += 2 * b2;
      if (d < 0) d += 4 * (dx + b2);
      else { y--; dy -= 2 * a2; d += 4 * (dx - dy + b2); }
    }
    d = b2 * (2 * x + 1) * (2 * x + 1) + 4 * a2 * (y - 1) * (y - 1) - 4 * a2 * b2;
    while (y >= 0) {
      put(x, y, false);
      y--; dy -= 2 * a2;
      if (d > 0) d += 4 * (a2 - dy);
      else { x++; dx += 2 * b2; d += 4 * (dx - dy + a2); }
    }
  };

  // Pen plot. Rectangles are exact; circles are drawn as rectangles for
  // now (the Sierra circle tables and splatter bitmap are not in yet).
  const penPlot = (x, y) => {
    const s = penSize;
    let left = x - ((s + 1) >> 1);
    if (left < 0) left = 0;
    if (left + s > PIC_W - 1) left = PIC_W - 1 - s;
    let top = y - s;
    if (top < 0) top = 0;
    if (top + 2 * s > PIC_H - 1) top = PIC_H - 1 - 2 * s;
    for (let yy = top; yy <= top + 2 * s; yy++)
      for (let xx = left; xx <= left + s; xx++) plot(xx, yy);
  };

  const arg = () => bytes[p] < 0xf0;
  while (p < bytes.length) {
    const op = bytes[p++];
    if (op === 0xff) break;
    switch (op) {
      case 0xf0: vis = bytes[p++] & 15; visOn = true; break;
      case 0xf1: visOn = false; break;
      case 0xf2: pri = bytes[p++] & 15; priOn = true; break;
      case 0xf3: priOn = false; break;
      case 0xf4: case 0xf5: {
        if (!arg()) break;
        let x = bytes[p++], y = bytes[p++];
        plot(x, y);
        let vertical = op === 0xf4;
        while (p < bytes.length && arg()) {
          const v = bytes[p++];
          if (vertical) { line(x, y, x, v); y = v; }
          else { line(x, y, v, y); x = v; }
          vertical = !vertical;
        }
        break;
      }
      case 0xf6: {
        if (!arg()) break;
        let x = bytes[p++], y = bytes[p++];
        plot(x, y);
        while (p + 1 < bytes.length && arg()) {
          const nx = bytes[p++], ny = bytes[p++];
          line(x, y, nx, ny); x = nx; y = ny;
        }
        break;
      }
      case 0xf7: {
        if (!arg()) break;
        let x = bytes[p++], y = bytes[p++];
        plot(x, y);
        while (p < bytes.length && arg()) {
          const d = bytes[p++];
          let ddx = (d >> 4) & 7, ddy = d & 7;
          if (d & 0x80) ddx = -ddx;
          if (d & 0x08) ddy = -ddy;
          line(x, y, x + ddx, y + ddy); x += ddx; y += ddy;
        }
        break;
      }
      case 0xf8:
        while (p + 1 < bytes.length && arg()) { const x = bytes[p++]; fill(x, bytes[p++]); }
        break;
      case 0xf9: {
        const v = bytes[p++];
        penSplat = !!(v & 0x20); penRect = !!(v & 0x10); penSize = v & 7;
        break;
      }
      case 0xfa:
        while (p < bytes.length && arg()) {
          if (penSplat) p++;
          const x = bytes[p++]; penPlot(x, bytes[p++]);
        }
        break;
      case 0xfb:
        while (p + 4 < bytes.length && arg()) {
          arc(bytes[p], bytes[p + 1], bytes[p + 2], bytes[p + 3], bytes[p + 4]);
          p += 5;
        }
        break;
      case 0xfc: pattern = bytes[p++] & 15; break;
      case 0xfd: color2 = bytes[p++] & 15; break;
      default: break; // 0xfe reserved
    }
  }
  return buf;
}
