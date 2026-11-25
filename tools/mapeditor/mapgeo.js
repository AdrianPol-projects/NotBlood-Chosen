// Blood Map Maker: geometry of a full Blood map (MapFile's model) and the edits on it.
//
// Build keeps a sector's walls together (wallptr, wallnum), each wall pointing at the next corner
// (point2) and, for an opening, at the wall on the other side (nextwall, nextsector). Edits are done
// on a graph of objects (sectors with loops of walls, walls pointing at walls) and written back to
// those numbers, so nothing gets out of step. A sector has one outer loop and any number of inner
// loops (pillars, or the edges of sectors inside it).
(function (root) {
  'use strict';

  // ---- reading the geometry ---------------------------------------------------------------------
  function loopsOf(map, s) {
    const sec = map.sectors[s], loops = [], seen = new Set();
    for (let w = sec.wallptr; w < sec.wallptr + sec.wallnum; w++) {
      if (seen.has(w)) continue;
      const loop = [];
      let i = w, guard = 0;
      while (!seen.has(i) && guard++ < 65536) {
        seen.add(i); loop.push(i);
        i = map.walls[i].point2;
        if (i < sec.wallptr || i >= sec.wallptr + sec.wallnum) break;
      }
      loops.push(loop);
    }
    return loops;
  }

  function area2(pts) {
    let a = 0;
    for (let i = 0; i < pts.length; i++) { const p = pts[i], q = pts[(i + 1) % pts.length]; a += p[0] * q[1] - q[0] * p[1]; }
    return a;
  }

  // like Build's inside(): crossings over every wall of the sector, so holes count
  function inside(map, s, x, y) {
    const sec = map.sectors[s];
    let c = false;
    for (let w = sec.wallptr; w < sec.wallptr + sec.wallnum; w++) {
      const a = map.walls[w], b = map.walls[a.point2];
      if (!b) continue;
      if ((a.y < y) !== (b.y < y) && a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y) < x) c = !c;
    }
    return c;
  }
  function sectorAt(map, x, y, hint) {
    if (hint >= 0 && hint < map.sectors.length && inside(map, hint, x, y)) return hint;
    if (hint >= 0 && hint < map.sectors.length) {
      const sec = map.sectors[hint];
      for (let w = sec.wallptr; w < sec.wallptr + sec.wallnum; w++) {
        const n = map.walls[w].nextsector;
        if (n >= 0 && inside(map, n, x, y)) return n;
      }
    }
    for (let s = 0; s < map.sectors.length; s++) if (inside(map, s, x, y)) return s;
    return -1;
  }

  // floor and ceiling height at a point (Build's getflorzofslope/getcielzofslope), in z units (down is more)
  function slopeZ(map, s, x, y, ceil) {
    const sec = map.sectors[s];
    const z = ceil ? sec.ceilingz : sec.floorz;
    if (!((ceil ? sec.ceilingstat : sec.floorstat) & 2)) return z;
    const heinum = ceil ? sec.ceilingheinum : sec.floorheinum;
    if (!heinum) return z;
    const a = map.walls[sec.wallptr], b = map.walls[a.point2];
    const dx = b.x - a.x, dy = b.y - a.y;
    const len = Math.sqrt(dx * dx + dy * dy) * 32;
    if (!len) return z;
    const j = (dx * (y - a.y) - dy * (x - a.x)) / 8;
    return z + heinum * j / len;
  }

  // ---- triangles for a sector (holes joined into the outline first) -----------------------------
  function triangulateSector(map, s) {
    const loops = loopsOf(map, s).map((l) => l.map((w) => [map.walls[w].x, map.walls[w].y]));
    const outers = [], holes = [];
    for (const l of loops) { if (l.length < 3) continue; (area2(l) > 0 ? outers : holes).push(l); }
    const tris = [];
    for (const outer of outers) {
      let poly = outer.slice();
      // the holes inside this outline, rightmost first, each joined by a cut to a visible corner
      const mine = holes.filter((h) => pointInPoly(h[0], outer)).sort((a, b) => Math.max(...b.map((p) => p[0])) - Math.max(...a.map((p) => p[0])));
      for (const h of mine) poly = bridge(poly, h);
      tris.push(...earClip(poly));
    }
    return tris; // [[x,y],[x,y],[x,y]]...
  }
  function pointInPoly(p, pts) {
    let c = false;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
      const a = pts[i], b = pts[j];
      if ((a[1] > p[1]) !== (b[1] > p[1]) && p[0] < a[0] + (p[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1])) c = !c;
    }
    return c;
  }
  function segCross(a, b, c, d) {
    const cr = (o, u, v) => (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0]);
    const d1 = cr(c, d, a), d2 = cr(c, d, b), d3 = cr(a, b, c), d4 = cr(a, b, d);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
  }
  function bridge(poly, hole) {
    // the hole's rightmost corner, joined to the nearest outline corner the cut doesn't cross anything on the way
    let hi = 0;
    hole.forEach((p, i) => { if (p[0] > hole[hi][0]) hi = i; });
    const h = hole[hi];
    let best = -1, bd = Infinity;
    for (let i = 0; i < poly.length; i++) {
      const p = poly[i], d = (p[0] - h[0]) ** 2 + (p[1] - h[1]) ** 2;
      if (d >= bd) continue;
      let ok = true;
      for (let k = 0; k < poly.length && ok; k++) { const a = poly[k], b = poly[(k + 1) % poly.length]; if (segCross(h, p, a, b)) ok = false; }
      for (let k = 0; k < hole.length && ok; k++) { const a = hole[k], b = hole[(k + 1) % hole.length]; if (segCross(h, p, a, b)) ok = false; }
      if (ok) { bd = d; best = i; }
    }
    if (best < 0) return poly; // shouldn't happen; leave the hole out
    const ring = hole.slice(hi).concat(hole.slice(0, hi)); // starts at h; holes go the other way round already
    return poly.slice(0, best + 1).concat(ring, [h], poly.slice(best));
  }
  function earClip(pts) {
    const idx = pts.map((_, i) => i), tris = [];
    const cross = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    const inTri = (p, a, b, c) => cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0;
    let guard = 0;
    while (idx.length > 3 && guard++ < 100000) {
      let cut = false;
      for (let k = 0; k < idx.length; k++) {
        const ia = idx[(k + idx.length - 1) % idx.length], ib = idx[k], ic = idx[(k + 1) % idx.length];
        const a = pts[ia], b = pts[ib], c = pts[ic];
        if (cross(a, b, c) <= 0) continue;
        let ok = true;
        for (const j of idx) {
          if (j === ia || j === ib || j === ic) continue;
          const p = pts[j];
          if ((p[0] === a[0] && p[1] === a[1]) || (p[0] === b[0] && p[1] === b[1]) || (p[0] === c[0] && p[1] === c[1])) continue;
          if (inTri(p, a, b, c)) { ok = false; break; }
        }
        if (!ok) continue;
        tris.push([a, b, c]);
        idx.splice(k, 1);
        cut = true;
        break;
      }
      if (!cut) { // a degenerate corner: drop the flattest one and go on
        let worst = 0, wv = Infinity;
        for (let k = 0; k < idx.length; k++) {
          const v = Math.abs(cross(pts[idx[(k + idx.length - 1) % idx.length]], pts[idx[k]], pts[idx[(k + 1) % idx.length]]));
          if (v < wv) { wv = v; worst = k; }
        }
        idx.splice(worst, 1);
      }
    }
    if (idx.length === 3) tris.push([pts[idx[0]], pts[idx[1]], pts[idx[2]]]);
    return tris;
  }

  // ---- the graph -------------------------------------------------------------------------------------
  // sectors: { f: sector fields (without wallptr/wallnum), loops: [[wall]] }
  // walls: { f: wall fields, next: wall (point2), other: wall|null (nextwall), sector }
  // sprites: { f: sprite fields, sector }
  function toGraph(map) {
    const W = map.walls.map((w) => ({ f: w, next: null, other: null, sector: null }));
    W.forEach((o, i) => { o.next = W[map.walls[i].point2] || null; o.other = map.walls[i].nextwall >= 0 ? W[map.walls[i].nextwall] || null : null; });
    const S = map.sectors.map((s, i) => ({ f: s, loops: [], order: [] }));
    S.forEach((o, i) => {
      o.loops = loopsOf(map, i).map((l) => l.map((w) => W[w]));
      // the walls as the file has them; kept for sectors an edit doesn't change, so their numbers stay
      for (let w = s0(map, i); w < s0(map, i) + map.sectors[i].wallnum; w++) o.order.push(W[w]);
      for (const l of o.loops) for (const w of l) w.sector = o;
    });
    // what each wall pointed at in the file (some maps have a stale nextsector; kept while the link stays)
    W.forEach((o, i) => { o.origOther = o.other; o.origSector = map.walls[i].nextsector >= 0 ? S[map.walls[i].nextsector] || null : null; });
    const P = map.sprites.map((p, i) => ({ f: p, sector: S[p.sectnum] || null, old: i }));
    S.forEach((o, i) => { o.old = i; });
    return { map, sectors: S, sprites: P };
  }
  function s0(map, i) { return map.sectors[i].wallptr; }
  function fromGraph(g) {
    const map = g.map;
    const walls = [], sectors = [], sIndex = new Map(), wIndex = new Map();
    g.sectors.forEach((s, i) => sIndex.set(s, i));
    for (const s of g.sectors) {
      const first = walls.length;
      const flat = s.loops.flat();
      const same = s.order && s.order.length === flat.length && flat.every((w) => s.order.includes(w));
      for (const w of same ? s.order : flat) { wIndex.set(w, walls.length); walls.push(w); }
      s.f.wallptr = first; s.f.wallnum = walls.length - first;
      sectors.push(s.f);
    }
    map.walls = walls.map((w) => {
      // point2 is the next corner of the same loop
      w.f.point2 = wIndex.get(w.next);
      w.f.nextwall = w.other && wIndex.has(w.other) ? wIndex.get(w.other) : -1;
      w.f.nextsector = w.f.nextwall < 0 ? -1 : w.other === w.origOther && w.origSector && sIndex.has(w.origSector) ? sIndex.get(w.origSector) : sIndex.get(w.other.sector);
      return w.f;
    });
    map.sectors = sectors;
    const kept = g.sprites.filter((p) => p.sector && sIndex.has(p.sector));
    map.sprites = kept.map((p) => { p.f.sectnum = sIndex.get(p.sector); return p.f; });
    // numbers that point at sprites and sectors follow their new places
    const spriteMap = new Map(), sectorMap = new Map();
    kept.forEach((p, i) => { if (p.old !== undefined) spriteMap.set(p.old, i); });
    g.sectors.forEach((o, i) => { if (o.old !== undefined) sectorMap.set(o.old, i); });
    renumber(map, spriteMap, sectorMap);
    return map;
  }
  // Sliding and rotating sectors name their marker sprites by number (XSECTOR marker0/1, 65535 for
  // none), and the markers (types 3-8) name their sector in "owner". After sprites or sectors move
  // to other numbers, these follow (a deleted one becomes "none").
  const NONE16 = 65535;
  function isMarker(sp) { return sp.type >= 3 && sp.type <= 8; }
  function renumber(map, spriteMap, sectorMap) {
    const sameSprites = [...spriteMap].every(([a, b]) => a === b), sameSectors = [...sectorMap].every(([a, b]) => a === b);
    if (sameSprites && sameSectors && spriteMap.size === map.sprites.length) return;
    for (const sec of map.sectors) {
      if (!sec.ext) continue;
      for (const k of ['marker0', 'marker1']) {
        const v = sec.ext[k];
        if (v === NONE16 || v === undefined) continue;
        sec.ext[k] = spriteMap.has(v) ? spriteMap.get(v) : (v === 0 && !spriteMap.has(0) ? 0 : NONE16);
      }
    }
    for (const sp of map.sprites) if (isMarker(sp) && sp.owner >= 0) sp.owner = sectorMap.has(sp.owner) ? sectorMap.get(sp.owner) : -1;
  }
  function deleteSprite(map, i) {
    const n = map.sprites.length;
    map.sprites.splice(i, 1);
    const spriteMap = new Map(), sectorMap = new Map();
    for (let k = 0; k < n; k++) if (k !== i) spriteMap.set(k, k < i ? k : k - 1);
    map.sectors.forEach((_, k) => sectorMap.set(k, k));
    renumber(map, spriteMap, sectorMap);
  }

  function relink(loop) { loop.forEach((w, i) => { w.next = loop[(i + 1) % loop.length]; }); }

  // ---- edits -----------------------------------------------------------------------------------------
  const DEFAULT_SECTOR = { ceilingz: -32768, floorz: 0, ceilingstat: 0, floorstat: 0, ceilingpicnum: 568, ceilingheinum: 0, ceilingshade: 0, ceilingpal: 0,
    ceilingxpanning: 0, ceilingypanning: 0, floorpicnum: 270, floorheinum: 0, floorshade: 0, floorpal: 0, floorxpanning: 0, floorypanning: 0,
    visibility: 0, fogpal: 0, type: 0, hitag: 0, extra: -1 };
  const DEFAULT_WALL = { cstat: 0, picnum: 449, overpicnum: 449, shade: 0, pal: 0, xrepeat: 8, yrepeat: 8, xpanning: 0, ypanning: 0, type: 0, hitag: 0, extra: -1 };

  function onSegment(p, a, b) {
    const cr = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
    return cr === 0 && Math.min(a[0], b[0]) <= p[0] && p[0] <= Math.max(a[0], b[0]) && Math.min(a[1], b[1]) <= p[1] && p[1] <= Math.max(a[1], b[1]);
  }
  function fixRepeat(w) {
    const n = w.next, len = Math.hypot(n.f.x - w.f.x, n.f.y - w.f.y);
    w.f.xrepeat = Math.max(1, Math.min(255, Math.round(len / 128)));
  }
  // put a corner at p into wall w (and the wall on its other side)
  function splitAt(g, w, p) {
    const loop = w.sector.loops.find((l) => l.includes(w));
    const nw = { f: { ...w.f, x: p[0], y: p[1], extra: -1 }, next: null, other: null, sector: w.sector };
    delete nw.f.ext;
    loop.splice(loop.indexOf(w) + 1, 0, nw);
    relink(loop);
    if (w.other) {
      const o = w.other, oloop = o.sector.loops.find((l) => l.includes(o));
      const no = { f: { ...o.f, x: p[0], y: p[1], extra: -1 }, next: null, other: null, sector: o.sector };
      delete no.f.ext;
      oloop.splice(oloop.indexOf(o) + 1, 0, no);
      relink(oloop);
      // w: a->p, nw: p->b ; o: b->p, no: p->a
      w.other = no; no.other = w; nw.other = o; o.other = nw;
    }
    fixRepeat(w); fixRepeat(nw);
    return nw;
  }

  // why a new sector with this outline can't go in ("" when it can), and which sector it's inside (-1: none)
  function newSectorPlace(map, pts) {
    if (pts.length < 3 || area2(pts) === 0) return { why: 'A sector needs at least 3 corners and some area' };
    const n = pts.length;
    for (let i = 0; i < n; i++) for (let j = i + 1; j < n; j++) {
      if (j === i + 1 || (i === 0 && j === n - 1)) continue;
      if (segCross(pts[i], pts[(i + 1) % n], pts[j], pts[(j + 1) % n])) return { why: 'The outline crosses itself' };
    }
    // no wall of the map may cross the new outline
    for (let w = 0; w < map.walls.length; w++) {
      const a = map.walls[w], b = map.walls[a.point2];
      for (let i = 0; i < n; i++) if (segCross([a.x, a.y], [b.x, b.y], pts[i], pts[(i + 1) % n])) return { why: 'It crosses an existing wall' };
    }
    // inside a sector, or outside all of them (probe the middle of each edge, a little inwards)
    const probes = [];
    for (let i = 0; i < n; i++) {
      const a = pts[i], b = pts[(i + 1) % n], mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2;
      const len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
      // clockwise on screen: the inside is to the right of a->b, i.e. (-dy, dx)
      probes.push([mx - (b[1] - a[1]) / len * 2, my + (b[0] - a[0]) / len * 2]);
    }
    const holders = new Set(probes.map((p) => sectorAt(map, p[0], p[1], -1)));
    if (holders.size > 1) return { why: 'It overlaps more than one sector' };
    const host = [...holders][0];
    // an existing sector completely inside the new one is not allowed either
    for (let s = 0; s < map.sectors.length; s++) {
      if (s === host) continue;
      const sec = map.sectors[s];
      for (let w = sec.wallptr; w < sec.wallptr + sec.wallnum; w++) {
        const c = map.walls[w];
        if (pointInPoly([c.x, c.y], pts) && !onAnyEdge([c.x, c.y], pts)) return { why: 'Another sector would be inside it' };
      }
    }
    return { why: '', host };
  }
  function onAnyEdge(p, pts) { for (let i = 0; i < pts.length; i++) if (onSegment(p, pts[i], pts[(i + 1) % pts.length])) return true; return false; }

  // a new sector; it opens onto every wall of other sectors that lies along its outline, and inside
  // another sector it becomes a sector within it (the outer one gets an inner loop around it)
  function addSector(map, ptsIn, fields, wallFields) {
    let pts = ptsIn.map((p) => [Math.round(p[0]), Math.round(p[1])]);
    if (area2(pts) < 0) pts = pts.reverse();
    const place = newSectorPlace(map, pts);
    if (place.why) throw new Error(place.why);
    const g = toGraph(map);
    const sec = { f: { ...DEFAULT_SECTOR, ...(fields || {}) }, loops: [] };
    delete sec.f.ext; sec.f.extra = -1;
    const loop = pts.map((p) => ({ f: { ...DEFAULT_WALL, ...(wallFields || {}), x: p[0], y: p[1] }, next: null, other: null, sector: sec }));
    relink(loop);
    sec.loops.push(loop);
    g.sectors.push(sec);
    if (place.host >= 0) {
      const host = g.sectors[place.host];
      const inner = pts.slice().reverse().map((p) => ({ f: { ...DEFAULT_WALL, ...(wallFields || {}), picnum: host.loops[0][0].f.picnum, overpicnum: host.loops[0][0].f.picnum, x: p[0], y: p[1] }, next: null, other: null, sector: host }));
      relink(inner);
      host.loops.push(inner);
    }
    // split walls where corners of the other side lie on them, then join what matches
    const all = () => g.sectors.flatMap((s) => s.loops.flat());
    let changed = true, guard = 0;
    while (changed && guard++ < 1000) {
      changed = false;
      const walls = all();
      const corners = walls.map((w) => [w.f.x, w.f.y]);
      for (const w of walls) {
        if (w.other) continue;
        const a = [w.f.x, w.f.y], b = [w.next.f.x, w.next.f.y];
        const mid = corners.find((p) => !(p[0] === a[0] && p[1] === a[1]) && !(p[0] === b[0] && p[1] === b[1]) && onSegment(p, a, b));
        if (mid && (loop.includes(w) || walls.some((o) => loop.includes(o) && ((o.f.x === mid[0] && o.f.y === mid[1]))))) { splitAt(g, w, mid); changed = true; break; }
      }
    }
    const byEdge = new Map();
    for (const w of all()) if (!w.other) byEdge.set(w.f.x + ',' + w.f.y + ',' + w.next.f.x + ',' + w.next.f.y, w);
    for (const w of all()) {
      if (w.other || w.sector !== sec) continue;
      const o = byEdge.get(w.next.f.x + ',' + w.next.f.y + ',' + w.f.x + ',' + w.f.y);
      if (o && o.sector !== sec && !o.other) { w.other = o; o.other = w; }
    }
    for (const w of sec.loops[0]) fixRepeat(w);
    fromGraph(g);
    return map.sectors.length - 1;
  }

  // remove a sector and its sprites; the walls that opened onto it become solid
  function deleteSector(map, s) {
    const g = toGraph(map);
    const sec = g.sectors[s];
    for (const l of sec.loops) for (const w of l) if (w.other) { w.other.other = null; }
    g.sectors.splice(s, 1);
    g.sprites = g.sprites.filter((p) => p.sector !== sec);
    fromGraph(g);
  }

  function splitWall(map, w, p) {
    const g = toGraph(map);
    const wall = g.sectors.flatMap((s) => s.loops.flat()).find((o) => o.f === map.walls[w]);
    if (!p) p = [Math.round((wall.f.x + wall.next.f.x) / 2), Math.round((wall.f.y + wall.next.f.y) / 2)];
    splitAt(g, wall, p);
    fromGraph(g);
  }

  // move a corner: every wall starting there (both sides of openings) moves with it
  function moveCorner(map, x, y, nx, ny) {
    for (const w of map.walls) if (w.x === x && w.y === y) { w.x = nx; w.y = ny; }
  }

  // remove a corner (if its loops keep at least 3 corners); an opening through it closes up
  function deleteCorner(map, x, y) {
    const g = toGraph(map);
    const at = g.sectors.flatMap((s) => s.loops.flat()).filter((w) => w.f.x === x && w.f.y === y);
    for (const w of at) {
      const loop = w.sector.loops.find((l) => l.includes(w));
      if (loop.length <= 3) throw new Error('A loop of walls needs at least 3 corners');
    }
    for (const w of at) {
      const loop = w.sector.loops.find((l) => l.includes(w));
      const prev = loop[(loop.indexOf(w) + loop.length - 1) % loop.length];
      // prev (a->x) and w (x->b) become one wall a->b; the opening of either is lost
      if (prev.other) { prev.other.other = null; prev.other = null; }
      if (w.other) { w.other.other = null; w.other = null; }
      loop.splice(loop.indexOf(w), 1);
      relink(loop);
      fixRepeat(prev);
    }
    // what's now the same edge on both sides opens again
    const byEdge = new Map();
    const all = g.sectors.flatMap((s) => s.loops.flat());
    for (const w of all) if (!w.other) byEdge.set(w.f.x + ',' + w.f.y + ',' + w.next.f.x + ',' + w.next.f.y, w);
    for (const w of all) {
      if (w.other) continue;
      const o = byEdge.get(w.next.f.x + ',' + w.next.f.y + ',' + w.f.x + ',' + w.f.y);
      if (o && o.sector !== w.sector && !o.other) { w.other = o; o.other = w; }
    }
    fromGraph(g);
  }

  // what's wrong with the map's structure (the game refuses such a map, or would crash)
  function problems(map, warnings) {
    const out = [];
    warnings = warnings || [];
    const ns = map.sectors.length, nw = map.walls.length;
    if (!ns) out.push('The map has no sectors.');
    map.sectors.forEach((s, i) => { if (s.wallptr < 0 || s.wallnum < 3 || s.wallptr + s.wallnum > nw) out.push('Sector ' + i + ' has broken walls.'); });
    map.walls.forEach((w, i) => {
      if (!(w.point2 >= 0 && w.point2 < nw)) out.push('Wall ' + i + ' has no next corner.');
      if (w.nextwall >= nw || w.nextsector >= ns) out.push('Wall ' + i + ' opens onto nothing.');
      if (w.nextwall >= 0 && map.walls[w.nextwall] && map.walls[w.nextwall].nextwall !== i) warnings.push('Wall ' + i + ' and its other side don\'t point at each other (the game copes).');
      if (w.picnum < 0 || w.picnum >= 30720) out.push('Wall ' + i + ' has a bad tile.');
    });
    map.sprites.forEach((p, i) => { if (p.sectnum < 0 || p.sectnum >= ns) out.push('Sprite ' + i + ' is in no sector.'); });
    // sliding/rotating sectors whose marker sprites are gone or aren't markers
    map.sectors.forEach((s, i) => {
      if (!s.ext || ![604, 613, 614, 615, 616, 617].includes(s.type)) return;
      for (const k of ['marker0', 'marker1']) {
        const v = s.ext[k];
        if (k === 'marker1' && (s.type === 604 || s.type === 613 || s.type === 615 || s.type === 617)) continue; // rotating ones use one marker
        if (v === NONE16 || v >= map.sprites.length || !isMarker(map.sprites[v])) warnings.push('Sector ' + i + ' (' + (s.type === 604 ? 'teleporter' : 'sliding/rotating door') + ') lost its ' + k + ' sprite.');
      }
    });
    if (map.sectors.length > 4096 || map.walls.length > 16384 || map.sprites.length > 16384) out.push('The map is too big for the game.');
    return out;
  }

  // a map with nothing in it yet (version 7, like the game saves)
  function emptyMap() {
    const header2 = new Array(128).fill(0);
    'Blood Map Maker'.split('').forEach((ch, i) => { header2[i] = ch.charCodeAt(0); });
    return { version: 0x700, encrypted: true, header: { startX: 0, startY: 0, startZ: 0, startAng: 0, startSector: 0, skyBits: 4, visibility: 800,
      songId: 0x7474614d, parallaxType: 2, mapRev: 1, numSectors: 0, numWalls: 0, numSprites: 0 }, header2, xsize: { sprite: 56, wall: 24, sector: 60 },
      skyOffsets: Array.from({ length: 16 }, (_, i) => i), sectors: [], walls: [], sprites: [], crcOk: true, trailing: null };
  }

  root.MapGeo = { emptyMap, loopsOf, inside, sectorAt, slopeZ, triangulateSector, area2, pointInPoly, toGraph, fromGraph,
    addSector, deleteSector, deleteSprite, splitWall, moveCorner, deleteCorner, newSectorPlace, problems, DEFAULT_SECTOR, DEFAULT_WALL };
  if (typeof module !== 'undefined' && module.exports) module.exports = root.MapGeo;
})(typeof window !== 'undefined' ? window : this);
