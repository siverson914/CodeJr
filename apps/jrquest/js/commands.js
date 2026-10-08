// AGI v2 command tables shared by the interpreter and the tools.
// Each entry is [name, number of argument bytes]. Index = opcode.
// said (test 14) is the one variable-length command: a count byte, then
// that many 16-bit little-endian word group numbers.

export const TESTS = [
  null,
  ['equaln', 2], ['equalv', 2], ['lessn', 2], ['lessv', 2],
  ['greatern', 2], ['greaterv', 2], ['isset', 1], ['issetv', 1],
  ['has', 1], ['obj.in.room', 2], ['posn', 5], ['controller', 1],
  ['have.key', 0], ['said', -1], ['compare.strings', 2], ['obj.in.box', 5],
  ['center.posn', 5], ['right.posn', 5],
];

export const ACTIONS = [
  ['return', 0], ['increment', 1], ['decrement', 1], ['assignn', 2],
  ['assignv', 2], ['addn', 2], ['addv', 2], ['subn', 2],
  ['subv', 2], ['lindirectv', 2], ['rindirect', 2], ['lindirectn', 2],
  ['set', 1], ['reset', 1], ['toggle', 1], ['set.v', 1],
  ['reset.v', 1], ['toggle.v', 1], ['new.room', 1], ['new.room.v', 1],
  ['load.logics', 1], ['load.logics.v', 1], ['call', 1], ['call.v', 1],
  ['load.pic', 1], ['draw.pic', 1], ['show.pic', 0], ['discard.pic', 1],
  ['overlay.pic', 1], ['show.pri.screen', 0], ['load.view', 1], ['load.view.v', 1],
  ['discard.view', 1], ['animate.obj', 1], ['unanimate.all', 0], ['draw', 1],
  ['erase', 1], ['position', 3], ['position.v', 3], ['get.posn', 3],
  ['reposition', 3], ['set.view', 2], ['set.view.v', 2], ['set.loop', 2],
  ['set.loop.v', 2], ['fix.loop', 1], ['release.loop', 1], ['set.cel', 2],
  ['set.cel.v', 2], ['last.cel', 2], ['current.cel', 2], ['current.loop', 2],
  ['current.view', 2], ['number.of.loops', 2], ['set.priority', 2], ['set.priority.v', 2],
  ['release.priority', 1], ['get.priority', 2], ['stop.update', 1], ['start.update', 1],
  ['force.update', 1], ['ignore.horizon', 1], ['observe.horizon', 1], ['set.horizon', 1],
  ['object.on.water', 1], ['object.on.land', 1], ['object.on.anything', 1], ['ignore.objs', 1],
  ['observe.objs', 1], ['distance', 3], ['stop.cycling', 1], ['start.cycling', 1],
  ['normal.cycle', 1], ['end.of.loop', 2], ['reverse.cycle', 1], ['reverse.loop', 2],
  ['cycle.time', 2], ['stop.motion', 1], ['start.motion', 1], ['step.size', 2],
  ['step.time', 2], ['move.obj', 5], ['move.obj.v', 5], ['follow.ego', 3],
  ['wander', 1], ['normal.motion', 1], ['set.dir', 2], ['get.dir', 2],
  ['ignore.blocks', 1], ['observe.blocks', 1], ['block', 4], ['unblock', 0],
  ['get', 1], ['get.v', 1], ['drop', 1], ['put', 2],
  ['put.v', 2], ['get.room.v', 2], ['load.sound', 1], ['sound', 2],
  ['stop.sound', 0], ['print', 1], ['print.v', 1], ['display', 3],
  ['display.v', 3], ['clear.lines', 3], ['text.screen', 0], ['graphics', 0],
  ['set.cursor.char', 1], ['set.text.attribute', 2], ['shake.screen', 1], ['configure.screen', 3],
  ['status.line.on', 0], ['status.line.off', 0], ['set.string', 2], ['get.string', 5],
  ['word.to.string', 2], ['parse', 1], ['get.num', 2], ['prevent.input', 0],
  ['accept.input', 0], ['set.key', 3], ['add.to.pic', 7], ['add.to.pic.v', 7],
  ['status', 0], ['save.game', 0], ['restore.game', 0], ['init.disk', 0],
  ['restart.game', 0], ['show.obj', 1], ['random', 3], ['program.control', 0],
  ['player.control', 0], ['obj.status.v', 1], ['quit', 1], ['show.mem', 0],
  ['pause', 0], ['echo.line', 0], ['cancel.line', 0], ['init.joy', 0],
  ['toggle.monitor', 0], ['version', 0], ['script.size', 1], ['set.game.id', 1],
  ['log', 1], ['set.scan.start', 0], ['reset.scan.start', 0], ['reposition.to', 3],
  ['reposition.to.v', 3], ['trace.on', 0], ['trace.info', 3], ['print.at', 4],
  ['print.at.v', 4], ['discard.view.v', 1], ['clear.text.rect', 5], ['set.upper.left', 2],
  ['set.menu', 1], ['set.menu.item', 2], ['submit.menu', 0], ['enable.item', 1],
  ['disable.item', 1], ['menu.input', 0], ['show.obj.v', 1], ['open.dialogue', 0],
  ['close.dialogue', 0], ['mul.n', 2], ['mul.v', 2], ['div.n', 2],
  ['div.v', 2], ['close.window', 0], ['set.simple', 1], ['push.script', 0],
  ['pop.script', 0], ['hold.key', 0], ['set.pri.base', 1], ['discard.sound', 1],
  ['hide.mouse', 0], ['allow.menu', 1], ['show.mouse', 0], ['fence.mouse', 4],
  ['mouse.posn', 2], ['release.key', 0], ['adj.ego.move.to.xy', 0],
];

// JRQ extensions. Opcodes 200 and up; plain AGI data never contains them.
export const EXT_ACTIONS = {
  200: ['speak', 1],        // speak(n): play speech clip n, wait
  201: ['talk', 1],         // talk(n): live conversation with role prompt n
  202: ['link.status', 1],  // link.status(v): 0 none, 1 trying, 2 connected
  203: ['score.event', 1],  // score.event(n): note event n for the ending
  204: ['str.date', 2],
  205: ['demo.play', 1],    // demo.play(n): ask the host to play recorded script n from the start
      // str.date(s, v): parse string s as a DOS date, see FORMATS.md
};

export function actionInfo(op) {
  return op < ACTIONS.length ? ACTIONS[op] : EXT_ACTIONS[op] || null;
}
