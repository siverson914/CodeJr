// Renders a phrase with SAM (Software Automatic Mouth, 1982) as 8-bit PCM
// at 22050 Hz. Used by genspeech.py; needs `npm i sam-js` somewhere on NODE_PATH.
const SamJs = require('sam-js');
const fs = require('fs');
const [, , text, out, speed, pitch] = process.argv;
const sam = new SamJs({ speed: +speed || 72, pitch: +pitch || 64, throat: 128, mouth: 128 });
fs.writeFileSync(out, Buffer.from(sam.buf8(text)));
