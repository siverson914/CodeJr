// The JRQ reference interpreter: AGI version 2 (2.917 behavior) plus the
// JRQ extensions. See FORMATS.md, "The interpreter module".
//
// No DOM, no fs, no process. Everything that is game state is an
// integer, a short string, or a plain object of those, so snapshot()
// can copy it and the 8088 interpreter can reproduce it.

import { loadResource, decodeLogic, decodeWords, decodeObject, saidPatterns, groupNames } from './resources.js';
import { parseLine, saidMatches } from './parser.js';
import { animMethods, newObject, OBJ_FIELDS, MOTION, W, H } from './anim.js';
import { screenMethods } from './screen.js';
import { opsMethods, RUN, missingCommands } from './ops.js';
import { fnv1a, packBytes, unpackBytes, toBase64, fromBase64 } from './util.js';

export { PALETTE } from './font.js';
export { missingCommands };

// Key codes for keyDown(): ASCII, or scan code << 8 for the rest, the
// same numbers set.key takes as its two bytes.
export const KEY = {
  ENTER: 13, ESC: 27, BACKSPACE: 8, TAB: 9,
  UP: 0x4800, DOWN: 0x5000, LEFT: 0x4b00, RIGHT: 0x4d00,
  HOME: 0x4700, PGUP: 0x4900, END: 0x4f00, PGDN: 0x5100, CENTER: 0x4c00,
  F1: 0x3b00, F2: 0x3c00, F3: 0x3d00, F4: 0x3e00, F5: 0x3f00,
  F6: 0x4000, F7: 0x4100, F8: 0x4200, F9: 0x4300, F10: 0x4400,
};

const DIR_KEYS = {
  0x4800: 1, 0x4900: 2, 0x4d00: 3, 0x5100: 4, 0x5000: 5, 0x4f00: 6, 0x4b00: 7, 0x4700: 8, 0x4c00: 0,
};

const WAIT_CODE = { window: 1, anykey: 2, edit: 3, inv: 4, menu: 5, slot: 6, promise: 7 };
const SPINNER = '|/-\\';

// State that is small and plain enough to copy as JSON.
const PLAIN = [
  'horizon', 'playerControl', 'block', 'gfx', 'picShown', 'picTop', 'inputRow',
  'statusRow', 'statusOn', 'inputEnabled', 'textFg', 'textBg', 'cursorChar',
  'line', 'lastLine', 'egoWords', 'win', 'wait', 'stack', 'phase', 'loaded',
  'scanStart', 'menus', 'menuDone', 'menuAllowed', 'snd', 'seed', 'cycles',
  'clockLast', 'eventOrder', 'aiHelps', 'holdKey', 'keyWaiting', 'pendingLine',
  'pendingCtl', 'queue', 'first', 'drawnUpd', 'drawnNon', 'priBase', 'gameId',
  'shownScore', 'shownSound', 'curLogic', 'strings', 'keymap',
];

export class Interpreter {
  constructor(resources, host = {}) {
    this.res = resources;
    this.host = host || {};
    this.words = resources.words ? decodeWords(resources.words) : { dict: Object.create(null), maxParts: 1 };
    this.groupNames = groupNames(this.words.dict);
    const object = resources.object ? decodeObject(resources.object) : { max: 16, items: [] };
    this.items = object.items;
    // The OBJECT file's byte is the highest object number.
    this.maxObjs = (object.max || 16) + 1;
    this.logics = {};
    this.views = {};
    this.sounds = {};
    this.screen = new Uint8Array(160 * 200);
    this.gbuf = new Uint8Array(W * H);
    this.priTable = new Uint8Array(H);
    this.text = [];
    for (let r = 0; r < 25; r++) {
      const row = [];
      for (let c = 0; c < 40; c++) row.push({ ch: 32, fg: 15, bg: -1 });
      this.text.push(row);
    }
    this.vars = new Uint8Array(256);
    this.flags = new Uint8Array(256);
    this.controllers = new Uint8Array(256);
    this.invRoom = new Uint8Array(256);
    this.events = new Uint8Array(256);
    this.spare = newObject(255);
    this.ioDone = null;
    this.reset();
  }

