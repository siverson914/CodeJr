// The review page: the generator on the real game, the trigger reader on
// a small logic, the feedback endpoints, and the page itself in headless
// Chromium (skipped where there is none).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawn } from 'node:child_process';
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, existsSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { buildReview, extractScript, readDefines, parsePuzzles, parseGameplan, textHtml } from '../tools/review.js';
import { startServer, feedbackReport } from '../tools/serve.js';

const QUEST = join(dirname(fileURLToPath(import.meta.url)), '..');
const CHROME = [process.env.CHROME, '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/google-chrome-stable', '/usr/bin/google-chrome']
  .find((p) => p && existsSync(p));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// The real game, packed and turned into a page once for all the tests.
let built = null;
function review() {
  if (!built) {
    built = (async () => {
      const dir = mkdtempSync(join(tmpdir(), 'jrq-review-'));
      const build = join(dir, 'build');
      execFileSync('node', ['tools/pack.js', 'game', build], { cwd: QUEST });
      const r = await buildReview(build, { game: join(QUEST, 'game'), docs: QUEST });
      writeFileSync(join(build, 'review.html'), r.html);
      return { ...r, dir, build, file: join(build, 'review.html') };
    })();
  }
  return built;
}
const texts = (script) => script.cards.flatMap((c) => c.groups.flatMap((g) => g.items.filter((i) => i.type === 'text')
  .map((i) => ({ ...i, card: c.trigger, group: g.trigger }))));

