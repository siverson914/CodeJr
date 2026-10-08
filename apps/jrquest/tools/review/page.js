// The review page's own script. No requests are made unless the page
// came from tools/serve.js (http): opened from a file it keeps comments
// in this browser.
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var all = function (sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); };
  var LOCAL_KEY = 'jrq.review.comments.v1';

  // ---- pictures: filled from one table, shown at whole-number sizes ----

  var IMAGES = JSON.parse($('images').textContent);
  all('img[data-img]').forEach(function (im) { im.src = IMAGES[im.dataset.img] || ''; });

  function fit() {
    all('img.px').forEach(function (im) {
      var w = Number(im.dataset.w);
      var spr = im.closest('.sprites');
      // Sprites sit in a padded, bordered box; a screenshot fills its figure.
      var avail = spr ? spr.clientWidth - 14 : im.closest('figure').clientWidth;
      var scale = Math.max(1, Math.min(3, Math.floor(avail / w)));
      im.style.width = w <= avail ? (w * scale) + 'px' : '100%';
    });
  }
  window.addEventListener('resize', fit);
  fit();

  // ---- comments: where they live ----

  var comments = [];
  var mode = 'local';

  function readLocal() {
    try {
      var list = JSON.parse(localStorage.getItem(LOCAL_KEY) || '[]');
      return Array.isArray(list) ? list : [];
    } catch (e) { return []; }
  }
  function writeLocal(list) {
    try { localStorage.setItem(LOCAL_KEY, JSON.stringify(list)); return true; } catch (e) { return false; }
  }
  function api(method, path, body) {
    var opts = { method: method, headers: {} };
    if (body) { opts.headers['content-type'] = 'application/json'; opts.body = JSON.stringify(body); }
    return fetch(path, opts).then(function (r) { return r.ok ? r.json() : null; }).catch(function () { return null; });
  }
  function say(text, cls) {
    var w = $('where');
    w.textContent = text;
    w.className = 'notice' + (cls ? ' ' + cls : '');
  }
  function useLocal() {
    mode = 'local';
    comments = readLocal();
    say('Comments are saved in this browser only. Use Copy all as text to send them.', 'local');
    render();
  }
  // A comment the server did not take is kept here, and offered again on the next load.
  function keepLocally(c) {
    var list = readLocal().filter(function (x) { return x.id !== c.id; });
    list.push(c);
    writeLocal(list);
    say('The server did not take that comment. It is kept in this browser; reload the page to send it again.', 'local');
  }
  function offerUpload() {
    var have = {};
    comments.forEach(function (c) { have[c.id] = true; });
    var local = readLocal();
    var missing = local.filter(function (c) { return c && c.id && !have[c.id]; });
    if (!missing.length) { if (local.length) writeLocal([]); return; }
    var box = $('offer');
    box.hidden = false;
    box.textContent = missing.length + ' comment' + (missing.length === 1 ? '' : 's') + ' saved in this browser '
      + (missing.length === 1 ? 'is' : 'are') + ' not on the server. ';
    var up = document.createElement('button');
    up.type = 'button';
    up.id = 'upload';
    up.textContent = 'Upload them';
    up.onclick = function () {
      up.disabled = true;
      var left = missing.slice(), failed = [];
      (function next() {
        var c = left.shift();
        if (!c) {
          writeLocal(failed);
          box.hidden = failed.length === 0;
          if (failed.length) box.textContent = failed.length + ' could not be uploaded and are still in this browser.';
          render();
          return;
        }
        api('POST', '/api/feedback', c).then(function (r) {
          if (r && r.comment) comments.push(r.comment); else failed.push(c);
          next();
        });
      })();
    };
    box.appendChild(up);
  }
  function load() {
    // From a file there is nobody to ask, and nothing is requested.
    if (!/^https?:$/.test(location.protocol)) { useLocal(); return; }
    api('GET', '/api/ping').then(function (p) {
      if (!(p && p.server === 'jrq' && p.feedback)) { useLocal(); return; }
      api('GET', '/api/feedback').then(function (d) {
        if (!d || !Array.isArray(d.comments)) { useLocal(); return; }
        mode = 'server';
        comments = d.comments;
        say('Comments are saved on this laptop, in review/feedback.json and review/feedback.md.');
        render();
        offerUpload();
      });
    });
  }
  function store(c) {
    var at = comments.findIndex(function (x) { return x.id === c.id; });
    if (at < 0) comments.push(c); else comments[at] = c;
    render();
    if (mode === 'server') {
      api('POST', '/api/feedback', c).then(function (r) { if (!(r && r.comment)) keepLocally(c); });
    } else if (!writeLocal(comments)) {
      say('This browser would not save the comment (private window?). Copy all as text before closing the page.', 'local');
    }
  }
  function remove(id) {
    comments = comments.filter(function (x) { return x.id !== id; });
    render();
    if (mode === 'server') {
      api('DELETE', '/api/feedback/' + encodeURIComponent(id)).then(function (r) {
        if (!r) say('The server did not delete that comment; it will be back when the page reloads.', 'local');
      });
    } else writeLocal(comments);
  }

  // ---- comments: on the page ----

  function when(iso) {
    var d = new Date(iso);
    if (isNaN(d)) return '';
    return d.toLocaleString('en-US', { month: 'short', day: 'numeric', year: 'numeric', hour: 'numeric', minute: '2-digit' })
      .replace(' AM', ' am').replace(' PM', ' pm');
  }
  function quoteOf(ln) {
    return all('.tx > .l, .tx > .gap', ln).map(function (x) { return x.className === 'gap' ? '' : x.textContent; }).join('\n');
  }
  // What a comment needs to know about the thing it is on.
  function about(host) {
    var sec = host.closest('section');
    var out = {
      target: host.dataset.t,
      group: sec ? sec.dataset.group : 'Whole game',
      order: sec ? Number(sec.dataset.order) : -1,
      quote: '', trigger: '',
    };
    if (host.classList.contains('ln')) {
      var card = host.closest('.card'), grp = host.closest('.grp');
      out.kind = 'line';
      out.label = out.group + ', a line';
      out.quote = quoteOf(host);
      out.trigger = (card ? card.querySelector('.trig').textContent : '') + (grp && grp.dataset.trig ? ' > ' + grp.dataset.trig : '');
    } else {
      out.kind = host.dataset.kind || 'note';
      out.label = host.dataset.label || out.group;
    }
    return out;
  }
  function hostsOf(target) {
    return all('[data-t]').filter(function (h) { return h.dataset.t === target; });
  }
  function slot(host) {
    var into = host.tagName === 'TR' ? host.lastElementChild : host;
    var cs = null;
    for (var i = 0; i < into.children.length; i++) if (into.children[i].className === 'cs') cs = into.children[i];
    if (!cs) {
      cs = document.createElement('div');
      cs.className = 'cs';
      into.appendChild(cs);
    }
    return cs;
  }
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function act(name, id, text) {
    var b = el('button', '', text);
    b.type = 'button';
    b.dataset.act = name;
    b.dataset.id = id;
    return b;
  }
  function commentBox(c) {
    var box = el('div', 'cm' + (c.done ? ' done' : ''));
    box.dataset.id = c.id;
    box.appendChild(el('div', 'body', c.text));
    var meta = el('div', 'meta');
    meta.appendChild(el('span', '', (c.done ? 'Done. ' : '') + when(c.time)));
    meta.appendChild(act('done', c.id, c.done ? 'Reopen' : 'Mark done'));
    meta.appendChild(act('edit', c.id, 'Edit'));
    meta.appendChild(act('delete', c.id, 'Delete'));
    box.appendChild(meta);
    return box;
  }
  function editor(host, existing) {
    var cs = slot(host);
    var old = cs.querySelector('.ed');
    if (old) { old.querySelector('textarea').focus(); return; }
    var form = el('div', 'ed');
    var ta = el('textarea');
    ta.rows = 3;
    ta.maxLength = 4000;
    ta.placeholder = 'Your comment';
    ta.value = existing ? existing.text : '';
    var row = el('div', 'row');
    var save = el('button', 'save', 'Save');
    var cancel = el('button', '', 'Cancel');
    save.type = cancel.type = 'button';
    save.onclick = function () {
      var text = ta.value.trim();
      if (!text) { ta.focus(); return; }
      if (existing) {
        store(Object.assign({}, existing, { text: text, edited: new Date().toISOString() }));
      } else {
        var c = about(host);
        c.id = 'c' + Date.now().toString(36) + Math.random().toString(36).slice(2, 7);
        c.text = text;
        c.time = new Date().toISOString();
        c.done = false;
        store(c);
      }
    };
    cancel.onclick = function () { form.remove(); };
    row.appendChild(save);
    row.appendChild(cancel);
    form.appendChild(ta);
    form.appendChild(row);
    cs.appendChild(form);
    ta.focus();
  }

  function report() {
    var open = comments.filter(function (c) { return !c.done; }).length;
    var out = ["Jr's Quest review feedback",
      comments.length + ' comment' + (comments.length === 1 ? '' : 's') + ' (' + open + ' open, ' + (comments.length - open) + ' done)', ''];
    groups().forEach(function (g) {
      out.push('== ' + g.name + ' ==');
      g.list.forEach(function (c) {
        out.push('[' + (c.done ? 'done' : 'open') + '] ' + (c.label || c.kind) + ' (' + c.target + ')');
        if (c.trigger) out.push('  Trigger: ' + c.trigger);
        if (c.quote) out.push('  Line: ' + c.quote.replace(/\n/g, '\n        '));
        out.push('  Comment: ' + String(c.text).replace(/\n/g, '\n           '));
        out.push('  Time: ' + when(c.time));
        out.push('');
      });
    });
    return out.join('\n');
  }
  function groups() {
    var by = {};
    comments.forEach(function (c) {
      var k = c.group || 'Whole game';
      if (!by[k]) by[k] = { name: k, order: typeof c.order === 'number' ? c.order : 999, list: [] };
      by[k].list.push(c);
    });
    return Object.keys(by).map(function (k) { return by[k]; }).sort(function (a, b) {
      return a.order - b.order || (a.name < b.name ? -1 : 1);
    }).map(function (g) {
      g.list.sort(function (a, b) { return String(a.time) < String(b.time) ? -1 : 1; });
      return g;
    });
  }

  function render() {
    all('.cs').forEach(function (cs) { cs.remove(); });
    all('.cb.has').forEach(function (b) {
      b.classList.remove('has');
      b.textContent = b.dataset.l;
    });
    var by = {};
    comments.forEach(function (c) { (by[c.target] = by[c.target] || []).push(c); });
    Object.keys(by).forEach(function (target) {
      hostsOf(target).forEach(function (host) {
        var cs = slot(host);
        by[target].forEach(function (c) { cs.appendChild(commentBox(c)); });
        var b = host.querySelector('.cb');
        if (b) {
          if (b.dataset.l === undefined) b.dataset.l = b.textContent;
          b.classList.add('has');
          b.textContent = b.classList.contains('wide') ? b.dataset.l + ' (' + by[target].length + ')' : String(by[target].length);
        }
      });
    });
    var open = comments.filter(function (c) { return !c.done; }).length;
    $('count').textContent = String(open);
    $('count').title = comments.length + ' in all';

    var panel = $('panel');
    panel.textContent = '';
    if (!comments.length) panel.appendChild(el('p', 'quiet', 'No comments yet. Every room, picture, sprite, puzzle and line of text has a comment button.'));
    groups().forEach(function (g) {
      panel.appendChild(el('h3', '', g.name));
      g.list.forEach(function (c) {
        var item = el('div', 'item' + (c.done ? ' done' : ''));
        var what = el('div', 'what', c.label || c.kind);
        if (hostsOf(c.target).length) {
          var go = el('a', '', 'Show');
          go.href = '#';
          go.dataset.go = c.target;
          what.appendChild(document.createTextNode(' '));
          what.appendChild(go);
        } else if (c.kind === 'line') {
          what.appendChild(el('span', 'quiet', ' (this line is no longer in the game as quoted)'));
        }
        item.appendChild(what);
        if (c.trigger) item.appendChild(el('div', 'trg', c.trigger));
        if (c.quote) item.appendChild(el('blockquote', '', c.quote));
        item.appendChild(commentBox(c));
        panel.appendChild(item);
      });
    });
    if (!$('report').hidden) $('report').value = report();
  }

  // ---- clicks ----

  document.addEventListener('click', function (e) {
    var t = e.target;
    if (!(t instanceof Element)) return;
    var b = t.closest('.cb');
    if (b) { editor(b.closest('[data-t]')); return; }
    var a = t.closest('[data-act]');
    if (a) {
      var c = comments.find(function (x) { return x.id === a.dataset.id; });
      if (!c) return;
      if (a.dataset.act === 'done') store(Object.assign({}, c, { done: !c.done }));
      else if (a.dataset.act === 'edit') {
        var host = a.closest('[data-t]') || hostsOf(c.target)[0];
        if (host) { editor(host, c); host.scrollIntoView({ block: 'nearest' }); }
      } else if (a.dataset.act === 'delete') {
        // Two taps, not a dialog: the first asks, the second deletes.
        if (a.dataset.sure) remove(c.id);
        else {
          a.dataset.sure = '1';
          a.textContent = 'Really delete?';
          setTimeout(function () { if (a.isConnected) { delete a.dataset.sure; a.textContent = 'Delete'; } }, 4000);
        }
      }
      return;
    }
    var tab = t.closest('[data-show]');
    if (tab) {
      var fig = tab.closest('figure'), im = fig.querySelector('img');
      im.dataset.img = tab.dataset.show;
      im.src = IMAGES[tab.dataset.show];
      all('[data-show]', fig).forEach(function (x) { x.classList.toggle('on', x === tab); });
      var cap = fig.querySelector('.cap');
      var kind = tab.dataset.show.charAt(0);
      cap.textContent = kind === 's' ? 'As the player sees it.'
        : kind === 'p' ? 'The picture alone, before anyone walks in.'
          : 'Priority and control: black lines stop JR, green is a trigger, cyan is water; the other colors say what he can walk behind.';
      return;
    }
    var go = t.closest('[data-go]');
    if (go) {
      e.preventDefault();
      var hosts = hostsOf(go.dataset.go);
      if (hosts[0]) hosts[0].scrollIntoView({ block: 'center' });
    }
  });

  $('jump').addEventListener('change', function () {
    if (this.value) location.hash = '#' + this.value;
    this.value = '';
  });
  $('plumb').addEventListener('change', function () { document.body.classList.toggle('plumb', this.checked); });
  $('hidedone').addEventListener('change', function () { document.body.classList.toggle('hidedone', this.checked); });

  $('copy').addEventListener('click', function () {
    var text = report(), box = $('report'), note = $('copied');
    box.hidden = false;
    box.value = text;
    var manual = function () {
      box.focus();
      box.select();
      var ok = false;
      try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
      note.textContent = ok ? 'Copied ' + comments.length + ' comments.' : 'Select the text below and copy it.';
    };
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function () { note.textContent = 'Copied ' + comments.length + ' comments.'; }, manual);
    } else manual();
  });
  $('download').addEventListener('click', function () {
    var blob = new Blob([JSON.stringify({ version: 1, comments: comments }, null, 2) + '\n'], { type: 'application/json' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'jrquest-feedback.json';
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(function () { URL.revokeObjectURL(a.href); }, 1000);
  });

  // ---- search: filter the lines as he types ----

  var lines = null, timer = 0;
  function search(q) {
    q = q.trim().toLowerCase();
    if (!lines) lines = all('.ln').map(function (ln) { return { ln: ln, text: ln.querySelector('.tx').textContent.toLowerCase() }; });
    document.body.classList.toggle('searching', q !== '');
    var hits = 0;
    lines.forEach(function (x) {
      var show = q === '' || x.text.indexOf(q) >= 0;
      if (show && q !== '') hits++;
      x.ln.classList.toggle('hid', !show);
    });
    all('.grp, .card, section.room').forEach(function (box) {
      box.classList.toggle('hid', q !== '' && !box.querySelector('.ln:not(.hid)'));
    });
    var found = $('found');
    found.hidden = q === '';
    found.textContent = hits + ' line' + (hits === 1 ? '' : 's') + ' with "' + q + '"';
  }
  $('find').addEventListener('input', function () {
    var q = this.value;
    clearTimeout(timer);
    timer = setTimeout(function () { search(q); }, 120);
  });

  // For tests and for the curious.
  window.review = { comments: function () { return comments; }, mode: function () { return mode; }, search: search };
  load();
})();
