// Recorded play: feeds an interpreter a script of cycle counts, keys and
// typed text, one game cycle at a time. No DOM in here: the browser shell
// calls step() at the game's own rate, and a test can call it flat out.
//
// The lines are tools/play.js script lines (FORMATS.md, "Recorded play"):
//   N            run N game cycles
//   key NAME     press one key: up down left right home end pgup pgdn
//                center enter esc tab backspace space f1..f10
//   char TEXT    type the characters, no Enter
//   type TEXT    type the characters and press Enter
//   # comment    (and blank lines, and a test's shot, expect and hash
//                lines) are skipped

import { KEY } from './agi.js';

const KEY_NAMES = {
  up: KEY.UP, down: KEY.DOWN, left: KEY.LEFT, right: KEY.RIGHT,
  home: KEY.HOME, end: KEY.END, pgup: KEY.PGUP, pgdn: KEY.PGDN, center: KEY.CENTER,
  enter: KEY.ENTER, esc: KEY.ESC, tab: KEY.TAB, backspace: KEY.BACKSPACE, space: 32,
};
for (let i = 1; i <= 10; i++) KEY_NAMES[`f${i}`] = KEY[`F${i}`];

// Script text to a list of { op: 'run', n } | { op: 'key', code } |
// { op: 'char', text } | { op: 'type', text }. Throws on a line it does
// not know, with the line number.
export function parseDemo(text) {
  const ops = [];
  const lines = String(text).split('\n');
  for (let n = 0; n < lines.length; n++) {
    const t = lines[n].replace(/\r$/, '').trim();
    if (t === '' || t.startsWith('#')) continue;
    const space = t.indexOf(' ');
    const cmd = space < 0 ? t : t.slice(0, space);
    const arg = space < 0 ? '' : t.slice(space + 1);
    if (/^\d+$/.test(t)) {
      if (Number(t) > 0) ops.push({ op: 'run', n: Number(t) });
    } else if (cmd === 'key') {
      const code = KEY_NAMES[arg.trim().toLowerCase()];
      if (code === undefined) throw new Error(`recorded play, line ${n + 1}: no key called "${arg}"`);
      ops.push({ op: 'key', code });
    } else if (cmd === 'char') ops.push({ op: 'char', text: arg.trim() });
    else if (cmd === 'type') ops.push({ op: 'type', text: arg });
    else if (cmd === 'shot' || cmd === 'expect' || cmd === 'hash') continue;
    else throw new Error(`recorded play, line ${n + 1}: don't understand "${t}"`);
  }
  return ops;
}

export class DemoPlayer {
  constructor(text) {
    this.ops = parseDemo(text);
    this.at = 0;        // the next op
    this.left = 0;      // cycles still to run of the current count
    this.cycles = 0;    // cycles run so far
    this.total = 0;     // cycles in the whole script
    for (const o of this.ops) if (o.op === 'run') this.total += o.n;
  }

  get done() {
    return this.left === 0 && this.at >= this.ops.length;
  }

  // Do everything the script does before its next cycle, then run that
  // one cycle. False when the script has nothing left (no cycle is run).
  step(agi) {
    while (this.left === 0 && this.at < this.ops.length) {
      const o = this.ops[this.at++];
      if (o.op === 'run') this.left = o.n;
      else if (o.op === 'key') { agi.keyDown(o.code); agi.keyUp(o.code); }
      else if (o.op === 'char') { for (const ch of o.text) agi.typeChar(ch); }
      else { for (const ch of o.text) agi.typeChar(ch); agi.enter(); }
    }
    if (this.left === 0) return false;
    this.left--;
    this.cycles++;
    agi.cycle();
    return true;
  }
}
