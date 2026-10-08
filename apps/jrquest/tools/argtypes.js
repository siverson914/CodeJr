// Argument types for every test and action command, in opcode order to
// match js/commands.js. One letter an argument byte:
//   v variable   f flag      n plain number   o screen object
//   i inventory  s string    w word           m message   c controller
//
// Checked against ScummVM's engines/agi/opcodes.cpp (argument counts) and
// op_cmd.cpp / op_test.cpp (whether each byte is used as a value, a
// variable number or a flag number). ScummVM's table only tells v from n,
// so the o, i, f, s, w, m and c letters come from how its code uses the
// byte and from the AGI Specifications' command reference.

import { TESTS, ACTIONS, EXT_ACTIONS } from '../js/commands.js';

const TEST_TYPES = {
  'equaln': 'vn', 'equalv': 'vv', 'lessn': 'vn', 'lessv': 'vv',
  'greatern': 'vn', 'greaterv': 'vv', 'isset': 'f', 'issetv': 'v',
  'has': 'i', 'obj.in.room': 'iv', 'posn': 'onnnn', 'controller': 'c',
  'have.key': '', 'said': null, 'compare.strings': 'ss', 'obj.in.box': 'onnnn',
  'center.posn': 'onnnn', 'right.posn': 'onnnn',
};

