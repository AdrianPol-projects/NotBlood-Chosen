// NotBlood Chosen map editor: the map model, its checks and the Blood .MAP writer.
// Works in a browser (window.MapCore) and in Node (require), so the tests can run it too.
//
// A map is a list of rooms (simple polygons on a grid, each becomes a Blood sector) and things
// (player starts, weapons, ammo, items). Rooms that touch along an edge become one open space:
// on export every edge is split where other rooms' corners lie on it, and the matching halves
// are joined (nextwall/nextsector), like a Build editor joins sectors. The file is written the
// way the game itself saves maps (dbSaveMap() in source/blood/src/db.cpp): version 7, encrypted.
(function (root) {
  'use strict';

  // ---- what can be placed -------------------------------------------------------------------
  // type: Blood sprite type; pic, xr/yr, cstat: as in Blood's own maps; dz: height over the floor
  const THINGS = [
    { group: 'Starts', type: 1, name: 'Single player / co-op start', pic: 2522, xr: 64, yr: 64, cstat: 128, stat: 0, start: true },
    { group: 'Starts', type: 2, name: 'Bloodbath start', pic: 2522, xr: 64, yr: 64, cstat: 128, stat: 0, start: true },
    { group: 'Weapons', type: 43, name: 'Flare gun', pic: 524 },
    { group: 'Weapons', type: 41, name: 'Sawed-off shotgun', pic: 559 },
    { group: 'Weapons', type: 42, name: 'Tommy gun', pic: 558 },
    { group: 'Weapons', type: 46, name: 'Napalm launcher', pic: 526 },
    { group: 'Weapons', type: 49, name: 'TNT', pic: 589, xr: 48, yr: 48, cstat: 384 },
    { group: 'Weapons', type: 48, name: 'Spray can', pic: 618, xr: 40, yr: 40, cstat: 384 },
    { group: 'Weapons', type: 45, name: 'Tesla cannon', pic: 539 },
    { group: 'Weapons', type: 50, name: 'Life leech', pic: 800 },
    { group: 'Weapons', type: 44, name: 'Voodoo doll', pic: 525 },
    { group: 'Ammo', type: 76, name: 'Flares', pic: 816 },
    { group: 'Ammo', type: 67, name: 'Shotgun shells (few)', pic: 619 },
    { group: 'Ammo', type: 68, name: 'Shotgun shells (box)', pic: 812 },
    { group: 'Ammo', type: 69, name: 'Tommy gun bullets (few)', pic: 813 },
    { group: 'Ammo', type: 72, name: 'Tommy gun drum', pic: 817 },
    { group: 'Ammo', type: 79, name: 'Gasoline can', pic: 801 },
    { group: 'Ammo', type: 62, name: 'TNT bundle', pic: 589, cstat: 384 },
    { group: 'Ammo', type: 63, name: 'TNT box', pic: 809, cstat: 384 },
    { group: 'Ammo', type: 64, name: 'Proximity bombs', pic: 811, xr: 40, yr: 40 },
    { group: 'Ammo', type: 65, name: 'Remote bombs', pic: 810, xr: 40, yr: 40 },
    { group: 'Ammo', type: 60, name: 'Spray can (ammo)', pic: 618, xr: 40, yr: 40, cstat: 384 },
    { group: 'Ammo', type: 73, name: 'Tesla charge', pic: 548, xr: 24, yr: 24 },
    { group: 'Ammo', type: 66, name: 'Trapped soul', pic: 820, xr: 24, yr: 24 },
    { group: 'Health', type: 109, name: 'Life essence', pic: 2169, xr: 40, yr: 40, dz: 160 },
    { group: 'Health', type: 110, name: 'Life seed', pic: 2433, xr: 40, yr: 40 },
    { group: 'Health', type: 107, name: "Doctor's bag", pic: 519 },
    { group: 'Health', type: 108, name: 'Medicine pouch', pic: 822, xr: 40, yr: 40 },
    { group: 'Armor', type: 140, name: 'Basic armor', pic: 2628, xr: 64, yr: 64, dz: 3840 },
    { group: 'Armor', type: 141, name: 'Body armor', pic: 2586, xr: 64, yr: 64, dz: 3840 },
    { group: 'Armor', type: 142, name: 'Fire armor', pic: 2578, xr: 64, yr: 64, dz: 3840 },
    { group: 'Armor', type: 143, name: 'Spirit armor', pic: 2602, xr: 64, yr: 64, dz: 3840 },
    { group: 'Armor', type: 144, name: 'Super armor', pic: 2594, xr: 64, yr: 64, dz: 3840 },
    { group: 'Power-ups', type: 117, name: 'Guns akimbo', pic: 829, xr: 40, yr: 40 },
    { group: 'Power-ups', type: 113, name: 'Invisibility', pic: 896, xr: 40, yr: 40 },
    { group: 'Power-ups', type: 114, name: 'Death mask', pic: 825, xr: 40, yr: 40 },
    { group: 'Power-ups', type: 124, name: 'Reflective shots', pic: 2428, xr: 40, yr: 40 },
    { group: 'Power-ups', type: 115, name: 'Jump boots', pic: 827, xr: 40, yr: 40 },
    { group: 'Power-ups', type: 125, name: 'Beast vision', pic: 839, xr: 40, yr: 40 },
    { group: 'Power-ups', type: 118, name: 'Diving suit', pic: 830, xr: 80, yr: 64 },
    { group: 'Power-ups', type: 121, name: 'Crystal ball', pic: 760, xr: 40, yr: 40 },
    { group: 'Keys', type: 100, name: 'Skull key', pic: 2552, xr: 32, yr: 32 },
    { group: 'Keys', type: 101, name: 'Eye key', pic: 2553, xr: 32, yr: 32 },
    { group: 'Keys', type: 102, name: 'Fire key', pic: 2554, xr: 32, yr: 32 },
    { group: 'Keys', type: 103, name: 'Dagger key', pic: 2555, xr: 32, yr: 32 },
    { group: 'Keys', type: 104, name: 'Spider key', pic: 2556, xr: 32, yr: 32 },
    { group: 'Keys', type: 105, name: 'Moon key', pic: 2557, xr: 32, yr: 32 },
    // enemies: picture, size and flags as in Blood's own levels (status 6: an enemy)
    { group: 'Enemies', type: 201, name: 'Cultist (tommy gun)', pic: 2820, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 202, name: 'Cultist (shotgun)', pic: 2825, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 248, name: 'Cultist (TNT)', pic: 2820, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 247, name: 'Cultist (tesla)', pic: 2820, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 203, name: 'Axe zombie', pic: 1170, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 205, name: 'Axe zombie (in the ground)', pic: 3054, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 204, name: 'Butcher', pic: 1370, xr: 48, yr: 48, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 206, name: 'Flesh gargoyle', pic: 1470, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 207, name: 'Stone gargoyle', pic: 1470, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 210, name: 'Phantasm', pic: 3060, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 211, name: 'Hell hound', pic: 1270, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 212, name: 'Hand', pic: 1980, xr: 32, yr: 32, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 213, name: 'Brown spider', pic: 1920, xr: 16, yr: 16, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 214, name: 'Red spider', pic: 1925, xr: 24, yr: 24, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 215, name: 'Black spider', pic: 1935, xr: 32, yr: 32, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 216, name: 'Mother spider', pic: 1930, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 217, name: 'Gill beast', pic: 1570, xr: 48, yr: 48, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 218, name: 'Bone eel', pic: 1870, xr: 32, yr: 32, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 219, name: 'Bat', pic: 1948, xr: 32, yr: 32, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 220, name: 'Rat', pic: 1745, xr: 24, yr: 24, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 221, name: 'Green pod', pic: 1792, xr: 32, yr: 32, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 223, name: 'Fire pod', pic: 1792, xr: 48, yr: 48, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 227, name: 'Cerberus', pic: 2680, xr: 64, yr: 64, cstat: 384, stat: 6, enemy: true },
    { group: 'Enemies', type: 245, name: 'Innocent', pic: 3798, xr: 40, yr: 40, cstat: 384, stat: 6, enemy: true },
  ];
  const THING_BY_TYPE = {};
  for (const t of THINGS) THING_BY_TYPE[t.type] = t;

  // tiles Blood's own levels use most (numbers in Blood's TILES*.ART)
  const COMMON_TILES = {
    wall: [449, 556, 458, 568, 273, 448, 80, 20, 255],
    floor: [270, 287, 253, 448, 568, 255, 438, 274],
    ceiling: [568, 449, 255, 448, 556],
    sky: [3678, 2500],
    door: [5, 302, 2453, 495, 195, 22],
  };
  const KEY_NAMES = ['none', 'skull key', 'eye key', 'fire key', 'dagger key', 'spider key', 'moon key'];

  const Z_PER_UNIT = 16; // Build: 1 unit across = 16 units up and down
  const LIMITS = { sectors: 4096, walls: 16384, sprites: 16384, tiles: 30720, coord: 0x7ffff };

  function newRoom(pts) {
    return {
      pts: pts.map((p) => [p[0] | 0, p[1] | 0]),
      floor: 0,          // height of the floor, in map units (up is more)
      height: 2048,      // floor to ceiling, in map units
      floorPic: 270, ceilPic: 568, wallPic: 449,
      sky: false,        // open sky instead of a ceiling
      light: 0,          // 0 normal, up to 30 darker; negative brighter (Blood's shade)
      kind: 'room',      // 'room', 'door' (opens upwards), 'lift' (floor goes up)
      secret: false,     // walking in counts as a secret found
      doorPic: 302,      // door: the texture on its faces (the neighbours' walls towards it)
      speed: 10,         // door/lift: tenths of a second to open or rise
      wait: 30,          // door/lift: tenths of a second before it goes back (0: stays)
      key: 0,            // door: the key it needs (KEY_NAMES)
      rise: 512,         // lift: how far the floor goes up, in map units
    };
  }

  function newProject() {
    return { version: 2, name: 'MYMAP', rooms: [], things: [], visibility: 800, skyPic: 3678 };
  }

  // rooms from older projects get the fields added since
  function upgradeProject(prj) {
    const base = newRoom([]);
    prj.rooms.forEach((r) => { for (const k in base) if (r[k] === undefined && k !== 'pts') r[k] = base[k]; });
    if (prj.visibility === undefined) prj.visibility = 0;
    return prj;
  }

  // ---- geometry -------------------------------------------------------------------------------

  // shoelace sum: > 0 for a loop that goes clockwise on screen (y down), Build's order for a sector
  function area2(pts) {
    let a = 0;
    for (let i = 0; i < pts.length; i++) {
      const p = pts[i], q = pts[(i + 1) % pts.length];
      a += p[0] * q[1] - q[0] * p[1];
    }
    return a;
  }

  function cross(o, a, b) {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  }

  // the inside of segment ab and cd cross at a single point (touching ends or overlapping doesn't count)
  function properCross(a, b, c, d) {
    const d1 = cross(c, d, a), d2 = cross(c, d, b), d3 = cross(a, b, c), d4 = cross(a, b, d);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
  }

  function onSegment(p, a, b) { // p on segment ab, ends included
    return cross(a, b, p) === 0 && Math.min(a[0], b[0]) <= p[0] && p[0] <= Math.max(a[0], b[0])
      && Math.min(a[1], b[1]) <= p[1] && p[1] <= Math.max(a[1], b[1]);
  }

  // 1 inside, 0 on the edge, -1 outside
  function pointInPoly(p, pts) {
    let inside = false;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
      const a = pts[i], b = pts[j];
      if (onSegment(p, a, b)) return 0;
      if ((a[1] > p[1]) !== (b[1] > p[1])) {
        const x = a[0] + (p[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
        if (p[0] < x) inside = !inside;
      }
    }
    return inside ? 1 : -1;
  }

  function samePoint(a, b) { return a[0] === b[0] && a[1] === b[1]; }

  // tidy a drawn outline: drop repeated and straight-through corners, clockwise order
  function cleanPoly(pts) {
    let out = [];
    for (const p of pts) if (!out.length || !samePoint(out[out.length - 1], p)) out.push([p[0], p[1]]);
    if (out.length > 1 && samePoint(out[0], out[out.length - 1])) out.pop();
    let changed = true;
    while (changed && out.length >= 3) {
      changed = false;
      for (let i = 0; i < out.length; i++) {
        const a = out[(i + out.length - 1) % out.length], b = out[i], c = out[(i + 1) % out.length];
        if (cross(a, b, c) === 0 && (b[0] - a[0]) * (c[0] - b[0]) + (b[1] - a[1]) * (c[1] - b[1]) >= 0) {
          out.splice(i, 1);
          changed = true;
          break;
        }
      }
    }
    if (out.length >= 3 && area2(out) < 0) out.reverse();
    return out;
  }

  // why this outline can't be a room next to the others ("" when it can)
  function roomProblem(pts, rooms, skipIndex) {
    if (pts.length < 3 || area2(pts) === 0) return 'A room needs at least 3 corners and some area';
    for (const p of pts)
      if (Math.abs(p[0]) > LIMITS.coord || Math.abs(p[1]) > LIMITS.coord) return 'Too far from the middle of the map';
    const n = pts.length;
    for (let i = 0; i < n; i++)
      for (let j = i + 1; j < n; j++) {
        if (j === i + 1 || (i === 0 && j === n - 1)) continue;
        const a = pts[i], b = pts[(i + 1) % n], c = pts[j], d = pts[(j + 1) % n];
        if (properCross(a, b, c, d) || onSegment(c, a, b) || onSegment(a, c, d)) return 'The outline crosses itself';
      }
    for (let r = 0; r < rooms.length; r++) {
      if (r === skipIndex) continue;
      const q = rooms[r].pts;
      for (let i = 0; i < n; i++)
        for (let j = 0; j < q.length; j++)
          if (properCross(pts[i], pts[(i + 1) % n], q[j], q[(j + 1) % q.length])) return 'It overlaps another room';
      // a corner inside the other, or the middle of an edge (both outlines can be on the same lines)
      const probes = [];
      for (let i = 0; i < n; i++) {
        const a = pts[i], b = pts[(i + 1) % n];
        probes.push(a, [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2]);
      }
      if (probes.some((p) => pointInPoly(p, q) > 0)) return 'It overlaps another room';
      const back = [];
      for (let j = 0; j < q.length; j++) {
        const a = q[j], b = q[(j + 1) % q.length];
        back.push(a, [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2]);
      }
      if (back.some((p) => pointInPoly(p, pts) > 0)) return 'It overlaps another room';
      // the same outline twice: everything on the edges, a point inside both
      const c = centroidInside(pts);
      if (c && pointInPoly(c, q) > 0) return 'It overlaps another room';
    }
    return '';
  }

  // some point strictly inside the polygon
  function centroidInside(pts) {
    const n = pts.length;
    for (let i = 0; i < n; i++) {
      const a = pts[i], b = pts[(i + 1) % n], c = pts[(i + 2) % n];
      if (cross(a, b, c) <= 0) continue;
      const p = [(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3];
      if (pointInPoly(p, pts) > 0) return p;
    }
    let sx = 0, sy = 0;
    for (const p of pts) { sx += p[0]; sy += p[1]; }
    const p = [sx / n, sy / n];
    return pointInPoly(p, pts) > 0 ? p : null;
  }

  function roomAt(p, rooms) {
    for (let r = rooms.length - 1; r >= 0; r--) if (pointInPoly(p, rooms[r].pts) > 0) return r;
    return -1;
  }

  // ---- checks before writing --------------------------------------------------------------------

  // [{level: 'error'|'warning', text}]
  function checkProject(prj) {
    const out = [];
    const err = (t) => out.push({ level: 'error', text: t });
    const warn = (t) => out.push({ level: 'warning', text: t });
    if (!prj.rooms.length) err('Draw at least one room.');
    prj.rooms.forEach((r, i) => {
      const why = roomProblem(r.pts, prj.rooms, i);
      if (why) err('Room ' + (i + 1) + ': ' + why.toLowerCase() + '.');
      if (r.height < 64) err('Room ' + (i + 1) + ' is too low (at least 64 high).');
      const name = (r.kind === 'door' ? 'Door ' : r.kind === 'lift' ? 'Lift ' : 'Room ') + (i + 1);
      if (r.secret && r.kind !== 'room') err(name + ': only a normal room can be a secret (put the secret room behind the door).');
      if (r.kind === 'lift' && !(r.rise > 0 && r.rise < r.height - 64)) err(name + ': it has to rise by more than 0 and less than its height.');
      if (r.kind === 'door' && !(r.key >= 0 && r.key <= 6)) err(name + ': unknown key.');
      if (r.kind !== 'room' && !(r.speed > 0 && r.speed < 4096 && r.wait >= 0 && r.wait < 4096)) err(name + ': speed and wait have to be between 1 and 4095.');
    });
    if (prj.rooms.length && !roomProblemsAny(prj)) {
      const m = buildMap(prj, false);
      prj.rooms.forEach((r, i) => {
        if (r.kind === 'room') return;
        const touches = m.walls.some((w) => w.sector === i && w.nextsector >= 0);
        if (!touches) out.push({ level: 'warning', text: (r.kind === 'door' ? 'Door ' : 'Lift ') + (i + 1) + ' touches no other room, so nobody can get to it.' });
      });
    }
    let sp = 0, dm = 0;
    prj.things.forEach((t, i) => {
      if (!THING_BY_TYPE[t.type]) err('Thing ' + (i + 1) + ' has an unknown type.');
      if (roomAt([t.x, t.y], prj.rooms) < 0) err((THING_BY_TYPE[t.type] || { name: 'Thing' }).name + ' ' + (i + 1) + ' is not inside a room.');
      const tr = roomAt([t.x, t.y], prj.rooms);
      if (tr >= 0 && prj.rooms[tr].kind === 'door') warn((THING_BY_TYPE[t.type] || { name: 'Thing' }).name + ' ' + (i + 1) + ' is in a door, which is closed at the start.');
      if (t.type === 1) sp++;
      if (t.type === 2) dm++;
    });
    if (!sp) err('Put a single player start (Starts) in a room: the game starts there.');
    if (dm < 2) warn('Bloodbath needs bloodbath starts: put at least 2, better 8 (one per player).');
    if (sp > 1 && sp < 8) warn('Co-op uses one single player start per player (1st for player 1, 2nd for player 2, ...).');
    const n = buildMap(prj, true);
    if (n.sectors > LIMITS.sectors || n.walls > LIMITS.walls || n.sprites > LIMITS.sprites) err('The map is too big for the game.');
    return out;
  }

  function roomProblemsAny(prj) {
    return prj.rooms.some((r, i) => roomProblem(r.pts, prj.rooms, i));
  }

  // ---- the .MAP file ---------------------------------------------------------------------------

  const HEADER_NEW = 0x7474614d;

  class Out {
    constructor() { this.buf = new Uint8Array(1 << 16); this.n = 0; }
    grow(k) {
      if (this.n + k <= this.buf.length) return;
      let size = this.buf.length * 2;
      while (size < this.n + k) size *= 2;
      const b = new Uint8Array(size);
      b.set(this.buf.subarray(0, this.n));
      this.buf = b;
    }
    bytes(arr) { this.grow(arr.length); this.buf.set(arr, this.n); this.n += arr.length; }
    result() { return this.buf.slice(0, this.n); }
  }

  // a little-endian record built field by field
  class Rec {
    constructor(size) { this.b = new Uint8Array(size); this.v = new DataView(this.b.buffer); this.o = 0; }
    i8(x) { this.v.setInt8(this.o, x); this.o += 1; return this; }
    u8(x) { this.v.setUint8(this.o, x); this.o += 1; return this; }
    i16(x) { this.v.setInt16(this.o, x, true); this.o += 2; return this; }
    u16(x) { this.v.setUint16(this.o, x, true); this.o += 2; return this; }
    i32(x) { this.v.setInt32(this.o, x, true); this.o += 4; return this; }
    u32(x) { this.v.setUint32(this.o, x >>> 0, true); this.o += 4; return this; }
    done() { if (this.o !== this.b.length) throw new Error('record size ' + this.o + ' != ' + this.b.length); return this.b; }
  }

  function crypt(bytes, key) {
    const out = new Uint8Array(bytes.length);
    for (let i = 0; i < bytes.length; i++) out[i] = bytes[i] ^ ((key + i) & 0xff);
    return out;
  }

  // Blood's packed x-structures: fields written lowest bit first (BitWriter in common_game.h)
  function bitPack(size, fields) {
    const b = new Uint8Array(size);
    let pos = 0;
    for (const [value, bits] of fields) {
      const v = bits === 32 ? value >>> 0 : value & (Math.pow(2, bits) - 1); // negative numbers as two's complement
      for (let i = 0; i < bits; i++, pos++) {
        if (Math.floor(v / Math.pow(2, i)) % 2) b[pos >> 3] |= 1 << (pos & 7);
      }
    }
    return b;
  }

  function xsprite(nSprite, data1, x) {
    // up to data1 as dbSaveMap() writes them; everything after it stays 0
    x = x || {};
    return bitPack(56, [
      [nSprite, 14], [0, 1], [0, 17], [x.txID || 0, 10], [x.rxID || 0, 10], [x.command || 0, 8], [x.triggerOn || 0, 1], [0, 1], [0, 2], [0, 12], [0, 12], [0, 1], [0, 1],
      [0, 2], [0, 2], [0, 1], [0, 1], [0, 8], [0, 1], [0, 1], [0, 1], [0, 3], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1],
      [0, 1], [0, 2], [0, 5], [0, 1], [0, 1], [0, 1], [0, 1], [data1 & 0xffff, 16],
    ]);
  }

  // a sector's extra data (XSECTOR), in dbSaveMap()'s order; everything not given stays 0
  function xsector(nSector, x) {
    const f = (k, bits) => [x[k] || 0, bits];
    return bitPack(60, [
      [nSector, 14], f('state', 1), f('busy', 17), f('data', 16), f('txID', 10), f('busyWaveA', 3), f('busyWaveB', 3), f('rxID', 10),
      f('command', 8), f('triggerOn', 1), f('triggerOff', 1), f('busyTimeA', 12), f('waitTimeA', 12), f('restState', 1), f('interruptable', 1),
      f('amplitude', 8), f('freq', 8), f('reTriggerA', 1), f('reTriggerB', 1), f('phase', 8), f('wave', 4), f('shadeAlways', 1),
      f('shadeFloor', 1), f('shadeCeiling', 1), f('shadeWalls', 1), f('shade', 8), f('panAlways', 1), f('panFloor', 1), f('panCeiling', 1),
      f('Drag', 1), f('Underwater', 1), f('Depth', 3), f('panVel', 8), f('panAngle', 11), f('unused1', 1), f('decoupled', 1),
      f('triggerOnce', 1), f('isTriggered', 1), f('Key', 3), f('Push', 1), f('Vector', 1), f('Reserved', 1), f('Enter', 1), f('Exit', 1),
      f('Wallpush', 1), f('color', 1), f('unused2', 1), f('busyTimeB', 12), f('waitTimeB', 12), f('stopOn', 1), f('stopOff', 1),
      f('ceilpal', 4), f('offCeilZ', 32), f('onCeilZ', 32), f('offFloorZ', 32), f('onFloorZ', 32),
    ]);
  }

  // what makes a room a door, a lift or a secret (null: a plain room), and the heights it starts at
  function sectorExtra(r, i) {
    const floorz = -(r.floor * Z_PER_UNIT), ceilz = floorz - r.height * Z_PER_UNIT;
    if (r.kind === 'door') {
      // Blood's "Z motion" sector: shut (ceiling on the floor) until a player uses its walls
      return {
        type: 600, ceilz: floorz, floorz,
        x: { busyTimeA: r.speed, busyTimeB: r.speed, waitTimeA: r.wait, reTriggerA: r.wait ? 1 : 0, interruptable: 1,
          Wallpush: 1, Key: r.key | 0, offCeilZ: floorz, onCeilZ: ceilz, offFloorZ: floorz, onFloorZ: floorz },
      };
    }
    if (r.kind === 'lift') {
      // a Z motion sector whose floor goes up: use it while standing on it, or use its side from below
      const up = floorz - r.rise * Z_PER_UNIT;
      return {
        type: 600, ceilz, floorz,
        x: { busyTimeA: r.speed, busyTimeB: r.speed, waitTimeA: r.wait, reTriggerA: r.wait ? 1 : 0, interruptable: 1,
          Push: 1, Wallpush: 1, offCeilZ: ceilz, onCeilZ: ceilz, offFloorZ: floorz, onFloorZ: up },
      };
    }
    if (r.secret) {
      // like Blood's own: entering once sends "secret 0 found" (channel 2, command 64)
      return { type: 0, ceilz, floorz, x: { Enter: 1, triggerOn: 1, triggerOnce: 1, txID: 2, command: 64 } };
    }
    return { type: 0, ceilz, floorz, x: null };
  }

  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  function crc32(bytes, n) {
    let c = 0xffffffff;
    for (let i = 0; i < n; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  // sectors, walls and sprites as plain objects; with countOnly just their numbers
  function buildMap(prj, countOnly) {
    const rooms = prj.rooms;
    const allPts = [];
    for (const r of rooms) for (const p of r.pts) allPts.push(p);
    const sectors = [], walls = [];
    rooms.forEach((r, s) => {
      // every corner of another room on an edge splits it there
      const loop = [];
      const n = r.pts.length;
      for (let i = 0; i < n; i++) {
        const a = r.pts[i], b = r.pts[(i + 1) % n];
        loop.push(a);
        const mids = [];
        for (const p of allPts) {
          if (samePoint(p, a) || samePoint(p, b) || !onSegment(p, a, b)) continue;
          const t = Math.abs(b[0] - a[0]) >= Math.abs(b[1] - a[1]) ? (p[0] - a[0]) / (b[0] - a[0]) : (p[1] - a[1]) / (b[1] - a[1]);
          if (!mids.some((m) => samePoint(m.p, p))) mids.push({ p, t });
        }
        mids.sort((x, y) => x.t - y.t);
        for (const m of mids) loop.push(m.p);
      }
      const first = walls.length;
      for (let i = 0; i < loop.length; i++) walls.push({ x: loop[i][0], y: loop[i][1], point2: first + (i + 1) % loop.length, sector: s, nextwall: -1, nextsector: -1 });
      sectors.push({ wallptr: first, wallnum: loop.length, room: r });
    });
    if (countOnly) return { sectors: sectors.length, walls: walls.length, sprites: prj.things.length };
    // join matching edges of neighbouring rooms
    const key = (w) => w.x + ',' + w.y + ',' + walls[w.point2].x + ',' + walls[w.point2].y;
    const byEdge = new Map();
    walls.forEach((w, i) => byEdge.set(key(w), i));
    walls.forEach((w, i) => {
      const o = walls[w.point2];
      const j = byEdge.get(o.x + ',' + o.y + ',' + w.x + ',' + w.y);
      if (j !== undefined && walls[j].sector !== w.sector) {
        w.nextwall = j;
        w.nextsector = walls[j].sector;
      }
    });
    const sprites = prj.things.map((t, i) => {
      const info = THING_BY_TYPE[t.type];
      const s = roomAt([t.x, t.y], rooms);
      const r = rooms[s];
      return { t, info, sector: s, z: r ? -(r.floor * Z_PER_UNIT) - ((info && info.dz) || 0) : 0 };
    });
    return { sectors, walls, sprites };
  }

  // the .MAP bytes (Uint8Array); throws an Error saying what's wrong when it can't
  function exportMap(prj) {
    const problems = checkProject(prj).filter((p) => p.level === 'error');
    if (problems.length) throw new Error(problems.map((p) => p.text).join('\n'));
    const { sectors, walls, sprites } = buildMap(prj, false);
    const rev = 1;
    const out = new Out();
    // the level's number of secrets: an invisible sprite that tells the game at the start
    // (listens on channel 7, "level start", and sends "64 + count" to channel 1, like Blood's maps)
    const nSecrets = prj.rooms.filter((r) => r.secret).length;
    const counter = nSecrets ? sprites.find((s) => s.t.type === 1) || sprites[0] : null;
    const nSprites = sprites.length + (counter ? 1 : 0);
    out.bytes([0x42, 0x4c, 0x4d, 0x1a, 0x00, 0x07]); // "BLM\x1a", version 0x700
    // where the game puts a player when the map has no start (the first single player start)
    const start = sprites.find((s) => s.t.type === 1) || sprites[0];
    const head = new Rec(37)
      .i32(start ? start.t.x : 0).i32(start ? start.t.y : 0).i32(start ? start.z : 0)
      .i16(start ? (start.t.ang || 0) & 2047 : 0).i16(start ? start.sector : 0)
      .i16(4)                           // sky: 16 tiles wide
      .i32(prj.visibility | 0)
      .u32(HEADER_NEW)                  // "song id": marks an encrypted v7 map
      .u8(2)                            // parallax type
      .i32(rev)
      .i16(sectors.length).i16(walls.length).i16(nSprites).done();
    out.bytes(crypt(head, HEADER_NEW));
    const head2 = new Uint8Array(128);
    const name = 'NotBlood Chosen map editor';
    for (let i = 0; i < name.length; i++) head2[i] = name.charCodeAt(i);
    const hv = new DataView(head2.buffer);
    hv.setInt32(0x40, 56, true); // x-sprite size
    hv.setInt32(0x44, 24, true); // x-wall size
    hv.setInt32(0x48, 60, true); // x-sector size
    out.bytes(crypt(head2, walls.length));
    const sky = new Rec(32);
    for (let i = 0; i < 16; i++) sky.i16(i);
    out.bytes(crypt(sky.done(), 32));

    let nExtra = 0;
    sectors.forEach((s, i) => {
      const r = s.room;
      const ex = sectorExtra(r, i), floorz = ex.floorz, ceilz = ex.ceilz;
      const shade = Math.max(-128, Math.min(127, r.light | 0));
      const rec = new Rec(40)
        .i16(s.wallptr).i16(s.wallnum).i32(ceilz).i32(floorz)
        .u16(r.sky ? 1 : 0).u16(0)                          // ceiling and floor stat (1: parallax sky)
        .i16(r.sky ? prj.skyPic : r.ceilPic).i16(0).i8(shade).u8(0).u8(0).u8(0)
        .i16(r.floorPic).i16(0).i8(shade).u8(0).u8(0).u8(0)
        .u8(0).u8(0)                                        // visibility, fogpal
        .i16(ex.type).i16(0).i16(ex.x ? ++nExtra : -1);     // type, hitag, extra
      out.bytes(crypt(rec.done(), rev * 40));
      if (ex.x) out.bytes(xsector(i, ex.x));
    });
    walls.forEach((w) => {
      const r = sectors[w.sector].room;
      const o = walls[w.point2];
      const len = Math.hypot(o.x - w.x, o.y - w.y);
      // the face of a door is the wall of the room in front of it
      const next = w.nextsector >= 0 ? sectors[w.nextsector].room : null;
      const pic = next && next.kind === 'door' ? next.doorPic : r.wallPic;
      const rec = new Rec(32)
        .i32(w.x).i32(w.y).i16(w.point2).i16(w.nextwall).i16(w.nextsector)
        .u16(0)                                             // cstat
        .i16(pic).i16(pic)                                  // picnum, overpicnum
        .i8(Math.max(-128, Math.min(127, r.light | 0))).u8(0)
        .u8(Math.max(1, Math.min(255, Math.round(len / 128)))).u8(8) // x and y repeat
        .u8(0).u8(0)                                        // panning
        .i16(0).i16(0).i16(-1);                             // type, hitag, extra (none)
      out.bytes(crypt(rec.done(), (rev * 40) | HEADER_NEW));
    });
    const startNumber = { 1: 0, 2: 0 };
    sprites.forEach((s, i) => {
      const t = s.t, info = s.info;
      let pic = info.pic, data1 = 0;
      if (info.start) {
        data1 = startNumber[t.type]++ & 7; // which player starts here
        pic = 2522 + data1;
      }
      const rec = new Rec(44)
        .i32(t.x).i32(t.y).i32(s.z)
        .u16(info.cstat != null ? info.cstat : 128).i16(pic).i8(info.start ? 0 : -8).u8(0)
        .u8(32).u8(0)                                       // clipdist, blend
        .u8(info.xr || 48).u8(info.yr || 48).i8(0).i8(0)
        .i16(s.sector).i16(info.stat != null ? info.stat : 3).i16((t.ang || 0) & 2047)
        .i16(-1).i16(0).i16(0).i16(0)                       // owner, velocities
        .i16(t.type).i16(0).i16(i + 1);                     // type, flags, extra
      out.bytes(crypt(rec.done(), (rev * 44) | HEADER_NEW));
      out.bytes(xsprite(i, data1));
    });
    if (counter) {
      const rec = new Rec(44)
        .i32(counter.t.x).i32(counter.t.y).i32(counter.z)
        .u16(0x8000).i16(0).i8(0).u8(0).u8(0).u8(0)         // invisible
        .u8(16).u8(16).i8(0).i8(0)
        .i16(counter.sector).i16(0).i16(0)
        .i16(-1).i16(0).i16(0).i16(0)
        .i16(0).i16(0).i16(sprites.length + 1);
      out.bytes(crypt(rec.done(), (rev * 44) | HEADER_NEW));
      out.bytes(xsprite(sprites.length, 0, { rxID: 7, txID: 1, command: 64 + Math.min(nSecrets, 191), triggerOn: 1 }));
    }
    const body = out.result();
    const crc = crc32(body, body.length);
    const full = new Uint8Array(body.length + 4);
    full.set(body);
    new DataView(full.buffer).setUint32(body.length, crc, true);
    return full;
  }

  // file name the game accepts: letters, digits, _ and -, at most 8 characters like Blood's own
  function mapFileName(name) {
    const s = String(name || '').toUpperCase().replace(/[^A-Z0-9_-]/g, '').slice(0, 8);
    return (s || 'MYMAP') + '.MAP';
  }

  const VERSION = '0.3';

  const api = {
    VERSION, THINGS, KEY_NAMES, upgradeProject, sectorExtra, THING_BY_TYPE, COMMON_TILES, Z_PER_UNIT, LIMITS,
    newRoom, newProject, cleanPoly, roomProblem, pointInPoly, roomAt, area2, centroidInside,
    checkProject, buildMap, exportMap, mapFileName, crc32,
  };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.MapCore = api;
})(typeof window !== 'undefined' ? window : this);
