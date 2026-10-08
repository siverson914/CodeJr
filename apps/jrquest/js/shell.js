// The browser shell: canvas, keyboard, on-screen keys for phones, sound,
// saved games. The interpreter knows nothing about any of this.

import { Interpreter, KEY, PALETTE } from './agi.js';
import { renderFrame, FRAME_W, FRAME_H } from './render.js';
import { fromBase64 } from './util.js';
import { findServer, remoteHost } from './remote.js';
import { DemoPlayer } from './demo.js';

const FILES = ['LOGDIR', 'PICDIR', 'VIEWDIR', 'SNDDIR', 'WORDS.TOK', 'OBJECT'];
const $ = (id) => document.getElementById(id);

function note(text) {
  $('note').textContent = text;
}

// A note with moving dots, for a wait the game's own screen does not
// show (the live talk's reply). '' ends it.
let busyTimer = 0;
function busyNote(text) {
  clearInterval(busyTimer);
  busyTimer = 0;
  note(text);
  if (!text) return;
  let dots = 0;
  busyTimer = setInterval(() => {
    dots = (dots + 1) % 4;
    note(text + '.'.repeat(dots));
  }, 400);
}

// ---- resources: inlined (one-page build) or fetched from build/ ----

function volsUsed(dirs) {
  let max = 0;
  for (const d of dirs) {
    for (let i = 0; d && i + 2 < d.length; i += 3) {
      if (d[i] === 0xff && d[i + 1] === 0xff && d[i + 2] === 0xff) continue;
      if ((d[i] >> 4) > max) max = d[i] >> 4;
    }
  }
  return max + 1;
}

// One built file by name ("LOGDIR", "demo/1.txt"), or null.
async function getFile(name) {
  const inline = globalThis.JRQ_RESOURCES;
  if (inline) return inline[name] ? fromBase64(inline[name]) : null;
  const base = new URLSearchParams(location.search).get('build') || '../build/';
  try {
    const r = await fetch(base.replace(/\/?$/, '/') + name);
    return r.ok ? new Uint8Array(await r.arrayBuffer()) : null;
  } catch (e) {
    return null;
  }
}

async function loadResources() {
  const get = getFile;
  const [logdir, picdir, viewdir, snddir, words, object] = await Promise.all(FILES.map(get));
  if (!logdir) throw new Error('no game files found (build/LOGDIR)');
  const vols = [];
  for (let n = 0; n < volsUsed([logdir, picdir, viewdir, snddir]); n++) vols.push(await get(`VOL.${n}`));
  let game = {};
  const json = await get('game.json');
  if (json) {
    try { game = JSON.parse(new TextDecoder().decode(json)); } catch (e) { game = {}; }
  }
  return { logdir, picdir, viewdir, snddir, vols, words, object, game };
}

// ---- sound: three square waves and noise, like the PCjr's chip ----

const audio = { ctx: null, nodes: [] };

function audioContext() {
  if (!audio.ctx) {
    const Ctx = globalThis.AudioContext || globalThis.webkitAudioContext;
    if (!Ctx) return null;
    try { audio.ctx = new Ctx(); } catch (e) { return null; }
  }
  if (audio.ctx.state === 'suspended') audio.ctx.resume().catch(() => {});
  return audio.ctx;
}

function stopSound() {
  for (const n of audio.nodes) {
    try { n.stop(); } catch (e) { /* already stopped */ }
  }
  audio.nodes = [];
}

// The chip's attenuation is 2 dB a step; 15 is silence.
function level(att) {
  return att >= 15 ? 0 : 0.07 * Math.pow(10, -att / 10);
}