const ACTION_TYPES = {
  'return': '', 'increment': 'v', 'decrement': 'v', 'assignn': 'vn',
  'assignv': 'vv', 'addn': 'vn', 'addv': 'vv', 'subn': 'vn',
  'subv': 'vv', 'lindirectv': 'vv', 'rindirect': 'vv', 'lindirectn': 'vn',
  'set': 'f', 'reset': 'f', 'toggle': 'f', 'set.v': 'v',
  'reset.v': 'v', 'toggle.v': 'v', 'new.room': 'n', 'new.room.v': 'v',
  'load.logics': 'n', 'load.logics.v': 'v', 'call': 'n', 'call.v': 'v',
  // The picture commands take the picture number in a variable.
  'load.pic': 'v', 'draw.pic': 'v', 'show.pic': '', 'discard.pic': 'v',
  'overlay.pic': 'v', 'show.pri.screen': '', 'load.view': 'n', 'load.view.v': 'v',
  'discard.view': 'n', 'animate.obj': 'o', 'unanimate.all': '', 'draw': 'o',
  'erase': 'o', 'position': 'onn', 'position.v': 'ovv', 'get.posn': 'ovv',
  'reposition': 'ovv', 'set.view': 'on', 'set.view.v': 'ov', 'set.loop': 'on',
  'set.loop.v': 'ov', 'fix.loop': 'o', 'release.loop': 'o', 'set.cel': 'on',
  'set.cel.v': 'ov', 'last.cel': 'ov', 'current.cel': 'ov', 'current.loop': 'ov',
  'current.view': 'ov', 'number.of.loops': 'ov', 'set.priority': 'on', 'set.priority.v': 'ov',
  'release.priority': 'o', 'get.priority': 'ov', 'stop.update': 'o', 'start.update': 'o',
  'force.update': 'o', 'ignore.horizon': 'o', 'observe.horizon': 'o', 'set.horizon': 'n',
  'object.on.water': 'o', 'object.on.land': 'o', 'object.on.anything': 'o', 'ignore.objs': 'o',
  'observe.objs': 'o', 'distance': 'oov', 'stop.cycling': 'o', 'start.cycling': 'o',
  'normal.cycle': 'o', 'end.of.loop': 'of', 'reverse.cycle': 'o', 'reverse.loop': 'of',
  'cycle.time': 'ov', 'stop.motion': 'o', 'start.motion': 'o', 'step.size': 'ov',
  'step.time': 'ov', 'move.obj': 'onnnf', 'move.obj.v': 'ovvvf', 'follow.ego': 'onf',
  'wander': 'o', 'normal.motion': 'o', 'set.dir': 'ov', 'get.dir': 'ov',
  'ignore.blocks': 'o', 'observe.blocks': 'o', 'block': 'nnnn', 'unblock': '',
  // put takes its room in a variable even without the .v (ScummVM cmdPut).
  'get': 'i', 'get.v': 'v', 'drop': 'i', 'put': 'iv',
  'put.v': 'vv', 'get.room.v': 'vv', 'load.sound': 'n', 'sound': 'nf',
  'stop.sound': '', 'print': 'm', 'print.v': 'v', 'display': 'nnm',
  'display.v': 'vvv', 'clear.lines': 'nnn', 'text.screen': '', 'graphics': '',
  'set.cursor.char': 'm', 'set.text.attribute': 'nn', 'shake.screen': 'n', 'configure.screen': 'nnn',
  'status.line.on': '', 'status.line.off': '', 'set.string': 'sm', 'get.string': 'smnnn',
  'word.to.string': 'sw', 'parse': 's', 'get.num': 'mv', 'prevent.input': '',
  'accept.input': '', 'set.key': 'nnc', 'add.to.pic': 'nnnnnnn', 'add.to.pic.v': 'vvvvvvv',
  'status': '', 'save.game': '', 'restore.game': '', 'init.disk': '',
  'restart.game': '', 'show.obj': 'n', 'random': 'nnv', 'program.control': '',
  'player.control': '', 'obj.status.v': 'v', 'quit': 'n', 'show.mem': '',
  'pause': '', 'echo.line': '', 'cancel.line': '', 'init.joy': '',
  'toggle.monitor': '', 'version': '', 'script.size': 'n', 'set.game.id': 'm',
  'log': 'm', 'set.scan.start': '', 'reset.scan.start': '', 'reposition.to': 'onn',
  'reposition.to.v': 'ovv', 'trace.on': '', 'trace.info': 'nnn', 'print.at': 'mnnn',
  'print.at.v': 'vnnn', 'discard.view.v': 'v', 'clear.text.rect': 'nnnnn', 'set.upper.left': 'nn',
  'set.menu': 'm', 'set.menu.item': 'mc', 'submit.menu': '', 'enable.item': 'c',
  'disable.item': 'c', 'menu.input': '', 'show.obj.v': 'v', 'open.dialogue': '',
  'close.dialogue': '', 'mul.n': 'vn', 'mul.v': 'vv', 'div.n': 'vn',
  'div.v': 'vv', 'close.window': '', 'set.simple': 's', 'push.script': '',
  'pop.script': '', 'hold.key': '', 'set.pri.base': 'n', 'discard.sound': 'n',
  'hide.mouse': '', 'allow.menu': 'n', 'show.mouse': '', 'fence.mouse': 'nnnn',
  'mouse.posn': 'vv', 'release.key': '', 'adj.ego.move.to.xy': '',
  // JRQ extensions.
  'speak': 'n', 'talk': 'n', 'link.status': 'v', 'score.event': 'n',
  'str.date': 'sv', 'demo.play': 'n',
};

function build(entries, types, kind) {
  const byName = new Map();
  for (const [opText, entry] of entries) {
    if (!entry) continue;
    const [name, count] = entry;
    const t = types[name];
    if (t === undefined) throw new Error(`argtypes: no types for ${kind} ${name}`);
    if (t !== null && t.length !== count) {
      throw new Error(`argtypes: ${kind} ${name} has ${count} arguments in commands.js, ${t.length} here`);
    }
    byName.set(name, { name, op: Number(opText), types: t });
  }
  return byName;
}

// name -> { name, op, types }. types is null for said (variable length).
export const TEST_BY_NAME = build(Object.entries(TESTS), TEST_TYPES, 'test');
export const ACTION_BY_NAME = build(
  [...Object.entries(ACTIONS), ...Object.entries(EXT_ACTIONS)], ACTION_TYPES, 'action');

const byOp = (map) => new Map([...map.values()].map((e) => [e.op, e]));
export const TEST_BY_OP = byOp(TEST_BY_NAME);
export const ACTION_BY_OP = byOp(ACTION_BY_NAME);

export const TYPE_NAMES = {
  v: 'variable', f: 'flag', n: 'number', o: 'object', i: 'inventory item',
  s: 'string', w: 'word', m: 'message', c: 'controller',
};
