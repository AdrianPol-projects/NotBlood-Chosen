// Blood Map Maker: the player's own Blood files, for real textures in the 3D view and the tile
// picker. Nothing of Blood is included: the player picks TILES000.ART ... TILES017.ART (and any
// TILES0xx.ART of mods) and BLOOD.RFF from their game folder once; they're kept in this browser.
//
// ART: Build's tile files (header, sizes, animation info, then each tile's pixels column by column).
// RFF: Blood's archive; the palette (BLOOD.PAL) and the shading table (NORMAL.PLU) come from it.
// Both read the way the game reads them (source/build tiles, source/blood/src/resource.cpp).
(function (root) {
  'use strict';

  const DB = 'bloodmapmaker', STORE = 'files';
  const tiles = new Map(); // tile number -> { w, h, data (Uint8Array, column by column), picanm }
  let palette = null;      // Uint8Array(768), 0..255
  let shades = null;       // Uint8Array(64 * 256): shade -> palette index -> palette index
  let listeners = [];

  function u16(dv, o) { return dv.getUint16(o, true); }

  // one ART file into the tile table
  function addArt(buf) {
    // newer tools put "BUILDART" in front of the usual header (tiles.cpp reads both)
    const sig = new Uint8Array(buf, 0, Math.min(8, buf.byteLength));
    if (String.fromCharCode(...sig) === 'BUILDART') buf = buf.slice(8);
    const dv = new DataView(buf);
    if (buf.byteLength < 16 || dv.getInt32(0, true) !== 1) throw new Error('not a Build ART file');
    const first = dv.getInt32(8, true), last = dv.getInt32(12, true), n = last - first + 1;
    if (first < 0 || n <= 0 || n > 30720 || 16 + n * 8 > buf.byteLength) throw new Error('damaged ART file');
    let pos = 16 + n * 8;
    let count = 0;
    for (let i = 0; i < n; i++) {
      const w = dv.getInt16(16 + i * 2, true), h = dv.getInt16(16 + n * 2 + i * 2, true);
      const picanm = dv.getInt32(16 + n * 4 + i * 4, true);
      if (w <= 0 || h <= 0) continue;
      if (pos + w * h > buf.byteLength) throw new Error('damaged ART file');
      tiles.set(first + i, { w, h, data: new Uint8Array(buf, pos, w * h), picanm });
      pos += w * h;
      count++;
    }
    return count;
  }

  function rffCrypt(bytes, key) { // Resource::Crypt: xor with (key >> 1), key counting up
    for (let i = 0; i < bytes.length; i++, key = (key + 1) & 0xffff) bytes[i] ^= (key >> 1) & 0xff;
  }

  // BLOOD.PAL and NORMAL.PLU out of BLOOD.RFF, reading only the parts needed (it's big)
  async function readRff(file) {
    const head = new DataView(await file.slice(0, 32).arrayBuffer());
    if (head.byteLength < 32 || head.getUint32(0, true) !== 0x1a464652) throw new Error('not an RFF file'); // "RFF\x1a"
    const version = u16(head, 4), offset = head.getUint32(8, true), count = head.getUint32(12, true);
    if (count > 100000 || offset + count * 48 > file.size) throw new Error('damaged RFF file');
    const dict = new Uint8Array(await file.slice(offset, offset + count * 48).arrayBuffer());
    if ((version & 0xff00) === 0x300) rffCrypt(dict, (offset + (version & 0xff) * offset) & 0xffff);
    else if ((version & 0xff00) !== 0x200) throw new Error('unknown RFF version');
    const dv = new DataView(dict.buffer);
    const str = (o, n) => { let s = ''; for (let i = 0; i < n && dict[o + i]; i++) s += String.fromCharCode(dict[o + i]); return s.toUpperCase(); };
    const found = {};
    for (let i = 0; i < count; i++) {
      const e = i * 48, type = str(e + 33, 3), name = str(e + 36, 8), flags = dict[e + 32], id = dv.getInt32(e + 44, true);
      const key = type === 'PAL' && (name === 'BLOOD' || ((flags & 1) && id === 0)) ? 'pal' : type === 'PLU' && name === 'NORMAL' ? 'plu' : null;
      if (!key || found[key]) continue;
      const at = dv.getUint32(e + 16, true), size = dv.getUint32(e + 20, true);
      if (at + size > file.size) continue;
      const data = new Uint8Array(await file.slice(at, at + size).arrayBuffer());
      if (flags & 16) rffCrypt(data.subarray(0, Math.min(256, data.length)), 0);
      found[key] = data;
    }
    if (!found.pal || found.pal.length < 768) throw new Error('BLOOD.PAL not found in this RFF');
    let pal = found.pal.slice(0, 768);
    if (pal.every((v) => v < 64)) pal = pal.map((v) => (v << 2) | (v >> 4)); // 6-bit palettes
    return { pal, plu: found.plu && found.plu.length >= 64 * 256 ? found.plu.slice(0, 64 * 256) : null };
  }

  // ---- keeping them in the browser -----------------------------------------------------------------
  function db() {
    return new Promise((resolve, reject) => {
      const rq = indexedDB.open(DB, 1);
      rq.onupgradeneeded = () => rq.result.createObjectStore(STORE);
      rq.onsuccess = () => resolve(rq.result);
      rq.onerror = () => reject(rq.error);
    });
  }
  async function dbPut(key, value) {
    const d = await db();
    await new Promise((resolve, reject) => { const t = d.transaction(STORE, 'readwrite'); t.objectStore(STORE).put(value, key); t.oncomplete = resolve; t.onerror = () => reject(t.error); });
    d.close();
  }
  async function dbGet(key) {
    const d = await db();
    const v = await new Promise((resolve, reject) => { const rq = d.transaction(STORE).objectStore(STORE).get(key); rq.onsuccess = () => resolve(rq.result); rq.onerror = () => reject(rq.error); });
    d.close();
    return v;
  }

  function changed() { listeners.forEach((f) => { try { f(); } catch (e) {} }); }

  // the files the player picked: ART and RFF files, anything else is ignored
  async function load(fileList) {
    const files = [...fileList];
    const arts = files.filter((f) => /\.art$/i.test(f.name)).sort((a, b) => a.name.localeCompare(b.name));
    const rff = files.find((f) => /^blood\.rff$/i.test(f.name)) || files.find((f) => /\.rff$/i.test(f.name));
    const report = { tiles: 0, palette: false, problems: [] };
    const saved = { arts: [], pal: null, plu: null };
    for (const f of arts) {
      try {
        const buf = await f.arrayBuffer();
        report.tiles += addArt(buf);
        saved.arts.push({ name: f.name.toUpperCase(), buf });
      } catch (e) { report.problems.push(f.name + ': ' + e.message); }
    }
    if (rff) {
      try {
        const r = await readRff(rff);
        palette = r.pal; shades = r.plu; report.palette = true;
        saved.pal = palette; saved.plu = shades;
      } catch (e) { report.problems.push(rff.name + ': ' + e.message); }
    }
    // keep what we have together with what was kept before
    try {
      const old = (await dbGet('game')) || { arts: [] };
      const names = new Set(saved.arts.map((a) => a.name));
      const merged = { arts: old.arts.filter((a) => !names.has(a.name)).concat(saved.arts), pal: saved.pal || old.pal || null, plu: saved.plu || old.plu || null };
      await dbPut('game', merged);
    } catch (e) { report.problems.push('Could not keep the files in this browser (' + e.message + ')'); }
    changed();
    return report;
  }

  async function restore() {
    try {
      const g = await dbGet('game');
      if (!g) return false;
      for (const a of g.arts || []) { try { addArt(a.buf); } catch (e) {} }
      if (g.pal) { palette = new Uint8Array(g.pal); shades = g.plu ? new Uint8Array(g.plu) : null; }
      changed();
      return true;
    } catch (e) { return false; }
  }

  async function forget() {
    tiles.clear(); palette = null; shades = null;
    try { await dbPut('game', null); } catch (e) {}
    changed();
  }

  // a tile as RGBA (for pictures in the editor), shade 0; null when unknown
  function tileImage(n, maxSize) {
    const t = tiles.get(n);
    if (!t || !palette) return null;
    const step = Math.max(1, Math.ceil(Math.max(t.w, t.h) / (maxSize || 1e9)));
    const w = Math.ceil(t.w / step), h = Math.ceil(t.h / step);
    const img = new ImageData(w, h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = t.data[(x * step) * t.h + y * step], o = (y * w + x) * 4;
      if (i === 255) continue; // see-through
      img.data[o] = palette[i * 3]; img.data[o + 1] = palette[i * 3 + 1]; img.data[o + 2] = palette[i * 3 + 2]; img.data[o + 3] = 255;
    }
    return img;
  }

  root.GameFiles = {
    load, restore, forget, addArt, readRff, tileImage,
    tile: (n) => tiles.get(n) || null,
    tileCount: () => tiles.size,
    maxTile: () => { let m = -1; tiles.forEach((_, k) => { if (k > m) m = k; }); return m; },
    palette: () => palette,
    shades: () => shades,
    ready: () => !!palette && tiles.size > 0,
    onChange: (f) => listeners.push(f),
  };
})(typeof window !== 'undefined' ? window : this);
