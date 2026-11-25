// Blood Map Maker: 3D view and walkthrough (WebGL 1, no libraries).
//
// The level is drawn from the same sectors and walls the .MAP export writes (MapCore.buildMap).
// With the player's Blood files loaded (gamefiles.js) it looks like the game: Blood's tiles,
// mapped the way Build maps them (walls: xrepeat/yrepeat, floors: 16 map units per pixel), Blood's
// palette and its shading table (room light plus darkness with distance), sprites for items and
// enemies, and the parallax sky. Without them every tile number gets a colour of its own.
//
// Walking uses rough Blood-like player sizes (PLAYER). Doors and lifts work (Use), keys are picked
// up, and entering a secret room says so; enemies stand still. A quick check of a map, not the
// game's exact physics.
(function (root) {
  'use strict';
  const M = root.MapCore;
  const G = () => root.GameFiles;

  const PLAYER = { radius: 128, height: 720, eye: 576, step: 256, jump: 5.3, gravity: 0.55, speed: 30, run: 56, reach: 900 }; // map units, per 1/60 s

  // ---- matrices (column-major) -------------------------------------------------------------------
  function perspective(fovy, aspect, near, far) {
    const f = 1 / Math.tan(fovy / 2), nf = 1 / (near - far);
    return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0]);
  }
  function lookAt(e, c, up) {
    let zx = e[0] - c[0], zy = e[1] - c[1], zz = e[2] - c[2];
    let l = Math.hypot(zx, zy, zz) || 1; zx /= l; zy /= l; zz /= l;
    let xx = up[1] * zz - up[2] * zy, xy = up[2] * zx - up[0] * zz, xz = up[0] * zy - up[1] * zx;
    l = Math.hypot(xx, xy, xz) || 1; xx /= l; xy /= l; xz /= l;
    const yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    return new Float32Array([xx, yx, zx, 0, xy, yy, zy, 0, xz, yz, zz, 0,
      -(xx * e[0] + xy * e[1] + xz * e[2]), -(yx * e[0] + yy * e[1] + yz * e[2]), -(zx * e[0] + zy * e[1] + zz * e[2]), 1]);
  }
  function mul(a, b) {
    const o = new Float32Array(16);
    for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
      o[c * 4 + r] = s;
    }
    return o;
  }

  // a colour for a tile number (no Blood files): the same tile always looks the same
  function tileColor(pic, kind) {
    const h = (pic * 2654435761) >>> 0;
    const hue = (h % 360) / 360, sat = 0.25 + ((h >> 9) % 30) / 100, val = kind === 'floor' ? 0.42 : kind === 'ceil' ? 0.3 : 0.52;
    const i = Math.floor(hue * 6), f = hue * 6 - i, p = val * (1 - sat), q = val * (1 - f * sat), t = val * (1 - (1 - f) * sat);
    return [[val, t, p], [q, val, p], [p, val, t], [p, q, val], [t, p, val], [val, p, q]][i % 6];
  }
  const THING_COLORS = { Starts: [0.44, 0.75, 0.45], Weapons: [0.94, 0.64, 0.29], Ammo: [0.85, 0.76, 0.42], Health: [0.94, 0.36, 0.29], Armor: [0.44, 0.66, 0.86], 'Power-ups': [0.76, 0.48, 0.63], Keys: [1, 1, 1], Enemies: [0.8, 0.15, 0.12] };

  // ear clipping for a room outline (MapCore keeps them clockwise on screen: positive area)
  function triangulate(pts) {
    const idx = pts.map((_, i) => i), tris = [];
    const cross = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    const inTri = (p, a, b, c) => cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0;
    let guard = 0;
    while (idx.length > 3 && guard++ < 10000) {
      let cut = false;
      for (let k = 0; k < idx.length; k++) {
        const ia = idx[(k + idx.length - 1) % idx.length], ib = idx[k], ic = idx[(k + 1) % idx.length];
        const a = pts[ia], b = pts[ib], c = pts[ic];
        if (cross(a, b, c) <= 0) continue;
        let ok = true;
        for (const j of idx) {
          if (j === ia || j === ib || j === ic) continue;
          if (inTri(pts[j], a, b, c)) { ok = false; break; }
        }
        if (!ok) continue;
        tris.push([ia, ib, ic]);
        idx.splice(k, 1);
        cut = true;
        break;
      }
      if (!cut) break;
    }
    if (idx.length === 3) tris.push(idx.slice());
    return tris;
  }

  // ---- doors and lifts: where floors and ceilings are right now -------------------------------------
  function Heights(prj) {
    const rooms = prj.rooms;
    const st = rooms.map(() => ({ open: 0, target: 0, waitUntil: 0 }));
    const floorOf = (s) => { const r = rooms[s]; return r.kind === 'lift' ? r.floor + r.rise * st[s].open : r.floor; };
    const ceilOf = (s) => { const r = rooms[s]; return r.kind === 'door' ? r.floor + r.height * st[s].open : r.floor + r.height; };
    return { st, floorOf, ceilOf, moving: () => st.some((x) => x.open !== x.target) };
  }

  // ---- the level as triangles, one batch per tile -------------------------------------------------
  // world: X = map x, Y = height (up), Z = map y. uv in tile pixels.
  function buildMesh(prj, hts, opts) {
    opts = opts || {};
    hts = hts || Heights(prj);
    const batches = new Map(); // key -> { pic, ceil, pos, uv, col, shade }
    const batch = (pic, ceil) => {
      const key = pic + (ceil ? 'c' : '');
      let b = batches.get(key);
      if (!b) batches.set(key, b = { pic, ceil, pos: [], uv: [], col: [], shade: [] });
      return b;
    };
    const m = M.buildMap(prj, false);
    const rooms = prj.rooms;
    const { floorOf, ceilOf } = hts;
    const shadeOf = (s) => rooms[s].light || 0;
    function wallQuad(pic, a, b, y1, y2, top, s, c) {
      if (y2 <= y1 + 0.01) return;
      const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
      const xrep = Math.max(1, Math.min(255, Math.round(len / 128))); // as the export writes it
      const u1 = xrep * 8;
      // Build: yrepeat 8 is 256 z units (16 map units) per pixel, measured from the ceiling
      const v = (y) => (top - y) / 16;
      const q = [[a, y1, 0, v(y1)], [b, y1, u1, v(y1)], [b, y2, u1, v(y2)], [a, y2, 0, v(y2)]];
      const bt = batch(pic, false);
      for (const k of [0, 1, 2, 0, 2, 3]) {
        const [p, y, u, vv] = q[k];
        bt.pos.push(p[0], y, p[1]); bt.uv.push(u, vv); bt.col.push(...c); bt.shade.push(s);
      }
    }
    m.walls.forEach((w) => {
      const s = w.sector, o = m.walls[w.point2], a = [w.x, w.y], b = [o.x, o.y];
      const r = rooms[s];
      const next = w.nextsector >= 0 ? rooms[w.nextsector] : null;
      const pic = next && next.kind === 'door' ? next.doorPic : r.wallPic; // a door's face, as exported
      const dir = Math.atan2(b[1] - a[1], b[0] - a[0]);
      const c = tileColor(pic, 'wall').map((x) => x * (0.78 + 0.22 * Math.cos(dir * 2)));
      const sh = shadeOf(s) + Math.round(4 * Math.abs(Math.sin(dir))); // walls facing different ways a little different
      const f = floorOf(s), cl = ceilOf(s);
      if (w.nextsector < 0) { wallQuad(pic, a, b, f, cl, cl, sh, c); return; }
      const n = w.nextsector;
      if (floorOf(n) > f) wallQuad(pic, a, b, f, Math.min(floorOf(n), cl), cl, sh, c);
      if (ceilOf(n) < cl && !(r.sky && rooms[n].sky)) wallQuad(pic, a, b, Math.max(ceilOf(n), f), cl, cl, sh, c);
    });
    rooms.forEach((r, s) => {
      const tris = triangulate(r.pts);
      const f = floorOf(s), cl = ceilOf(s);
      const fb = batch(r.floorPic, false), cb = r.sky ? null : batch(r.ceilPic, true);
      const cf = tileColor(r.floorPic, 'floor'), cc = tileColor(r.ceilPic, 'ceil');
      for (const t of tris) {
        for (const k of t) { const p = r.pts[k]; fb.pos.push(p[0], f, p[1]); fb.uv.push(p[0] / 16, p[1] / 16); fb.col.push(...cf); fb.shade.push(shadeOf(s)); }
        if (cb && cl > f) for (const k of [t[0], t[2], t[1]]) { const p = r.pts[k]; cb.pos.push(p[0], cl, p[1]); cb.uv.push(p[0] / 16, p[1] / 16); cb.col.push(...cc); cb.shade.push(shadeOf(s)); }
      }
    });
    // things without pictures (or the player starts, which are invisible in the game): little boxes
    const boxes = batch(-1, false);
    prj.things.forEach((t, i) => {
      if (opts.hide && opts.hide.has(i)) return;
      const info = M.THING_BY_TYPE[t.type] || { group: '' };
      if (opts.textured && !info.start && G() && G().tile(info.pic)) return; // drawn as a sprite
      const s = M.roomAt([t.x, t.y], rooms);
      if (s < 0) return;
      const c = THING_COLORS[info.group] || [0.7, 0.7, 0.7];
      const big = info.start ? 96 : info.enemy ? 120 : 56, hgt = info.start ? 320 : info.enemy ? 560 : 112, y1 = floorOf(s), y2 = y1 + hgt;
      const x1 = t.x - big, x2 = t.x + big, z1 = t.y - big, z2 = t.y + big;
      const quad = (a, b, ya, yb, cc) => {
        const v = [[a[0], ya, a[1]], [b[0], ya, b[1]], [b[0], yb, b[1]], [a[0], yb, a[1]]];
        for (const k of [0, 1, 2, 0, 2, 3]) { boxes.pos.push(...v[k]); boxes.uv.push(0, 0); boxes.col.push(...cc); boxes.shade.push(0); }
      };
      [[[x1, z1], [x2, z1]], [[x2, z1], [x2, z2]], [[x2, z2], [x1, z2]], [[x1, z2], [x1, z1]]].forEach(([a, b], k) => quad(a, b, y1, y2, c.map((v) => v * (0.75 + 0.08 * k))));
      for (const [x, z] of [[x1, z1], [x2, z1], [x2, z2], [x1, z1], [x2, z2], [x1, z2]]) { boxes.pos.push(x, y2, z); boxes.uv.push(0, 0); boxes.col.push(...c); boxes.shade.push(0); }
      if (info.start) {
        const a = ((t.ang || 0) / 2048) * Math.PI * 2;
        quad([t.x - Math.sin(a) * 30, t.y + Math.cos(a) * 30], [t.x + Math.cos(a) * 220, t.y + Math.sin(a) * 220], y2 - 90, y2 - 30, [0.9, 1, 0.9]);
      }
    });
    let ceilStart = 0, count = 0;
    const list = [...batches.values()].sort((a, b) => (a.ceil - b.ceil));
    for (const b of list) { b.first = count; b.count = b.pos.length / 3; count += b.count; if (!b.ceil) ceilStart = count; }
    return { batches: list, map: m, count, ceilStart };
  }

  // ---- WebGL --------------------------------------------------------------------------------------
  const VS = `attribute vec3 aPos; attribute vec2 aUV; attribute vec3 aCol; attribute float aShade;
uniform mat4 uMVP; uniform vec3 uEye;
varying vec2 vUV; varying vec3 vCol; varying float vShade; varying float vDist; varying vec3 vPos;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); vUV = aUV; vCol = aCol; vShade = aShade; vDist = distance(aPos, uEye); vPos = aPos; }`;
  const FS = `precision mediump float;
varying vec2 vUV; varying vec3 vCol; varying float vShade; varying float vDist; varying vec3 vPos;
uniform int uMode;          // 0: colours, 1: Blood tile (palette + shading), 2: tile, no shading table
uniform sampler2D uTex; uniform vec2 uSize; uniform sampler2D uPal; uniform sampler2D uLook;
uniform float uVis; uniform float uSeeThrough; uniform vec3 uFog; uniform float uFogDist;
void main() {
  if (uMode == 0) {
    vec3 p = vPos / 256.0;
    float g = 0.97 - 0.06 * (step(fract(p.x), 0.03) + step(fract(p.z), 0.03) + step(fract(p.y), 0.03));
    gl_FragColor = vec4(mix(vCol * g, uFog, clamp(vDist / uFogDist, 0.0, 0.85)), 1.0);
    return;
  }
  // the tile is stored column by column (ART), so x and y swap in the texture
  vec2 px = mod(floor(vUV), uSize);
  float idx = floor(texture2D(uTex, vec2((px.y + 0.5) / uSize.y, (px.x + 0.5) / uSize.x)).r * 255.0 + 0.5);
  if (uSeeThrough > 0.5 && idx > 254.5) discard;
  float shade = clamp(vShade + vDist * uVis, 0.0, 63.0);
  float pi = idx;
  if (uMode == 1) pi = floor(texture2D(uLook, vec2((idx + 0.5) / 256.0, (floor(shade) + 0.5) / 64.0)).r * 255.0 + 0.5);
  vec3 c = texture2D(uPal, vec2((pi + 0.5) / 256.0, 0.5)).rgb;
  if (uMode == 2) c *= 1.0 - shade / 64.0;
  gl_FragColor = vec4(c, 1.0);
}`;

  function makeGL(canvas) {
    const gl = canvas.getContext('webgl', { antialias: false }) || canvas.getContext('experimental-webgl');
    if (!gl) return null;
    const sh = (type, src) => {
      const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    };
    const prog = gl.createProgram();
    gl.attachShader(prog, sh(gl.VERTEX_SHADER, VS)); gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, FS));
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
    gl.useProgram(prog);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    const u = (n) => gl.getUniformLocation(prog, n);
    const r = {
      gl, prog,
      a: { pos: gl.getAttribLocation(prog, 'aPos'), uv: gl.getAttribLocation(prog, 'aUV'), col: gl.getAttribLocation(prog, 'aCol'), shade: gl.getAttribLocation(prog, 'aShade') },
      u: { mvp: u('uMVP'), eye: u('uEye'), mode: u('uMode'), tex: u('uTex'), size: u('uSize'), pal: u('uPal'), look: u('uLook'), vis: u('uVis'), see: u('uSeeThrough'), fog: u('uFog'), fogDist: u('uFogDist') },
      level: { pos: gl.createBuffer(), uv: gl.createBuffer(), col: gl.createBuffer(), shade: gl.createBuffer() },
      dyn: { pos: gl.createBuffer(), uv: gl.createBuffer(), col: gl.createBuffer(), shade: gl.createBuffer() },
      tiles: new Map(), palTex: null, lookTex: null,
    };
    gl.uniform1i(r.u.tex, 0); gl.uniform1i(r.u.pal, 1); gl.uniform1i(r.u.look, 2);
    return r;
  }

  function lumTexture(gl, w, h, data) {
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, w, h, 0, gl.LUMINANCE, gl.UNSIGNED_BYTE, data);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return t;
  }
  // the palette and shading table, once
  function gameTextures(r) {
    const g = G();
    if (!g || !g.ready()) return false;
    const gl = r.gl;
    if (!r.palTex) {
      r.palTex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, r.palTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, 256, 1, 0, gl.RGB, gl.UNSIGNED_BYTE, g.palette());
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      if (g.shades()) r.lookTex = lumTexture(gl, 256, 64, g.shades());
    }
    return true;
  }
  function tileTexture(r, pic) {
    if (r.tiles.has(pic)) return r.tiles.get(pic);
    const t = G() && G().tile(pic);
    // stored as it is in the ART file: h pixels wide (one column), w columns high
    const v = t ? { tex: lumTexture(r.gl, t.h, t.w, t.data), w: t.w, h: t.h } : null;
    r.tiles.set(pic, v);
    return v;
  }

  function uploadInto(r, bufs, data) {
    const gl = r.gl;
    [['pos', 3], ['uv', 2], ['col', 3], ['shade', 1]].forEach(([k, n]) => {
      gl.bindBuffer(gl.ARRAY_BUFFER, bufs[k]);
      gl.bufferData(gl.ARRAY_BUFFER, data[k], gl.DYNAMIC_DRAW);
    });
  }
  function bindBuffers(r, bufs) {
    const gl = r.gl;
    [['pos', 3], ['uv', 2], ['col', 3], ['shade', 1]].forEach(([k, n]) => {
      gl.bindBuffer(gl.ARRAY_BUFFER, bufs[k]);
      gl.enableVertexAttribArray(r.a[k]);
      gl.vertexAttribPointer(r.a[k], n, gl.FLOAT, false, 0, 0);
    });
  }
  function flatten(batches) {
    const n = batches.reduce((s, b) => s + b.count, 0);
    const out = { pos: new Float32Array(n * 3), uv: new Float32Array(n * 2), col: new Float32Array(n * 3), shade: new Float32Array(n) };
    let o = 0;
    for (const b of batches) { out.pos.set(b.pos, o * 3); out.uv.set(b.uv, o * 2); out.col.set(b.col, o * 3); out.shade.set(b.shade, o); o += b.count; }
    return out;
  }

  // ---- walking ------------------------------------------------------------------------------------
  // Like Build: the player is a circle on the map plus a height range. A wall stops it when it's
  // solid, the step up is too high, or the opening is too low (a shut door has no opening at all).
  function Walker(prj, map, hts) {
    hts = hts || Heights(prj);
    const rooms = prj.rooms;
    const { floorOf, ceilOf } = hts;
    const edges = map.walls.map((w) => ({ a: [w.x, w.y], b: [map.walls[w.point2].x, map.walls[w.point2].y], s: w.sector, n: w.nextsector }));
    const st = { x: 0, y: 0, z: 0, vz: 0, ang: 0, pitch: 0, sector: -1, onGround: true };
    const start = prj.things.find((t) => t.type === 1) || prj.things.find((t) => t.type === 2);
    if (start) { st.x = start.x; st.y = start.y; st.ang = ((start.ang || 0) / 2048) * Math.PI * 2; }
    else if (rooms.length) { const c = M.centroidInside(rooms[0].pts) || rooms[0].pts[0]; st.x = c[0]; st.y = c[1]; }
    st.sector = M.roomAt([st.x, st.y], rooms);
    if (st.sector >= 0) st.z = floorOf(st.sector);

    function blocks(e, z) {
      if (e.n < 0) return true;
      const lo = Math.max(floorOf(e.n), floorOf(e.s)), hi = Math.min(ceilOf(e.n), ceilOf(e.s));
      if (floorOf(e.n) - z > PLAYER.step) return true;
      return hi - Math.max(lo, z) < PLAYER.height * 0.8;
    }
    function closest(p, a, b) {
      const dx = b[0] - a[0], dy = b[1] - a[1], l = dx * dx + dy * dy;
      let t = l ? ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l : 0;
      t = Math.max(0, Math.min(1, t));
      return [a[0] + dx * t, a[1] + dy * t];
    }
    const cr = (o, u, v) => (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0]);
    function crosses(p, q, a, b) {
      const d1 = cr(a, b, p), d2 = cr(a, b, q), d3 = cr(p, q, a), d4 = cr(p, q, b);
      return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
    }
    function tryMove(dx, dy) {
      let p = [st.x + dx, st.y + dy];
      for (let it = 0; it < 3; it++) {
        for (const e of edges) {
          if (!blocks(e, st.z)) continue;
          const c = closest(p, e.a, e.b), ddx = p[0] - c[0], ddy = p[1] - c[1], d = Math.hypot(ddx, ddy);
          if (d < PLAYER.radius) {
            if (d > 0.001) { p[0] = c[0] + ddx / d * PLAYER.radius; p[1] = c[1] + ddy / d * PLAYER.radius; }
            else p = [st.x, st.y];
          }
        }
      }
      for (const e of edges) if (blocks(e, st.z) && crosses([st.x, st.y], p, e.a, e.b)) return;
      const s = M.roomAt(p, rooms);
      if (s < 0) return;
      if (floorOf(s) - st.z > PLAYER.step) return;
      if (ceilOf(s) - Math.max(st.z, floorOf(s)) < PLAYER.height * 0.8) return;
      st.x = p[0]; st.y = p[1]; st.sector = s;
    }
    // what "use" reaches: the nearest wall straight ahead (its other side: a door or lift), else
    // the lift it stands on
    function useTarget() {
      const a = [st.x, st.y], b = [st.x + Math.cos(st.ang) * PLAYER.reach, st.y + Math.sin(st.ang) * PLAYER.reach];
      let best = null, bd = Infinity;
      for (const e of edges) {
        if (e.s !== st.sector || !crosses(a, b, e.a, e.b)) continue;
        const c = closest(a, e.a, e.b), d = Math.hypot(c[0] - a[0], c[1] - a[1]);
        if (d < bd) { bd = d; best = e; }
      }
      if (best && best.n >= 0 && rooms[best.n].kind !== 'room') return best.n;
      if (st.sector >= 0 && rooms[st.sector].kind === 'lift') return st.sector;
      return -1;
    }
    return {
      st, PLAYER, useTarget,
      step(fwd, side, run, jump, dt) {
        const sp = (run ? PLAYER.run : PLAYER.speed) * dt;
        const cs = Math.cos(st.ang), sn = Math.sin(st.ang);
        const dx = (cs * fwd - sn * side) * sp, dy = (sn * fwd + cs * side) * sp;
        const n = Math.max(1, Math.ceil(Math.hypot(dx, dy) / (PLAYER.radius / 2)));
        for (let i = 0; i < n; i++) tryMove(dx / n, dy / n);
        if (st.sector < 0) return;
        const fl = floorOf(st.sector), cl = ceilOf(st.sector);
        if (jump && st.onGround) { st.vz = PLAYER.jump * 16; st.onGround = false; }
        st.vz -= PLAYER.gravity * 16 * dt;
        st.z += st.vz * dt;
        if (st.z <= fl) { st.z = fl; st.vz = 0; st.onGround = true; }
        else if (st.onGround && st.z - fl > 4 && st.vz <= 0) st.onGround = false;
        if (st.z + PLAYER.height > cl) { st.z = Math.max(fl, cl - PLAYER.height); if (st.vz > 0) st.vz = 0; }
      },
      eye() { return [st.x, st.z + PLAYER.eye, st.y]; },
    };
  }

  // a door or lift moving towards where it's going; the time is in ms
  function runMovers(prj, hts, walker, now, dtMs) {
    let changed = false;
    prj.rooms.forEach((r, s) => {
      if (r.kind === 'room') return;
      const x = hts.st[s];
      if (x.open === 1 && x.target === 1 && r.wait > 0 && x.waitUntil && now >= x.waitUntil) { x.target = 0; x.waitUntil = 0; }
      if (x.open === x.target) return;
      const step = dtMs / Math.max(100, r.speed * 100);
      let next = x.target > x.open ? Math.min(1, x.open + step) : Math.max(0, x.open - step);
      // a shutting door or a lift going up doesn't squash the player: it waits
      if (walker && walker.st.sector === s) {
        if (r.kind === 'door' && next < x.open && r.floor + r.height * next < walker.st.z + PLAYER.height) next = x.open;
      }
      if (next !== x.open) { x.open = next; changed = true; }
      if (x.open === 1 && x.target === 1 && r.wait > 0) x.waitUntil = now + r.wait * 100;
    });
    return changed;
  }

  // ---- the viewer ------------------------------------------------------------------------------------
  function open(prj, overlay) {
    overlay.innerHTML = `
<canvas class="v3d"></canvas>
<div class="v3d-top"><button data-m="orbit">Look around</button><button data-m="walk">Walk</button><span class="v3d-info"></span><button data-m="close" class="on">Close</button></div>
<div class="v3d-stick"><div class="v3d-knob"></div></div>
<button class="v3d-use">Use</button><button class="v3d-jump">Jump</button><button class="v3d-run">Run</button>
<div class="v3d-msg"></div>
<div class="v3d-help"></div>`;
    overlay.classList.add('open');
    const cv = overlay.querySelector('canvas');
    let r = null;
    try { r = makeGL(cv); } catch (e) { r = null; }
    const info = overlay.querySelector('.v3d-info'), help = overlay.querySelector('.v3d-help'), msgEl = overlay.querySelector('.v3d-msg');
    if (!r) help.textContent = 'This browser has no WebGL, so there is no 3D view.';
    const textured = !!(r && gameTextures(r));
    const hts = Heights(prj);
    const hidden = new Set(); // keys picked up
    let mesh = buildMesh(prj, hts, { textured, hide: hidden });
    if (r) uploadInto(r, r.level, flatten(mesh.batches));
    const walker = Walker(prj, mesh.map, hts);
    const keysHeld = new Set(), secretsFound = new Set();
    const nSecrets = prj.rooms.filter((x) => x.secret).length;
    let x1 = Infinity, y1 = Infinity, x2 = -Infinity, y2 = -Infinity, top = 0;
    prj.rooms.forEach((rm) => { rm.pts.forEach((p) => { x1 = Math.min(x1, p[0]); y1 = Math.min(y1, p[1]); x2 = Math.max(x2, p[0]); y2 = Math.max(y2, p[1]); }); top = Math.max(top, rm.floor + rm.height); });
    if (!isFinite(x1)) { x1 = y1 = 0; x2 = y2 = 4096; }
    const orbit = { cx: (x1 + x2) / 2, cy: top / 2, cz: (y1 + y2) / 2, yaw: 0.7, pitch: 0.75, dist: Math.max(x2 - x1, y2 - y1) * 1.2 + 3000 };
    let mode = 'orbit', running = true, last = performance.now(), run = false, usePressed = false, jumpPressed = false;
    const keys = {}, stick = { x: 0, y: 0, id: null }, look = { id: null, x: 0, y: 0 };

    function message(t) { msgEl.textContent = t; msgEl.classList.add('show'); clearTimeout(message.t); message.t = setTimeout(() => msgEl.classList.remove('show'), 2500); }
    function setMode(m) {
      mode = m;
      overlay.querySelectorAll('[data-m]').forEach((b) => b.classList.toggle('on', b.dataset.m === m || b.dataset.m === 'close'));
      overlay.classList.toggle('walking', m === 'walk');
      help.textContent = (m === 'walk'
        ? 'Left: move. Right: look. Use opens doors and starts lifts. Keyboard: WASD, mouse drag, E use, Space jump, Shift run. Sizes are close to Blood\'s, not exact.'
        : 'Drag to turn the map, pinch or scroll to zoom. Ceilings are left out here; Walk shows them.')
        + (textured ? '' : ' (Load your Blood files in ☰ for the real textures.)');
      help.classList.remove('gone'); clearTimeout(help.t); help.t = setTimeout(() => help.classList.add('gone'), 8000);
      if (m === 'walk' && walker.st.sector < 0) info.textContent = 'No start inside a room';
    }
    const kd = (e) => { keys[e.code] = true; if (e.code === 'Space') e.preventDefault(); if (e.code === 'KeyE' || e.code === 'Enter') usePressed = true; };
    const ku = (e) => { keys[e.code] = false; };
    overlay.querySelectorAll('[data-m]').forEach((b) => b.onclick = () => {
      if (b.dataset.m === 'close') { running = false; overlay.classList.remove('open', 'walking'); overlay.innerHTML = ''; window.removeEventListener('keydown', kd); window.removeEventListener('keyup', ku); return; }
      setMode(b.dataset.m);
    });
    overlay.querySelector('.v3d-jump').addEventListener('pointerdown', (e) => { e.preventDefault(); jumpPressed = true; });
    overlay.querySelector('.v3d-use').addEventListener('pointerdown', (e) => { e.preventDefault(); usePressed = true; });
    const runBtn = overlay.querySelector('.v3d-run');
    runBtn.onclick = () => { run = !run; runBtn.classList.toggle('on', run); };
    window.addEventListener('keydown', kd); window.addEventListener('keyup', ku);

    const stickEl = overlay.querySelector('.v3d-stick'), knob = overlay.querySelector('.v3d-knob');
    const pinch = new Map();
    let pinchD = 0;
    cv.addEventListener('pointerdown', (e) => {
      cv.setPointerCapture(e.pointerId);
      if (mode === 'walk') {
        const half = cv.getBoundingClientRect().width / 2;
        if (e.offsetX < half && stick.id === null && e.pointerType !== 'mouse') { stick.id = e.pointerId; stick.ox = e.offsetX; stick.oy = e.offsetY; stick.x = stick.y = 0; stickEl.style.left = (e.offsetX - 60) + 'px'; stickEl.style.top = (e.offsetY - 60) + 'px'; stickEl.classList.add('on'); }
        else if (look.id === null) { look.id = e.pointerId; look.x = e.offsetX; look.y = e.offsetY; }
      } else {
        pinch.set(e.pointerId, [e.offsetX, e.offsetY]);
        if (pinch.size === 2) { const [a, b] = [...pinch.values()]; pinchD = Math.hypot(a[0] - b[0], a[1] - b[1]); }
      }
    });
    cv.addEventListener('pointermove', (e) => {
      if (mode === 'walk') {
        if (e.pointerId === stick.id) {
          let dx = e.offsetX - stick.ox, dy = e.offsetY - stick.oy; const l = Math.hypot(dx, dy);
          if (l > 50) { dx *= 50 / l; dy *= 50 / l; }
          stick.x = dx / 50; stick.y = dy / 50;
          knob.style.transform = 'translate(' + dx + 'px,' + dy + 'px)';
        } else if (e.pointerId === look.id) {
          const dx = e.offsetX - look.x, dy = e.offsetY - look.y; look.x = e.offsetX; look.y = e.offsetY;
          walker.st.ang += dx * 0.006; walker.st.pitch = Math.max(-1.2, Math.min(1.2, walker.st.pitch - dy * 0.005));
        }
        return;
      }
      const prev = pinch.get(e.pointerId);
      if (!prev) return;
      if (pinch.size === 1) {
        orbit.yaw += (e.offsetX - prev[0]) * 0.008;
        orbit.pitch = Math.max(0.05, Math.min(1.5, orbit.pitch + (e.offsetY - prev[1]) * 0.006));
      }
      pinch.set(e.pointerId, [e.offsetX, e.offsetY]);
      if (pinch.size === 2) {
        const [a, b] = [...pinch.values()]; const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
        if (pinchD) orbit.dist = Math.max(800, Math.min(200000, orbit.dist * pinchD / d));
        pinchD = d;
      }
    });
    const end = (e) => {
      pinch.delete(e.pointerId);
      if (e.pointerId === stick.id) { stick.id = null; stick.x = stick.y = 0; knob.style.transform = ''; stickEl.classList.remove('on'); }
      if (e.pointerId === look.id) look.id = null;
    };
    cv.addEventListener('pointerup', end); cv.addEventListener('pointercancel', end);
    cv.addEventListener('wheel', (e) => { e.preventDefault(); orbit.dist = Math.max(800, Math.min(200000, orbit.dist * Math.exp(e.deltaY * 0.001))); }, { passive: false });

    function doUse() {
      const s = walker.useTarget();
      if (s < 0) return;
      const rm = prj.rooms[s], x = hts.st[s];
      if (rm.kind === 'door' && rm.key && !keysHeld.has(rm.key)) { message('You need the ' + M.KEY_NAMES[rm.key]); return; }
      x.target = x.target > 0.5 ? 0 : 1;
      x.waitUntil = 0;
    }
    function gameplay(now) {
      const st = walker.st;
      // keys: walk over them
      prj.things.forEach((t, i) => {
        if (t.type < 100 || t.type > 105 || hidden.has(i)) return;
        if (Math.hypot(t.x - st.x, t.y - st.y) < 220 && M.roomAt([t.x, t.y], prj.rooms) === st.sector) {
          hidden.add(i); keysHeld.add(t.type - 99); message('Picked up the ' + M.KEY_NAMES[t.type - 99]); rebuild = true;
        }
      });
      if (st.sector >= 0 && prj.rooms[st.sector].secret && !secretsFound.has(st.sector)) {
        secretsFound.add(st.sector);
        message('A secret is revealed! (' + secretsFound.size + ' of ' + nSecrets + ')');
      }
    }

    // sprites facing the viewer: items, weapons, enemies (with Blood's pictures)
    function spriteBatches(eye) {
      if (!textured) return [];
      const out = new Map();
      prj.things.forEach((t, i) => {
        const inf = M.THING_BY_TYPE[t.type];
        if (!inf || inf.start || hidden.has(i)) return;
        const tile = G().tile(inf.pic);
        const s = M.roomAt([t.x, t.y], prj.rooms);
        if (!tile || s < 0) return;
        const w = tile.w * (inf.xr || 48) / 4, h = tile.h * (inf.yr || 48) / 4;
        const dx = eye[0] - t.x, dz = eye[2] - t.y, l = Math.hypot(dx, dz) || 1;
        const rx = -dz / l * w / 2, rz = dx / l * w / 2;
        const y0 = hts.floorOf(s), y1 = y0 + h;
        let b = out.get(inf.pic);
        if (!b) out.set(inf.pic, b = { pic: inf.pic, sprite: true, pos: [], uv: [], col: [], shade: [] });
        const q = [[t.x - rx, y0, t.y - rz, 0, tile.h - 0.01], [t.x + rx, y0, t.y + rz, tile.w - 0.01, tile.h - 0.01], [t.x + rx, y1, t.y + rz, tile.w - 0.01, 0], [t.x - rx, y1, t.y - rz, 0, 0]];
        for (const k of [0, 1, 2, 0, 2, 3]) { const v = q[k]; b.pos.push(v[0], v[1], v[2]); b.uv.push(v[3], v[4]); b.col.push(1, 1, 1); b.shade.push(prj.rooms[s].light || 0); }
      });
      let first = 0;
      const list = [...out.values()];
      for (const b of list) { b.count = b.pos.length / 3; b.first = first; first += b.count; }
      return list;
    }
    // the sky: a ring of tiles around the viewer (Build's parallax sky: 16 tiles, the sky tile and those after it)
    function skyBatches(eye) {
      if (!textured || !prj.rooms.some((x) => x.sky)) return [];
      const out = [];
      const R = 60000, n = 16, base = prj.skyPic;
      for (let i = 0; i < n; i++) {
        const pic = G().tile(base + i) ? base + i : base, tile = G().tile(pic);
        if (!tile) continue;
        const a1 = (i / n) * Math.PI * 2, a2 = ((i + 1) / n) * Math.PI * 2, hh = R * 0.9;
        const p1 = [eye[0] + Math.cos(a1) * R, eye[2] + Math.sin(a1) * R], p2 = [eye[0] + Math.cos(a2) * R, eye[2] + Math.sin(a2) * R];
        const yb = eye[1] - hh * 0.35, yt = eye[1] + hh;
        const b = { pic, sky: true, pos: [], uv: [], col: [], shade: [] };
        const q = [[p1, yb, 0, tile.h - 0.01], [p2, yb, tile.w - 0.01, tile.h - 0.01], [p2, yt, tile.w - 0.01, 0], [p1, yt, 0, 0]];
        for (const k of [0, 1, 2, 0, 2, 3]) { const [p, y, u, v] = q[k]; b.pos.push(p[0], y, p[1]); b.uv.push(u, v); b.col.push(0, 0, 0); b.shade.push(0); }
        out.push(b);
      }
      let first = 0;
      for (const b of out) { b.count = 6; b.first = first; first += 6; }
      return out;
    }

    function drawBatches(list, bufs, uploadData, skipCeil) {
      const gl = r.gl;
      if (uploadData) uploadInto(r, bufs, flatten(list));
      bindBuffers(r, bufs);
      for (const b of list) {
        if (!b.count || (skipCeil && b.ceil)) continue;
        const t = textured && b.pic >= 0 ? tileTexture(r, b.pic) : null;
        if (t) {
          gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, t.tex);
          gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, r.palTex);
          if (r.lookTex) { gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, r.lookTex); }
          gl.uniform1i(r.u.mode, r.lookTex ? 1 : 2);
          gl.uniform2f(r.u.size, t.w, t.h);
          gl.uniform1f(r.u.see, b.sprite || b.sky ? 1 : 0);
        } else gl.uniform1i(r.u.mode, 0);
        gl.drawArrays(gl.TRIANGLES, b.first, b.count);
      }
    }

    let rebuild = false;
    function frame(now) {
      if (!running) return;
      const dtMs = Math.min(50, now - last), dt = dtMs / (1000 / 60); last = now;
      const dpr = Math.min(2, window.devicePixelRatio || 1), w = cv.clientWidth, h = cv.clientHeight;
      if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
      let eye, at;
      if (mode === 'walk') {
        if (usePressed) doUse();
        const fwd = -stick.y + ((keys.KeyW || keys.ArrowUp) ? 1 : 0) - ((keys.KeyS || keys.ArrowDown) ? 1 : 0);
        const side = stick.x + (keys.KeyD ? 1 : 0) - (keys.KeyA ? 1 : 0);
        if (keys.ArrowLeft) walker.st.ang -= 0.04 * dt;
        if (keys.ArrowRight) walker.st.ang += 0.04 * dt;
        walker.step(Math.max(-1, Math.min(1, fwd)), Math.max(-1, Math.min(1, side)), run || keys.ShiftLeft || keys.ShiftRight, jumpPressed || keys.Space, dt);
        gameplay(now);
        eye = walker.eye();
        const st = walker.st;
        at = [eye[0] + Math.cos(st.ang) * Math.cos(st.pitch) * 100, eye[1] + Math.sin(st.pitch) * 100, eye[2] + Math.sin(st.ang) * Math.cos(st.pitch) * 100];
        info.textContent = st.sector >= 0 ? (prj.rooms[st.sector].kind === 'lift' ? 'Lift ' : 'Room ') + (st.sector + 1) + (nSecrets ? '  secrets ' + secretsFound.size + '/' + nSecrets : '') : '';
      } else {
        eye = [orbit.cx + Math.cos(orbit.yaw) * Math.cos(orbit.pitch) * orbit.dist, orbit.cy + Math.sin(orbit.pitch) * orbit.dist, orbit.cz + Math.sin(orbit.yaw) * Math.cos(orbit.pitch) * orbit.dist];
        at = [orbit.cx, orbit.cy, orbit.cz];
        info.textContent = prj.rooms.length + ' rooms';
      }
      usePressed = jumpPressed = false;
      if (runMovers(prj, hts, walker, now, dtMs) || rebuild) {
        rebuild = false;
        mesh = buildMesh(prj, hts, { textured, hide: hidden });
        if (r) uploadInto(r, r.level, flatten(mesh.batches));
      }
      if (r) {
        const gl = r.gl;
        gl.viewport(0, 0, cv.width, cv.height);
        gl.clearColor(0.09, 0.05, 0.06, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.disable(gl.CULL_FACE);
        const proj = perspective(mode === 'walk' ? 1.25 : 0.9, cv.width / Math.max(1, cv.height), mode === 'walk' ? 16 : 64, 400000);
        gl.uniformMatrix4fv(r.u.mvp, false, mul(proj, lookAt(eye, at, [0, 1, 0])));
        gl.uniform3fv(r.u.eye, eye);
        gl.uniform3fv(r.u.fog, mode === 'walk' ? [0.09, 0.05, 0.06] : [0.12, 0.07, 0.07]);
        gl.uniform1f(r.u.fogDist, mode === 'walk' ? 14000 : orbit.dist * 4);
        // Blood's darkness with distance (the map's visibility); none in the overview
        gl.uniform1f(r.u.vis, mode === 'walk' ? (prj.visibility || 0) / 327680 : 0);
        if (mode === 'walk') {
          gl.disable(gl.DEPTH_TEST);
          drawBatches(skyBatches(eye), r.dyn, true, false);
        }
        gl.enable(gl.DEPTH_TEST);
        drawBatches(mesh.batches, r.level, false, mode !== 'walk');
        drawBatches(spriteBatches(eye), r.dyn, true, false);
      }
      requestAnimationFrame(frame);
    }
    setMode('orbit');
    requestAnimationFrame(frame);
    return { setMode, walker, heights: hts, textured, close: () => overlay.querySelector('[data-m=close]').click() };
  }

  root.View3D = { open, buildMesh, triangulate, Walker, Heights, runMovers, PLAYER };
})(typeof window !== 'undefined' ? window : this);
