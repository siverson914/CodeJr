// The logic executor: if/else/goto, the test commands and every action
// in commands.js. Mixed into Interpreter.
//
// A logic never runs on the JavaScript stack: the call stack is
// this.stack ({ n, ip } frames), so a blocking command can stop in the
// middle, cycle() can return, and a later cycle() carries on.

import { TESTS, ACTIONS, EXT_ACTIONS, actionInfo } from './commands.js';
import { drawPicture } from './pic.js';
import { loadResource, decodeSound } from './resources.js';
import { saidMatches } from './parser.js';
import { F, MOTION, CYCLE, W, H } from './anim.js';

export const RUN = { DONE: 0, BLOCKED: 1, AGAIN: 2 };

const word = (c, p) => c[p] | (c[p + 1] << 8);
const signed = (b) => (b & 0x80 ? b - 256 : b);

function plainString(s) {
  return s.toLowerCase().replace(/[ \t.,;:'!-]/g, '');
}

// ---- tests: (a, c, p) -> boolean, p at the first argument ----

const TEST_FN = {
  'equaln': (a, c, p) => a.vars[c[p]] === c[p + 1],
  'equalv': (a, c, p) => a.vars[c[p]] === a.vars[c[p + 1]],
  'lessn': (a, c, p) => a.vars[c[p]] < c[p + 1],
  'lessv': (a, c, p) => a.vars[c[p]] < a.vars[c[p + 1]],
  'greatern': (a, c, p) => a.vars[c[p]] > c[p + 1],
  'greaterv': (a, c, p) => a.vars[c[p]] > a.vars[c[p + 1]],
  'isset': (a, c, p) => a.flags[c[p]] !== 0,
  'issetv': (a, c, p) => a.flags[a.vars[c[p]]] !== 0,
  'has': (a, c, p) => a.invRoom[c[p]] === 255,
  'obj.in.room': (a, c, p) => a.invRoom[c[p]] === a.vars[c[p + 1]],
  'posn': (a, c, p) => { const o = a.obj(c[p]); return inBox(o.x, o.x, o.y, c, p); },
  'controller': (a, c, p) => a.controllers[c[p]] !== 0,
  'have.key': (a) => { const k = a.keyWaiting; a.keyWaiting = 0; return k !== 0; },
  'said': (a, c, p) => {
    if (a.flags[4] || !a.flags[2]) return false;
    const pattern = [];
    for (let i = 0; i < c[p]; i++) pattern.push(word(c, p + 1 + i * 2));
    if (!saidMatches(a.egoWords.map((w) => w.id), pattern)) return false;
    a.flags[4] = 1;
    return true;
  },
  'compare.strings': (a, c, p) => plainString(a.str(c[p])) === plainString(a.str(c[p + 1])),
  'obj.in.box': (a, c, p) => { const o = a.obj(c[p]); return inBox(o.x, o.x + o.xSize - 1, o.y, c, p); },
  'center.posn': (a, c, p) => { const o = a.obj(c[p]); const x = o.x + (o.xSize >> 1); return inBox(x, x, o.y, c, p); },
  'right.posn': (a, c, p) => { const o = a.obj(c[p]); const x = o.x + o.xSize - 1; return inBox(x, x, o.y, c, p); },
};

function inBox(xl, xr, y, c, p) {
  return xl >= c[p + 1] && y >= c[p + 2] && xr <= c[p + 3] && y <= c[p + 4];
}

// ---- actions: (a, c, p) ----

const nop = () => {};

function setV(a, n, value) { a.vars[n] = value & 255; }

function picBytes(a, n) {
  const bytes = loadResource(a.res.picdir, a.res.vols, n);
  if (!bytes) a.log(`picture ${n} is missing`);
  return bytes;
}

function drawPic(a, n, overlay) {
  const bytes = picBytes(a, n);
  a.eraseAll();
  if (!overlay) a.gbuf.fill(0x4f);
  if (bytes) {
    try { drawPicture(bytes, a.gbuf, !overlay); } catch (e) { a.log(`picture ${n}: ${e.message}`); }
  }
  a.drawAll();
  a.picShown = 0;
}

function moveObj(a, o, x, y, stepSize, flag) {
  o.motion = MOTION.MOVE;
  o.moveX = x;
  o.moveY = y;
  o.moveStep = o.stepSize;
  if (stepSize) o.stepSize = stepSize;
  o.moveFlag = flag;
  a.flags[flag] = 0;
  o.flags |= F.UPDATE;
  if (o.n === 0) a.playerControl = 0;
  a.motionMove(o);
}

function reposition(a, o, toX, toY) {
  o.x = toX;
  o.y = toY;
  o.flags |= F.UPDATE_POS;
  a.fixPosition(o);
}

// clear.lines colors as the PC interpreter treats them: black or
// white over a picture, always black on a text screen.
function textColor(a, color) {
  return a.gfx && color ? 15 : 0;
}

function showObj(a, n) {
  const view = a.getView(n);
  if (!view) return;
  const cel = view.loops[0][0];
  const x0 = (W - cel.w) >> 1, y0 = H - cel.h, top = a.picTop * 8;
  for (let y = 0; y < cel.h; y++) {
    for (let x = 0; x < cel.w; x++) {
      const col = cel.pix[y * cel.w + x];
      const sx = x0 + x, sy = y0 + y + top;
      if (col !== 255 && sx >= 0 && sx < W && sy >= 0 && sy < 200) a.screen[sy * W + sx] = col;
    }
  }
  a.openWindow(a.format(view.desc || '', a.curLogic), 2);
  a.wait = { type: 'window', timer: 0, then: 'showobj' };
}

function strDate(a, s, v) {
  const m = /^(\d{1,4})[-/.](\d{1,4})[-/.](\d{1,4})$/.exec(a.str(s).trim());
  a.vars[v] = 0;
  if (!m) return;
  const month = Number(m[1]), day = Number(m[2]);
  let year = Number(m[3]);
  // DOS takes 80 to 99 as 19xx and nothing else with two digits.
  if (year >= 80 && year <= 99) year += 1900;
  if (month < 1 || month > 12 || day < 1 || day > 31 || year < 1980 || year > 2099) return;
  a.vars[v] = 1;
  setV(a, v + 1, month);
  setV(a, v + 2, day);
  setV(a, v + 3, year - 1980);
}

const ACTION_FN = {
  'return': nop,
  'increment': (a, c, p) => { if (a.vars[c[p]] < 255) a.vars[c[p]]++; },
  'decrement': (a, c, p) => { if (a.vars[c[p]] > 0) a.vars[c[p]]--; },
  'assignn': (a, c, p) => { a.vars[c[p]] = c[p + 1]; },
  'assignv': (a, c, p) => { a.vars[c[p]] = a.vars[c[p + 1]]; },
  'addn': (a, c, p) => { setV(a, c[p], a.vars[c[p]] + c[p + 1]); },
  'addv': (a, c, p) => { setV(a, c[p], a.vars[c[p]] + a.vars[c[p + 1]]); },
  'subn': (a, c, p) => { setV(a, c[p], a.vars[c[p]] - c[p + 1]); },
  'subv': (a, c, p) => { setV(a, c[p], a.vars[c[p]] - a.vars[c[p + 1]]); },
  'lindirectv': (a, c, p) => { a.vars[a.vars[c[p]]] = a.vars[c[p + 1]]; },
  'rindirect': (a, c, p) => { a.vars[c[p]] = a.vars[a.vars[c[p + 1]]]; },
  'lindirectn': (a, c, p) => { a.vars[a.vars[c[p]]] = c[p + 1]; },
  'set': (a, c, p) => { a.flags[c[p]] = 1; },
  'reset': (a, c, p) => { a.flags[c[p]] = 0; },
  'toggle': (a, c, p) => { a.flags[c[p]] ^= 1; },
  'set.v': (a, c, p) => { a.flags[a.vars[c[p]]] = 1; },
  'reset.v': (a, c, p) => { a.flags[a.vars[c[p]]] = 0; },
  'toggle.v': (a, c, p) => { a.flags[a.vars[c[p]]] ^= 1; },
  'new.room': (a, c, p) => a.newRoom(c[p]),
  'new.room.v': (a, c, p) => a.newRoom(a.vars[c[p]]),
  'load.logics': (a, c, p) => a.loadLogic(c[p]),
  'load.logics.v': (a, c, p) => a.loadLogic(a.vars[c[p]]),
  'call': (a, c, p) => a.callLogic(c[p]),
  'call.v': (a, c, p) => a.callLogic(a.vars[c[p]]),
  // Resources are decoded when first used, so load and discard are free.
  'load.pic': nop,
  'draw.pic': (a, c, p) => drawPic(a, a.vars[c[p]], false),
  'show.pic': (a) => {
    a.flags[15] = 0;
    a.closeWindow();
    // Text over the picture goes when the picture is shown again.
    a.clearRows(a.picTop, a.picTop + 20, -1);
    a.picShown = 1;
    a.showPic();
    a.drawStatus();
    a.drawInput();
  },
  'discard.pic': nop,
  'overlay.pic': (a, c, p) => drawPic(a, a.vars[c[p]], true),
  'show.pri.screen': (a) => {
    const top = a.picTop * 8;
    for (let i = 0; i < W * H; i++) {
      if (i + top * W < a.screen.length) a.screen[i + top * W] = a.gbuf[i] >> 4;
    }
    a.wait = { type: 'anykey', then: 'pri' };
  },
  'load.view': nop,
  'load.view.v': nop,
  'discard.view': nop,
  'animate.obj': (a, c, p) => {
    const o = a.obj(c[p]);
    if (o.flags & F.ANIMATED) return;
    o.flags = F.ANIMATED | F.UPDATE | F.CYCLING;
    o.motion = MOTION.NORMAL;
    o.cycle = CYCLE.NORMAL;
    o.dir = 0;
  },
  'unanimate.all': (a) => {
    a.eraseAll();
    for (const o of a.objs) o.flags &= ~(F.ANIMATED | F.DRAWN);
  },
  'draw': (a, c, p) => {
    const o = a.obj(c[p]);
    if (o.flags & F.DRAWN) return;
    if (!o.hasView) { a.log(`draw(o${c[p]}) before set.view`); return; }
    o.flags |= F.UPDATE;
    a.fixPosition(o);
    o.px = o.x;
    o.py = o.y;
    o.pxs = o.xSize;
    o.pys = o.ySize;
    a.eraseAll();
    o.flags |= F.DRAWN;
    a.drawAll();
    a.commitObj(o);
    o.flags &= ~F.DONT_UPDATE;
  },
  'erase': (a, c, p) => {
    const o = a.obj(c[p]);
    if (!(o.flags & F.DRAWN)) return;
    a.eraseAll();
    o.flags &= ~F.DRAWN;
    a.drawAll();
    a.commitObj(o);
  },
  'position': (a, c, p) => { const o = a.obj(c[p]); o.x = o.px = c[p + 1]; o.y = o.py = c[p + 2]; },
  'position.v': (a, c, p) => { const o = a.obj(c[p]); o.x = o.px = a.vars[c[p + 1]]; o.y = o.py = a.vars[c[p + 2]]; },
  'get.posn': (a, c, p) => { const o = a.obj(c[p]); setV(a, c[p + 1], o.x); setV(a, c[p + 2], o.y); },
  'reposition': (a, c, p) => {
    const o = a.obj(c[p]);
    const dx = signed(a.vars[c[p + 1]]), dy = signed(a.vars[c[p + 2]]);
    reposition(a, o, dx < 0 && o.x < -dx ? 0 : o.x + dx, dy < 0 && o.y < -dy ? 0 : o.y + dy);
  },
  'set.view': (a, c, p) => a.setView(a.obj(c[p]), c[p + 1]),
  'set.view.v': (a, c, p) => a.setView(a.obj(c[p]), a.vars[c[p + 1]]),
  'set.loop': (a, c, p) => a.setLoop(a.obj(c[p]), c[p + 1]),
  'set.loop.v': (a, c, p) => a.setLoop(a.obj(c[p]), a.vars[c[p + 1]]),
  'fix.loop': (a, c, p) => { a.obj(c[p]).flags |= F.FIX_LOOP; },
  'release.loop': (a, c, p) => { a.obj(c[p]).flags &= ~F.FIX_LOOP; },
  'set.cel': (a, c, p) => { const o = a.obj(c[p]); a.setCel(o, c[p + 1]); o.flags &= ~F.DONT_UPDATE; },
  'set.cel.v': (a, c, p) => { const o = a.obj(c[p]); a.setCel(o, a.vars[c[p + 1]]); o.flags &= ~F.DONT_UPDATE; },
  'last.cel': (a, c, p) => { const o = a.obj(c[p]); setV(a, c[p + 1], o.numCels ? o.numCels - 1 : 0); },
  'current.cel': (a, c, p) => { a.vars[c[p + 1]] = a.obj(c[p]).cel; },
  'current.loop': (a, c, p) => { a.vars[c[p + 1]] = a.obj(c[p]).loop; },
  'current.view': (a, c, p) => { a.vars[c[p + 1]] = a.obj(c[p]).view; },
  'number.of.loops': (a, c, p) => { a.vars[c[p + 1]] = a.obj(c[p]).numLoops; },
  'set.priority': (a, c, p) => { const o = a.obj(c[p]); o.flags |= F.FIXED_PRI; o.priority = c[p + 1] & 15; },
  'set.priority.v': (a, c, p) => { const o = a.obj(c[p]); o.flags |= F.FIXED_PRI; o.priority = a.vars[c[p + 1]] & 15; },
  'release.priority': (a, c, p) => { a.obj(c[p]).flags &= ~F.FIXED_PRI; },
  'get.priority': (a, c, p) => { a.vars[c[p + 1]] = a.obj(c[p]).priority; },
  'stop.update': (a, c, p) => {
    const o = a.obj(c[p]);
    if (!(o.flags & F.UPDATE)) return;
    a.eraseAll();
    o.flags &= ~F.UPDATE;
    a.drawAll();
  },
  'start.update': (a, c, p) => {
    const o = a.obj(c[p]);
    if (o.flags & F.UPDATE) return;
    a.eraseAll();
    o.flags |= F.UPDATE;
    a.drawAll();
  },
  'force.update': (a) => {
    a.eraseAll();
    a.drawAll();
    for (const n of a.drawnNon) a.commitObj(a.objs[n]);
    for (const n of a.drawnUpd) a.commitObj(a.objs[n]);
  },
  'ignore.horizon': (a, c, p) => { a.obj(c[p]).flags |= F.IGNORE_HORIZON; },
  'observe.horizon': (a, c, p) => { a.obj(c[p]).flags &= ~F.IGNORE_HORIZON; },
  'set.horizon': (a, c, p) => { a.horizon = c[p]; },
  'object.on.water': (a, c, p) => { a.obj(c[p]).flags |= F.ON_WATER; },
  'object.on.land': (a, c, p) => { a.obj(c[p]).flags |= F.ON_LAND; },
  'object.on.anything': (a, c, p) => { a.obj(c[p]).flags &= ~(F.ON_WATER | F.ON_LAND); },
  'ignore.objs': (a, c, p) => { a.obj(c[p]).flags |= F.IGNORE_OBJS; },
  'observe.objs': (a, c, p) => { a.obj(c[p]).flags &= ~F.IGNORE_OBJS; },
  'distance': (a, c, p) => {
    const o1 = a.obj(c[p]), o2 = a.obj(c[p + 1]);
    let d = 255;
    if ((o1.flags & F.DRAWN) && (o2.flags & F.DRAWN)) {
      d = Math.abs((o1.x + (o1.xSize >> 1)) - (o2.x + (o2.xSize >> 1))) + Math.abs(o1.y - o2.y);
      if (d > 254) d = 254;
    }
    a.vars[c[p + 2]] = d;
  },
  'stop.cycling': (a, c, p) => { a.obj(c[p]).flags &= ~F.CYCLING; },
  'start.cycling': (a, c, p) => { a.obj(c[p]).flags |= F.CYCLING; },
  'normal.cycle': (a, c, p) => { const o = a.obj(c[p]); o.cycle = CYCLE.NORMAL; o.flags |= F.CYCLING; },
  'end.of.loop': (a, c, p) => {
    const o = a.obj(c[p]);
    o.cycle = CYCLE.END;
    o.flags |= F.DONT_UPDATE | F.UPDATE | F.CYCLING;
    o.loopFlag = c[p + 1];
    a.flags[c[p + 1]] = 0;
  },
  'reverse.cycle': (a, c, p) => { const o = a.obj(c[p]); o.cycle = CYCLE.REVERSE; o.flags |= F.CYCLING; },
  'reverse.loop': (a, c, p) => {
    const o = a.obj(c[p]);
    o.cycle = CYCLE.REV_END;
    o.flags |= F.DONT_UPDATE | F.UPDATE | F.CYCLING;
    o.loopFlag = c[p + 1];
    a.flags[c[p + 1]] = 0;
  },
  'cycle.time': (a, c, p) => { const o = a.obj(c[p]); o.cycleTime = o.cycleTimeCount = a.vars[c[p + 1]]; },
  'stop.motion': (a, c, p) => {
    const o = a.obj(c[p]);
    o.dir = 0;
    o.motion = MOTION.NORMAL;
    if (o.n === 0) { a.vars[6] = 0; a.playerControl = 0; }
  },
  'start.motion': (a, c, p) => {
    const o = a.obj(c[p]);
    o.motion = MOTION.NORMAL;
    if (o.n === 0) { a.vars[6] = 0; a.playerControl = 1; }
  },
  'step.size': (a, c, p) => { a.obj(c[p]).stepSize = a.vars[c[p + 1]]; },
  'step.time': (a, c, p) => { const o = a.obj(c[p]); o.stepTime = o.stepTimeCount = a.vars[c[p + 1]]; },
  'move.obj': (a, c, p) => moveObj(a, a.obj(c[p]), c[p + 1], c[p + 2], c[p + 3], c[p + 4]),
  'move.obj.v': (a, c, p) => moveObj(a, a.obj(c[p]), a.vars[c[p + 1]], a.vars[c[p + 2]], a.vars[c[p + 3]], c[p + 4]),
  'follow.ego': (a, c, p) => {
    const o = a.obj(c[p]);
    o.motion = MOTION.FOLLOW;
    o.followStep = c[p + 1] <= o.stepSize ? o.stepSize : c[p + 1];
    o.followFlag = c[p + 2];
    o.followCount = 255;
    a.flags[c[p + 2]] = 0;
    o.flags |= F.UPDATE;
  },
  'wander': (a, c, p) => {
    const o = a.obj(c[p]);
    if (o.n === 0) a.playerControl = 0;
    o.motion = MOTION.WANDER;
    o.wanderCount = 0;
    o.flags |= F.UPDATE;
  },
  'normal.motion': (a, c, p) => { a.obj(c[p]).motion = MOTION.NORMAL; },
  'set.dir': (a, c, p) => { const d = a.vars[c[p + 1]]; a.obj(c[p]).dir = d > 8 ? 0 : d; },
  'get.dir': (a, c, p) => { a.vars[c[p + 1]] = a.obj(c[p]).dir; },
  'ignore.blocks': (a, c, p) => { a.obj(c[p]).flags |= F.IGNORE_BLOCKS; },
  'observe.blocks': (a, c, p) => { a.obj(c[p]).flags &= ~F.IGNORE_BLOCKS; },
  'block': (a, c, p) => { a.block = { on: 1, x1: c[p], y1: c[p + 1], x2: c[p + 2], y2: c[p + 3] }; },
  'unblock': (a) => { a.block.on = 0; },
  'get': (a, c, p) => { a.invRoom[c[p]] = 255; },
  'get.v': (a, c, p) => { a.invRoom[a.vars[c[p]]] = 255; },
  'drop': (a, c, p) => { a.invRoom[c[p]] = 0; },
  'put': (a, c, p) => { a.invRoom[c[p]] = a.vars[c[p + 1]]; },
  'put.v': (a, c, p) => { a.invRoom[a.vars[c[p]]] = a.vars[c[p + 1]]; },
  'get.room.v': (a, c, p) => { a.vars[c[p + 1]] = a.invRoom[a.vars[c[p]]]; },
  'load.sound': nop,
  'sound': (a, c, p) => a.startSound(c[p], c[p + 1]),
  'stop.sound': (a) => a.stopSound(),
  'print': (a, c, p) => a.print(a.message(a.curLogic, c[p])),
  'print.v': (a, c, p) => a.print(a.message(a.curLogic, a.vars[c[p]])),
  'display': (a, c, p) => a.displayText(c[p], c[p + 1], a.format(a.message(a.curLogic, c[p + 2]), a.curLogic)),
  'display.v': (a, c, p) => a.displayText(a.vars[c[p]], a.vars[c[p + 1]],
    a.format(a.message(a.curLogic, a.vars[c[p + 2]]), a.curLogic)),
  'clear.lines': (a, c, p) => a.clearRows(c[p], c[p + 1], textColor(a, c[p + 2])),
  'text.screen': (a) => {
    a.gfx = 0;
    a.win = null;
    a.clearRows(0, 24, a.textBg, a.textFg);
  },
  'graphics': (a) => {
    a.gfx = 1;
    a.win = null;
    a.clearRows(0, 24, -1);
    a.showPic();
    a.drawStatus();
    a.drawInput();
  },
  'set.cursor.char': (a, c, p) => { a.cursorChar = a.message(a.curLogic, c[p]).slice(0, 1); a.drawInput(); },
  'set.text.attribute': (a, c, p) => { a.textFg = c[p] & 15; a.textBg = c[p + 1] & 15; },
  'shake.screen': (a, c, p) => { if (a.host.shake) a.host.shake(c[p]); },
  'configure.screen': (a, c, p) => {
    const moved = a.picTop !== c[p];
    a.picTop = c[p];
    a.inputRow = c[p + 1];
    a.statusRow = c[p + 2];
    if (moved) { a.screen.fill(0); if (a.picShown) a.showPic(); }
  },
  'status.line.on': (a) => { a.statusOn = 1; a.drawStatus(); },
  'status.line.off': (a) => { a.statusOn = 0; a.clearRows(a.statusRow, a.statusRow, a.gfx ? -1 : a.textBg); },
  'set.string': (a, c, p) => { a.setStr(c[p], a.message(a.curLogic, c[p + 1])); if (c[p] === 0) a.drawInput(); },
  'get.string': (a, c, p) => {
    const prompt = a.format(a.message(a.curLogic, c[p + 1]), a.curLogic);
    const at = a.attr();
    const col = a.writeAt(c[p + 2], c[p + 3], prompt, at.fg, at.bg);
    a.wait = { type: 'edit', then: 'string', s: c[p], row: c[p + 2], col, max: Math.min(c[p + 4], 40 - col), buf: '' };
    a.drawEdit();
  },
  'word.to.string': (a, c, p) => { a.setStr(c[p], a.egoWords[c[p + 1]] ? a.egoWords[c[p + 1]].text : ''); },
  'parse': (a, c, p) => {
    a.flags[2] = 0;
    a.flags[4] = 0;
    a.takeLine(a.format(a.str(c[p]), a.curLogic));
  },
  'get.num': (a, c, p) => {
    const prompt = a.format(a.message(a.curLogic, c[p]), a.curLogic);
    a.clearRows(a.inputRow, a.inputRow, a.gfx ? 0 : a.textBg);
    const at = a.attr();
    const col = a.writeAt(a.inputRow, 0, prompt, at.fg, at.bg);
    a.wait = { type: 'edit', then: 'num', v: c[p + 1], row: a.inputRow, col, max: 3, buf: '' };
    a.drawEdit();
  },
  'prevent.input': (a) => {
    a.inputEnabled = 0;
    if (a.gfx) a.clearRows(a.inputRow, a.inputRow, 0);
  },
  'accept.input': (a) => { a.inputEnabled = 1; a.drawInput(); },
  'set.key': (a, c, p) => {
    const code = c[p] | (c[p + 1] << 8);
    for (let i = 0; i < a.keymap.length; i += 2) {
      if (a.keymap[i] === code && a.keymap[i + 1] === c[p + 2]) return;
    }
    a.keymap.push(code, c[p + 2]);
  },
  'add.to.pic': (a, c, p) => a.addToPic(c[p], c[p + 1], c[p + 2], c[p + 3], c[p + 4], c[p + 5] & 15, c[p + 6]),
  'add.to.pic.v': (a, c, p) => a.addToPic(a.vars[c[p]], a.vars[c[p + 1]], a.vars[c[p + 2]],
    a.vars[c[p + 3]], a.vars[c[p + 4]], a.vars[c[p + 5]] & 15, a.vars[c[p + 6]]),
  'status': (a) => a.openInventory(),
  'save.game': (a) => a.askSlot('save'),
  'restore.game': (a) => a.askSlot('restore'),
  'init.disk': nop,
  'restart.game': (a) => {
    if (a.flags[16]) { a.restart(); return; }
    a.openWindow('Press ENTER to restart\nthe game.\n\nPress ESC to continue\nthis game.');
    a.wait = { type: 'window', timer: 0, then: 'restart' };
  },
  'show.obj': (a, c, p) => showObj(a, c[p]),
  // Integer rule: low + random % (high - low + 1), random being 0 to 255.
  'random': (a, c, p) => {
    const range = c[p + 1] - c[p] + 1;
    a.vars[c[p + 2]] = range > 0 ? c[p] + a.rnd() % range : c[p];
  },
  'program.control': (a) => { a.playerControl = 0; },
  'player.control': (a) => { a.playerControl = 1; a.objs[0].motion = MOTION.NORMAL; },
  'obj.status.v': (a, c, p) => {
    const o = a.obj(a.vars[c[p]]);
    a.print(`Object ${o.n}:\nx: ${o.x} xsize: ${o.xSize}\ny: ${o.y} ysize: ${o.ySize}\npri: ${o.priority}\nstepsize: ${o.stepSize}`);
  },
  'quit': (a, c, p) => {
    if (c[p]) { a.end(); return; }
    a.openWindow('Press ENTER to quit.\nPress ESC to keep playing.');
    a.wait = { type: 'window', timer: 0, then: 'quit' };
  },
  'show.mem': nop,
  'pause': (a) => {
    a.openWindow('      Game paused.\nPress Enter to continue.');
    a.wait = { type: 'window', timer: 0, then: 'close' };
  },
  'echo.line': (a) => {
    if (a.line.length < a.lastLine.length) a.line += a.lastLine.slice(a.line.length);
    a.drawInput();
  },
  'cancel.line': (a) => { a.line = ''; a.drawInput(); },
  'init.joy': nop,
  'toggle.monitor': nop,
  'version': (a) => {
    const g = a.res.game || {};
    a.print(`${g.name || g.id || 'JRQ'}\n\nJRQ interpreter 0.1\nAGI 2.917 compatible`);
  },
  'script.size': nop,
  'set.game.id': (a, c, p) => { a.gameId = a.message(a.curLogic, c[p]); },
  'log': (a, c, p) => a.log(a.format(a.message(a.curLogic, c[p]), a.curLogic)),
  'set.scan.start': (a) => { a.scanStart[a.curLogic] = a.stack[a.stack.length - 1].ip; },
  'reset.scan.start': (a) => { delete a.scanStart[a.curLogic]; },
  'reposition.to': (a, c, p) => reposition(a, a.obj(c[p]), c[p + 1], c[p + 2]),
  'reposition.to.v': (a, c, p) => reposition(a, a.obj(c[p]), a.vars[c[p + 1]], a.vars[c[p + 2]]),
  'trace.on': nop,
  'trace.info': nop,
  'print.at': (a, c, p) => a.print(a.message(a.curLogic, c[p]), c[p + 1], c[p + 2], c[p + 3]),
  'print.at.v': (a, c, p) => a.print(a.message(a.curLogic, a.vars[c[p]]), c[p + 1], c[p + 2], c[p + 3]),
  'discard.view.v': nop,
  'clear.text.rect': (a, c, p) => a.clearRect(c[p], c[p + 1], c[p + 2], c[p + 3], textColor(a, c[p + 4])),
  'set.upper.left': nop,
  'set.menu': (a, c, p) => {
    if (!a.menuDone) a.menus.push({ name: a.message(a.curLogic, c[p]), items: [] });
  },
  'set.menu.item': (a, c, p) => {
    if (a.menuDone || !a.menus.length) return;
    a.menus[a.menus.length - 1].items.push({ name: a.message(a.curLogic, c[p]), ctl: c[p + 1], on: 1 });
  },
  'submit.menu': (a) => { a.menuDone = 1; },
  'enable.item': (a, c, p) => { for (const m of a.menus) for (const it of m.items) if (it.ctl === c[p]) it.on = 1; },
  'disable.item': (a, c, p) => { for (const m of a.menus) for (const it of m.items) if (it.ctl === c[p]) it.on = 0; },
  'menu.input': (a) => { if (a.flags[14] && a.menuAllowed) a.openMenu(); },
  'show.obj.v': (a, c, p) => showObj(a, a.vars[c[p]]),
  'open.dialogue': nop,
  'close.dialogue': nop,
  'mul.n': (a, c, p) => { setV(a, c[p], a.vars[c[p]] * c[p + 1]); },
  'mul.v': (a, c, p) => { setV(a, c[p], a.vars[c[p]] * a.vars[c[p + 1]]); },
  // Integer rule: division truncates, and dividing by zero gives 0.
  'div.n': (a, c, p) => { a.vars[c[p]] = c[p + 1] ? (a.vars[c[p]] / c[p + 1]) | 0 : 0; },
  'div.v': (a, c, p) => { const d = a.vars[c[p + 1]]; a.vars[c[p]] = d ? (a.vars[c[p]] / d) | 0 : 0; },
  'close.window': (a) => a.closeWindow(),
  'set.simple': nop,
  'push.script': nop,
  'pop.script': nop,
  'hold.key': (a) => { a.holdKey = 1; },
  'set.pri.base': (a, c, p) => a.setPriBase(c[p] < H ? c[p] : H - 1),
  'discard.sound': nop,
  'hide.mouse': nop,
  'allow.menu': (a, c, p) => { a.menuAllowed = c[p] ? 1 : 0; },
  'show.mouse': nop,
  'fence.mouse': nop,
  'mouse.posn': nop,
  'release.key': (a) => { a.holdKey = 0; },
  'adj.ego.move.to.xy': nop,
  // ---- JRQ extensions ----
  'speak': (a, c, p) => {
    if (a.host.speak) a.waitFor(() => a.host.speak(c[p]), 'speak');
  },
  'talk': (a, c, p) => {
    if (!a.host.talk) { a.flags[29] = 1; return; }
    const n = c[p];
    a.waitFor((w) => a.host.talk(n, a.facts(n), a.talkIo(w)), 'talk');
  },
  'link.status': (a, c, p) => {
    const s = a.host.link ? a.host.link() | 0 : 0;
    a.vars[c[p]] = s < 0 ? 0 : s > 2 ? 2 : s;
  },
  'score.event': (a, c, p) => {
    const n = c[p];
    if (a.events[n]) return;
    a.events[n] = 1;
    a.eventOrder.push(n);
    const table = (a.res.game && a.res.game.events) || null;
    const points = table ? table[n] | 0 : 0;
    if (points > 0) {
      setV(a, 3, Math.min(255, a.vars[3] + points));
    }
  },
  'str.date': (a, c, p) => strDate(a, c[p], c[p + 1]),
  // The host restarts the game and feeds it recorded script n. With no
  // host for it, flag 27 says so and the logic carries on.
  'demo.play': (a, c, p) => {
    if (a.host.demo) a.host.demo(c[p]);
    else a.flags[27] = 1;
  },
};

// Opcode -> function tables, checked once against commands.js.
const TEST_BY_OP = TESTS.map((t) => (t ? TEST_FN[t[0]] : null));
const ACTION_BY_OP = [];
ACTIONS.forEach((t, op) => { ACTION_BY_OP[op] = ACTION_FN[t[0]]; });
for (const op of Object.keys(EXT_ACTIONS)) ACTION_BY_OP[op] = ACTION_FN[EXT_ACTIONS[op][0]];

// Names in commands.js that have no code here (the tests want none).
export function missingCommands() {
  const out = [];
  TESTS.forEach((t) => { if (t && !TEST_FN[t[0]]) out.push(t[0]); });
  ACTIONS.forEach((t) => { if (!ACTION_FN[t[0]]) out.push(t[0]); });
  for (const op of Object.keys(EXT_ACTIONS)) if (!ACTION_FN[EXT_ACTIONS[op][0]]) out.push(EXT_ACTIONS[op][0]);
  return out;
}

function testSize(c, ip, op) {
  if (op === 14) return c[ip] * 2 + 1;
  return TESTS[op] ? TESTS[op][1] : 0;
}

export const opsMethods = {
  // A bad object number must not crash the game: it gets a throwaway.
  obj(n) {
    if (n < this.objs.length) return this.objs[n];
    this.log(`object ${n} is out of range`);
    return this.spare;
  },

  str(n) { return this.strings[n % this.strings.length]; },
  setStr(n, s) { this.strings[n % this.strings.length] = s.slice(0, 40); },

  callLogic(n) {
    if (this.stack.length >= 32) { this.log(`call(${n}): too deep`); return; }
    if (!this.getLogic(n)) { this.log(`logic ${n} is missing`); return; }
    this.stack.push({ n, ip: this.scanStart[n] || 0 });
  },

  // Evaluate the test list after an FF. Returns the next ip: into the
  // block when true, past it when false.
  evalIf(c, ip) {
    let not = false, inOr = false, orVal = false;
    for (;;) {
      const op = c[ip++];
      if (op === undefined) return c.length;
      if (op === 0xff) return ip + 2;
      if (op === 0xfd) { not = !not; continue; }
      if (op === 0xfc) {
        if (inOr) { inOr = false; if (!orVal) return this.skipIf(c, ip); }
        else { inOr = true; orVal = false; }
        continue;
      }
      const fn = TEST_BY_OP[op];
      let r = fn ? fn(this, c, ip) : false;
      if (!fn) this.log(`unknown test ${op}`);
      ip += testSize(c, ip, op);
      if (not) { r = !r; not = false; }
      if (inOr) orVal = orVal || r;
      else if (!r) return this.skipIf(c, ip);
    }
  },

  skipIf(c, ip) {
    for (;;) {
      const op = c[ip++];
      if (op === undefined) return c.length;
      if (op === 0xff) break;
      if (op === 0xfc || op === 0xfd) continue;
      ip += testSize(c, ip, op);
    }
    return ip + 2 + word(c, ip);
  },

  // Run the logic stack until it empties, a command blocks, or a
  // command asks for logic 0 to start over (new.room, restart, restore).
  runStack() {
    let budget = 2000000;
    while (this.stack.length) {
      if (this.exitAll) break;
      const f = this.stack[this.stack.length - 1];
      const lg = this.getLogic(f.n);
      if (!lg) { this.stack.pop(); continue; }
      const c = lg.code;
      let ip = f.ip;
      for (;;) {
        if (--budget < 0) {
          this.log(`logic ${f.n} never returns; stopped`);
          this.stack.length = 0;
          return RUN.DONE;
        }
        if (ip >= c.length) { this.stack.pop(); break; }
        const op = c[ip++];
        if (op === 0xff) { ip = this.evalIf(c, ip); continue; }
        if (op === 0xfe) {
          let off = word(c, ip);
          if (off & 0x8000) off -= 0x10000;
          ip += 2 + off;
          continue;
        }
        if (op === 0) { this.stack.pop(); break; }
        const info = actionInfo(op);
        const fn = ACTION_BY_OP[op];
        if (!info || !fn) {
          this.log(`logic ${f.n}: unknown action ${op} at ${ip - 1}`);
          this.stack.pop();
          break;
        }
        const p = ip;
        ip += info[1];
        // Saved first, so a command that blocks resumes after itself.
        f.ip = ip;
        this.curLogic = f.n;
        fn(this, c, p);
        if (this.exitAll) break;
        if (this.wait) return RUN.BLOCKED;
        if (this.stack[this.stack.length - 1] !== f) break;
      }
    }
    if (this.exitAll) {
      this.exitAll = 0;
      this.stack.length = 0;
      return this.ended ? RUN.DONE : RUN.AGAIN;
    }
    return RUN.DONE;
  },

  // ---- sound ----

  startSound(n, flag) {
    this.stopSound();
    this.flags[flag] = 0;
    const bytes = loadResource(this.res.snddir, this.res.vols, n);
    if (!bytes) { this.log(`sound ${n} is missing`); this.flags[flag] = 1; return; }
    let snd = this.sounds[n];
    if (!snd) {
      try { snd = decodeSound(bytes); } catch (e) { snd = { events: [], duration: 0 }; }
      this.sounds[n] = snd;
    }
    this.snd = { n, flag, left: snd.duration };
    if (this.flags[9] && this.host.sound) this.host.sound(snd.events);
  },

  stopSound() {
    if (!this.snd) return;
    this.flags[this.snd.flag] = 1;
    this.snd = null;
    if (this.host.stopSound) this.host.stopSound();
  },

  // The done flag is set by counting cycles, not by the host, so it
  // lands on the same cycle everywhere: one cycle is 3 * v10 sixtieths.
  tickSound() {
    if (!this.snd) return;
    this.snd.left -= 3 * (this.vars[10] || 1);
    if (this.snd.left <= 0) {
      this.flags[this.snd.flag] = 1;
      this.snd = null;
    }
  },
};
