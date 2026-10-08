// The browser shell, in headless Chromium driven over DevTools: no window.
// W on the title starts the recorded play, the caption shows, the keyboard
// is the script's, and Esc gives an ordinary game back. Run once from the
// one-file bundle (the demo is inlined) and once from a static server (the
// demo is fetched from build/demo/1.txt). Skipped where there is no Chromium.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, existsSync, rmSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join, extname } from 'node:path';
import { pathToFileURL } from 'node:url';
import { bundleHtml } from '../tools/bundle.js';

const CHROME = [process.env.CHROME, '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/google-chrome-stable', '/usr/bin/google-chrome']
  .find((p) => p && existsSync(p));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Headless Chromium and one page in it. Returns { send, evaluate, key, close }.
async function browser() {
  const profile = mkdtempSync(join(tmpdir(), 'jrq-chrome-'));
  const child = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--mute-audio', '--disable-extensions', 'about:blank'],
  { stdio: ['ignore', 'ignore', 'pipe'] });
  const close = () => {
    child.kill('SIGKILL');
    try { rmSync(profile, { recursive: true, force: true }); } catch (e) { /* still exiting */ }
  };
  try {
    const port = await new Promise((resolve, reject) => {
      let err = '';
      const timer = setTimeout(() => reject(new Error(`Chromium did not start: ${err.slice(0, 300)}`)), 20000);
      child.stderr.on('data', (d) => {
        err += d;
        const m = /DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)\//.exec(err);
        if (m) { clearTimeout(timer); resolve(Number(m[1])); }
      });
      child.on('exit', () => { clearTimeout(timer); reject(new Error(`Chromium exited: ${err.slice(0, 300)}`)); });
    });
    let page = null;
    for (let i = 0; i < 50 && !page; i++) {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      page = list.find((t) => t.type === 'page');
      if (!page) await sleep(100);
    }
    const ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = () => reject(new Error('no DevTools socket')); });
    let id = 0;
    const waiting = new Map();
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && waiting.has(msg.id)) {
        const { resolve, reject } = waiting.get(msg.id);
        waiting.delete(msg.id);
        if (msg.error) reject(new Error(msg.error.message)); else resolve(msg.result);
      }
    };
    const send = (method, params = {}) => new Promise((resolve, reject) => {
      waiting.set(++id, { resolve, reject });
      ws.send(JSON.stringify({ id, method, params }));
    });
    const evaluate = async (expression) => {
      const r = await send('Runtime.evaluate', { expression, returnByValue: true });
      if (r.exceptionDetails) throw new Error(`in the page: ${r.exceptionDetails.text} ${expression}`);
      return r.result.value;
    };
    const key = async (k) => {
      const named = { Enter: 13, Escape: 27 }[k];
      const ev = named ? { key: k, code: k, windowsVirtualKeyCode: named } : { key: k, text: k };
      await send('Input.dispatchKeyEvent', { type: 'keyDown', ...ev });
      await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k });
    };
    return { send, evaluate, key, close: () => { ws.close(); close(); } };
  } catch (e) {
    close();
    throw e;
  }
}

// Poll the page until the expression is true.
async function until(b, expression, what, ms = 30000) {
  const end = Date.now() + ms;
  for (;;) {
    if (await b.evaluate(`(() => { try { return !!(${expression}); } catch (e) { return false; } })()`)) return;
    if (Date.now() > end) {
      const state = await b.evaluate(`JSON.stringify({ note: document.getElementById('note').textContent, room: globalThis.jrq && jrq.agi.vars[0], demo: !!(globalThis.jrq && jrq.demo) })`);
      throw new Error(`timed out waiting for ${what}: ${state}`);
    }
    await sleep(100);
  }
}

const ROOM = 'jrq.agi.vars[0]';
const NOTE = `document.getElementById('note').textContent`;

// From a fresh page: past the date prompt by hand, to the title's menu.
async function toTitle(b, url) {
  await b.send('Page.navigate', { url });
  await until(b, `globalThis.jrq && ${ROOM} === 90`, 'the date prompt');
  for (const ch of '10-05-2026') await b.key(ch);
  await b.key('Enter');
  await until(b, `${ROOM} === 98`, 'the title screen');
}