function playSound(events) {
  stopSound();
  const ctx = audioContext();
  if (!ctx) return;
  const t0 = ctx.currentTime + 0.03;
  for (let v = 0; v < 4; v++) {
    const mine = events.filter((e) => e.voice === v);
    if (!mine.length) continue;
    const gain = ctx.createGain();
    gain.gain.setValueAtTime(0, t0);
    gain.connect(ctx.destination);
    let src;
    if (v < 3) {
      src = ctx.createOscillator();
      src.type = 'square';
    } else {
      const len = ctx.sampleRate;
      const buf = ctx.createBuffer(1, len, ctx.sampleRate);
      const data = buf.getChannelData(0);
      for (let i = 0; i < len; i++) data[i] = Math.random() * 2 - 1;
      src = ctx.createBufferSource();
      src.buffer = buf;
      src.loop = true;
    }
    let end = t0;
    for (const e of mine) {
      const at = t0 + e.t / 60;
      if (v < 3) {
        if (e.div > 0) src.frequency.setValueAtTime(111860 / e.div, at);
        gain.gain.setValueAtTime(e.div > 0 ? level(e.att) : 0, at);
      } else {
        // Shift rate picks the pitch of the hiss: slower is lower.
        src.playbackRate.setValueAtTime([1, 0.5, 0.25, 0.4][e.div & 3], at);
        gain.gain.setValueAtTime(level(e.att) * 0.8, at);
      }
      end = at + e.dur / 60;
    }
    gain.gain.setValueAtTime(0, end);
    src.connect(gain);
    src.start(t0);
    src.stop(end + 0.05);
    audio.nodes.push(src);
  }
}

// ---- the host the interpreter talks to ----

function makeHost(game, served) {
  const id = (game && game.id) || 'JRQ';
  const started = performance.now();
  const host = {
    time: () => Math.floor((performance.now() - started) / 1000),
    random: () => Math.floor(Math.random() * 256),
    sound: playSound,
    stopSound,
    log: (text) => console.log(`[jrq] ${text}`),
    save(slot, obj) {
      try {
        localStorage.setItem(`jrq.${id}.save.${slot}`, JSON.stringify(obj));
        return true;
      } catch (e) {
        return false;
      }
    },
    load(slot) {
      try {
        const text = localStorage.getItem(`jrq.${id}.save.${slot}`);
        return text ? JSON.parse(text) : null;
      } catch (e) {
        return null;
      }
    },
  };
  // The AI parser and the live talk need tools/serve.js behind the page.
  // Without it these members stay unset and the game is classic AGI.
  if (served) {
    Object.assign(host, remoteHost((...a) => fetch(...a), {
      minutes: () => Math.round((performance.now() - started) / 60000),
      status: busyNote,
    }));
  }
  const url = globalThis.JRQ_RESOLVE_URL;
  if (url) {
    host.resolve = async (text, patterns) => {
      const r = await fetch(url, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ text, patterns: patterns.map((p) => p.text) }),
      });
      if (!r.ok) return null;
      const answer = await r.json();
      return answer && typeof answer.sentence === 'string' ? answer.sentence : null;
    };
  }
  // "Let the AI play": the game asks, and the loop starts it between cycles.
  host.demo = (n) => { demoAsked = n; };
  // A page can add or replace members (speak, talk, link, resolve).
  return Object.assign(host, globalThis.JRQ_HOST || {});
}

// The host for a recorded play. Nothing in it can differ between two
// runs: no random (the interpreter's own seeded generator is used), no
// clock (the interpreter counts cycles), no AI parser, no live talk, no
// link, no saved games. Sound is the only thing left.
function makeDemoHost() {
  return { sound: playSound, stopSound, log: (text) => console.log(`[jrq demo] ${text}`) };
}

// ---- screen ----

const canvas = $('screen');
const ctx2d = canvas.getContext('2d');
const image = ctx2d.createImageData(FRAME_W, FRAME_H);
const rgba = new Uint32Array(image.data.buffer);
const frame = new Uint8Array(FRAME_W * FRAME_H);
const lut = new Uint32Array(16);
{
  // Byte order differs between machines; ask a typed array which it is.
  const little = new Uint8Array(new Uint32Array([1]).buffer)[0] === 1;
  PALETTE.forEach(([r, g, b], i) => {
    lut[i] = little ? (0xff000000 | (b << 16) | (g << 8) | r) >>> 0 : ((r << 24) | (g << 16) | (b << 8) | 0xff) >>> 0;
  });
}

let agi = null;

// ---- recorded play ("Let the AI play" on the title screen) ----

let resources = null;      // the built game, kept so the interpreter can be started again
let normalHost = null;     // the host of an ordinary game
let demo = null;           // the DemoPlayer while the game plays itself
let demoAsked = 0;         // the script number the game just asked for
const DEMO_PLAYING = 'The AI is playing. Press Esc to stop.';
const DEMO_DONE = 'The AI has finished. Press Esc to play it yourself.';