  // The state a new game starts in. restart.game comes back here.
  reset() {
    this.vars.fill(0);
    this.flags.fill(0);
    this.controllers.fill(0);
    this.events.fill(0);
    this.invRoom.fill(0);
    this.items.forEach((it, i) => { this.invRoom[i] = it.room; });
    this.vars[8] = 255;    // free memory: plenty
    this.vars[10] = 2;     // delay: normal speed
    this.vars[20] = 1;     // computer: PCjr
    this.vars[22] = 3;     // sound: three voices and noise
    this.vars[23] = 15;    // volume
    this.vars[24] = 41;    // input line length
    this.vars[26] = 3;     // monitor: 16 colors
    this.flags[5] = 1;     // the first cycle is a new room
    this.flags[9] = 1;     // sound on
    this.flags[11] = 1;    // logic 0 runs for the first time
    this.strings = new Array(24).fill('');
    this.keymap = [];
    this.objs = [];
    for (let i = 0; i < this.maxObjs; i++) this.objs.push(newObject(i));
    this.gbuf.fill(0x4f);
    this.screen.fill(0);
    for (const row of this.text) for (const cell of row) { cell.ch = 32; cell.fg = 15; cell.bg = -1; }
    this.setPriBase(48);
    this.horizon = 36;
    this.playerControl = 1;
    this.block = { on: 0, x1: 0, y1: 0, x2: 0, y2: 0 };
    this.gfx = 1;
    this.picShown = 0;
    this.picTop = 1;
    this.inputRow = 22;
    this.statusRow = 0;
    this.statusOn = 0;
    this.inputEnabled = 1;
    this.textFg = 15;
    this.textBg = 0;
    this.cursorChar = '_';
    this.line = '';
    this.lastLine = '';
    this.egoWords = [];
    this.win = null;
    this.wait = null;
    this.stack = [];
    this.phase = 0;
    this.exitAll = 0;
    this.ended = 0;
    this.loaded = [0];
    this.scanStart = {};
    this.menus = [];
    this.menuDone = 0;
    this.menuAllowed = 1;
    this.snd = null;
    this.seed = 0x1234;
    this.cycles = 0;
    this.clockLast = 0;
    this.eventOrder = [];
    this.aiHelps = 0;
    this.holdKey = 0;
    this.keyWaiting = 0;
    this.pendingLine = null;
    this.pendingCtl = [];
    this.queue = [];
    this.first = 1;
    this.drawnUpd = [];
    this.drawnNon = [];
    this.gameId = '';
    this.shownScore = -1;
    this.shownSound = -1;
    this.curLogic = 0;
  }

  log(text) {
    if (this.host.log) this.host.log(text);
  }

  // 0 to 255. Default: a 16-bit generator both interpreters can run.
  rnd() {
    if (this.host.random) return this.host.random() & 255;
    this.seed = (this.seed * 25173 + 13849) & 0xffff;
    return this.seed >> 8;
  }

  // True while the game waits on the host (a promise), so a headless
  // runner knows to let the event loop turn.
  get pending() {
    return !!(this.wait && this.wait.type === 'promise' && !this.wait.done);
  }

  get delay() {
    return this.vars[10] || 1;
  }

  getLogic(n) {
    if (n in this.logics) return this.logics[n];
    const bytes = loadResource(this.res.logdir, this.res.vols, n);
    let lg = null;
    if (bytes) {
      try { lg = decodeLogic(bytes); } catch (e) { lg = null; }
    }
    this.logics[n] = lg;
    return lg;
  }

  loadLogic(n) {
    if (!this.loaded.includes(n)) this.loaded.push(n);
  }

  // ---- the cycle ----

  cycle() {
    if (this.ended) return;
    if (this.wait) {
      if (!this.pollWait()) return;
      if (this.wait || this.ended) return;
      if (this.phase === 0) this.exitAll = 0;
    }
    for (let rooms = 0; ; rooms++) {
      if (this.phase === 0) {
        if (!this.startCycle()) return;
        this.phase = 1;
        this.stack = [{ n: 0, ip: this.scanStart[0] || 0 }];
      }
      const r = this.runStack();
      if (r === RUN.BLOCKED || this.ended) return;
      if (r === RUN.AGAIN && rooms < 100) {
        // new.room, restart or restore: logic 0 starts over at once.
        if (this.phase === 0) continue;
        this.vars[9] = 0;
        this.vars[4] = 0;
        this.vars[5] = 0;
        this.flags[2] = 0;
        this.controllers.fill(0);
        this.stack = [{ n: 0, ip: this.scanStart[0] || 0 }];
        continue;
      }
      this.endCycle();
      this.phase = 0;
      return;
    }
  }

