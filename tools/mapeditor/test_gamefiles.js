// node test_gamefiles.js [TILES.ART]: reads an ART file and a made-up encrypted RFF the way
// resource.cpp writes them (Blood's own files aren't in the repository)
global.window = global;
require('./gamefiles.js');
const fs = require('fs');
const G = global.GameFiles;
let failed = 0;
const expect = (c, what) => { if (!c) { failed++; console.log('FAIL: ' + what); } };
(async () => {
  const art = process.argv[2];
  if (art) {
    const buf = fs.readFileSync(art);
    const n = G.addArt(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.length));
    expect(n > 0, 'tiles read from ' + art);
    console.log(art + ': ' + n + ' tiles, highest ' + G.maxTile());
  }
  // an RFF (version 0x301, encrypted dictionary) with BLOOD.PAL (6-bit, encrypted data) and NORMAL.PLU
  const crypt = (b, key) => { for (let i = 0; i < b.length; i++, key = (key + 1) & 0xffff) b[i] ^= (key >> 1) & 0xff; };
  const pal = new Uint8Array(768).map((_, i) => i % 64), plu = new Uint8Array(64 * 256).map((_, i) => (i * 7) & 255);
  const files = [['PAL', 'BLOOD', pal, 16], ['PLU', 'NORMAL', plu, 0], ['RAW', 'OTHER', new Uint8Array(10), 0]];
  let off = 32; const parts = [];
  const dict = new Uint8Array(files.length * 48); const dv = new DataView(dict.buffer);
  files.forEach(([type, name, data, flags], i) => {
    const d = data.slice(); if (flags & 16) crypt(d.subarray(0, Math.min(256, d.length)), 0);
    parts.push(d); dv.setUint32(i * 48 + 16, off, true); dv.setUint32(i * 48 + 20, d.length, true); dict[i * 48 + 32] = flags;
    for (let k = 0; k < 3; k++) dict[i * 48 + 33 + k] = type.charCodeAt(k);
    for (let k = 0; k < name.length; k++) dict[i * 48 + 36 + k] = name.charCodeAt(k);
    off += d.length;
  });
  const version = 0x301; crypt(dict, (off + (version & 0xff) * off) & 0xffff);
  const head = new Uint8Array(32); const hv = new DataView(head.buffer);
  hv.setUint32(0, 0x1a464652, true); hv.setUint16(4, version, true); hv.setUint32(8, off, true); hv.setUint32(12, files.length, true);
  const file = new File([head, ...parts, dict], 'BLOOD.RFF');
  const r = await G.readRff(file);
  expect(r.pal[3] === 12 && r.pal[63] === 255, 'palette read and made 8-bit');
  expect(r.pal.length === 768 && r.pal[5] === ((5 % 64) << 2 | (5 % 64) >> 4), 'palette values');
  expect(r.plu && r.plu[100] === plu[100] && r.plu[16383] === plu[16383], 'shade table read');
  console.log(failed ? failed + ' file checks failed' : 'game file checks passed');
  process.exit(failed ? 1 : 0);
})();
