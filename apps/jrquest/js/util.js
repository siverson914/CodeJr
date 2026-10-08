// Small helpers shared by the interpreter modules. No DOM, no Node.

// 32-bit FNV-1a over a byte array.
export function fnv1a(bytes) {
  let h = 0x811c9dc5;
  for (let i = 0; i < bytes.length; i++) {
    h ^= bytes[i];
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const B64_REV = new Uint8Array(128);
for (let i = 0; i < 64; i++) B64_REV[B64.charCodeAt(i)] = i;

// Our own base64 so the modules need neither btoa nor Buffer.
export function toBase64(bytes) {
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const a = bytes[i], b = bytes[i + 1], c = bytes[i + 2];
    out += B64[a >> 2] + B64[((a & 3) << 4) | ((b || 0) >> 4)];
    out += i + 1 < bytes.length ? B64[((b & 15) << 2) | ((c || 0) >> 6)] : '=';
    out += i + 2 < bytes.length ? B64[c & 63] : '=';
  }
  return out;
}

export function fromBase64(text) {
  const s = text.replace(/[^A-Za-z0-9+/]/g, '');
  const n = (s.length * 3) >> 2;
  const out = new Uint8Array(n);
  let o = 0;
  for (let i = 0; i < s.length; i += 4) {
    const a = B64_REV[s.charCodeAt(i)], b = B64_REV[s.charCodeAt(i + 1)];
    const c = B64_REV[s.charCodeAt(i + 2)], d = B64_REV[s.charCodeAt(i + 3)];
    if (o < n) out[o++] = (a << 2) | (b >> 4);
    if (o < n) out[o++] = ((b & 15) << 4) | (c >> 2);
    if (o < n) out[o++] = ((c & 3) << 6) | d;
  }
  return out;
}

// Snapshots hold screens, which are mostly runs: (count, value) pairs.
export function packBytes(bytes) {
  const out = [];
  for (let i = 0; i < bytes.length;) {
    const v = bytes[i];
    let n = 1;
    while (n < 255 && i + n < bytes.length && bytes[i + n] === v) n++;
    out.push(n, v);
    i += n;
  }
  return toBase64(Uint8Array.from(out));
}

export function unpackBytes(text, into) {
  const src = fromBase64(text);
  let o = 0;
  for (let i = 0; i + 1 < src.length; i += 2) {
    into.fill(src[i + 1], o, o + src[i]);
    o += src[i];
  }
  return into;
}

export function bytesToString(bytes, start, end) {
  let s = '';
  for (let i = start; i < end; i++) s += String.fromCharCode(bytes[i]);
  return s;
}