  // Clock, input, ego's direction, motion. False if a wait began (the
  // AI parser) and the logics must not run yet.
  startCycle() {
    this.cycles++;
    this.tickClock();
    this.flags[2] = 0;
    this.flags[4] = 0;
    this.vars[9] = 0;
    this.vars[19] = 0;
    this.keyWaiting = 0;
    this.controllers.fill(0);
    for (const ctl of this.pendingCtl) this.controllers[ctl] = 1;
    this.pendingCtl = [];

    let lineTaken = false;
    if (this.pendingLine) {
      // The AI parser's answer (or the original line): no second ask.
      const text = this.pendingLine.text;
      this.pendingLine = null;
      this.takeLine(text);
      lineTaken = true;
    }
    while (this.queue.length) {
      const ev = this.queue[0];
      if (ev.k === 'line') {
        // One line a cycle; the next one waits its turn in the queue.
        if (lineTaken) break;
        this.queue.shift();
        if (!this.submitLine(ev.text)) return false;
        lineTaken = true;
        continue;
      }
      this.queue.shift();
      this.handleKey(ev);
    }

    const ego = this.objs[0];
    if (this.playerControl) ego.dir = this.vars[6];
    else this.vars[6] = ego.dir;
    this.checkAllMotions();
    return true;
  }

  endCycle() {
    this.objs[0].dir = this.vars[6];
    if (this.vars[3] !== this.shownScore || this.flags[9] !== this.shownSound) this.drawStatus();
    this.vars[4] = 0;
    this.vars[5] = 0;
    this.flags[5] = 0;
    this.flags[6] = 0;
    this.flags[12] = 0;
    if (this.first) { this.first = 0; this.flags[11] = 0; }
    if (this.gfx) this.updateObjects();
    this.tickSound();
  }

  // v11 seconds, v12 minutes, v13 hours, v14 days.
  tickClock() {
    const now = this.host.time ? this.host.time() | 0 : (this.cycles / 20) | 0;
    if (this.clockLast < 0 || now < this.clockLast) this.clockLast = now;
    let d = now - this.clockLast;
    this.clockLast = now;
    if (d > 86400) d = 86400;
    const v = this.vars;
    for (; d > 0; d--) {
      if (++v[11] < 60) continue;
      v[11] = 0;
      if (++v[12] < 60) continue;
      v[12] = 0;
      if (++v[13] < 24) continue;
      v[13] = 0;
      v[14]++;
    }
  }

  newRoom(n) {
    this.stopSound();
    this.eraseAll();
    for (const o of this.objs) {
      o.flags &= ~(0x40 | 0x01);
      o.flags |= 0x10;
      o.stepTime = o.stepTimeCount = o.cycleTime = o.cycleTimeCount = o.stepSize = 1;
    }
    this.loaded = [0];
    this.loadLogic(n);
    this.playerControl = 1;
    this.block.on = 0;
    this.horizon = 36;
    this.vars[1] = this.vars[0];
    this.vars[0] = n;
    this.vars[4] = 0;
    this.vars[5] = 0;
    const ego = this.objs[0];
    this.vars[16] = ego.view;
    // Ego comes in on the side opposite the edge it left by.
    const edge = this.vars[2];
    if (edge === 1) ego.y = H - 1;
    else if (edge === 2) ego.x = 0;
    else if (edge === 3) ego.y = this.horizon + 1;
    else if (edge === 4) ego.x = W - ego.xSize;
    this.vars[2] = 0;
    this.flags[5] = 1;
    this.exitAll = 1;
    this.drawStatus();
    this.drawInput();
  }

  restart() {
    const sound = this.flags[9];
    if (this.host.stopSound) this.host.stopSound();
    this.reset();
    this.flags[9] = sound;
    this.flags[6] = 1;
    this.exitAll = 1;
  }

  end() {
    this.stopSound();
    this.ended = 1;
    this.exitAll = 1;
    if (this.host.quit) this.host.quit();
  }

  // ---- input from the shell ----

  // Where typed characters go right now: 'edit' (get.string, get.num),
  // 'talk' (the host asked for a line), 'line' (the input line), or
  // null (a dialog has the keyboard, or the game takes no typing).
  editTarget() {
    const w = this.wait;
    if (w) {
      if (w.type === 'edit') return 'edit';
      if (w.type === 'promise' && w.sub === 'line') return 'talk';
      if (w.type === 'anykey' || w.type === 'inv' || w.type === 'menu' || w.type === 'slot') return null;
    }
    return this.inputEnabled && this.gfx ? 'line' : null;
  }

