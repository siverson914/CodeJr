// The classic AGI parser: clean the line, look words up (longest match
// first, so "pick up" beats "pick"), and match said() patterns.

const SEPARATORS = ',.?!();:[]{}';
const DROPPED = '\'`-"';

export function cleanInput(text) {
  let out = '';
  for (const ch of text.toLowerCase()) {
    if (DROPPED.includes(ch)) continue;
    out += SEPARATORS.includes(ch) ? ' ' : ch;
  }
  return out.replace(/\s+/g, ' ').trim();
}

// Returns { words: [{ id, text }], unknown } where unknown is 0 or the
// 1-based position of the first word that is not in the dictionary.
// Group 0 words are dropped. Parsing stops at an unknown word.
export function parseLine(words, text) {
  const parts = cleanInput(text).split(' ').filter((p) => p !== '');
  const out = [];
  let unknown = 0;
  for (let i = 0; i < parts.length;) {
    let found = -1, used = 1;
    for (let n = Math.min(words.maxParts, parts.length - i); n >= 1; n--) {
      const w = parts.slice(i, i + n).join(' ');
      if (w in words.dict) { found = words.dict[w]; used = n; break; }
    }
    // Sierra's parser lets a stray "a" or "i" pass even when not listed.
    if (found < 0 && (parts[i] === 'a' || parts[i] === 'i')) found = 0;
    if (found !== 0) {
      out.push({ id: found, text: parts.slice(i, i + used).join(' ') });
      if (found < 0) { unknown = out.length; break; }
    }
    i += used;
  }
  return { words: out, unknown };
}

// Does a said() pattern (group numbers) accept these word ids?
// 1 matches any one word, 9999 matches the rest of the line.
export function saidMatches(ids, pattern) {
  let c = 0, n = ids.length, left = pattern.length, z = 0;
  for (; left && n; c++, left--, n--) {
    z = pattern[c];
    if (z === 9999) { left = 1; continue; }
    if (z !== 1 && ids[c] !== z) return false;
  }
  if (n && z !== 9999) return false;
  if (left && pattern[c] !== 9999) return false;
  return true;
}
