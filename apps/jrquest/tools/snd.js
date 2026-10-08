// .snd text -> SOUND resource (PCjr sound chip notes), and back.
// The text format is described in FORMATS.md under "*.snd".

import { readFileSync, writeFileSync } from 'node:fs';
import { SourceError, Bytes, word, isMain, runMain } from './util.js';

// The chip divides its 3.579545 MHz clock by 32, then by the 10-bit
// divisor, to get the tone.
const CLOCK = 3579545 / 32;
const SEMITONE = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };

export function noteDivisor(letter, accidental, octave) {
  const midi = 12 * (octave + 1) + SEMITONE[letter] + (accidental === '#' ? 1 : accidental === 'b' ? -1 : 0);
  const hz = 440 * Math.pow(2, (midi - 69) / 12);
  return Math.round(CLOCK / hz);
}

// Returns four voices (three tone, one noise), each a list of
// { duration, divisor, attenuation } with duration in 1/60 s. For the
// noise voice, divisor is the chip's 3-bit noise control value.
export function parseSound(text, file = 'sound') {
  const voices = [[], [], [], []];
  let tempo = 120;
  let voice = -1;
  let vol = 15;
  let beats = 0;      // position in the voice, in 1/192 of a whole note
  let ticks = 0;      // the same position in 1/60 s, as already written
  text.split('\n').forEach((raw, i) => {
    const fail = (msg) => { throw new SourceError(file, i + 1, msg); };
    const line = raw.replace(/(^|\s)#.*/, '').trim().toLowerCase();
    if (!line) return;
    let m;
    if ((m = /^tempo\s+(\d+)$/.exec(line))) {
      tempo = Number(m[1]);
      if (tempo < 20 || tempo > 600) fail('tempo must be 20 to 600');
      if (voices.some((v) => v.length)) fail('tempo must come before the notes');
      return;
    }
    if ((m = /^voice\s+(1|2|3|noise)$/.exec(line))) {
      voice = m[1] === 'noise' ? 3 : Number(m[1]) - 1;
      if (voices[voice].length) fail(`voice ${m[1]} is already written above`);
      vol = 15; beats = 0; ticks = 0;
      return;
    }
    if (/^(tempo|voice)\b/.test(line)) fail(`bad ${line.split(/\s+/)[0]} line`);
    if (voice < 0) fail('notes before any "voice" line');
    for (const tok of line.replace(/\bvol\s+(?=\d)/g, 'vol=').split(/\s+/)) {
      if (tok === '|') continue;   // bar lines are for the reader
      if ((m = /^vol=?(\d+)$/.exec(tok))) {
        vol = Number(m[1]);
        if (vol > 15) fail('vol must be 0 to 15');
        continue;
      }
      m = /^(r|[a-g][#b]?\d|[wp][0-3])\/(1|2|4|8|16|32)(\.?)$/.exec(tok);
      if (!m) fail(`not a note: ${tok}`);
      // Lengths are summed exactly and only then turned into ticks, so
      // rounding never lets two voices drift apart.
      beats += (192 / Number(m[2])) * (m[3] ? 1.5 : 1);
      const end = Math.round((beats * 14400) / (192 * tempo));
      const duration = end - ticks;
      ticks = end;
      if (duration < 1) fail(`${tok} is shorter than 1/60 s at tempo ${tempo}`);
      const name = m[1];
      let divisor = 0;
      let attenuation = 15 - vol;
      if (name === 'r') {
        attenuation = 15;
      } else if (voice === 3) {
        if (!/^[wp]/.test(name)) fail(`the noise voice plays w0 to w3 and p0 to p3, not ${name}`);
        divisor = (name[0] === 'w' ? 4 : 0) | Number(name[1]);
      } else {
        if (/^[wp]/.test(name)) fail(`${name} is a noise: put it under "voice noise"`);
        divisor = noteDivisor(name[0], name.length === 3 ? name[1] : '', Number(name.at(-1)));
        if (divisor < 1 || divisor > 1023) fail(`${name} is out of the chip's range (a2 to about b9)`);
      }
      voices[voice].push({ duration, divisor, attenuation });
    }
  });
  return voices;
}

export function encodeSound(voices) {
  const out = new Bytes();
  for (let v = 0; v < 4; v++) out.word(0);
  for (let v = 0; v < 4; v++) {
    out.setWord(v * 2, out.length);
    const reg = 0x80 | (v << 5);
    for (const n of voices[v] || []) {
      out.word(n.duration);
      // The chip's two frequency bytes: the high 6 bits, then the latch
      // byte with the low 4. Noise has one control byte, sent as the latch.
      if (v === 3) out.byte(0).byte(reg | (n.divisor & 7));
      else out.byte((n.divisor >> 4) & 0x3F).byte(reg | (n.divisor & 0x0F));
      out.byte(reg | 0x10 | (n.attenuation & 0x0F));
    }
    out.byte(0xFF).byte(0xFF);
  }
  return out.done();
}

export function decodeSound(bytes) {
  const voices = [];
  for (let v = 0; v < 4; v++) {
    const notes = [];
    for (let p = word(bytes, v * 2); word(bytes, p) !== 0xFFFF; p += 5) {
      if (p + 5 > bytes.length) throw new Error(`sound: voice ${v + 1} has no end mark`);
      notes.push({
        duration: word(bytes, p),
        divisor: v === 3 ? bytes[p + 3] & 7 : ((bytes[p + 2] & 0x3F) << 4) | (bytes[p + 3] & 0x0F),
        attenuation: bytes[p + 4] & 0x0F,
      });
    }
    voices.push(notes);
  }
  return voices;
}

if (isMain(import.meta)) {
  runMain('snd.js in.snd out.bin', 2, ([src, dst]) => {
    const voices = parseSound(readFileSync(src, 'utf8'), src);
    const bytes = encodeSound(voices);
    writeFileSync(dst, bytes);
    const ticks = Math.max(...voices.map((v) => v.reduce((t, n) => t + n.duration, 0)));
    console.log(`${dst}: ${voices.map((v) => v.length).join('+')} notes, ${(ticks / 60).toFixed(1)} s, ${bytes.length} bytes`);
  });
}