  // A printable character. It shows at once; the cycle only notes it.
  typeChar(ch) {
    const code = typeof ch === 'string' ? ch.charCodeAt(0) : ch;
    if (!(code >= 32 && code <= 126)) return;
    const c = String.fromCharCode(code);
    const t = this.editTarget();
    if (t === 'edit') {
      const w = this.wait;
      const ok = w.then !== 'num' || (code >= 48 && code <= 57);
      if (ok && w.buf.length < w.max) { w.buf += c; this.drawEdit(); }
    } else if (t === 'talk') {
      if (this.wait.buf.length < 38) { this.wait.buf += c; this.drawTalkLine(); }
    } else if (t === 'line') {
      if (this.line.length < this.maxLine()) { this.line += c; this.drawInput(); }
    }
    this.queue.push({ k: 'seen', code });
  }

  backspace() {
    const t = this.editTarget();
    if (t === 'edit') { this.wait.buf = this.wait.buf.slice(0, -1); this.drawEdit(); }
    else if (t === 'talk') { this.wait.buf = this.wait.buf.slice(0, -1); this.drawTalkLine(); }
    else if (t === 'line') { this.line = this.line.slice(0, -1); this.drawInput(); }
    this.queue.push({ k: 'seen', code: 8 });
  }

  // Enter sends the input line when one is being typed and the parser
  // can take it (now, or after the AI parser's current answer). The
  // line is queued, so nothing typed while the game is busy is lost.
  enter() {
    const w = this.wait;
    const free = !w || (w.type === 'promise' && w.then === 'resolve');
    if (free && this.line !== '' && this.editTarget() === 'line') {
      this.queue.push({ k: 'line', text: this.line });
      this.line = '';
      this.drawInput();
    } else this.queue.push({ k: 'key', code: 13 });
  }

  keyDown(code) {
    if (code === 13) this.enter();
    else if (code === 8) this.backspace();
    else if (code >= 32 && code <= 126) this.typeChar(code);
    else this.queue.push({ k: 'key', code });
  }

  // Only matters after hold.key: letting go of the arrow stops ego.
  keyUp(code) {
    if (this.holdKey && code in DIR_KEYS) this.queue.push({ k: 'up', code });
  }

  handleKey(ev) {
    const code = ev.code;
    if (ev.k === 'up') {
      if (this.holdKey && this.playerControl && DIR_KEYS[code] === this.vars[6]) this.vars[6] = 0;
      return;
    }
    this.vars[19] = code & 255;
    this.keyWaiting = 1;
    let mapped = false;
    for (let i = 0; i < this.keymap.length; i += 2) {
      if (this.keymap[i] === code) { this.controllers[this.keymap[i + 1]] = 1; mapped = true; }
    }
    if (mapped || ev.k !== 'key' || !(code in DIR_KEYS) || !this.playerControl) return;
    // Press to walk, press the same way again to stop.
    const d = DIR_KEYS[code];
    this.vars[6] = !this.holdKey && this.vars[6] === d ? 0 : d;
    this.objs[0].motion = MOTION.NORMAL;
  }

  // ---- the parser ----

  // Put a line in front of the logics: words, v9, flag 2.
  takeLine(text) {
    const r = parseLine(this.words, text);
    this.egoWords = r.words;
    this.vars[9] = r.unknown;
    this.flags[2] = r.words.length ? 1 : 0;
  }

  // The said() patterns of the loaded logics, for the AI parser.
  patterns() {
    const out = [], seen = new Set();
    for (const n of this.loaded) {
      const lg = this.getLogic(n);
      if (!lg) continue;
      if (!lg.said) lg.said = saidPatterns(lg.code);
      for (const groups of lg.said) {
        const k = groups.join(',');
        if (seen.has(k)) continue;
        seen.add(k);
        out.push({ groups, text: groups.map((g) => this.groupNames[g] || String(g)).join(' ') });
      }
    }
    return out;
  }