test('the page has every room, the script with its triggers, the hints and the puzzle chain', { timeout: 120000 }, async () => {
  const { html, data } = await review();
  for (const n of [98, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]) {
    assert.ok(html.includes(`id="room-${n}"`), `room ${n}`);
    assert.ok(html.includes(`data-t="shot:${n}"`), `screenshot ${n}`);
    const room = data.rooms.find((r) => r.n === n);
    assert.equal(room.arrived, true, `the screenshot run reached room ${n}`);
    assert.ok(data.images[`s${n}`].startsWith('data:image/png;base64,'));
  }
  assert.deepEqual(data.rooms.map((r) => r.n), [98, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
  assert.equal(data.totals.rooms, 16);

  // Room 2: the line, under READ CODE, with its points beside it.
  const two = data.rooms.find((r) => r.n === 2);
  assert.equal(two.name, 'Castle Steve');
  const line = texts(two.script).find((t) => t.text.startsWith('You read the code'));
  assert.match(line.card, /READ CODE/);
  assert.match(line.group, /otherwise \(not codeRead\)/);
  assert.match(line.id, /^L2:[0-9a-f]{8}$/);
  const card = two.script.cards.find((c) => /READ CODE/.test(c.trigger));
  assert.ok(card.groups.some((g) => g.items.some((i) => i.type === 'points' && i.n === 4)));
  assert.ok(html.includes('You read the code. All of it.'));
  assert.ok(html.includes('<b class="sp">PRIMEAGEN:</b>'));
  assert.deepEqual(two.exits, { north: 'closed (the house)', east: '3', south: '11', west: '5' });
  assert.ok(two.views.includes(2) && two.views.includes(3) && two.views.includes(4));
  // A room with a brief and its own sprites.
  const three = data.rooms.find((r) => r.n === 3);
  assert.match(three.brief.text, /Tokens grow like flowers/);
  assert.ok(three.views.includes(50));

  // The hint ladder, with its numbered steps said in words.
  const hints = data.shared.find((s) => s.n === 104);
  assert.equal(hints.name, 'The hint ladder');
  const hint = texts(hints.script).find((t) => t.text.includes('READ CODE, then FIX CODE'));
  assert.match(hint.card, /hint step 1 \(still not gateOpen\)/);
  assert.match(hint.group, /the third time/);
  assert.ok(html.includes('READ CODE, then FIX CODE'));
  assert.deepEqual(data.shared.map((s) => s.n), [0, 80, 83, 84, 90, 99, 104]);
  for (const s of data.shared) assert.equal(s.script.error, null, `logic ${s.n} parsed`);
  for (const r of data.rooms) assert.equal(r.script.error, null, `room ${r.n} parsed`);

  // The puzzle chain and the map came out of the two documents.
  assert.equal(data.puzzles.rows.length, 16);
  assert.equal(data.puzzles.total, 158);
  assert.ok(html.includes('<table class="puz">'));
  assert.ok(html.includes('when pigs fly'));
  assert.deepEqual(data.plan.grid.map((row) => row.rooms.map((r) => r.n)), [[11, 12, 13, 14], [6, 7, 8, 9], [2, 3, 4, 5]]);
  assert.deepEqual(data.plan.interiors.map((r) => r.n), [1, 10, 15, 16]);

  // Totals, the gallery, the vocabulary.
  assert.ok(data.totals.lines > 1000 && data.totals.sprites > 60 && data.totals.words > 1000 && data.totals.bytes > 100000);
  assert.ok(html.includes('data-t="view:0"'));
  assert.ok(data.vocabulary.some((g) => g.words.includes('look') && g.words.includes('examine')));

  // One file: nothing to fetch, and small enough for a phone.
  assert.ok(html.length < 4 * 1024 * 1024, `${html.length} bytes`);
  assert.doesNotMatch(html, /(?:src|href)="(?:https?:)?\/\//);
  assert.doesNotMatch(html, /<link\b|<script[^>]*\bsrc=|@import|url\(\s*["']?https?:/);
});

test('triggers: nested if and else, the init block, ||, score, goto, strings put together', () => {
  const dir = mkdtempSync(join(tmpdir(), 'jrq-lgc-'));
  writeFileSync(join(dir, 'defines.h'), '#define newRoom f5\n#define score v3\n#define tokens v40   // loose tokens\n#define gateOpen f35 // the gate is open\n#define dead f44\n');
  const source = `// a small room
#include "defines.h"
if (isset(newRoom)) {
  print("You arrive.");
  if (arriveFrom == 1) { print("From the south."); }
}
// ---- looking ----
if (said("look", "bed") || said("examine", "bed")) {
  if (isset(gateOpen)) { print("Open."); }
  else {
    if (!has("Seed") && tokens > 2) { score += 4; print("JR: Mine.\\nSTEVE: Sure."); get("Seed"); }
    else { print("Closed."); print("Still closed."); }
  }
}
if (said("die") && isset(gateOpen)) { set.string(s6, "Part one"); set.string(s5, "%s6, part two"); set(dead); }
if (said("pay")) {
  if (tokens > 2) { tokens -= 3; goto(paid); }
  print("Too poor.");
}
if (egoEdge == 2) { new.room(3); }
goto(end);
paid:
print("Paid. You have %v40 left.");
end:
if (said("take", "anyword") || said("say", "rol")) { print("No."); }
return();
`;
  writeFileSync(join(dir, '007_test.lgc'), source);
  const s = extractScript(source, { logic: 7, defs: readDefines(join(dir, '007_test.lgc')) });
  assert.equal(s.error, null);
  const shape = s.cards.map((c) => [c.trigger, c.groups.map((g) => [g.trigger, g.items.map((i) => (i.type === 'text' ? i.text : i.type === 'points' ? `+${i.n}` : `(${i.text})`))])]);
  assert.deepEqual(shape, [
    ['on entering', [
      ['', ['You arrive.']],
      ['arriving from the south (he walked north)', ['From the south.']],
    ]],
    ['LOOK BED or EXAMINE BED', [
      ['when gateOpen', ['Open.']],
      ['otherwise (not gateOpen) › when not holding Seed and tokens is over 2', ['+4', 'JR: Mine.\nSTEVE: Sure.', '(JR gets: Seed)']],
      ['otherwise (not gateOpen) › otherwise', ['Closed.', 'Still closed.']],
    ]],
    ['DIE, when gateOpen', [
      ['', ['Part one, part two', '(JR dies here)']],
    ]],
    ['PAY', [
      ['when tokens is over 2', ['(costs 3 tokens)', '(continues at "paid", further down)']],
      ['otherwise (tokens is at most 2)', ['Too poor.']],
    ]],
    ['at "paid" (jumped to from: PAY)', [
      ['', ['Paid. You have [tokens] left.']],
    ]],
    ['TAKE ANYTHING or SAY ...', [
      ['', ['No.']],
    ]],
  ]);
  assert.equal(s.lines, 10);
  // Headings come from the banner comments; kinds and ids from the text.
  assert.equal(s.cards[1].heading, 'looking');
  assert.equal(s.cards[0].heading, null);
  const death = s.cards[2].groups[0].items[0];
  assert.equal(death.kind, 'death');
  assert.equal(death.id, `L7:${createHash('sha1').update('Part one, part two').digest('hex').slice(0, 8)}`);
  // The same text gives the same id wherever it moves to.
  const moved = extractScript(`if (said("x")) { print("Too poor."); }`, { logic: 7 });
  assert.equal(moved.cards[0].groups[0].items[0].id, s.cards[3].groups[1].items[0].id);

  // A file that will not parse still shows its lines, and says so.
  const broken = extractScript('if (said("look") { print("Still here."); }', { logic: 9 });
  assert.match(broken.error, /line 1/);
  assert.equal(broken.cards[0].groups[0].items[0].text, 'Still here.');

  assert.equal(textHtml('JR: Hi <b>.\n\nA plain line [score].'),
    '<span class="l"><b class="sp">JR:</b> Hi &lt;b&gt;.</span><span class="gap"></span><span class="l">A plain line <span class="code">[score]</span>.</span>');
});

test('the two documents: a table that cannot be read is an error, not a crash', () => {
  const ok = parsePuzzles('## The puzzle chain\n\ntext\n\n| # | Where | The problem | The answer | Points |\n|---|---|---|---|---|\n| 0 | Here | `x` | y | 2 |\n\nThat is 2.\n');
  assert.deepEqual(ok, { rows: [{ n: '0', where: 'Here', problem: '`x`', answer: 'y', points: '2' }], total: 2 });
  assert.match(parsePuzzles('# nothing here').error, /no "## The puzzle chain"/);
  assert.match(parsePuzzles('## The puzzle chain\n\nno table\n\n## Next\n\n| a |\n').error, /no table/);
  assert.match(parsePuzzles('## The puzzle chain\n\n| a | b |\n|---|---|\n| 1 | 2 |\n').error, /other columns/);
  const plan = parseGameplan('nothing useful');
  assert.equal(plan.grid.length, 0);
  assert.equal(plan.problems.length, 2);
});

// ---- the feedback endpoints ----

function makeRoot() {
  const root = mkdtempSync(join(tmpdir(), 'jrq-fb-'));
  mkdirSync(join(root, 'js'));
  mkdirSync(join(root, 'build'));
  writeFileSync(join(root, 'js', 'index.html'), '<!doctype html><title>game</title>');
  return root;
}

test('feedback: add, change, mark done, delete; bad input changes nothing; the report is written', async () => {
  const root = makeRoot();
  const s = await startServer({ root, port: 0, model: async () => 'NONE' });
  const json = join(root, 'review', 'feedback.json'), md = join(root, 'review', 'feedback.md');
  const call = (method, path, body, headers = {}) => fetch(s.url + path, {
    method,
    headers: body === undefined ? headers : { 'content-type': 'application/json', ...headers },
    body: body === undefined ? undefined : (typeof body === 'string' ? body : JSON.stringify(body)),
  });
  const first = {
    id: 'c1abc', target: 'L2:0123abcd', kind: 'line', group: 'Room 2: Castle Steve', order: 2, label: 'Room 2: Castle Steve, a line',
    quote: 'PRIMEAGEN: All of it?\nJR: It\'s how I was raised.', trigger: 'READ CODE or READ GATE > otherwise (not codeRead)',
    text: 'Funnier if he says it twice.', time: '2026-10-06T16:40:00.000Z',
  };
  try {
    // Before anything is written there is nothing, and no folder.
    assert.deepEqual(await (await call('GET', 'api/feedback')).json(), { comments: [] });
    assert.equal(existsSync(join(root, 'review')), false);
    assert.equal((await (await fetch(`${s.url}api/ping`)).json()).feedback, true);

    // Add.
    let r = await call('POST', 'api/feedback', first);
    assert.equal(r.status, 200);
    let { comment } = await r.json();
    assert.equal(comment.text, first.text);
    assert.equal(comment.done, false);
    assert.equal(comment.time, first.time);
    assert.ok(comment.updated);
    await call('POST', 'api/feedback', { id: 'c2', target: 'game', kind: 'game', group: 'Whole game', order: -1, label: 'The whole game', text: 'More goats.' });
    let doc = JSON.parse(readFileSync(json, 'utf8'));
    assert.equal(doc.version, 1);
    assert.deepEqual(doc.comments.map((c) => c.id), ['c1abc', 'c2']);
    assert.match(readFileSync(json, 'utf8'), /\n  "comments": \[\n    \{\n/);   // pretty-printed
    assert.equal(existsSync(`${json}.tmp`), false);

    // Change the text, then mark done with nothing else sent.
    r = await call('POST', 'api/feedback', { id: 'c1abc', text: 'Funnier if he says it three times.' });
    assert.equal((await r.json()).comment.quote, first.quote);
    r = await call('POST', 'api/feedback', { id: 'c1abc', done: true });
    comment = (await r.json()).comment;
    assert.equal(comment.done, true);
    assert.equal(comment.text, 'Funnier if he says it three times.');
    assert.equal(comment.time, first.time);
    const list = (await (await call('GET', 'api/feedback')).json()).comments;
    assert.equal(list.length, 2);
    assert.equal(list[0].done, true);

    // The report: grouped by room, whole game first, each with its line.
    const report = readFileSync(md, 'utf8');
    assert.match(report, /^# Jr's Quest review feedback\n\n2 comments: 1 open, 1 done\./);
    assert.ok(report.indexOf('## Whole game') < report.indexOf('## Room 2: Castle Steve'));
    assert.match(report, /### \[done\] Room 2: Castle Steve, a line\n\n- Target: `L2:0123abcd` \(comment id `c1abc`\)\n- Trigger: READ CODE or READ GATE > otherwise \(not codeRead\)\n- Time: Oct \d+, 2026, \d+:40 [ap]m\n/);
    assert.match(report, /> PRIMEAGEN: All of it\?\n> JR: It's how I was raised\.\n\nThe comment:\n\nFunnier if he says it three times\.\n/);
    assert.match(report, /### \[open\] The whole game\n/);
    assert.equal(report, feedbackReport(list));

    // Bad input: refused, and both files are exactly as they were.
    const before = readFileSync(json, 'utf8') + readFileSync(md, 'utf8');
    const bad = [
      [{ target: 'game', text: 'no id' }, 400],
      [{ id: '../x', target: 'game', text: 'bad id' }, 400],
      [{ id: 'c3', target: 'game' }, 400],
      [{ id: 'c3', text: 'no target' }, 400],
      [{ id: 'c3', target: 'game', text: '   ' }, 400],
      [{ id: 'c3', target: 'game', text: 'x'.repeat(4001) }, 400],
      [{ id: 'c1abc', done: 'yes' }, 400],
      [{ id: 'c1abc', text: 7 }, 400],
      [{ id: 'c1abc', quote: ['a'] }, 400],
      [[first], 400],
      ['{"id": "c3", "target": "game", "text": "cut off', 400],
    ];
    for (const [body, status] of bad) {
      const res = await call('POST', 'api/feedback', body);
      assert.equal(res.status, status, JSON.stringify(body).slice(0, 60));
      assert.ok((await res.json()).error);
    }
    assert.equal((await call('POST', 'api/feedback', first, { origin: 'http://evil.example' })).status, 403);
    assert.equal((await call('POST', 'api/feedback', first, { 'content-type': 'text/plain' })).status, 415);
    assert.equal((await call('DELETE', 'api/feedback/c2', undefined, { origin: 'http://evil.example' })).status, 403);
    assert.equal((await call('DELETE', 'api/feedback/nope')).status, 404);
    assert.equal((await call('DELETE', 'api/feedback/..%2Fx')).status, 404);
    assert.equal((await call('PUT', 'api/feedback', first)).status, 404);
    assert.equal(readFileSync(json, 'utf8') + readFileSync(md, 'utf8'), before);
    // Long quotes are cut, not refused: the comment is what matters.
    r = await call('POST', 'api/feedback', { id: 'c4', target: 'game', text: 'ok', quote: 'q'.repeat(5000) });
    assert.equal((await r.json()).comment.quote.length, 2000);

    // Delete.
    assert.deepEqual(await (await call('DELETE', 'api/feedback/c2')).json(), { ok: true });
    assert.equal((await call('DELETE', 'api/feedback/c2')).status, 404);
    doc = JSON.parse(readFileSync(json, 'utf8'));
    assert.deepEqual(doc.comments.map((c) => c.id), ['c1abc', 'c4']);
    assert.doesNotMatch(readFileSync(md, 'utf8'), /More goats/);

    // A damaged file is left alone, and the page is told.
    writeFileSync(json, '{ "comments": [ oops');
    r = await call('POST', 'api/feedback', { id: 'c9', target: 'game', text: 'lost?' });
    assert.equal(r.status, 500);
    assert.match((await r.json()).error, /feedback\.json is damaged/);
    assert.equal((await call('GET', 'api/feedback')).status, 500);
    assert.equal((await call('DELETE', 'api/feedback/c1abc')).status, 500);
    assert.equal(readFileSync(json, 'utf8'), '{ "comments": [ oops');

    // /admin is the built page, or a plain word about how to build it.
    r = await fetch(`${s.url}admin`);
    assert.equal(r.status, 404);
    assert.match(await r.text(), /just quest-review/);
    writeFileSync(join(root, 'build', 'review.html'), '<!doctype html><title>review</title>');
    r = await fetch(`${s.url}admin`);
    assert.equal(r.status, 200);
    assert.match(r.headers.get('content-type'), /text\/html/);
    assert.match(await r.text(), /<title>review/);
  } finally { await s.close(); }
});

// ---- the page in a browser ----

// Headless Chromium and one page in it, over DevTools. `requests` lists
// every URL the page asked for.
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
    const waiting = new Map(), requests = [], errors = [];
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.method === 'Network.requestWillBeSent') requests.push(msg.params.request.url);
      if (msg.method === 'Runtime.exceptionThrown') errors.push(msg.params.exceptionDetails.text);
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
    const until = async (expression, what, ms = 20000) => {
      const end = Date.now() + ms;
      for (;;) {
        if (await evaluate(`(() => { try { return !!(${expression}); } catch (e) { return false; } })()`)) return;
        if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
        await sleep(100);
      }
    };
    await send('Runtime.enable');
    await send('Network.enable');
    await send('Page.enable');
    return { send, evaluate, until, requests, errors, close: () => { ws.close(); close(); } };
  } catch (e) {
    close();
    throw e;
  }
}

test('in a browser: a comment on a line shows in Feedback and is still there after a reload', { skip: !CHROME && 'no Chromium here', timeout: 180000 }, async () => {
  const { build, file, dir } = await review();
  const feedbackDir = join(dir, 'review');
  const s = await startServer({ root: QUEST, build, port: 0, feedbackDir, cacheFile: join(dir, 'cache.json'), model: async () => 'NONE' });
  const b = await browser();
  const LINE = `[...document.querySelectorAll('#room-2 .ln')].find((l) => l.textContent.includes('You read the code'))`;
  try {
    // A phone's width: nothing sticks out sideways, and pixels stay whole.
    await b.send('Emulation.setDeviceMetricsOverride', { width: 360, height: 800, deviceScaleFactor: 1, mobile: true });
    await b.send('Page.navigate', { url: `${s.url}admin` });
    await b.until(`window.review && review.mode() === 'server'`, 'the page to find the server');
    assert.match(await b.evaluate(`document.getElementById('where').textContent`), /saved on this laptop/);
    assert.equal(await b.evaluate(`document.documentElement.scrollWidth <= window.innerWidth`), true);
    assert.equal(await b.evaluate(`document.querySelector('#room-2 .shot img').getBoundingClientRect().width`), 320);
    assert.equal(await b.evaluate(`document.querySelector('#room-2 .shot img').naturalWidth`), 320);
    assert.equal(await b.evaluate(`getComputedStyle(document.querySelector('#room-2 .shot img')).imageRendering`), 'pixelated');

    // Comment on the line.
    await b.evaluate(`${LINE}.querySelector('.cb').click()`);
    await b.evaluate(`(() => { const ta = ${LINE}.querySelector('textarea'); ta.value = 'Make this one longer.'; ${LINE}.querySelector('.save').click(); })()`);
    await b.until(`document.querySelector('#panel .cm')`, 'the comment in the Feedback panel');
    const panel = await b.evaluate(`document.getElementById('panel').innerText`);
    assert.match(panel, /Room 2: Castle Steve/);
    assert.match(panel, /READ CODE or READ GATE > .*otherwise \(not codeRead\)/);
    assert.match(panel, /You read the code\. All of it\./);
    assert.match(panel, /Make this one longer\./);
    assert.equal(await b.evaluate(`document.getElementById('count').textContent`), '1');
    assert.equal(await b.evaluate(`${LINE}.querySelector('.cb').classList.contains('has')`), true);

    // It reached the server's files.
    const json = join(feedbackDir, 'feedback.json');
    for (let i = 0; i < 50 && !existsSync(json); i++) await sleep(100);
    const saved = JSON.parse(readFileSync(json, 'utf8')).comments;
    assert.equal(saved.length, 1);
    assert.equal(saved[0].text, 'Make this one longer.');
    assert.equal(saved[0].kind, 'line');
    assert.match(saved[0].target, /^L2:[0-9a-f]{8}$/);
    assert.match(saved[0].quote, /^You read the code\. All of it\./);
    assert.match(readFileSync(join(feedbackDir, 'feedback.md'), 'utf8'), /Make this one longer\./);

    // Reload: still there, on the line and in the panel. Then mark it done.
    await b.send('Page.reload');
    await b.until(`window.review && review.mode() === 'server' && review.comments().length === 1`, 'the comment after a reload');
    assert.equal(await b.evaluate(`${LINE}.querySelector('.cm .body').textContent`), 'Make this one longer.');
    assert.match(await b.evaluate(`document.getElementById('panel').innerText`), /Make this one longer\./);
    await b.evaluate(`document.querySelector('#panel [data-act="done"]').click()`);
    for (let i = 0; i < 50 && !JSON.parse(readFileSync(json, 'utf8')).comments[0].done; i++) await sleep(100);
    assert.equal(JSON.parse(readFileSync(json, 'utf8')).comments[0].done, true);
    assert.equal(await b.evaluate(`document.getElementById('count').textContent`), '0');

    // Search narrows the page to the lines that match.
    await b.evaluate(`review.search('megahertz')`);
    assert.equal(await b.evaluate(`[...document.querySelectorAll('.ln:not(.hid)')].every((l) => /megahertz/i.test(l.textContent))`), true);
    assert.match(await b.evaluate(`document.getElementById('found').textContent`), /^\d+ lines? with "megahertz"$/);
    await b.evaluate(`review.search('')`);

    // From a file: no requests at all, comments in this browser, and it says so.
    b.requests.length = 0;
    await b.send('Page.navigate', { url: pathToFileURL(file).href });
    await b.until(`window.review && document.getElementById('where').textContent.includes('this browser only')`, 'the page opened from a file');
    assert.equal(await b.evaluate(`review.mode()`), 'local');
    await b.evaluate(`document.querySelector('#room-3 .ln .cb').click()`);
    await b.evaluate(`(() => { const ed = document.querySelector('#room-3 .ed'); ed.querySelector('textarea').value = 'Kept in the browser.'; ed.querySelector('.save').click(); })()`);
    await b.send('Page.reload');
    await b.until(`window.review && review.comments().length === 1`, 'the local comment after a reload');
    assert.match(await b.evaluate(`document.getElementById('panel').innerText`), /Kept in the browser\./);
    await sleep(300);
    assert.deepEqual(b.requests.filter((u) => !u.startsWith('file:') && !u.startsWith('data:')), []);
    assert.deepEqual(b.errors, []);
  } finally {
    b.close();
    await s.close();
  }
});