function setAgi(next) {
  agi = next;
  globalThis.jrq = { agi, get demo() { return demo; } };
}

// Start script n from power-on. If this build has no such script, say so
// and leave the game where it is.
async function startDemo(n) {
  const bytes = await getFile(`demo/${n}.txt`);
  let player = null;
  if (bytes) {
    try { player = new DemoPlayer(new TextDecoder().decode(bytes)); } catch (e) { console.error(e); }
  }
  if (!player) {
    note('This copy has no recorded play.');
    return;
  }
  stopSound();
  busyNote('');
  demo = player;
  setAgi(new Interpreter(resources, makeDemoHost()));
  document.body.classList.add('demo');
  note(DEMO_PLAYING);
  fit();
}

// Esc or a tap on the picture: back to an ordinary game, from the start.
function stopDemo() {
  if (!demo) return;
  demo = null;
  stopSound();
  setAgi(new Interpreter(resources, normalHost));
  document.body.classList.remove('demo');
  note('');
  fit();
  draw();
}

function draw() {
  if (!agi) return;
  renderFrame(agi, frame);
  for (let i = 0; i < frame.length; i++) rgba[i] = lut[frame[i] & 15];
  ctx2d.putImageData(image, 0, 0);
}

// Square pixels: 320x200 times one factor. Whole numbers when there is
// room for 2x or more, so every game pixel is the same size.
function fit() {
  const pad = $('pad');
  const below = (pad.offsetParent ? pad.offsetHeight + 10 : 0) + $('note').offsetHeight + 36;
  const w = window.innerWidth - 16, h = window.innerHeight - below;
  let scale = Math.min(w / FRAME_W, h / FRAME_H);
  if (scale >= 2) scale = Math.floor(scale);
  if (scale < 0.5) scale = 0.5;
  canvas.style.width = `${Math.round(FRAME_W * scale)}px`;
  canvas.style.height = `${Math.round(FRAME_H * scale)}px`;
}

// ---- input ----

const DOM_KEYS = {
  ArrowUp: KEY.UP, ArrowDown: KEY.DOWN, ArrowLeft: KEY.LEFT, ArrowRight: KEY.RIGHT,
  Home: KEY.HOME, End: KEY.END, PageUp: KEY.PGUP, PageDown: KEY.PGDN, Clear: KEY.CENTER,
  Enter: KEY.ENTER, Escape: KEY.ESC, Backspace: KEY.BACKSPACE, Tab: KEY.TAB,
};
for (let i = 1; i <= 10; i++) DOM_KEYS[`F${i}`] = KEY[`F${i}`];

function press(code) {
  if (!agi || demo) return;
  audioContext();
  agi.keyDown(code);
  draw();
}

function onKeyDown(e) {
  if (!agi || e.metaKey || e.altKey) return;
  if (demo) {
    // The script has the keyboard. Esc takes it back.
    if (e.ctrlKey) return;
    e.preventDefault();
    if (e.key === 'Escape') stopDemo();
    return;
  }
  if (e.target === $('line')) {
    // The phone's text field edits its own line; arrows still walk.
    if (!(e.key in DOM_KEYS) || e.key === 'Backspace' || e.key === 'Enter'
      || e.key === 'Home' || e.key === 'End') return;
  }
  if (e.key in DOM_KEYS) {
    e.preventDefault();
    press(DOM_KEYS[e.key]);
  } else if (e.ctrlKey && e.key.length === 1) {
    const c = e.key.toLowerCase().charCodeAt(0) - 96;
    if (c >= 1 && c <= 26 && 'sryqijp'.includes(e.key.toLowerCase())) { e.preventDefault(); press(c); }
  } else if (e.key.length === 1 && e.key >= ' ' && e.key <= '~') {
    e.preventDefault();
    audioContext();
    agi.typeChar(e.key);
    draw();
  }
}

function onKeyUp(e) {
  if (agi && !demo && e.key in DOM_KEYS) agi.keyUp(DOM_KEYS[e.key]);
}