  // A typed line. False if it went to the AI parser first: that is a
  // miss, meaning an unknown word, or known words that no said() in the
  // loaded logics could accept.
  submitLine(text) {
    this.lastLine = text;
    if (this.host.resolve) {
      const r = parseLine(this.words, text);
      if (r.words.length) {
        const ids = r.words.map((w) => w.id);
        const patterns = this.patterns();
        if (r.unknown || !patterns.some((p) => saidMatches(ids, p.groups))) {
          this.waitFor(() => this.host.resolve(text, patterns), 'resolve');
          this.wait.text = text;
          this.drawThinking();
          return false;
        }
      }
    }
    this.takeLine(text);
    return true;
  }

  // ---- waits ----

  // Start waiting on the host. The promise's answer lands in the wait,
  // and the next cycle() picks it up.
  waitFor(start, then) {
    const w = { type: 'promise', then, done: 0, value: null, failed: 0, sub: null, buf: '', spin: 0 };
    this.wait = w;
    let p;
    try { p = start(w); } catch (e) { p = Promise.reject(e); }
    Promise.resolve(p).then(
      (v) => { if (this.wait === w) { w.done = 1; w.value = v === undefined ? null : v; } },
      (e) => { if (this.wait === w) { w.done = 1; w.failed = 1; } this.log(`${then}: ${e && e.message}`); },
    );
  }

  // What talk() hands the host so it can use the game's own text box.
  talkIo(w) {
    return {
      print: (text) => new Promise((done) => {
        if (this.wait !== w) { done(); return; }
        this.openWindow(String(text));
        w.sub = 'window';
        this.ioDone = done;
      }),
      ask: (prompt = '>') => new Promise((done) => {
        if (this.wait !== w) { done(null); return; }
        w.sub = 'line';
        w.buf = '';
        w.prompt = String(prompt).slice(0, 8);
        this.ioDone = done;
        this.drawTalkLine();
      }),
    };
  }

  facts(n) {
    const lg = this.getLogic(this.curLogic);
    return {
      n,
      prompt: (lg && lg.messages[n]) || null,
      score: this.vars[3],
      maxScore: this.vars[7],
      room: this.vars[0],
      events: this.eventOrder.slice(),
      aiHelps: this.aiHelps,
      inventory: this.items.filter((it, i) => this.invRoom[i] === 255).map((it) => it.name),
      vars: Array.from(this.vars),
      flags: Array.from(this.flags),
      strings: this.strings.slice(),
    };
  }

  // The next key meant for a dialog. Lines typed ahead stay queued.
  modalKey() {
    for (let i = 0; i < this.queue.length; i++) {
      const ev = this.queue[i];
      if (ev.k === 'line') continue;
      this.queue.splice(i, 1);
      if (ev.k === 'up') { i--; continue; }
      return ev.code;
    }
    return 0;
  }

  // True when the wait is over and the logics may carry on.
  pollWait() {
    const w = this.wait;
    let k;
    switch (w.type) {
      case 'window':
        if (w.timer > 0 && --w.timer === 0) return this.finish(w, 13);
        while ((k = this.modalKey())) if (k === 13 || k === 27) return this.finish(w, k);
        return false;
      case 'anykey':
        k = this.modalKey();
        return k ? this.finish(w, k) : false;
      case 'edit':
        while ((k = this.modalKey())) {
          if (k === 27) w.buf = '';
          if (k === 13 || k === 27) return this.finish(w, k);
        }
        return false;
      case 'slot':
        while ((k = this.modalKey())) if (k === 27 || (k >= 49 && k <= 57)) return this.finish(w, k);
        return false;
      case 'inv':
        while ((k = this.modalKey())) {
          if (!w.select) return this.finish(w, k);
          if (k === 13 || k === 27) return this.finish(w, k);
          const n = w.list.length;
          if (k === KEY.UP && w.sel >= 2) w.sel -= 2;
          else if (k === KEY.DOWN && w.sel + 2 < n) w.sel += 2;
          else if (k === KEY.LEFT && w.sel > 0) w.sel--;
          else if (k === KEY.RIGHT && w.sel + 1 < n) w.sel++;
          this.drawInventory(w);
        }
        return false;
      case 'menu':
        while ((k = this.modalKey())) {
          if (k === 13 || k === 27) return this.finish(w, k);
          const count = this.menus.length, items = this.menus[w.m].items.length;
          if (k === KEY.LEFT) { w.m = (w.m + count - 1) % count; w.i = 0; }
          else if (k === KEY.RIGHT) { w.m = (w.m + 1) % count; w.i = 0; }
          else if (k === KEY.UP && items) w.i = (w.i + items - 1) % items;
          else if (k === KEY.DOWN && items) w.i = (w.i + 1) % items;
          this.drawMenu(w);
        }
        return false;
      default:
        return this.pollPromise(w);
    }
  }

