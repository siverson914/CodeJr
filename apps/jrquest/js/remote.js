// The host members that need tools/serve.js: the AI parser, the live
// talk and the link status. No DOM here, so tests can run it in Node
// with a fake fetch and a fake io.

const post = async (fetchFn, url, body) => {
  const r = await fetchFn(url, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body),
  });
  let answer = null;
  try { answer = await r.json(); } catch (e) { /* not JSON: treated as a failure */ }
  return { ok: r.ok, answer };
};

// Is the page being served by tools/serve.js? Anything else (a file, a
// plain static server, no answer in time) is "no".
export async function findServer(fetchFn, base = '', ms = 1500) {
  try {
    const opts = typeof AbortSignal !== 'undefined' && AbortSignal.timeout ? { signal: AbortSignal.timeout(ms) } : {};
    const r = await fetchFn(`${base}/api/ping`, opts);
    if (!r.ok) return false;
    const a = await r.json();
    return !!(a && a.ok && a.server === 'jrq');
  } catch (e) {
    return false;
  }
}

// Cut a reply into pieces that each fit one message window.
export function pages(text, width = 30, lines = 10) {
  const rows = [];
  let row = '';
  for (const word of String(text).split(/\s+/).filter(Boolean)) {
    if (row && row.length + 1 + word.length > width) { rows.push(row); row = ''; }
    row = row ? `${row} ${word}` : word;
    // A word longer than a row is cut by the window itself.
  }
  if (row) rows.push(row);
  const out = [];
  for (let i = 0; i < rows.length; i += lines) out.push(rows.slice(i, i + lines).join(' '));
  return out;
}

const LEAVING = /^(bye|goodbye)[.!]*$/i;

// options: base (URL prefix, '' for same origin), minutes() (time
// played, for the facts), status(text) (shown while a reply is on its
// way; '' clears it).
export function remoteHost(fetchFn, options = {}) {
  const base = options.base || '';
  const status = options.status || (() => {});
  let link = 2;

  const call = async (path, body) => {
    try {
      const r = await post(fetchFn, base + path, body);
      link = 2;
      return r;
    } catch (e) {
      // The server went away; link.status() should say so.
      link = 0;
      return { ok: false, answer: null };
    }
  };

  return {
    link: () => link,

    async resolve(text, patterns) {
      const r = await call('/api/resolve', { text, patterns });
      return r.ok && r.answer && typeof r.answer.sentence === 'string' ? r.answer.sentence : null;
    },

    // One conversation: ask, send, show, until the player leaves. False
    // means the link failed, and the game goes on without it.
    async talk(n, facts, io) {
      const history = [];
      const sent = { ...facts };
      if (options.minutes) sent.minutes = options.minutes();
      for (;;) {
        const line = await io.ask('>');
        const text = line === null || line === undefined ? '' : String(line).trim();
        if (text === '' || LEAVING.test(text)) return true;
        status('The Mirror is thinking');
        let r = await call('/api/talk', { prompt: n, facts: sent, history, text });
        // One more try: a single slow answer should not end the scene.
        // (Not after a timeout: that was already a long wait.)
        const failed = !(r.ok && r.answer && r.answer.reply);
        if (failed && !(r.answer && r.answer.error === 'timeout')) {
          r = await call('/api/talk', { prompt: n, facts: sent, history, text });
        }
        status('');
        if (!(r.ok && r.answer && typeof r.answer.reply === 'string' && r.answer.reply)) return false;
        history.push({ role: 'player', text }, { role: 'mirror', text: r.answer.reply });
        for (const page of pages(r.answer.reply)) await io.print(page);
      }
    },
  };
}
