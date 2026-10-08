// The 40x25 text grid: status line, input line, display(), message
// windows, word wrap and %-codes. Mixed into Interpreter.

export const BOX = { TL: 218, TR: 191, BL: 192, BR: 217, H: 196, V: 179 };

// Break text into lines of at most `width` characters. Integer rule: a
// line breaks at the last space at or before the limit, a word longer
// than a line is cut at the limit, and "\n" always starts a new line.
export function wrapText(text, width) {
  const lines = [];
  for (let p of text.split('\n')) {
    while (p.length > width) {
      const k = p.lastIndexOf(' ', width);
      if (k <= 0) { lines.push(p.slice(0, width)); p = p.slice(width); }
      else { lines.push(p.slice(0, k)); p = p.slice(k + 1); }
    }
    lines.push(p);
  }
  return lines;
}

export const screenMethods = {
  put(row, col, ch, fg, bg) {
    if (row < 0 || row > 24 || col < 0 || col > 39) return;
    const cell = this.text[row][col];
    cell.ch = ch;
    cell.fg = fg;
    cell.bg = bg;
  },

  clearRows(r1, r2, bg, fg = 15) {
    for (let r = Math.max(0, r1); r <= r2 && r < 25; r++) {
      for (let c = 0; c < 40; c++) this.put(r, c, 32, fg, bg);
    }
  },

  clearRect(r1, c1, r2, c2, bg) {
    for (let r = r1; r <= r2; r++) for (let c = c1; c <= c2; c++) this.put(r, c, 32, 15, bg);
  },

  // Plain text at a place. "\n" goes to column 0 of the next row.
  writeAt(row, col, str, fg, bg) {
    for (let i = 0; i < str.length; i++) {
      const ch = str.charCodeAt(i);
      if (ch === 10) { row++; col = 0; continue; }
      if (col > 39) { row++; col = 0; }
      this.put(row, col++, ch & 255, fg, bg);
    }
    return col;
  },

  // The colors display() uses. Over a picture AGI only has black on
  // white or a color on black; on a text screen both are as given.
  attr() {
    if (this.gfx && this.textBg) return { fg: 0, bg: 15 };
    return { fg: this.textFg, bg: this.textBg };
  },

  displayText(row, col, text) {
    const a = this.attr();
    const lines = wrapText(text, col < 40 ? 40 - col : 40);
    this.writeAt(row, col, lines.join('\n'), a.fg, a.bg);
  },

  drawStatus() {
    this.shownScore = this.vars[3];
    this.shownSound = this.flags[9];
    if (!this.statusOn) return;
    let s = ` Score:${this.vars[3]} of ${this.vars[7]}`.padEnd(30).slice(0, 30);
    s += `Sound:${this.flags[9] ? 'on' : 'off'}`;
    this.writeAt(this.statusRow, 0, s.padEnd(40), 0, 15);
  },

  promptText() {
    return this.strings[0] || '';
  },

  maxLine() {
    return 39 - this.promptText().length;
  },

  drawInput() {
    if (!this.gfx || !this.inputEnabled) return;
    const row = this.inputRow;
    if (this.win && row >= this.win.r && row < this.win.r + this.win.h) return;
    const s = (this.promptText() + this.line + this.cursorChar).padEnd(40).slice(0, 40);
    this.writeAt(row, 0, s, 15, 0);
  },

  // The whole grid as bytes (ch, fg, bg + 1 per cell), for snapshots.
  textBytes() {
    const b = new Uint8Array(3000);
    let i = 0;
    for (let r = 0; r < 25; r++) {
      for (let c = 0; c < 40; c++) {
        const cell = this.text[r][c];
        b[i++] = cell.ch;
        b[i++] = cell.fg;
        b[i++] = cell.bg + 1;
      }
    }
    return b;
  },

  setTextBytes(b) {
    let i = 0;
    for (let r = 0; r < 25; r++) {
      for (let c = 0; c < 40; c++) {
        const cell = this.text[r][c];
        cell.ch = b[i++];
        cell.fg = b[i++];
        cell.bg = b[i++] - 1;
      }
    }
  },

  // ---- message windows ----

  // White box, red line border, black text. row and col place the text
  // (not the border); -1 centers. Returns nothing; the box stays until
  // closeWindow().
  openWindow(text, row = -1, col = -1, width = 0) {
    if (this.win) this.closeWindow();
    if (width <= 0) width = 30;
    if (width > 38) width = 38;
    const lines = wrapText(text, width);
    // Integer rule: a message of more than 19 lines is cut at 19.
    if (lines.length > 19) lines.length = 19;
    const h = lines.length;
    let w = 1;
    for (const l of lines) if (l.length > w) w = l.length;
    let r = row >= 0 ? row : ((20 - h - 1) >> 1) + 1;
    let c = col >= 0 ? col : (40 - w) >> 1;
    if (r + h > 24) r = 24 - h;
    if (r < 1) r = 1;
    if (c + w > 39) c = 39 - w;
    if (c < 1) c = 1;
    const win = { r: r - 1, c: c - 1, h: h + 2, w: w + 2, saved: [] };
    for (let y = 0; y < win.h; y++) {
      for (let x = 0; x < win.w; x++) {
        const cell = this.text[win.r + y][win.c + x];
        win.saved.push(cell.ch, cell.fg, cell.bg);
        let ch = 32;
        const top = y === 0, bottom = y === win.h - 1, left = x === 0, right = x === win.w - 1;
        if (top && left) ch = BOX.TL;
        else if (top && right) ch = BOX.TR;
        else if (bottom && left) ch = BOX.BL;
        else if (bottom && right) ch = BOX.BR;
        else if (top || bottom) ch = BOX.H;
        else if (left || right) ch = BOX.V;
        this.put(win.r + y, win.c + x, ch, ch === 32 ? 0 : 4, 15);
      }
    }
    for (let i = 0; i < h; i++) this.writeAt(r + i, c, lines[i], 0, 15);
    this.win = win;
  },

  closeWindow() {
    const win = this.win;
    if (!win) return;
    let i = 0;
    for (let y = 0; y < win.h; y++) {
      for (let x = 0; x < win.w; x++, i += 3) {
        this.put(win.r + y, win.c + x, win.saved[i], win.saved[i + 1], win.saved[i + 2]);
      }
    }
    this.win = null;
    this.drawInput();
  },

  // ---- %-codes ----

  message(logic, n) {
    const lg = this.getLogic(logic);
    const m = lg ? lg.messages[n] : null;
    if (m === null || m === undefined) {
      this.log(`logic ${logic} has no message ${n}`);
      return '';
    }
    return m;
  },

  // %vN (optionally %vN|W, zero padded), %mN, %gN, %sN, %wN, %N.
  format(text, logic, depth = 0) {
    if (depth > 8 || text.indexOf('%') < 0) return text;
    let out = '';
    for (let i = 0; i < text.length; i++) {
      const ch = text[i];
      if (ch !== '%' || i + 1 >= text.length) { out += ch; continue; }
      let j = i + 1;
      const kind = text[j];
      const letter = 'vmgsw'.includes(kind);
      if (letter) j++;
      let digits = '';
      while (j < text.length && text[j] >= '0' && text[j] <= '9') digits += text[j++];
      if (digits === '') { out += ch; continue; }
      const n = Number(digits) & 255;
      let piece = '';
      if (kind === 'v') {
        piece = String(this.vars[n]);
        if (text[j] === '|') {
          let wd = '';
          let k = j + 1;
          while (k < text.length && text[k] >= '0' && text[k] <= '9') wd += text[k++];
          if (wd !== '') { piece = piece.padStart(Number(wd), '0'); j = k; }
        }
      } else if (kind === 'm') piece = this.format(this.message(logic, n), logic, depth + 1);
      else if (kind === 'g') piece = this.format(this.message(0, n), 0, depth + 1);
      else if (kind === 's') piece = this.format(this.strings[n % this.strings.length], logic, depth + 1);
      else if (kind === 'w') piece = n >= 1 && n <= this.egoWords.length ? this.egoWords[n - 1].text : '';
      else piece = this.items[n] ? this.items[n].name : '';
      out += piece;
      i = j - 1;
    }
    return out;
  },
};