test('W on the title plays the recorded game; Esc gives the game back (one-file bundle)', { skip: !CHROME && 'no Chromium here', timeout: 120000 }, async () => {
  const dir = mkdtempSync(join(tmpdir(), 'jrq-shell-'));
  execFileSync('node', ['tools/pack.js', 'game', join(dir, 'build')]);
  writeFileSync(join(dir, 'game.html'), bundleHtml(join(dir, 'build')));
  const b = await browser();
  try {
    await toTitle(b, pathToFileURL(join(dir, 'game.html')).href);
    assert.equal(await b.evaluate('!!jrq.demo'), false);
    const before = await b.evaluate('jrq.agi.cycles');
    await b.key('w');
    await until(b, 'jrq.demo', 'the demo to start');
    // A new interpreter, from power-on, with nothing in its host that can vary.
    assert.equal(await b.evaluate(NOTE), 'The AI is playing. Press Esc to stop.');
    assert.equal(await b.evaluate(`document.body.classList.contains('demo')`), true);
    assert.equal(await b.evaluate(`getComputedStyle(document.getElementById('pad')).display`), 'none');
    assert.equal(await b.evaluate(`getComputedStyle(document.getElementById('toggle')).display`), 'none');
    assert.deepEqual(await b.evaluate(`['random', 'time', 'resolve', 'talk', 'link', 'demo', 'save', 'load'].filter((m) => m in jrq.agi.host)`), []);
    assert.ok(await b.evaluate('jrq.agi.cycles') < before + 1000);
    // The keyboard is the script's: these go nowhere.
    for (const ch of 'xyz') await b.key(ch);
    await b.key('Enter');
    // It types the wrong date, then the right one, presses P, and wakes up in room 1.
    await until(b, `${ROOM} === 98`, 'the demo to reach the title', 40000);
    assert.equal(await b.evaluate('jrq.agi.strings[1]'), '10-05-2026');
    await until(b, `${ROOM} === 1`, 'the demo to reach room 1', 40000);
    assert.equal(await b.evaluate(NOTE), 'The AI is playing. Press Esc to stop.');
    assert.equal(await b.evaluate('jrq.agi.vars[3]'), 2);                 // the date was worth 2 points
    assert.equal(await b.evaluate('jrq.demo.done'), false);
    // The page counts the same cycles the script does.
    assert.equal(await b.evaluate('jrq.demo.cycles >= jrq.agi.cycles'), true);
    // Esc: an ordinary game again, from the start, and the keyboard works.
    await b.key('Escape');
    await until(b, `!jrq.demo && ${ROOM} === 90`, 'an ordinary game after Esc');
    assert.equal(await b.evaluate(NOTE), '');
    assert.equal(await b.evaluate(`document.body.classList.contains('demo')`), false);
    assert.equal(await b.evaluate(`typeof jrq.agi.host.random`), 'function');
    for (const ch of '10-05-2026') await b.key(ch);
    await b.key('Enter');
    await until(b, `${ROOM} === 98`, 'the title again');
    await b.key('p');
    await until(b, `${ROOM} === 1`, 'room 1, played by hand');
  } finally {
    b.close();
  }
});

test('the demo is fetched from build/demo/1.txt on a static server; a tap stops it', { skip: !CHROME && 'no Chromium here', timeout: 120000 }, async () => {
  const dir = mkdtempSync(join(tmpdir(), 'jrq-shell-'));
  execFileSync('node', ['tools/pack.js', 'game', join(dir, 'build')]);
  const types = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.txt': 'text/plain' };
  const asked = [];
  const server = createServer((req, res) => {
    const path = req.url.split('?')[0];
    asked.push(path);
    const file = path.startsWith('/build/') ? join(dir, path) : join('js', path === '/' ? 'index.html' : path.slice(1));
    if (path.includes('..') || !existsSync(file)) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'content-type': types[extname(file)] || 'application/octet-stream' });
    res.end(readFileSync(file));
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const b = await browser();
  try {
    await toTitle(b, `http://127.0.0.1:${server.address().port}/index.html?build=/build/`);
    assert.ok(!asked.includes('/build/demo/1.txt'));      // not fetched until it is asked for
    await b.key('W');
    await until(b, 'jrq.demo', 'the demo to start');
    assert.ok(asked.includes('/build/demo/1.txt'));
    assert.equal(await b.evaluate(NOTE), 'The AI is playing. Press Esc to stop.');
    await until(b, `${ROOM} === 90 && jrq.demo.cycles > 5`, 'the demo to be running');
    // A tap on the picture stops it too.
    await b.evaluate(`document.getElementById('screen').dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })), 1`);
    await until(b, '!jrq.demo', 'the tap to stop the demo');
    assert.equal(await b.evaluate(NOTE), '');
  } finally {
    b.close();
    server.close();
  }
});