  pollPromise(w) {
    let k;
    if (w.sub === 'window') {
      while ((k = this.modalKey())) {
        if (k !== 13 && k !== 27) continue;
        this.closeWindow();
        w.sub = null;
        this.ioAnswer(undefined);
        break;
      }
    } else if (w.sub === 'line') {
      while ((k = this.modalKey())) {
        if (k !== 13 && k !== 27) continue;
        const answer = k === 13 ? w.buf : null;
        w.sub = null;
        w.buf = '';
        this.clearRows(this.inputRow, this.inputRow, 0);
        this.ioAnswer(answer);
        break;
      }
    } else if (w.then !== 'resolve') {
      // Keys pressed during a clip or a save mean nothing: use them up.
      while (this.modalKey()) { /* nothing */ }
    }
    if (w.done) return this.finish(w, 0);
    if (w.then === 'resolve') { w.spin = (w.spin + 1) & 3; this.drawThinking(); }
    return false;
  }

  ioAnswer(value) {
    const done = this.ioDone;
    this.ioDone = null;
    if (done) done(value);
  }

  // The wait is over: do what was waiting on it.
  finish(w, key) {
    this.wait = null;
    switch (w.then) {
      case 'close':
        this.closeWindow();
        break;
      case 'restart':
        this.closeWindow();
        if (key === 13) this.restart();
        break;
      case 'quit':
        this.closeWindow();
        if (key === 13) this.end();
        break;
      case 'showobj':
        this.closeWindow();
        this.showPic();
        break;
      case 'pri':
        this.showPic();
        break;
      case 'string': {
        this.setStr(w.s, w.buf);
        const at = this.attr();
        this.writeAt(w.row, w.col, (w.buf + '  ').slice(0, 40 - w.col), at.fg, at.bg);
        break;
      }
      case 'num':
        // Integer rule: the low 8 bits of the number typed.
        this.vars[w.v] = (parseInt(w.buf, 10) || 0) & 255;
        this.clearRows(this.inputRow, this.inputRow, this.gfx ? 0 : this.textBg);
        this.drawInput();
        break;
      case 'inv':
        this.setTextBytes(unpackBytes(w.saved, new Uint8Array(3000)));
        if (w.select) this.vars[25] = key === 13 && w.list.length ? w.list[w.sel] : 255;
        break;
      case 'menu': {
        this.setTextBytes(unpackBytes(w.saved, new Uint8Array(3000)));
        const it = this.menus[w.m].items[w.i];
        // The choice arrives as a controller on the next cycle.
        if (key === 13 && it && it.on) this.pendingCtl.push(it.ctl);
        break;
      }
      case 'slot':
        this.closeWindow();
        if (key !== 27) {
          if (w.mode === 'save') this.saveTo(key - 48);
          else this.restoreFrom(key - 48);
        }
        break;
      case 'talk':
        if (this.win) this.closeWindow();
        this.ioDone = null;
        this.clearRows(this.inputRow, this.inputRow, 0);
        this.drawInput();
        this.flags[w.failed || w.value === false ? 29 : 28] = 1;
        break;
      case 'resolve': {
        const row = this.inputRow + 1;
        if (row < 25) this.clearRows(row, row, -1);
        const s = !w.failed && typeof w.value === 'string' ? w.value.trim() : '';
        if (s) this.aiHelps++;
        this.pendingLine = { text: s || w.text };
        break;
      }
      case 'save':
        this.saved(!w.failed && w.value !== false);
        break;
      case 'load':
        this.loadedGame(w.failed ? null : w.value);
        break;
      default:
        break;
    }
    return true;
  }

  // ---- things that open a wait ----

  // A message window. It waits for Enter or Esc, or for v21 half
  // seconds (10 cycles each), unless flag 15 says to leave it up.
  print(text, row = -1, col = -1, width = 0) {
    this.openWindow(this.format(text, this.curLogic), row, col, width);
    if (this.flags[15]) return;
    const timer = this.vars[21] * 10;
    this.vars[21] = 0;
    this.wait = { type: 'window', timer, then: 'close' };
  }

  drawEdit() {
    const w = this.wait;
    const at = this.attr();
    this.writeAt(w.row, w.col, (w.buf + this.cursorChar + ' ').slice(0, 40 - w.col), at.fg, at.bg);
  }

