// Runs in the page. Turns what a person would see into lines of text, and
// numbers everything that can be followed, pressed or filled in. Every
// numbered element gets data-cj="n" so the server can find it again.
((WHOLE) => {
  const out = [];          // lines: arrays of [style, text]
  let line = [];
  let n = 0;
  const targets = [];      // {n, kind, label}
  const BLOCK = new Set(['block', 'flex', 'grid', 'list-item', 'table', 'table-row', 'table-caption',
    'flow-root', 'table-row-group', 'table-header-group', 'table-footer-group']);
  const SKIP = new Set(['SCRIPT', 'STYLE', 'NOSCRIPT', 'SVG', 'TEMPLATE', 'IFRAME', 'CANVAS', 'VIDEO', 'AUDIO',
    'OBJECT', 'HEAD', 'META', 'LINK', 'svg', 'PATH']);
  document.querySelectorAll('[data-cj]').forEach(e => e.removeAttribute('data-cj'));

  const push = (style, text) => {
    if (!text) return;
    const last = line[line.length - 1];
    if (last && last[0] === style) last[1] += text; else line.push([style, text]);
  };
  const lineText = () => line.map(s => s[1]).join('');
  // brk ends the line; para also leaves one blank line (between
  // paragraphs and around headings, not between every box on the page).
  let trimmed = false;  // something was left out that "w" would show
  let inside = 0;   // inside a link or button: its boxes are not lines of their own
  const brk = () => {
    if (inside) { space(); return; }
    const t = lineText().trim();
    if (t === '*') return;          // a list item's dot waits for its words
    if (t) out.push(line);
    line = [];
  };
  const para = () => {
    brk();
    if (out.length && out[out.length - 1].length) out.push([]);
  };
  const space = () => {
    const t = lineText();
    if (t && !/\s$/.test(t)) push('p', ' ');
  };
  const clean = s => (s || '').replace(/\s+/g, ' ');
  const target = (el, kind, label) => {
    n++;
    el.setAttribute('data-cj', String(n));
    targets.push({ n, kind, label: clean(label).trim().slice(0, 60) });
    return n;
  };
  // what aria-labelledby points at, said in words
  // (text only: the element may hold a style sheet, which textContent reads)
  const textOf = e => { let t = ''; for (const c of e.childNodes) {
    if (c.nodeType === 3) t += c.nodeValue + ' ';
    else if (c.nodeType === 1 && c.tagName === 'IMG') t += (c.alt || '') + ' ';
    else if (c.nodeType === 1 && !/^(STYLE|SCRIPT|TEMPLATE|NOSCRIPT)$/.test(c.tagName)) t += textOf(c);
  } return t; };
  const labelledBy = el => (el.getAttribute('aria-labelledby') || '').split(/\s+/).filter(Boolean)
    .map(id => { const e = document.getElementById(id); return e ? textOf(e) : ''; }).join(' ');
  // A button whose own text is only a number or nothing (a color swatch
  // labeled "0") is named by what it says to screen readers instead.
  const nameOf = (el, own) => {
    own = clean(own).trim();
    if (own && !/^\d{0,2}$/.test(own)) return own;
    const img = el.querySelector && el.querySelector('img[alt]');
    return clean(el.getAttribute('aria-label') || labelledBy(el) || el.title || (img && img.alt) || own).trim();
  };
  const labelOf = el => {
    let t = el.getAttribute('aria-label') || labelledBy(el) || el.getAttribute('placeholder') || '';
    if (!t && el.id) {
      const l = document.querySelector('label[for="' + CSS.escape(el.id) + '"]');
      if (l) t = l.innerText;
    }
    if (!t && el.closest('label')) t = el.closest('label').innerText;
    return clean(t || el.name || el.title || '').trim().slice(0, 30);
  };
  const hidden = el => {
    const cs = getComputedStyle(el);
    if (cs.display === 'none' || cs.visibility === 'hidden' || cs.visibility === 'collapse') return true;
    // aria-hidden is NOT skipped: it marks what a screen reader should
    // pass over, and the price a person sees is often just that, with a
    // copy for screen readers clipped to one pixel. The copy is skipped.
    const r = el.getBoundingClientRect();
    if (r.width <= 2 && r.height <= 2 && cs.overflow === 'hidden') return true;
    if (cs.clip === 'rect(0px, 0px, 0px, 0px)' || cs.clipPath === 'inset(50%)') return true;
    if (r.width === 0 && r.height === 0 && cs.overflow !== 'visible') return true;
    // (a price's decimal point is drawn with opacity 0 on some shops)
    if (parseFloat(cs.opacity) === 0 && (el.textContent || '').trim().length > 2) return true;
    // parked off the left or top edge (menus, screen-reader text)
    if (r.right < -50 || r.bottom + window.scrollY < -50) return true;
    return false;
  };

  const walk = (node, style) => {
    if (node.nodeType === 3) {
      const t = clean(node.nodeValue);
      if (t.trim()) {
        // after a link's number, words need a space; punctuation doesn't
        const last = line[line.length - 1];
        if (/^\s/.test(t) || (last && last[0] === 'n' && /^[A-Za-z0-9(]/.test(t.trim()))) space();
        push(style, t.trim());
        if (/\s$/.test(t)) push('p', ' ');
      } else if (t) space();
      return;
    }
    if (node.nodeType !== 1 || SKIP.has(node.tagName)) return;
    const el = node, tag = el.tagName;
    if (hidden(el)) return;
    if (tag === 'BR') { brk(); return; }
    if (tag === 'HR') { brk(); out.push([['p', '----------------------------------------']]); return; }
    if (tag === 'IMG') {
      const r = el.getBoundingClientRect();
      const alt = clean(el.alt).trim();
      if (r.width >= 120 && r.height >= 80) {
        const k = target(el, 'image', alt);
        push('c', '(picture ' + k + (alt ? ': ' + alt.slice(0, 50) : '') + ')');
        space();
      } else if (alt && alt.length < 40 && style === 'l') {
        push(style, alt);
      }
      return;
    }
    if (tag === 'INPUT') {
      const type = (el.type || 'text').toLowerCase();
      if (type === 'hidden') return;
      if (type === 'submit' || type === 'button' || type === 'image' || type === 'reset') {
        const name = nameOf(el, el.value || el.alt) || labelOf(el) || 'button';
        const k = target(el, 'button', name);
        push('c', '<' + k + ':' + name.slice(0, 30) + '>');
      } else if (type === 'checkbox' || type === 'radio') {
        const k = target(el, 'check', labelOf(el));
        push('c', '(' + k + ':' + (el.checked ? 'X' : ' ') + ')');
      } else {
        const k = target(el, type === 'password' ? 'password' : 'field', labelOf(el));
        const v = type === 'password' ? '*'.repeat(el.value.length) : el.value;
        push('c', '{' + k + ':' + labelOf(el) + (v ? '=' + v.slice(0, 30) : '') + '____}');
      }
      space();
      return;
    }
    if (tag === 'TEXTAREA') {
      const k = target(el, 'field', labelOf(el));
      push('c', '{' + k + ':' + labelOf(el) + (el.value ? '=' + clean(el.value).slice(0, 30) : '') + '____}');
      space();
      return;
    }
    if (tag === 'SELECT') {
      const k = target(el, 'select', labelOf(el));
      const o = el.options[el.selectedIndex];
      push('c', '{' + k + ':' + labelOf(el) + '=' + (o ? clean(o.text).trim().slice(0, 25) : '') + ' v}');
      space();
      return;
    }
    const cs = getComputedStyle(el);
    // (SPAN and its kind stay in the line whatever their display says: a
    // price is often three spans styled as boxes)
    const block = !/^(SPAN|B|I|EM|STRONG|SUP|SUB|SMALL|U|LABEL|FONT)$/.test(tag) && (BLOCK.has(cs.display) || /^(P|DIV|H[1-6]|LI|TR|UL|OL|SECTION|ARTICLE|HEADER|FOOTER|NAV|FORM|TABLE|BLOCKQUOTE|PRE|DT|DD|FIGURE|MAIN|ASIDE)$/.test(tag));
    let st = style;
    // h: a heading; g: a smaller one (H3, H4), a part inside the one above
    if (/^H[12]$/.test(tag)) st = 'h';
    else if (/^H[34]$/.test(tag)) st = 'g';
    const isPara = /^(P|H[1-6]|UL|OL|TABLE|BLOCKQUOTE|PRE|FIGURE|SECTION|ARTICLE|FORM|DL)$/.test(tag);
    if (!WHOLE && (cs.position === 'fixed' || cs.position === 'sticky') && el !== root) return; // bars that float over the page
    // Menus, the site's header and its footer wait for "w" too.
    if (!WHOLE && el !== root && (/^(NAV|HEADER|FOOTER)$/.test(tag) || /^(navigation|banner|contentinfo)$/.test(el.getAttribute('role') || '')
        || /^(navbar|nav-belt|nav-main|navFooter|skiplink|rhf)$/.test(el.id || ''))) { trimmed = true; return; }
    if (isPara) para(); else if (block) brk();
    if (tag === 'LI') push('p', '* ');
    if (line.length && line[line.length - 1][0] === 'n' && (tag === 'A' || tag === 'BUTTON')) space();
    const isLink = tag === 'A' && el.getAttribute('href') && !el.getAttribute('href').startsWith('javascript:void');
    const isButton = tag === 'BUTTON' || el.getAttribute('role') === 'button' || (tag === 'A' && !isLink && el.onclick);
    if (isLink || isButton) {
      el.removeAttribute('target');
      const before = lineText().length, outBefore = out.length;
      inside++;
      walk_children(el, isLink ? 'l' : 'c');
      inside--;
      // no space left hanging between the words and the number
      while (line.length && line[line.length - 1][0] === 'p' && !line[line.length - 1][1].trim()) line.pop();
      let said = out.length === outBefore ? lineText().length > before : true;
      const label = nameOf(el, el.innerText);
      if (!said) {
        const t = clean(el.getAttribute('aria-label') || el.title || '').trim().slice(0, 40);
        if (!t) return;   // a link with nothing to show: leave it out
        push(isLink ? 'l' : 'c', t);
      }
      const k = target(el, isLink ? 'link' : 'button', label);
      push('n', '[' + k + ']');
    } else {
      walk_children(el, st);
    }
    if (tag === 'TD' || tag === 'TH') { const t = lineText(); if (t.trim()) push('p', ' | '); }
    if (isPara) para(); else if (block) brk();
  };
  const walk_children = (el, style) => {
    for (const c of el.childNodes) walk(c, style);
    if (el.shadowRoot) for (const c of el.shadowRoot.childNodes) walk(c, style);
  };

  // The part of the page that is the page: its main content, when the
  // page says where that is and it holds a fair share of the text.
  // Otherwise (and when WHOLE is asked for) everything.
  let root = document.body, whole = true;
  if (!WHOLE) {
    const all = (document.body.innerText || '').length;
    let best = null, bestLen = 0;
    for (const c of document.querySelectorAll('main, [role="main"], article, #content, #main, #dp, #search')) {
      const len = (c.innerText || '').length;
      if (len > bestLen) { best = c; bestLen = len; }
    }
    if (best && bestLen > 400 && bestLen > all * 0.3) { root = best; whole = false; }
  }
  walk(root, 'p');
  brk();
  while (out.length && !out[out.length - 1].length) out.pop();
  return JSON.stringify({ title: document.title, url: location.href, lines: out, targets, whole: whole && !trimmed });
})(%WHOLE%)
