// Screen objects: motion, cycling, control lines, collisions, drawing
// with save-under. Mixed into Interpreter. Integers only: the 8088
// interpreter has to reproduce every number here.

import { decodeView, loadResource } from './resources.js';

export const W = 160;
export const H = 168;

export const F = {
  DRAWN: 0x0001, IGNORE_BLOCKS: 0x0002, FIXED_PRI: 0x0004, IGNORE_HORIZON: 0x0008,
  UPDATE: 0x0010, CYCLING: 0x0020, ANIMATED: 0x0040, MOTION: 0x0080,
  ON_WATER: 0x0100, IGNORE_OBJS: 0x0200, UPDATE_POS: 0x0400, ON_LAND: 0x0800,
  DONT_UPDATE: 0x1000, FIX_LOOP: 0x2000, DIDNT_MOVE: 0x4000,
};
const LIVE = F.ANIMATED | F.UPDATE | F.DRAWN;

export const MOTION = { NORMAL: 0, WANDER: 1, FOLLOW: 2, MOVE: 3 };
export const CYCLE = { NORMAL: 0, END: 1, REV_END: 2, REVERSE: 3 };

// Direction 0 is still, 1 is up, then clockwise to 8 (up and left).
const DX = [0, 0, 1, 1, 1, 0, -1, -1, -1];
const DY = [0, -1, -1, 0, 1, 1, 1, 0, -1];
// Loop to show for each direction; 4 means leave the loop alone.
const LOOP_2 = [4, 4, 0, 0, 0, 4, 1, 1, 1];
const LOOP_4 = [4, 3, 0, 0, 0, 2, 1, 1, 1];
const DIR_TABLE = [8, 1, 2, 7, 0, 3, 6, 5, 4];

// The numeric fields of an object, in the order snapshot() and hash() use.
export const OBJ_FIELDS = [
  'stepTime', 'stepTimeCount', 'x', 'y', 'view', 'loop', 'cel', 'numLoops',
  'numCels', 'xSize', 'ySize', 'stepSize', 'cycleTime', 'cycleTimeCount',
  'dir', 'motion', 'cycle', 'priority', 'flags', 'px', 'py', 'pxs', 'pys',
  'moveX', 'moveY', 'moveStep', 'moveFlag', 'followStep', 'followFlag',
  'followCount', 'wanderCount', 'loopFlag', 'hasView',
];

export function newObject(n) {
  const o = { n, bak: null };
  for (const f of OBJ_FIELDS) o[f] = 0;
  o.stepTime = o.stepTimeCount = o.stepSize = o.cycleTime = o.cycleTimeCount = 1;
  return o;
}

function step(delta, size) {
  return -size >= delta ? 0 : size <= delta ? 2 : 1;
}

function directionTo(x, y, tx, ty, size) {
  return DIR_TABLE[step(tx - x, size) + 3 * step(ty - y, size)];
}