  drawTalkLine() {
    const w = this.wait;
    const s = ((w.prompt || '') + w.buf + this.cursorChar).padEnd(40).slice(0, 40);
    this.writeAt(this.inputRow, 0, s, 15, 0);
  }

  // The AI parser is working: a spinner under the input line. The text
  // is the game's (game.json "thinking"), not the engine's.
  drawThinking() {
    const row = this.inputRow + 1;
    if (row > 24) return;
    const label = (this.res.game && this.res.game.thinking) || 'Thinking...';
    const s = (SPINNER[this.wait.spin & 3] + ' ' + label).padEnd(40).slice(0, 40);
    this.writeAt(row, 0, s, 7, 0);
  }

  openInventory() {
    const list = [];
    for (let i = 0; i < this.items.length; i++) if (this.invRoom[i] === 255) list.push(i);
    const w = { type: 'inv', then: 'inv', saved: packBytes(this.textBytes()), list, sel: 0, select: this.flags[13] };
    this.wait = w;
    this.drawInventory(w);
  }

  drawInventory(w) {
    const center = (row, s, fg = 0, bg = 15) => this.writeAt(row, (40 - s.length) >> 1, s, fg, bg);
    this.clearRows(0, 24, 15, 0);
    center(0, 'You are carrying:');
    if (!w.list.length) center(2, 'nothing');
    w.list.forEach((item, k) => {
      const name = this.items[item].name.slice(0, 19);
      const hot = w.select && k === w.sel;
      this.writeAt(2 + (k >> 1), k & 1 ? 39 - name.length : 1, name, hot ? 15 : 0, hot ? 0 : 15);
    });
    center(24, w.select ? 'ENTER to select, ESC to cancel' : 'Press a key to return to the game');
  }

  openMenu() {
    if (!this.menus.length) return;
    const w = { type: 'menu', then: 'menu', saved: packBytes(this.textBytes()), m: 0, i: 0 };
    this.wait = w;
    this.drawMenu(w);
  }

  drawMenu(w) {
    this.setTextBytes(unpackBytes(w.saved, new Uint8Array(3000)));
    this.clearRows(0, 0, 15, 0);
    let col = 1, at = 1;
    this.menus.forEach((m, k) => {
      if (k === w.m) at = col;
      this.writeAt(0, col, m.name, k === w.m ? 15 : 0, k === w.m ? 0 : 15);
      col += m.name.length + 1;
    });
    const items = this.menus[w.m].items;
    let width = 1;
    for (const it of items) if (it.name.length > width) width = it.name.length;
    if (at + width + 2 > 40) at = 38 - width;
    if (at < 1) at = 1;
    const text = items.map((it) => it.name.padEnd(width)).join('\n');
    const keep = this.win;
    this.win = null;
    this.openWindow(text, 2, at, 38);
    this.win = keep;
    items.forEach((it, k) => {
      const hot = k === w.i;
      this.writeAt(2 + k, at, it.name.padEnd(width), hot ? 15 : it.on ? 0 : 7, hot ? 0 : 15);
    });
  }

  // ---- saved games ----

  askSlot(mode) {
    this.openWindow(`${mode === 'save' ? 'Save' : 'Restore'} game\n\nPress 1 to 9 to pick a slot.\nPress ESC to cancel.`);
    this.wait = { type: 'slot', then: 'slot', mode };
  }

  saveTo(slot) {
    if (!this.host.save) { this.print('This copy of the game cannot save.'); return; }
    // A saved game starts at the top of a cycle, as AGI's do.
    const snap = this.snapshot();
    snap.plain.stack = [];
    snap.plain.phase = 0;
    snap.plain.wait = null;
    snap.plain.win = null;
    let r;
    try { r = this.host.save(slot, snap); } catch (e) { r = false; }
    if (r && typeof r.then === 'function') this.waitFor(() => r, 'save');
    else this.saved(r !== false);
  }

  saved(ok) {
    this.print(ok ? 'Game saved.' : 'The game could not be saved.');
  }

  restoreFrom(slot) {
    if (!this.host.load) { this.print('This copy of the game cannot restore.'); return; }
    let r;
    try { r = this.host.load(slot); } catch (e) { r = null; }
    if (r && typeof r.then === 'function') this.waitFor(() => r, 'load');
    else this.loadedGame(r);
  }

