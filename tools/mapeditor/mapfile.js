// Blood Map Maker: any Blood .MAP file, read and written without losing anything.
//
// The map is kept the way the game keeps it (source/blood/src/db.cpp, dbLoadMap/dbSaveMap):
// sectors, walls and sprites with every field, and their extra data (XSECTOR, XWALL, XSPRITE)
// unpacked field by field in Blood's bit order (as .ext). Reading a map and writing it again gives the same
// bytes, so editing one thing never changes the rest. Versions 6 (plain) and 7 (encrypted) both work.
(function (root) {
  'use strict';

  const HEADER_NEW = 0x7474614d, HEADER_OLD = 0x4d617474;
  const LIMITS = { sectors: 4096, walls: 16384, sprites: 16384, tiles: 30720, status: 1024 };

  // ---- the structures, field by field ([name, type]; types: i8 u8 i16 u16 i32 u32) ------------
  const SECTOR = [['wallptr', 'i16'], ['wallnum', 'i16'], ['ceilingz', 'i32'], ['floorz', 'i32'], ['ceilingstat', 'u16'], ['floorstat', 'u16'],
    ['ceilingpicnum', 'i16'], ['ceilingheinum', 'i16'], ['ceilingshade', 'i8'], ['ceilingpal', 'u8'], ['ceilingxpanning', 'u8'], ['ceilingypanning', 'u8'],
    ['floorpicnum', 'i16'], ['floorheinum', 'i16'], ['floorshade', 'i8'], ['floorpal', 'u8'], ['floorxpanning', 'u8'], ['floorypanning', 'u8'],
    ['visibility', 'u8'], ['fogpal', 'u8'], ['type', 'i16'], ['hitag', 'i16'], ['extra', 'i16']];
  const WALL = [['x', 'i32'], ['y', 'i32'], ['point2', 'i16'], ['nextwall', 'i16'], ['nextsector', 'i16'], ['cstat', 'u16'], ['picnum', 'i16'], ['overpicnum', 'i16'],
    ['shade', 'i8'], ['pal', 'u8'], ['xrepeat', 'u8'], ['yrepeat', 'u8'], ['xpanning', 'u8'], ['ypanning', 'u8'], ['type', 'i16'], ['hitag', 'i16'], ['extra', 'i16']];
  const SPRITE = [['x', 'i32'], ['y', 'i32'], ['z', 'i32'], ['cstat', 'u16'], ['picnum', 'i16'], ['shade', 'i8'], ['pal', 'u8'], ['clipdist', 'u8'], ['blend', 'u8'],
    ['xrepeat', 'u8'], ['yrepeat', 'u8'], ['xoffset', 'i8'], ['yoffset', 'i8'], ['sectnum', 'i16'], ['statnum', 'i16'], ['ang', 'i16'], ['owner', 'i16'],
    ['xvel', 'i16'], ['yvel', 'i16'], ['zvel', 'i16'], ['type', 'i16'], ['flags', 'i16'], ['extra', 'i16']];
  const SIZE = { i8: 1, u8: 1, i16: 2, u16: 2, i32: 4, u32: 4 };

  // bit fields in dbSaveMap()'s order; whatever bits follow the last field are kept as they are
  const XSECTOR = [['reference', 14], ['state', 1], ['busy', 17], ['data', 16], ['txID', 10], ['busyWaveA', 3], ['busyWaveB', 3], ['rxID', 10], ['command', 8],
    ['triggerOn', 1], ['triggerOff', 1], ['busyTimeA', 12], ['waitTimeA', 12], ['restState', 1], ['interruptable', 1], ['amplitude', 8, true], ['freq', 8],
    ['reTriggerA', 1], ['reTriggerB', 1], ['phase', 8], ['wave', 4], ['shadeAlways', 1], ['shadeFloor', 1], ['shadeCeiling', 1], ['shadeWalls', 1],
    ['shade', 8, true], ['panAlways', 1], ['panFloor', 1], ['panCeiling', 1], ['Drag', 1], ['Underwater', 1], ['Depth', 3], ['panVel', 8], ['panAngle', 11],
    ['unused1', 1], ['decoupled', 1], ['triggerOnce', 1], ['isTriggered', 1], ['Key', 3], ['Push', 1], ['Vector', 1], ['Reserved', 1], ['Enter', 1],
    ['Exit', 1], ['Wallpush', 1], ['color', 1], ['unused2', 1], ['busyTimeB', 12], ['waitTimeB', 12], ['stopOn', 1], ['stopOff', 1], ['ceilpal', 4],
    ['offCeilZ', 32, true], ['onCeilZ', 32, true], ['offFloorZ', 32, true], ['onFloorZ', 32, true], ['marker0', 16], ['marker1', 16], ['Crush', 1],
    ['ceilXPanFrac', 8], ['ceilYPanFrac', 8], ['floorXPanFrac', 8], ['damageType', 3], ['floorpal', 4], ['floorYPanFrac', 8], ['locked', 1],
    ['windVel', 10], ['windAng', 11], ['windAlways', 1], ['dudeLockout', 1], ['bobTheta', 11], ['bobZRange', 5], ['bobSpeed', 12, true],
    ['bobAlways', 1], ['bobFloor', 1], ['bobCeiling', 1], ['bobRotate', 1]];
  const XWALL = [['reference', 14], ['state', 1], ['busy', 17], ['data', 16, true], ['txID', 10], ['unused1', 6], ['rxID', 10], ['command', 8], ['triggerOn', 1],
    ['triggerOff', 1], ['busyTime', 12], ['waitTime', 12], ['restState', 1], ['interruptable', 1], ['panAlways', 1], ['panXVel', 8, true], ['panYVel', 8, true],
    ['decoupled', 1], ['triggerOnce', 1], ['isTriggered', 1], ['key', 3], ['triggerPush', 1], ['triggerVector', 1], ['triggerTouch', 1], ['unused2', 2],
    ['xpanFrac', 8], ['ypanFrac', 8], ['locked', 1], ['dudeLockout', 1], ['unused3', 4], ['unused4', 32]];
  const XSPRITE = [['reference', 14], ['state', 1], ['busy', 17], ['txID', 10], ['rxID', 10], ['command', 8], ['triggerOn', 1], ['triggerOff', 1], ['wave', 2],
    ['busyTime', 12], ['waitTime', 12], ['restState', 1], ['Interrutable', 1], ['unused1', 2], ['respawnPending', 2], ['unused2', 1], ['lT', 1],
    ['dropMsg', 8], ['Decoupled', 1], ['triggerOnce', 1], ['isTriggered', 1], ['key', 3], ['Push', 1], ['Vector', 1], ['Impact', 1], ['Pickup', 1],
    ['Touch', 1], ['Sight', 1], ['Proximity', 1], ['unused3', 2], ['lSkill', 5], ['lS', 1], ['lB', 1], ['lC', 1], ['DudeLockout', 1],
    ['data1', 16, true], ['data2', 16, true], ['data3', 16, true], ['goalAng', 11], ['dodgeDir', 2, true], ['locked', 1], ['medium', 2], ['respawn', 2],
    ['data4', 16], ['unused4', 6], ['lockMsg', 8], ['health', 12], ['dudeDeaf', 1], ['dudeAmbush', 1], ['dudeGuard', 1], ['dudeFlag4', 1],
    ['target', 16, true], ['targetX', 32, true], ['targetY', 32, true], ['targetZ', 32, true], ['burnTime', 16], ['burnSource', 16, true],
    ['height', 16], ['stateTimer', 16]];
  const XSIZE = { sector: 60, wall: 24, sprite: 56 };

  // ---- bytes -------------------------------------------------------------------------------------
  function crypt(bytes, key) { // dbCrypt(): xor with the key's low byte, the key counting up
    for (let i = 0; i < bytes.length; i++) bytes[i] ^= (key + i) & 0xff;
    return bytes;
  }

  function readStruct(dv, o, spec) {
    const r = {};
    for (const [k, t] of spec) {
      switch (t) {
        case 'i8': r[k] = dv.getInt8(o); break;
        case 'u8': r[k] = dv.getUint8(o); break;
        case 'i16': r[k] = dv.getInt16(o, true); break;
        case 'u16': r[k] = dv.getUint16(o, true); break;
        case 'i32': r[k] = dv.getInt32(o, true); break;
        case 'u32': r[k] = dv.getUint32(o, true); break;
      }
      o += SIZE[t];
    }
    return r;
  }
  function writeStruct(obj, spec) {
    const size = spec.reduce((s, [, t]) => s + SIZE[t], 0);
    const b = new Uint8Array(size), dv = new DataView(b.buffer);
    let o = 0;
    for (const [k, t] of spec) {
      const v = Math.trunc(+obj[k] || 0);
      switch (t) {
        case 'i8': dv.setInt8(o, clampInt(v, -128, 127)); break;
        case 'u8': dv.setUint8(o, clampInt(v, 0, 255)); break;
        case 'i16': dv.setInt16(o, clampInt(v, -32768, 32767), true); break;
        case 'u16': dv.setUint16(o, clampInt(v, 0, 65535), true); break;
        case 'i32': dv.setInt32(o, clampInt(v, -2147483648, 2147483647), true); break;
        case 'u32': dv.setUint32(o, clampInt(v, 0, 4294967295), true); break;
      }
      o += SIZE[t];
    }
    return b;
  }
  function clampInt(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

  // packed x-structures: lowest bit first (BitReader/BitWriter in common_game.h)
  function unpackBits(bytes, spec) {
    let v = 0n;
    for (let i = bytes.length - 1; i >= 0; i--) v = (v << 8n) | BigInt(bytes[i]);
    const r = {};
    let pos = 0n;
    for (const [k, bits, signed] of spec) {
      const n = BigInt(bits);
      let x = (v >> pos) & ((1n << n) - 1n);
      if (signed && (x >> (n - 1n)) & 1n) x -= 1n << n;
      r[k] = Number(x);
      pos += n;
    }
    const rest = v >> pos; // bits after the last known field (normally 0): kept untouched
    if (rest) r._rest = rest.toString();
    return r;
  }
  function packBits(obj, spec, size) {
    let v = 0n, pos = 0n;
    for (const [k, bits] of spec) {
      const n = BigInt(bits);
      const x = BigInt.asUintN(bits, BigInt(Math.trunc(+obj[k] || 0)));
      v |= x << pos;
      pos += n;
    }
    if (obj._rest) v |= BigInt(obj._rest) << pos;
    const b = new Uint8Array(size);
    for (let i = 0; i < size; i++) { b[i] = Number(v & 0xffn); v >>= 8n; }
    return b;
  }

  // ---- reading -------------------------------------------------------------------------------------
  class MapError extends Error {}

  function parseMap(input) {
    const data = input instanceof Uint8Array ? input : new Uint8Array(input);
    const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
    let pos = 0;
    const need = (n) => { if (n < 0 || pos + n > data.length) throw new MapError('the file ends too early'); };
    const take = (n) => { need(n); const b = data.slice(pos, pos + n); pos += n; return b; };
    if (data.length < 47 || data[0] !== 0x42 || data[1] !== 0x4c || data[2] !== 0x4d || data[3] !== 0x1a) throw new MapError('not a Blood map');
    const version = dv.getUint16(4, true);
    const v7 = (version & 0xff00) === 0x700;
    if (!v7 && (version & 0xff00) !== 0x600) throw new MapError('unknown map version ' + version.toString(16));
    pos = 6;
    let head = take(37);
    let encrypted = false;
    const song0 = new DataView(head.buffer).getUint32(22, true);
    if (song0 !== 0 && song0 !== HEADER_NEW && song0 !== HEADER_OLD) { crypt(head, HEADER_NEW); encrypted = true; }
    const h = readStruct(new DataView(head.buffer), 0, [['startX', 'i32'], ['startY', 'i32'], ['startZ', 'i32'], ['startAng', 'i16'], ['startSector', 'i16'],
      ['skyBits', 'i16'], ['visibility', 'i32'], ['songId', 'u32'], ['parallaxType', 'u8'], ['mapRev', 'i32'], ['numSectors', 'i16'], ['numWalls', 'i16'], ['numSprites', 'i16']]);
    if (!(h.numSectors > 0 && h.numSectors <= LIMITS.sectors && h.numWalls > 0 && h.numWalls <= LIMITS.walls && h.numSprites >= 0 && h.numSprites <= LIMITS.sprites && h.skyBits >= 0 && h.skyBits <= 8))
      throw new MapError('damaged header');
    const map = { version, encrypted, header: h, header2: null, xsize: { ...XSIZE }, skyOffsets: [], sectors: [], walls: [], sprites: [] };
    if (v7) {
      const h2 = crypt(take(128), h.numWalls);
      const d2 = new DataView(h2.buffer);
      map.xsize = { sprite: d2.getInt32(0x40, true), wall: d2.getInt32(0x44, true), sector: d2.getInt32(0x48, true) };
      if (!(map.xsize.sprite >= 0 && map.xsize.sprite <= 56 && map.xsize.wall >= 0 && map.xsize.wall <= 24 && map.xsize.sector >= 0 && map.xsize.sector <= 60))
        throw new MapError('damaged header (extra data sizes)');
      map.header2 = Array.from(h2);
    }
    const sky = take(2 << h.skyBits);
    if (v7) crypt(sky, (1 << h.skyBits) * 2);
    const sdv = new DataView(sky.buffer);
    for (let i = 0; i < 1 << h.skyBits; i++) map.skyOffsets.push(sdv.getInt16(i * 2, true));
    const xs = (bytes, full) => { const b = new Uint8Array(full); b.set(bytes); return b; };
    for (let i = 0; i < h.numSectors; i++) {
      const b = take(40);
      if (v7) crypt(b, h.mapRev * 40);
      const s = readStruct(new DataView(b.buffer), 0, SECTOR);
      if (s.extra > 0) s.ext = unpackBits(xs(take(map.xsize.sector), 60), XSECTOR);
      map.sectors.push(s);
    }
    for (let i = 0; i < h.numWalls; i++) {
      const b = take(32);
      if (v7) crypt(b, (h.mapRev * 40) | HEADER_NEW);
      const w = readStruct(new DataView(b.buffer), 0, WALL);
      if (w.extra > 0) w.ext = unpackBits(xs(take(map.xsize.wall), 24), XWALL);
      map.walls.push(w);
    }
    for (let i = 0; i < h.numSprites; i++) {
      const b = take(44);
      if (v7) crypt(b, (h.mapRev * 44) | HEADER_NEW);
      const s = readStruct(new DataView(b.buffer), 0, SPRITE);
      if (s.extra > 0) s.ext = unpackBits(xs(take(map.xsize.sprite), 56), XSPRITE);
      map.sprites.push(s);
    }
    need(4);
    const crc = dv.getUint32(pos, true);
    map.crcOk = crc32(data, pos) === crc;
    pos += 4;
    map.trailing = pos < data.length ? Array.from(data.slice(pos)) : null; // anything after the CRC (rare)
    return map;
  }

  // ---- writing ------------------------------------------------------------------------------------
  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
    return t;
  })();
  function crc32(bytes, n) {
    let c = 0xffffffff;
    for (let i = 0; i < n; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  // the counts and links that follow from the arrays; call before writing after edits
  function finish(map) {
    const h = map.header;
    h.numSectors = map.sectors.length; h.numWalls = map.walls.length; h.numSprites = map.sprites.length;
    let nx = 0;
    for (const list of [map.sectors, map.walls, map.sprites]) for (const o of list) { if (o.ext) { if (!(o.extra > 0)) o.extra = ++nx; } else if (o.extra > 0) o.extra = -1; }
  }

  function writeMap(map) {
    const h = map.header, v7 = (map.version & 0xff00) === 0x700;
    const parts = [];
    const sig = new Uint8Array(6); sig.set([0x42, 0x4c, 0x4d, 0x1a]); new DataView(sig.buffer).setUint16(4, map.version, true);
    parts.push(sig);
    const head = writeStruct({ ...h, numSectors: map.sectors.length, numWalls: map.walls.length, numSprites: map.sprites.length },
      [['startX', 'i32'], ['startY', 'i32'], ['startZ', 'i32'], ['startAng', 'i16'], ['startSector', 'i16'], ['skyBits', 'i16'], ['visibility', 'i32'],
        ['songId', 'u32'], ['parallaxType', 'u8'], ['mapRev', 'i32'], ['numSectors', 'i16'], ['numWalls', 'i16'], ['numSprites', 'i16']]);
    parts.push(map.encrypted ? crypt(head, HEADER_NEW) : head);
    const xsize = v7 ? map.xsize : XSIZE;
    if (v7) {
      const h2 = new Uint8Array(map.header2 || new Array(128).fill(0));
      const d2 = new DataView(h2.buffer);
      d2.setInt32(0x40, xsize.sprite, true); d2.setInt32(0x44, xsize.wall, true); d2.setInt32(0x48, xsize.sector, true);
      parts.push(crypt(h2, map.walls.length));
    }
    const sky = new Uint8Array(2 << h.skyBits), sdv = new DataView(sky.buffer);
    for (let i = 0; i < 1 << h.skyBits; i++) sdv.setInt16(i * 2, map.skyOffsets[i] || 0, true);
    parts.push(v7 ? crypt(sky, (1 << h.skyBits) * 2) : sky);
    for (const s of map.sectors) {
      const b = writeStruct(s, SECTOR);
      parts.push(v7 ? crypt(b, h.mapRev * 40) : b);
      if (s.extra > 0) parts.push(packBits(s.ext || {}, XSECTOR, 60).slice(0, xsize.sector));
    }
    for (const w of map.walls) {
      const b = writeStruct(w, WALL);
      parts.push(v7 ? crypt(b, (h.mapRev * 40) | HEADER_NEW) : b);
      if (w.extra > 0) parts.push(packBits(w.ext || {}, XWALL, 24).slice(0, xsize.wall));
    }
    for (const s of map.sprites) {
      const b = writeStruct(s, SPRITE);
      parts.push(v7 ? crypt(b, (h.mapRev * 44) | HEADER_NEW) : b);
      if (s.extra > 0) parts.push(packBits(s.ext || {}, XSPRITE, 56).slice(0, xsize.sprite));
    }
    const n = parts.reduce((s, p) => s + p.length, 0);
    const trailing = map.trailing || [];
    const out = new Uint8Array(n + 4 + trailing.length);
    let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    new DataView(out.buffer).setUint32(o, crc32(out, o), true);
    out.set(trailing, o + 4);
    return out;
  }

  root.MapFile = { parseMap, writeMap, finish, MapError, SECTOR, WALL, SPRITE, XSECTOR, XWALL, XSPRITE, LIMITS, crc32 };
  if (typeof module !== 'undefined' && module.exports) module.exports = root.MapFile;
})(typeof window !== 'undefined' ? window : this);