export const animMethods = {
  getView(n) {
    if (n in this.views) return this.views[n];
    const bytes = loadResource(this.res.viewdir, this.res.vols, n);
    let view = null;
    if (bytes) {
      try { view = decodeView(bytes); } catch (e) { view = null; }
    }
    if (!view) this.log(`view ${n} is missing or broken`);
    this.views[n] = view;
    return view;
  },

  celOf(o) {
    const view = o.hasView ? this.views[o.view] || this.getView(o.view) : null;
    return view ? view.loops[o.loop][o.cel] : null;
  },

  setView(o, n) {
    const view = this.getView(n);
    if (!view || !view.loops.length) return;
    o.view = n;
    o.hasView = 1;
    o.numLoops = view.loops.length;
    this.setLoop(o, o.loop >= o.numLoops ? 0 : o.loop);
  },

  setLoop(o, l) {
    if (!o.hasView) return;
    const view = this.getView(o.view);
    // Sierra stops with an error on a bad loop or cel number; we use 0.
    if (l >= view.loops.length) l = 0;
    o.loop = l;
    o.numCels = view.loops[l].length;
    this.setCel(o, o.cel >= o.numCels ? 0 : o.cel);
  },

  setCel(o, c) {
    if (!o.hasView) return;
    const cels = this.getView(o.view).loops[o.loop];
    if (c >= cels.length) c = 0;
    o.cel = c;
    o.xSize = cels[c].w;
    o.ySize = cels[c].h;
    // A bigger cel must not hang off the picture.
    if (o.x + o.xSize > W) { o.flags |= F.UPDATE_POS; o.x = W - o.xSize; }
    if (o.y - o.ySize + 1 < 0) { o.flags |= F.UPDATE_POS; o.y = o.ySize - 1; }
    if (o.y <= this.horizon && !(o.flags & F.IGNORE_HORIZON)) {
      o.flags |= F.UPDATE_POS;
      o.y = this.horizon + 1;
    }
  },

  // ---- priority bands ----

  setPriBase(base) {
    this.priBase = base;
    for (let y = 0; y < H; y++) {
      let p = y < base ? 4 : 5 + (((y - base) * 10 / (H - base)) | 0);
      if (p > 15) p = 15;
      this.priTable[y] = p;
    }
  },

  // The top row of a band: where a fixed-priority object sorts.
  priToY(p) {
    return this.priBase + Math.trunc((p - 5) * (H - this.priBase) / 10);
  },

  // ---- where an object may stand ----

  inBlock(x, y) {
    return x > this.block.x1 && x < this.block.x2 && y > this.block.y1 && y < this.block.y2;
  },

  goodPosition(o) {
    if (o.x < 0 || o.x + o.xSize > W) return false;
    if (o.y - o.ySize < -1 || o.y >= H) return false;
    if (!(o.flags & F.IGNORE_HORIZON) && o.y <= this.horizon) return false;
    return true;
  },

  collides(o) {
    if (o.flags & F.IGNORE_OBJS) return false;
    for (const u of this.objs) {
      if ((u.flags & (F.ANIMATED | F.DRAWN)) !== (F.ANIMATED | F.DRAWN)) continue;
      if (u.flags & F.IGNORE_OBJS) continue;
      if (u === o) continue;
      if (o.x + o.xSize < u.x || o.x > u.x + u.xSize) continue;
      if (o.y === u.y) return true;
      // Crossing another object's baseline counts as bumping into it.
      if ((o.y > u.y && o.py < u.py) || (o.y < u.y && o.py > u.py)) return true;
    }
    return false;
  },

  // The control lines under the object's baseline. Also sets its band,
  // and for ego the water and trigger flags.
  canBeHere(o) {
    if (!(o.flags & F.FIXED_PRI)) o.priority = this.priTable[o.y < 0 ? 0 : o.y >= H ? H - 1 : o.y];
    let trigger = false, water = false, ok = true;
    if (o.priority !== 15) {
      water = true;
      for (let i = 0; i < o.xSize; i++) {
        const x = o.x + i;
        const p = x >= 0 && x < W && o.y >= 0 && o.y < H ? this.gbuf[o.y * W + x] >> 4 : 4;
        if (p === 0) { ok = false; break; }
        if (p === 3) continue;
        water = false;
        if (p === 1) {
          if (!(o.flags & F.IGNORE_BLOCKS)) { ok = false; break; }
        } else if (p === 2) trigger = true;
      }
      if (ok) {
        if (!water && (o.flags & F.ON_WATER)) ok = false;
        else if (water && (o.flags & F.ON_LAND)) ok = false;
      }
    }
    if (o.n === 0) {
      this.flags[3] = trigger ? 1 : 0;
      this.flags[0] = water ? 1 : 0;
    }
    return ok;
  },

  // Spiral outward (left, down, right, up, each leg one longer) until
  // the object fits. Sierra's loop has no end; ours gives up after legs
  // of 200 so a picture with no free spot cannot hang the game.
  fixPosition(o) {
    if (!(o.flags & F.IGNORE_HORIZON) && o.y <= this.horizon) o.y = this.horizon + 1;
    let dir = 0, count = 1, size = 1;
    const x0 = o.x, y0 = o.y;
    while (!this.goodPosition(o) || this.collides(o) || !this.canBeHere(o)) {
      if (dir === 0) { o.x--; if (--count) continue; dir = 1; }
      else if (dir === 1) { o.y++; if (--count) continue; dir = 2; size++; }
      else if (dir === 2) { o.x++; if (--count) continue; dir = 3; }
      else { o.y--; if (--count) continue; dir = 0; size++; }
      count = size;
      if (size > 200) { o.x = x0; o.y = y0; break; }
    }
  },

  // ---- motion ----

  stopMove(o) {
    if (o.motion === MOTION.MOVE) {
      o.stepSize = o.moveStep;
      this.flags[o.moveFlag] = 1;
    }
    o.motion = MOTION.NORMAL;
    if (o.n === 0) {
      this.playerControl = 1;
      this.vars[6] = 0;
    }
  },

  motionMove(o) {
    o.dir = directionTo(o.x, o.y, o.moveX, o.moveY, o.stepSize);
    if (o.n === 0) this.vars[6] = o.dir;
    if (o.dir === 0) this.stopMove(o);
  },

  // Integer rule: a new heading is random % 9 and lasts 6 + random % 45
  // steps, or until the object is stopped by something.
  motionWander(o) {
    if (o.wanderCount === 0 || (o.flags & F.DIDNT_MOVE)) {
      o.dir = this.rnd() % 9;
      if (o.n === 0) this.vars[6] = o.dir;
      o.wanderCount = 6 + this.rnd() % 45;
    } else o.wanderCount--;
  },

  motionFollow(o) {
    const ego = this.objs[0];
    const ex = ego.x + (ego.xSize >> 1), ey = ego.y;
    const ox = o.x + (o.xSize >> 1), oy = o.y;
    const dir = directionTo(ox, oy, ex, ey, o.followStep);
    if (dir === 0) {
      o.dir = 0;
      o.motion = MOTION.NORMAL;
      this.flags[o.followFlag] = 1;
      return;
    }
    if (o.followCount === 255) o.followCount = 0;
    else if (o.flags & F.DIDNT_MOVE) {
      // Stuck: try a random heading for a while. Integer rule: heading
      // 1 + random % 8, for stepSize + random % (d - stepSize + 1) steps.
      o.dir = 1 + this.rnd() % 8;
      const d = (Math.abs(ey - oy) + Math.abs(ex - ox)) >> 1;
      o.followCount = d < o.stepSize ? o.stepSize : o.stepSize + this.rnd() % (d - o.stepSize + 1);
      return;
    }
    if (o.followCount !== 0) {
      o.followCount -= o.stepSize;
      if (o.followCount < 0) o.followCount = 0;
    } else o.dir = dir;
  },

  // Runs before the logics, so they see the direction about to be taken.
  checkAllMotions() {
    for (const o of this.objs) {
      if ((o.flags & LIVE) !== LIVE || o.stepTimeCount !== 1) continue;
      if (o.motion === MOTION.WANDER) this.motionWander(o);
      else if (o.motion === MOTION.FOLLOW) this.motionFollow(o);
      else if (o.motion === MOTION.MOVE) this.motionMove(o);
      if (this.block.on && !(o.flags & F.IGNORE_BLOCKS) && o.dir) {
        // Stop at the edge of the block() rectangle, from either side.
        const inside = this.inBlock(o.x, o.y);
        const nx = o.x + o.stepSize * DX[o.dir], ny = o.y + o.stepSize * DY[o.dir];
        if (this.inBlock(nx, ny) === inside) o.flags &= ~F.MOTION;
        else {
          o.flags |= F.MOTION;
          o.dir = 0;
          if (o.n === 0) this.vars[6] = 0;
        }
      }
    }
  },

  advanceCel(o) {
    if (o.flags & F.DONT_UPDATE) { o.flags &= ~F.DONT_UPDATE; return; }
    let cel = o.cel;
    const last = o.numCels - 1;
    let done = false;
    if (o.cycle === CYCLE.NORMAL) { if (++cel > last) cel = 0; }
    else if (o.cycle === CYCLE.REVERSE) { cel = cel === 0 ? last : cel - 1; }
    else if (o.cycle === CYCLE.END) { if (cel >= last || ++cel === last) done = true; }
    else { if (cel === 0 || --cel === 0) done = true; }
    if (done) {
      this.flags[o.loopFlag] = 1;
      o.flags &= ~F.CYCLING;
      o.dir = 0;
      o.cycle = CYCLE.NORMAL;
    }
    this.setCel(o, cel < 0 ? 0 : cel);
  },

  updatePosition() {
    this.vars[2] = 0;
    this.vars[4] = 0;
    this.vars[5] = 0;
    for (const o of this.objs) {
      if ((o.flags & LIVE) !== LIVE) continue;
      if (o.stepTimeCount > 1) { o.stepTimeCount--; continue; }
      o.stepTimeCount = o.stepTime;
      const oldX = o.x, oldY = o.y;
      let border = 0;
      // JRQ rule: a step of n pixels is taken one pixel at a time, and
      // stops at the last pixel that is allowed. Sierra tested only the
      // landing spot, so a fast walker could jump a one-row wall or a
      // one-row trigger line. Here he cannot, at any step size.
      const clamp = (x, y) => {
        let b = 0;
        if (x < 0) { x = 0; b = 4; }
        else if (x + o.xSize > W) { x = W - o.xSize; b = 2; }
        if (y - o.ySize + 1 < 0) { y = o.ySize - 1; b = 1; }
        else if (y > H - 1) { y = H - 1; b = 3; }
        else if (!(o.flags & F.IGNORE_HORIZON) && y <= this.horizon) { y = this.horizon + 1; b = 1; }
        return [x, y, b];
      };
      const moving = !(o.flags & F.UPDATE_POS) && o.dir !== 0;
      const steps = moving ? o.stepSize : 1;
      let x = o.x, y = o.y, trigger = 0, stuck = false;
      for (let s = 0; s < steps; s++) {
        const [nx, ny, b] = clamp(x + (moving ? DX[o.dir] : 0), y + (moving ? DY[o.dir] : 0));
        o.x = nx;
        o.y = ny;
        if (this.collides(o) || !this.canBeHere(o)) {
          o.x = x;
          o.y = y;
          stuck = s === 0;
          break;
        }
        x = nx;
        y = ny;
        if (o.n === 0 && this.flags[3]) trigger = 1;
        if (b) { border = b; break; }
      }
      if (stuck) {
        o.x = oldX;
        o.y = oldY;
        border = 0;
        this.fixPosition(o);
      } else if (x !== o.x || y !== o.y || steps > 1) {
        // Stopped part way: set the flags for where he actually stands,
        // but keep a trigger he crossed on the way.
        this.canBeHere(o);
        if (o.n === 0 && trigger) this.flags[3] = 1;
      }
      if (border) {
        if (o.n === 0) this.vars[2] = border;
        else { this.vars[4] = o.n; this.vars[5] = border; }
        if (o.motion === MOTION.MOVE) this.stopMove(o);
      }
      o.flags &= ~F.UPDATE_POS;
    }
  },

  // Once a cycle, after the logics.
  updateObjects() {
    let any = false;
    for (const o of this.objs) {
      if ((o.flags & LIVE) !== LIVE) continue;
      any = true;
      let loop = 4;
      if (!(o.flags & F.FIX_LOOP)) {
        if (o.numLoops === 2 || o.numLoops === 3) loop = LOOP_2[o.dir];
        else if (o.numLoops === 4) loop = LOOP_4[o.dir];
      }
      if (o.stepTimeCount === 1 && loop !== 4 && loop !== o.loop) this.setLoop(o, loop);
      if ((o.flags & F.CYCLING) && o.cycleTimeCount > 0 && --o.cycleTimeCount === 0) {
        this.advanceCel(o);
        o.cycleTimeCount = o.cycleTime;
      }
    }
    if (!any) return;
    this.eraseList(this.drawnUpd);
    this.updatePosition();
    this.drawList(true);
    for (const n of this.drawnUpd) {
      const o = this.objs[n];
      this.commitObj(o);
      if (o.stepTimeCount !== o.stepTime) continue;
      if (o.x === o.px && o.y === o.py) { o.flags |= F.DIDNT_MOVE; continue; }
      o.px = o.x;
      o.py = o.y;
      o.flags &= ~F.DIDNT_MOVE;
    }
    this.objs[0].flags &= ~(F.ON_WATER | F.ON_LAND);
  },

  // ---- drawing ----

  drawCel(o) {
    const cel = this.celOf(o);
    if (!cel) return;
    if (!(o.flags & F.FIXED_PRI)) o.priority = this.priTable[o.y < 0 ? 0 : o.y >= H ? H - 1 : o.y];
    const pri = o.priority, g = this.gbuf;
    const top = o.y - cel.h + 1;
    let hidden = 1;
    for (let cy = 0; cy < cel.h; cy++) {
      const y = top + cy;
      if (y < 0 || y >= H) continue;
      for (let cx = 0; cx < cel.w; cx++) {
        const color = cel.pix[cy * cel.w + cx];
        if (color === 255) continue;
        const x = o.x + cx;
        if (x < 0 || x >= W) continue;
        const i = y * W + x;
        const sp = g[i] >> 4;
        if (sp >= 3) {
          if (sp <= pri) { g[i] = (pri << 4) | color; hidden = 0; }
          continue;
        }
        // On a control line: judge by the first real priority below it,
        // and leave the line itself in place.
        let below = 0;
        for (let yy = y + 1; yy < H; yy++) {
          const q = g[yy * W + x] >> 4;
          if (q >= 3) { below = q; break; }
        }
        if (below <= pri) { g[i] = (g[i] & 0xf0) | color; hidden = 0; }
      }
    }
    // Flag 1: no pixel of ego is showing.
    if (o.n === 0) this.flags[1] = hidden;
  },

  saveUnder(o) {
    let x1 = o.x, y1 = o.y - o.ySize + 1, x2 = o.x + o.xSize - 1, y2 = o.y;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= W) x2 = W - 1;
    if (y2 >= H) y2 = H - 1;
    const w = x2 - x1 + 1, h = y2 - y1 + 1;
    if (w <= 0 || h <= 0) { o.bak = null; return; }
    const data = new Uint8Array(w * h);
    for (let r = 0; r < h; r++) {
      data.set(this.gbuf.subarray((y1 + r) * W + x1, (y1 + r) * W + x1 + w), r * w);
    }
    o.bak = { x: x1, y: y1, w, h, data };
  },

  restoreUnder(o) {
    const b = o.bak;
    if (!b) return;
    for (let r = 0; r < b.h; r++) {
      this.gbuf.set(b.data.subarray(r * b.w, r * b.w + b.w), (b.y + r) * W + b.x);
    }
    o.bak = null;
  },

  eraseList(list) {
    for (let i = list.length - 1; i >= 0; i--) this.restoreUnder(this.objs[list[i]]);
    list.length = 0;
  },

  // Back to front: by band for fixed-priority objects, else by baseline.
  drawList(updating) {
    const list = updating ? this.drawnUpd : this.drawnNon;
    const want = updating ? LIVE : F.ANIMATED | F.DRAWN;
    const found = [];
    for (const o of this.objs) {
      if ((o.flags & LIVE) !== want) continue;
      found.push({ n: o.n, key: (o.flags & F.FIXED_PRI) ? this.priToY(o.priority) : o.y });
    }
    found.sort((a, b) => a.key - b.key || a.n - b.n);
    for (const f of found) {
      const o = this.objs[f.n];
      this.saveUnder(o);
      this.drawCel(o);
      list.push(o.n);
    }
  },

  eraseAll() {
    this.eraseList(this.drawnUpd);
    this.eraseList(this.drawnNon);
  },

  drawAll() {
    this.drawList(false);
    this.drawList(true);
  },

  // Copy part of the picture buffer to the screen (inclusive corners).
  commitBlock(x1, y1, x2, y2) {
    if (!this.gfx) return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= W) x2 = W - 1;
    if (y2 >= H) y2 = H - 1;
    const top = this.picTop * 8;
    for (let y = y1; y <= y2; y++) {
      const sy = y + top;
      if (sy >= 200) break;
      for (let x = x1; x <= x2; x++) this.screen[sy * W + x] = this.gbuf[y * W + x] & 15;
    }
  },

  // Show an object's old and new rectangles.
  commitObj(o) {
    if (!this.picShown) return;
    const x1 = Math.min(o.x, o.px), x2 = Math.max(o.x + o.xSize, o.px + o.pxs) - 1;
    const y2 = Math.max(o.y, o.py);
    const y1 = Math.min(o.y - o.ySize, o.py - o.pys) + 1;
    o.pxs = o.xSize;
    o.pys = o.ySize;
    this.commitBlock(x1, y1, x2, y2);
  },

  showPic() {
    this.commitBlock(0, 0, W - 1, H - 1);
  },

  // add.to.pic: a cel becomes part of the picture, with an optional
  // box of control priority around its base.
  addToPic(view, loop, cel, x, y, pri, margin) {
    const o = newObject(255);
    o.x = x;
    o.y = y;
    o.flags = F.IGNORE_OBJS | F.IGNORE_HORIZON | F.FIXED_PRI;
    this.setView(o, view);
    if (!o.hasView) return;
    this.setLoop(o, loop);
    this.setCel(o, cel);
    o.priority = 15;
    this.fixPosition(o);
    if (pri === 0) o.flags = F.IGNORE_HORIZON;
    o.priority = pri;
    this.eraseAll();
    this.drawCel(o);
    if (margin <= 3) {
      // The box is as tall as the rest of the band above the baseline,
      // or the cel, whichever is less.
      const band = this.priTable[o.y];
      let height = 0;
      for (let yy = o.y; yy >= 0 && this.priTable[yy] === band; yy--) height++;
      if (o.ySize < height) height = o.ySize;
      const g = this.gbuf;
      const set = (px, py) => {
        if (px >= 0 && px < W && py >= 0 && py < H) g[py * W + px] = (g[py * W + px] & 15) | (margin << 4);
      };
      const topY = o.y - height + 1;
      for (let i = 0; i < o.xSize; i++) set(o.x + i, o.y);
      for (let yy = topY; yy <= o.y; yy++) { set(o.x, yy); set(o.x + o.xSize - 1, yy); }
      for (let i = 1; i < o.xSize - 1; i++) set(o.x + i, topY);
    }
    this.drawAll();
    if (this.picShown) this.commitBlock(o.x, o.y - o.ySize + 1, o.x + o.xSize - 1, o.y);
  },
};