  loadedGame(snap) {
    let ok = false;
    if (snap) {
      try { this.restore(snap); ok = true; } catch (e) { this.log(`restore: ${e.message}`); }
    }
    if (!ok) { this.print('There is no saved game in that slot.'); return; }
    this.flags[12] = 1;
  }

  // ---- snapshot, restore, hash ----

  snapshot() {
    const plain = {};
    for (const k of PLAIN) plain[k] = this[k];
    return {
      jrq: 1,
      vars: packBytes(this.vars),
      flags: packBytes(this.flags),
      controllers: packBytes(this.controllers),
      invRoom: packBytes(this.invRoom),
      objs: this.objs.map((o) => OBJ_FIELDS.map((f) => o[f])),
      baks: this.objs.map((o) => (o.bak ? [o.bak.x, o.bak.y, o.bak.w, o.bak.h, toBase64(o.bak.data)] : null)),
      gbuf: packBytes(this.gbuf),
      screen: packBytes(this.screen),
      text: packBytes(this.textBytes()),
      plain: JSON.parse(JSON.stringify(plain)),
    };
  }

  restore(snap) {
    if (!snap || snap.jrq !== 1 || !snap.plain) throw new Error('not a JRQ snapshot');
    if (this.host.stopSound) this.host.stopSound();
    unpackBytes(snap.vars, this.vars);
    unpackBytes(snap.flags, this.flags);
    unpackBytes(snap.controllers, this.controllers);
    unpackBytes(snap.invRoom, this.invRoom);
    this.objs = snap.objs.map((fields, i) => {
      const o = newObject(i);
      OBJ_FIELDS.forEach((f, k) => { o[f] = fields[k]; });
      const b = snap.baks[i];
      if (b) o.bak = { x: b[0], y: b[1], w: b[2], h: b[3], data: fromBase64(b[4]) };
      return o;
    });
    unpackBytes(snap.gbuf, this.gbuf);
    unpackBytes(snap.screen, this.screen);
    this.setTextBytes(unpackBytes(snap.text, new Uint8Array(3000)));
    const plain = JSON.parse(JSON.stringify(snap.plain));
    for (const k of PLAIN) this[k] = plain[k];
    this.events.fill(0);
    for (const n of this.eventOrder) this.events[n] = 1;
    this.setPriBase(this.priBase);
    this.exitAll = 0;
    this.ended = 0;
    this.ioDone = null;
    // A promise cannot be saved: a wait on the host ends with no answer.
    if (this.wait && this.wait.type === 'promise' && !this.wait.done) {
      this.wait.done = 1;
      this.wait.value = null;
      this.wait.failed = 1;
      this.wait.sub = null;
    }
    // With a real clock, time restarts from now.
    if (this.host.time) this.clockLast = -1;
  }

  // The bytes hash() covers, in the order FORMATS.md gives.
  stateBytes() {
    const out = [];
    const push = (bytes) => { for (let i = 0; i < bytes.length; i++) out.push(bytes[i]); };
    const text = (s, n) => { for (let i = 0; i < n; i++) out.push(i < s.length ? s.charCodeAt(i) & 255 : 0); };
    if (this.host.time) {
      // A wall clock must not reach the hash.
      const v = this.vars.slice();
      v[11] = v[12] = v[13] = v[14] = 0;
      push(v);
    } else push(this.vars);
    push(this.flags);
    push(this.invRoom);
    for (const s of this.strings) text(s, 40);
    for (const o of this.objs) {
      for (const f of OBJ_FIELDS) { out.push(o[f] & 255, (o[f] >> 8) & 255); }
    }
    push(this.gbuf);
    push(this.screen);
    push(this.textBytes());
    const b = this.block;
    out.push(this.horizon, this.playerControl, b.on, b.x1, b.y1, b.x2, b.y2, this.gfx, this.picShown,
      this.picTop, this.inputRow, this.statusRow, this.statusOn, this.inputEnabled, this.textFg,
      this.textBg, this.cursorChar ? this.cursorChar.charCodeAt(0) : 0);
    text(this.line, 40);
    out.push(this.wait ? WAIT_CODE[this.wait.type] : 0, this.seed & 255, this.seed >> 8);
    return Uint8Array.from(out);
  }

  hash() {
    return fnv1a(this.stateBytes());
  }
}

Object.assign(Interpreter.prototype, animMethods, screenMethods, opsMethods);