// A whole line from the phone's text field or the microphone: the same
// path as typing it and pressing Enter.
function sendLine(text) {
  if (!agi || demo) return;
  audioContext();
  for (const ch of text) agi.typeChar(ch);
  agi.enter();
  draw();
}

// Voice: the browser turns speech into text, and the text is a typed line.
function wireVoice() {
  const Rec = globalThis.SpeechRecognition || globalThis.webkitSpeechRecognition;
  const mic = $('mic');
  if (!Rec || !mic) return;
  mic.hidden = false;
  let rec = null;
  mic.addEventListener('click', () => {
    if (rec) { rec.stop(); return; }
    const field = $('line');
    let heard = '';
    rec = new Rec();
    rec.lang = 'en-US';
    rec.interimResults = true;
    rec.onresult = (e) => {
      heard = '';
      for (const r of e.results) heard += r[0].transcript;
      // Show the words as they come, so the player sees it is listening.
      field.value = heard.trim().slice(0, field.maxLength);
    };
    rec.onerror = (e) => {
      note(e.error === 'not-allowed' ? 'The microphone is blocked for this page.' : `Could not hear that (${e.error}).`);
    };
    rec.onend = () => {
      rec = null;
      mic.classList.remove('on');
      if ($('note').textContent === 'Listening...') note('');
      const text = field.value.trim();
      field.value = '';
      if (heard && text) sendLine(text);
    };
    try {
      rec.start();
      mic.classList.add('on');
      note('Listening...');
    } catch (e) {
      rec = null;
    }
  });
}

function wireControls() {
  window.addEventListener('keydown', onKeyDown);
  window.addEventListener('keyup', onKeyUp);
  window.addEventListener('resize', fit);
  for (const b of document.querySelectorAll('button[data-key]')) {
    b.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      press(KEY[b.dataset.key]);
    });
  }
  $('say').addEventListener('submit', (e) => {
    e.preventDefault();
    const field = $('line');
    sendLine(field.value);
    field.value = '';
  });
  wireVoice();
  $('toggle').addEventListener('click', () => {
    document.body.classList.toggle('touch');
    fit();
  });
  canvas.addEventListener('pointerdown', () => {
    audioContext();
    if (demo) stopDemo();
  });
  if (window.matchMedia && window.matchMedia('(pointer: coarse)').matches) document.body.classList.add('touch');
}

// ---- the loop: 20 cycles a second divided by the game's delay ----

function run() {
  let last = performance.now(), owed = 0;
  const tick = (now) => {
    owed += now - last;
    last = now;
    // A hidden tab comes back with minutes owed; don't replay them.
    if (owed > 500) owed = 500;
    // agi.delay is read each time: a recorded play swaps the interpreter.
    while (owed >= 50 * agi.delay) {
      owed -= 50 * agi.delay;
      try {
        if (demo) {
          // The script's keys, then one cycle. When it runs out the last
          // screen stays up and keeps moving; nothing more is typed.
          if (!demo.step(agi)) {
            if ($('note').textContent === DEMO_PLAYING) note(DEMO_DONE);
            agi.cycle();
          }
        } else agi.cycle();
      } catch (e) { note(`Stopped: ${e.message}`); console.error(e); return; }
      if (demoAsked) {
        // Asked for from inside a cycle: start it now that the cycle is over.
        const n = demoAsked;
        demoAsked = 0;
        owed = 0;
        startDemo(n);
        break;
      }
    }
    draw();
    if (agi.ended) { note('The game has ended. Reload the page to play again.'); return; }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

async function start() {
  wireControls();
  fit();
  note('Loading...');
  try {
    resources = await loadResources();
    if (resources.game && resources.game.name) document.title = resources.game.name;
    // A one-file build or a page opened from disk asks nobody: it has no
    // server, and the one-file build makes no network requests at all.
    const served = !globalThis.JRQ_RESOURCES && /^https?:$/.test(location.protocol)
      && await findServer((...a) => fetch(...a));
    normalHost = makeHost(resources.game, served);
    setAgi(new Interpreter(resources, normalHost));
    note('');
    fit();
    run();
  } catch (e) {
    note(`Could not start: ${e.message}`);
    console.error(e);
  }
}

start();
