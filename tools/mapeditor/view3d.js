// Blood Map Maker: 3D view and walkthrough of a full Blood map (MapFile's model), WebGL 1.
//
// Drawn like Build draws it: sectors with holes and slopes, each wall with its own tile, repeat,
// panning and flags (lower/upper parts, masked walls, swapped bottoms, aligned to floor or ceiling),
// floors and ceilings with their panning and flags, face/wall/floor sprites, and the parallax sky.
// With the player's Blood files (gamefiles.js) it uses Blood's tiles, palette and shading table;
// without them every tile number gets a colour of its own.
//
// The walkthrough moves a Blood-sized player (sizes close to Blood's, not exact) and runs Blood's
// triggers in a simplified form: using walls, sectors and sprites (switches), entering and leaving
// sectors, picking things up, keys, locks, the tx/rx channels with their commands, Z motion doors
// and lifts with their timing, secrets, messages and level exits. Enemies stand still; sliding,
// rotating and path sectors only change state.
(function (root) {
  'use strict';
  const G = () => root.GameFiles;
  const Geo = () => root.MapGeo;

  const PLAYER = { radius: 128, height: 720, eye: 576, step: 256, jump: 5.3, gravity: 0.55, speed: 30, run: 56, reach: 1000 }; // map units, per 1/60 s
  const Z = 16; // z units per map unit

  // ---- matrices -----------------------------------------------------------------------------------
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
    for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) { let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s; }
    return o;
  }

  function tileColor(pic, kind) {
    const h = (pic * 2654435761) >>> 0;
    const hue = (h % 360) / 360, sat = 0.25 + ((h >> 9) % 30) / 100, val = kind === 'floor' ? 0.42 : kind === 'ceil' ? 0.3 : 0.52;
    const i = Math.floor(hue * 6), f = hue * 6 - i, p = val * (1 - sat), q = val * (1 - f * sat), t = val * (1 - (1 - f) * sat);
    return [[val, t, p], [q, val, p], [p, val, t], [p, q, val], [t, p, val], [val, p, q]][i % 6];
  }
  const tileSize = (pic) => { const t = G() && G().tile(pic); return t ? [t.w, t.h] : [64, 64]; };
  // sprites only the editor shows: markers, generators, sound sources
  const editorOnly = (sp) => (sp.type >= 1 && sp.type <= 19) || (sp.type >= 700 && sp.type < 800) || (sp.cstat & 0x8000) || sp.statnum >= 1024;

  // ---- the level as triangles ------------------------------------------------------------------------
  // world: X = map x, Y = height up (map units), Z = map y. uv in tile pixels.
  function buildMesh(map, opts) {
    opts = opts || {};
    if (!opts.wallSector) opts.wallSector = wallSectors(map);
    const geo = Geo();
    const batches = new Map();
    const batch = (pic, flags) => {
      const key = pic + ':' + (flags || '');
      let b = batches.get(key);
      if (!b) batches.set(key, b = { pic, ceil: flags === 'c', see: flags === 'm', pos: [], uv: [], col: [], shade: [] });
      return b;
    };
    const vis = (s) => map.sectors[s].visibility;
    const push = (b, x, y, z, u, v, c, sh) => { b.pos.push(x, y, z); b.uv.push(u, v); b.col.push(c[0], c[1], c[2]); b.shade.push(sh); };
    // a wall piece between heights (z units, per end: za1..za2 at a, zb1..zb2 at b), anchored at zref
    function wallPiece(w, wall, a, b, za1, za2, zb1, zb2, pic, zref, flags, sh) {
      if (za2 - za1 <= 0.5 && zb2 - zb1 <= 0.5) return;
      if (za2 < za1) za2 = za1;
      if (zb2 < zb1) zb2 = zb1;
      const [tw, th] = tileSize(pic);
      const ul = wall.xrepeat * 8;
      let u0 = wall.xpanning, u1 = wall.xpanning + ul;
      if (wall.cstat & 8) { u0 = -u0; u1 = u0 - ul; }
      const yscale = wall.yrepeat / 2048 * (tw ? 1 : 1);
      const vy = (z) => ((wall.cstat & 256) ? -1 : 1) * (z - zref) * yscale + wall.ypanning * th / 256;
      const c = tileColor(pic, 'wall').map((x) => x * (0.78 + 0.22 * Math.cos(Math.atan2(b[1] - a[1], b[0] - a[0]) * 2)));
      const bt = batch(pic, flags);
      const q = [[a, za1, u0], [b, zb1, u1], [b, zb2, u1], [a, za2, u0]];
      for (const k of [0, 1, 2, 0, 2, 3]) { const [p, z, u] = q[k]; push(bt, p[0], -z / Z, p[1], u, vy(z), c, sh); }
    }
    const sz = (s, x, y, ceil) => geo.slopeZ(map, s, x, y, ceil);
    map.walls.forEach((wall, w) => {
      const s = opts.wallSector[w];
      if (s < 0) return;
      const sec = map.sectors[s], o = map.walls[wall.point2];
      if (!o) return;
      const a = [wall.x, wall.y], b = [o.x, o.y];
      const sh = wall.shade + vis(s) * 0;
      const cA = sz(s, a[0], a[1], true), cB = sz(s, b[0], b[1], true), fA = sz(s, a[0], a[1], false), fB = sz(s, b[0], b[1], false);
      const n = wall.nextsector;
      if (n < 0 || n >= map.sectors.length) {
        wallPiece(w, wall, a, b, cA, fA, cB, fB, wall.picnum, (wall.cstat & 4) ? sec.floorz : sec.ceilingz, '', sh);
        return;
      }
      const nsec = map.sectors[n];
      const ncA = sz(n, a[0], a[1], true), ncB = sz(n, b[0], b[1], true), nfA = sz(n, a[0], a[1], false), nfB = sz(n, b[0], b[1], false);
      // a step up ahead: the lower part (its tile from the other side with "swap bottom")
      if (nfA < fA || nfB < fB) {
        const lw = (wall.cstat & 2) && map.walls[wall.nextwall] ? map.walls[wall.nextwall] : wall;
        wallPiece(w, lw === wall ? wall : { ...lw, cstat: wall.cstat }, a, b, Math.max(nfA, cA), fA, Math.max(nfB, cB), fB, lw.picnum,
          (wall.cstat & 4) ? sec.ceilingz : nsec.floorz, '', sh);
      }
      // a lower ceiling ahead: the upper part (none between two skies)
      const bothSky = (sec.ceilingstat & 1) && (nsec.ceilingstat & 1);
      if (!bothSky && (ncA > cA || ncB > cB)) wallPiece(w, wall, a, b, cA, Math.min(ncA, fA), cB, Math.min(ncB, fB), wall.picnum, (wall.cstat & 4) ? sec.ceilingz : nsec.ceilingz, '', sh);
      // a masked wall in the opening
      if ((wall.cstat & 16) && wall.overpicnum >= 0 && wall.nextwall > w) {
        wallPiece(w, wall, a, b, Math.max(cA, ncA), Math.min(fA, nfA), Math.max(cB, ncB), Math.min(fB, nfB), wall.overpicnum,
          (wall.cstat & 4) ? Math.min(sec.floorz, nsec.floorz) : Math.max(sec.ceilingz, nsec.ceilingz), 'm', sh);
      }
    });
    // floors and ceilings
    map.sectors.forEach((sec, s) => {
      let tris;
      try { tris = geo.triangulateSector(map, s); } catch (e) { tris = []; }
      for (const ceil of [false, true]) {
        const stat = ceil ? sec.ceilingstat : sec.floorstat;
        if (stat & 1) continue; // parallax sky
        const pic = ceil ? sec.ceilingpicnum : sec.floorpicnum;
        const [tw, th] = tileSize(pic);
        const xp = ceil ? sec.ceilingxpanning : sec.floorxpanning, yp = ceil ? sec.ceilingypanning : sec.floorypanning;
        const sh = ceil ? sec.ceilingshade : sec.floorshade;
        const c = tileColor(pic, ceil ? 'ceil' : 'floor');
        const scale = (stat & 8) ? 8 : 16;
        let w0 = map.walls[sec.wallptr], w1 = w0 && map.walls[w0.point2], rel = (stat & 64) && w0 && w1;
        let ca = 1, sa = 0;
        if (rel) { const L = Math.hypot(w1.x - w0.x, w1.y - w0.y) || 1; ca = (w1.x - w0.x) / L; sa = (w1.y - w0.y) / L; }
        const uv = (x, y) => {
          let px = x, py = y;
          if (rel) { const dx = x - w0.x, dy = y - w0.y; px = dx * ca + dy * sa; py = -dx * sa + dy * ca; }
          let u = px / scale, v = py / scale;
          if (stat & 4) { const t = u; u = v; v = t; }
          if (stat & 16) u = -u;
          if (stat & 32) v = -v;
          return [u + xp * tw / 256, v + yp * th / 256];
        };
        const bt = batch(pic, ceil ? 'c' : '');
        for (const t of tris) {
          const order = ceil ? [t[0], t[2], t[1]] : t;
          for (const p of order) { const [u, v] = uv(p[0], p[1]); push(bt, p[0], -sz(s, p[0], p[1], ceil) / Z, p[1], u, v, c, sh); }
        }
      }
    });
    // wall- and floor-aligned sprites (the ones facing the viewer are made each frame)
    map.sprites.forEach((sp, i) => {
      if (!(sp.cstat & 48) || editorOnly(sp) || (opts.hidden && opts.hidden.has(i))) return;
      const t = G() && G().tile(sp.picnum);
      if (!t) return;
      const w = t.w * sp.xrepeat / 4, h = t.h * sp.yrepeat / 4;
      const bt = batch(sp.picnum, 'm');
      const a = (sp.ang & 2047) / 2048 * Math.PI * 2;
      const c = [1, 1, 1];
      const fu = (sp.cstat & 4) ? [t.w, 0] : [0, t.w], fv = (sp.cstat & 8) ? [0, t.h] : [t.h, 0];
      if ((sp.cstat & 48) === 16) { // wall sprite: stands along the line across its angle
        const dx = -Math.sin(a) * w / 2, dy = Math.cos(a) * w / 2;
        const zb = (sp.cstat & 128) ? sp.z + h * Z / 2 : sp.z, zt = zb - h * Z;
        const q = [[sp.x - dx, sp.y - dy, zb, fu[0], fv[0]], [sp.x + dx, sp.y + dy, zb, fu[1], fv[0]], [sp.x + dx, sp.y + dy, zt, fu[1], fv[1]], [sp.x - dx, sp.y - dy, zt, fu[0], fv[1]]];
        for (const k of [0, 1, 2, 0, 2, 3]) { const v = q[k]; push(bt, v[0], -v[2] / Z, v[1], v[3], v[4], c, sp.shade); }
      } else { // floor sprite: lies flat, turned by its angle
        const ux = Math.cos(a), uy = Math.sin(a);
        const corner = (sx, sy) => [sp.x + ux * sx - uy * sy, sp.y + uy * sx + ux * sy];
        const q = [[...corner(-h / 2, -w / 2), fu[0], fv[1]], [...corner(-h / 2, w / 2), fu[1], fv[1]], [...corner(h / 2, w / 2), fu[1], fv[0]], [...corner(h / 2, -w / 2), fu[0], fv[0]]];
        for (const k of [0, 1, 2, 0, 2, 3]) { const v = q[k]; push(bt, v[0], -sp.z / Z + 1, v[1], v[2], v[3], c, sp.shade); }
      }
    });
    const list = [...batches.values()].sort((a, b) => (a.ceil - b.ceil) || (a.see - b.see));
    let count = 0;
    for (const b of list) { b.first = count; b.count = b.pos.length / 3; count += b.count; }
    return { batches: list, count };
  }
  function wallSectors(map) {
    const out = new Int32Array(map.walls.length).fill(-1);
    map.sectors.forEach((s, i) => { for (let w = s.wallptr; w < s.wallptr + s.wallnum && w < out.length; w++) out[w] = i; });
    return out;
  }

  // ---- WebGL --------------------------------------------------------------------------------------
  const VS = `attribute vec3 aPos; attribute vec2 aUV; attribute vec3 aCol; attribute float aShade;
uniform mat4 uMVP; uniform vec3 uEye;
varying vec2 vUV; varying vec3 vCol; varying float vShade; varying float vDist; varying vec3 vPos;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); vUV = aUV; vCol = aCol; vShade = aShade; vDist = distance(aPos, uEye); vPos = aPos; }`;
  const FS = `precision mediump float;
varying vec2 vUV; varying vec3 vCol; varying float vShade; varying float vDist; varying vec3 vPos;
uniform int uMode; uniform sampler2D uTex; uniform vec2 uSize; uniform sampler2D uPal; uniform sampler2D uLook;
uniform float uVis; uniform float uSeeThrough; uniform vec3 uFog; uniform float uFogDist;
void main() {
  if (uMode == 0) {
    vec3 p = vPos / 256.0;
    float g = 0.97 - 0.06 * (step(fract(p.x), 0.03) + step(fract(p.z), 0.03) + step(fract(p.y), 0.03));
    gl_FragColor = vec4(mix(vCol * g * clamp(1.0 - vShade / 64.0, 0.2, 1.3), uFog, clamp(vDist / uFogDist, 0.0, 0.85)), 1.0);
    return;
  }
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
    const sh = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
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
    for (const [k, v] of [[gl.TEXTURE_MIN_FILTER, gl.NEAREST], [gl.TEXTURE_MAG_FILTER, gl.NEAREST], [gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE], [gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE]]) gl.texParameteri(gl.TEXTURE_2D, k, v);
    return t;
  }
  function gameTextures(r) {
    const g = G();
    if (!g || !g.ready()) return false;
    const gl = r.gl;
    if (!r.palTex) {
      r.palTex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, r.palTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, 256, 1, 0, gl.RGB, gl.UNSIGNED_BYTE, g.palette());
      for (const [k, v] of [[gl.TEXTURE_MIN_FILTER, gl.NEAREST], [gl.TEXTURE_MAG_FILTER, gl.NEAREST], [gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE], [gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE]]) gl.texParameteri(gl.TEXTURE_2D, k, v);
      if (g.shades()) r.lookTex = lumTexture(gl, 256, 64, g.shades());
    }
    return true;
  }
  function tileTexture(r, pic) {
    if (r.tiles.has(pic)) return r.tiles.get(pic);
    const t = G() && G().tile(pic);
    const v = t ? { tex: lumTexture(r.gl, t.h, t.w, t.data), w: t.w, h: t.h } : null;
    r.tiles.set(pic, v);
    return v;
  }
  function flatten(batches) {
    const n = batches.reduce((s, b) => s + b.count, 0);
    const out = { pos: new Float32Array(n * 3), uv: new Float32Array(n * 2), col: new Float32Array(n * 3), shade: new Float32Array(n) };
    let o = 0;
    for (const b of batches) { out.pos.set(b.pos, o * 3); out.uv.set(b.uv, o * 2); out.col.set(b.col, o * 3); out.shade.set(b.shade, o); o += b.count; }
    return out;
  }
  function uploadInto(r, bufs, data) {
    const gl = r.gl;
    for (const k of ['pos', 'uv', 'col', 'shade']) { gl.bindBuffer(gl.ARRAY_BUFFER, bufs[k]); gl.bufferData(gl.ARRAY_BUFFER, data[k], gl.DYNAMIC_DRAW); }
  }
  function bindBuffers(r, bufs) {
    const gl = r.gl;
    for (const [k, n] of [['pos', 3], ['uv', 2], ['col', 3], ['shade', 1]]) {
      gl.bindBuffer(gl.ARRAY_BUFFER, bufs[k]); gl.enableVertexAttribArray(r.a[k]); gl.vertexAttribPointer(r.a[k], n, gl.FLOAT, false, 0, 0);
    }
  }

  // ---- walking -----------------------------------------------------------------------------------
  // The player is a circle on the map plus a height range, in a sector. A wall stops it when it's
  // solid (or marked blocking), the step up is too high, or the opening is too low.
  function Walker(map, opts) {
    opts = opts || {};
    const geo = Geo();
    const ws = wallSectors(map);
    const floorAt = (s, x, y) => -geo.slopeZ(map, s, x, y, false) / Z;
    const ceilAt = (s, x, y) => -geo.slopeZ(map, s, x, y, true) / Z;
    const st = { x: 0, y: 0, z: 0, vz: 0, ang: 0, pitch: 0, sector: -1, onGround: true };
    const start = map.sprites.find((sp) => sp.type === 1) || map.sprites.find((sp) => sp.type === 2);
    if (opts.at) Object.assign(st, opts.at);
    else if (start) { st.x = start.x; st.y = start.y; st.ang = ((start.ang & 2047) / 2048) * Math.PI * 2; }
    else { st.x = map.header.startX; st.y = map.header.startY; st.ang = ((map.header.startAng & 2047) / 2048) * Math.PI * 2; }
    st.sector = geo.sectorAt(map, st.x, st.y, start ? start.sectnum : map.header.startSector);
    if (st.sector >= 0 && !opts.at) st.z = floorAt(st.sector, st.x, st.y);

    function blocks(w, x, y, z) {
      const wall = map.walls[w], n = wall.nextsector;
      if (n < 0 || n >= map.sectors.length || (wall.cstat & 1)) return true;
      const s = ws[w];
      const f = floorAt(n, x, y), c = Math.min(ceilAt(n, x, y), ceilAt(s, x, y));
      if (f - z > PLAYER.step) return true;
      return c - Math.max(f, z) < PLAYER.height * 0.8;
    }
    const closest = (p, a, b) => {
      const dx = b[0] - a[0], dy = b[1] - a[1], l = dx * dx + dy * dy;
      const t = l ? Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l)) : 0;
      return [a[0] + dx * t, a[1] + dy * t];
    };
    const cr = (o, u, v) => (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0]);
    const crosses = (p, q, a, b) => { const d1 = cr(a, b, p), d2 = cr(a, b, q), d3 = cr(p, q, a), d4 = cr(p, q, b); return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)); };
    // the walls near the player: its sector and the ones around it
    function nearWalls() {
      const out = [], seen = new Set();
      const add = (s) => {
        if (s < 0 || seen.has(s)) return; seen.add(s);
        const sec = map.sectors[s];
        for (let w = sec.wallptr; w < sec.wallptr + sec.wallnum; w++) out.push(w);
      };
      add(st.sector);
      const sec = map.sectors[st.sector];
      if (sec) for (let w = sec.wallptr; w < sec.wallptr + sec.wallnum; w++) { const n = map.walls[w].nextsector; if (n >= 0) { add(n); const ns = map.sectors[n]; for (let k = ns.wallptr; k < ns.wallptr + ns.wallnum; k++) add(map.walls[k].nextsector); } }
      return out;
    }
    function tryMove(dx, dy) {
      if (st.sector < 0) return;
      let p = [st.x + dx, st.y + dy];
      const walls = nearWalls();
      for (let it = 0; it < 3; it++) {
        for (const w of walls) {
          const a = map.walls[w], b = map.walls[a.point2];
          if (!blocks(w, p[0], p[1], st.z)) continue;
          const c = closest(p, [a.x, a.y], [b.x, b.y]), ddx = p[0] - c[0], ddy = p[1] - c[1], d = Math.hypot(ddx, ddy);
          if (d < PLAYER.radius) { if (d > 0.001) { p[0] = c[0] + ddx / d * PLAYER.radius; p[1] = c[1] + ddy / d * PLAYER.radius; } else p = [st.x, st.y]; }
        }
      }
      for (const w of walls) { const a = map.walls[w], b = map.walls[a.point2]; if (blocks(w, p[0], p[1], st.z) && crosses([st.x, st.y], p, [a.x, a.y], [b.x, b.y])) return; }
      const s = geo.sectorAt(map, p[0], p[1], st.sector);
      if (s < 0) return;
      if (floorAt(s, p[0], p[1]) - st.z > PLAYER.step) return;
      if (ceilAt(s, p[0], p[1]) - Math.max(st.z, floorAt(s, p[0], p[1])) < PLAYER.height * 0.8) return;
      st.x = p[0]; st.y = p[1]; st.sector = s;
    }
    // what "use" reaches: the nearest wall straight ahead in this sector, or the sector itself
    function useRay() {
      const a = [st.x, st.y], b = [st.x + Math.cos(st.ang) * PLAYER.reach, st.y + Math.sin(st.ang) * PLAYER.reach];
      let best = -1, bd = Infinity;
      for (const w of nearWalls()) {
        if (ws[w] !== st.sector) continue;
        const wa = map.walls[w], wb = map.walls[wa.point2];
        if (!crosses(a, b, [wa.x, wa.y], [wb.x, wb.y])) continue;
        const c = closest(a, [wa.x, wa.y], [wb.x, wb.y]), d = Math.hypot(c[0] - a[0], c[1] - a[1]);
        if (d < bd) { bd = d; best = w; }
      }
      return { wall: best, dist: bd };
    }
    return {
      st, PLAYER, useRay, floorAt, ceilAt,
      step(fwd, side, run, jump, dt) {
        const sp = (run ? PLAYER.run : PLAYER.speed) * dt;
        const cs = Math.cos(st.ang), sn = Math.sin(st.ang);
        const dx = (cs * fwd - sn * side) * sp, dy = (sn * fwd + cs * side) * sp;
        const n = Math.max(1, Math.ceil(Math.hypot(dx, dy) / (PLAYER.radius / 2)));
        for (let i = 0; i < n; i++) tryMove(dx / n, dy / n);
        if (st.sector < 0) return;
        const fl = floorAt(st.sector, st.x, st.y), cl = ceilAt(st.sector, st.x, st.y);
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

  // ---- Blood's triggers, simplified ---------------------------------------------------------------
  // objects: { kind: 'sector'|'wall'|'sprite', i } with their extra data (ext)
  function Triggers(map, events) {
    let quiet = false;
    const keys = new Set(), say = (t) => { if (!quiet && events.say) events.say(t); }, secretTotal = { n: 0 }, found = new Set();
    const movers = new Map(); // sector -> { from, to, t, dur, target }
    const ext = (o) => (o.kind === 'sector' ? map.sectors : o.kind === 'wall' ? map.walls : map.sprites)[o.i].ext;
    const all = [];
    map.sectors.forEach((s, i) => { if (s.ext) all.push({ kind: 'sector', i }); });
    map.walls.forEach((w, i) => { if (w.ext) all.push({ kind: 'wall', i }); });
    map.sprites.forEach((s, i) => { if (s.ext) all.push({ kind: 'sprite', i }); });
    const timers = [];
    let now = 0;
    const isZMotion = (s) => map.sectors[s].type === 600 || map.sectors[s].type === 602;
    // z motion sectors start where their state says (the game does the same when a level starts)
    map.sectors.forEach((s, i) => {
      if (!isZMotion(i) || !s.ext) return;
      const x = s.ext, k = x.state ? 1 : 0;
      setBusy(i, k);
    });
    function setBusy(s, k) {
      const sec = map.sectors[s], x = sec.ext;
      if (x.offFloorZ !== x.onFloorZ) sec.floorz = Math.round(x.offFloorZ + (x.onFloorZ - x.offFloorZ) * k);
      if (x.offCeilZ !== x.onCeilZ) sec.ceilingz = Math.round(x.offCeilZ + (x.onCeilZ - x.offCeilZ) * k);
      x._busy = k;
      if (sec.type === 602) for (const sp of map.sprites) if (sp.sectnum === s) { if (sp._z0 === undefined) sp._z0 = sp.z - x.offFloorZ; sp.z = sec.floorz + sp._z0; }
    }
    function setState(o, state) {
      const x = ext(o);
      if (x.state === state) return;
      x.state = state;
      if (o.kind === 'sector') {
        if (state && x.reTriggerA && x.waitTimeA) timers.push({ at: now + x.waitTimeA * 100, o, cmd: 0 });
        if (!state && x.reTriggerB && x.waitTimeB) timers.push({ at: now + x.waitTimeB * 100, o, cmd: 1 });
      } else if (x.waitTime && x.restState !== state) timers.push({ at: now + x.waitTime * 100, o, cmd: x.restState ? 1 : 0 });
      if (x.command !== 5 && x.txID && ((state && x.triggerOn) || (!state && x.triggerOff))) send(x.txID, x.command, o);
    }
    function operate(o, cmd) {
      const x = ext(o);
      if (!x) return;
      if (cmd === 6) { x.locked = 1; return; }
      if (cmd === 7) { x.locked = 0; return; }
      if (cmd === 8) { x.locked = x.locked ? 0 : 1; return; }
      const want = cmd === 0 ? 0 : cmd === 1 || cmd >= 64 ? 1 : x.state ? 0 : 1;
      if (o.kind === 'sector') {
        const sec = map.sectors[o.i];
        if (isZMotion(o.i)) {
          const cur = x._busy || 0;
          if (cur === want && !movers.has(o.i)) return;
          const dur = Math.max(1, (want ? x.busyTimeA : x.busyTimeB) * 100);
          movers.set(o.i, { from: cur, to: want, t: 0, dur: dur * Math.abs(want - cur) || 1 });
          return;
        }
        if (sec.type >= 611 && sec.type <= 617 && !x._told) { x._told = 1; say('(This sliding/rotating sector isn\'t moved in the walkthrough)'); }
      }
      if (o.kind === 'sprite') {
        const sp = map.sprites[o.i];
        if (sp.type === 21 && x.state) return; // one-way switch
        if (sp.type >= 20 && sp.type <= 23) say('Switch ' + (want ? 'on' : 'off'));
      }
      setState(o, want);
    }
    function send(channel, cmd, from) {
      if (channel === 1) { secretTotal.n = Math.max(0, cmd - 64); return; }
      if (channel === 2) {
        const key = from ? from.kind + from.i : Math.random();
        if (found.has(key)) return;
        found.add(key);
        say('A secret is revealed!' + (secretTotal.n ? ' (' + found.size + ' of ' + secretTotal.n + ')' : ''));
        return;
      }
      if (channel === 3) { say('(A message: text #' + Math.max(0, cmd - 64) + ')'); return; }
      if (channel === 4) { say('Level exit!'); return; }
      if (channel === 5) { say('Secret level exit!'); return; }
      for (const o of all) { const x = ext(o); if (x.rxID === channel && o !== from) operate(o, cmd); }
    }
    const KEY_NAMES = ['', 'skull key', 'eye key', 'fire key', 'dagger key', 'spider key', 'moon key', 'key 7'];
    function trigger(o, cmd) {
      const x = ext(o);
      if (!x || x.locked || x.isTriggered) return false;
      const need = o.kind === 'sector' ? x.Key : x.key;
      if (need && !keys.has(need)) { say('You need the ' + (KEY_NAMES[need] || 'key ' + need)); return true; }
      if (x.triggerOnce) x.isTriggered = 1;
      if (x.decoupled && x.txID) send(x.txID, x.command, o);
      else operate(o, cmd);
      return true;
    }
    return {
      keys, found, secretTotal, trigger, send, operate,
      start() { const real = say; quiet = true; send(7, 1); send(9, 1); quiet = false; },
      // the player went from one sector into another
      moved(from, to) {
        if (from >= 0 && map.sectors[from] && map.sectors[from].ext && map.sectors[from].ext.Exit) trigger({ kind: 'sector', i: from }, 43);
        if (to >= 0 && map.sectors[to] && map.sectors[to].ext && map.sectors[to].ext.Enter) trigger({ kind: 'sector', i: to }, 42);
      },
      // moves doors and lifts on; true when anything moved (the level has to be drawn again)
      tick(dtMs) {
        now += dtMs;
        for (let k = timers.length - 1; k >= 0; k--) if (timers[k].at <= now) { const t = timers.splice(k, 1)[0]; operate(t.o, t.cmd); }
        let moved = false;
        for (const [s, m] of movers) {
          m.t = Math.min(m.dur, m.t + dtMs);
          const k = m.from + (m.to - m.from) * (m.t / m.dur);
          setBusy(s, k);
          moved = true;
          if (m.t >= m.dur) { movers.delete(s); setState({ kind: 'sector', i: s }, m.to); }
        }
        return moved;
      },
      moving: (s) => movers.has(s),
    };
  }

  // ---- the viewer ------------------------------------------------------------------------------------
  // map: MapFile's model (it's changed while walking: doors move; pass a copy)
  function open(map, overlay, options) {
    options = options || {};
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
    const hidden = new Set(); // sprites picked up
    const ws = wallSectors(map);
    let mesh = buildMesh(map, { wallSector: ws, hidden });
    if (r) uploadInto(r, r.level, flatten(mesh.batches));
    const walker = Walker(map, options);
    function message(t) { msgEl.textContent = t; msgEl.classList.add('show'); clearTimeout(message.t); message.t = setTimeout(() => msgEl.classList.remove('show'), 2500); }
    const trig = Triggers(map, { say: message });
    let started = false;
    // the overview's camera
    let x1 = Infinity, y1 = Infinity, x2 = -Infinity, y2 = -Infinity, top = -Infinity, bottom = Infinity;
    for (const w of map.walls) { x1 = Math.min(x1, w.x); y1 = Math.min(y1, w.y); x2 = Math.max(x2, w.x); y2 = Math.max(y2, w.y); }
    for (const s of map.sectors) { top = Math.max(top, -s.ceilingz / Z); bottom = Math.min(bottom, -s.floorz / Z); }
    if (!isFinite(x1)) { x1 = y1 = 0; x2 = y2 = 4096; top = 2048; bottom = 0; }
    const orbit = { cx: (x1 + x2) / 2, cy: (top + bottom) / 2, cz: (y1 + y2) / 2, yaw: 0.7, pitch: 0.75, dist: Math.max(x2 - x1, y2 - y1) * 1.1 + 3000 };
    let mode = options.walk ? 'walk' : 'orbit', running = true, last = performance.now(), run = false, usePressed = false, jumpPressed = false;
    const keys = {}, stick = { x: 0, y: 0, id: null }, look = { id: null, x: 0, y: 0 };
    let rebuild = false, lastSector = walker.st.sector;

    function setMode(m) {
      mode = m;
      overlay.querySelectorAll('[data-m]').forEach((b) => b.classList.toggle('on', b.dataset.m === m || b.dataset.m === 'close'));
      overlay.classList.toggle('walking', m === 'walk');
      if (m === 'walk' && !started) { started = true; trig.start(); }
      help.textContent = (m === 'walk'
        ? 'Left: move. Right: look. Use: doors, switches, lifts. Keyboard: WASD, mouse drag, E use, Space jump, Shift run. Sizes are close to Blood\'s; enemies stand still.'
        : 'Drag to turn the map, pinch or scroll to zoom. Ceilings are left out here; Walk shows them.')
        + (textured ? '' : ' (Load your Blood files in ☰ for the real textures.)');
      help.classList.remove('gone'); clearTimeout(help.t); help.t = setTimeout(() => help.classList.add('gone'), 8000);
      if (m === 'walk' && walker.st.sector < 0) info.textContent = 'No start inside a sector';
    }
    const kd = (e) => { keys[e.code] = true; if (e.code === 'Space') e.preventDefault(); if (e.code === 'KeyE' || e.code === 'Enter') usePressed = true; };
    const ku = (e) => { keys[e.code] = false; };
    function close() { running = false; overlay.classList.remove('open', 'walking'); overlay.innerHTML = ''; window.removeEventListener('keydown', kd); window.removeEventListener('keyup', ku); if (options.onClose) options.onClose(walker.st); }
    overlay.querySelectorAll('[data-m]').forEach((b) => b.onclick = () => { if (b.dataset.m === 'close') close(); else setMode(b.dataset.m); });
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
      if (pinch.size === 1) { orbit.yaw += (e.offsetX - prev[0]) * 0.008; orbit.pitch = Math.max(0.05, Math.min(1.5, orbit.pitch + (e.offsetY - prev[1]) * 0.006)); }
      pinch.set(e.pointerId, [e.offsetX, e.offsetY]);
      if (pinch.size === 2) { const [a, b] = [...pinch.values()]; const d = Math.hypot(a[0] - b[0], a[1] - b[1]); if (pinchD) orbit.dist = Math.max(800, Math.min(400000, orbit.dist * pinchD / d)); pinchD = d; }
    });
    const end = (e) => { pinch.delete(e.pointerId); if (e.pointerId === stick.id) { stick.id = null; stick.x = stick.y = 0; knob.style.transform = ''; stickEl.classList.remove('on'); } if (e.pointerId === look.id) look.id = null; };
    cv.addEventListener('pointerup', end); cv.addEventListener('pointercancel', end);
    cv.addEventListener('wheel', (e) => { e.preventDefault(); orbit.dist = Math.max(800, Math.min(400000, orbit.dist * Math.exp(e.deltaY * 0.001))); }, { passive: false });

    // pressing use: a switch or other sprite in front, a wall, the sector behind the wall, or the sector the player is in
    function doUse() {
      const st = walker.st, ray = walker.useRay();
      let bestSprite = -1, bd = ray.wall >= 0 ? ray.dist : PLAYER.reach;
      map.sprites.forEach((sp, i) => {
        if (!sp.ext || !sp.ext.Push || hidden.has(i)) return;
        const dx = sp.x - st.x, dy = sp.y - st.y, d = Math.hypot(dx, dy);
        if (d > bd + 64) return;
        const off = Math.abs(((Math.atan2(dy, dx) - st.ang + Math.PI * 3) % (Math.PI * 2)) - Math.PI);
        if (off < 0.45 && Math.abs(-sp.z / Z - (st.z + PLAYER.eye)) < 900) { bd = d; bestSprite = i; }
      });
      if (bestSprite >= 0) { trig.trigger({ kind: 'sprite', i: bestSprite }, 30); return; }
      if (ray.wall >= 0) {
        const w = map.walls[ray.wall];
        if (w.ext && w.ext.triggerPush) { trig.trigger({ kind: 'wall', i: ray.wall }, 50); return; }
        const n = w.nextsector;
        if (n >= 0 && map.sectors[n].ext && map.sectors[n].ext.Wallpush) { trig.trigger({ kind: 'sector', i: n }, 40); return; }
      }
      const s = st.sector;
      if (s >= 0 && map.sectors[s].ext && map.sectors[s].ext.Push) trig.trigger({ kind: 'sector', i: s }, 40);
    }
    // walking over things, into sectors
    function gameplay() {
      const st = walker.st;
      if (st.sector !== lastSector) { trig.moved(lastSector, st.sector); lastSector = st.sector; }
      map.sprites.forEach((sp, i) => {
        if (hidden.has(i) || Math.hypot(sp.x - st.x, sp.y - st.y) > 220 || Math.abs(-sp.z / Z - st.z) > 900) return;
        const item = (sp.type >= 40 && sp.type < 51) || (sp.type >= 60 && sp.type < 81) || (sp.type >= 100 && sp.type <= 148);
        if (sp.type >= 100 && sp.type <= 106) { trig.keys.add(sp.type - 99); message('Picked up the ' + ['skull', 'eye', 'fire', 'dagger', 'spider', 'moon', 'seventh'][sp.type - 100] + ' key'); }
        if (sp.ext && (sp.ext.Pickup || sp.ext.Touch)) trig.trigger({ kind: 'sprite', i }, sp.ext.Pickup ? 32 : 33);
        if (item) { hidden.add(i); rebuild = true; }
      });
    }
    // sprites facing the viewer
    function spriteBatches(eye) {
      if (!textured) return [];
      const out = new Map();
      map.sprites.forEach((sp, i) => {
        if ((sp.cstat & 48) || editorOnly(sp) || hidden.has(i)) return;
        const t = G().tile(sp.picnum);
        if (!t) return;
        const w = t.w * sp.xrepeat / 4, h = t.h * sp.yrepeat / 4;
        const pan = G().tile(sp.picnum).picanm || 0;
        const xo = ((pan >> 8) << 24 >> 24) + sp.xoffset, yo = ((pan >> 16) << 24 >> 24) + sp.yoffset;
        const dx = eye[0] - sp.x, dz = eye[2] - sp.y, l = Math.hypot(dx, dz) || 1;
        const rx = -dz / l, rz = dx / l, sx = sp.xrepeat / 4, sy = sp.yrepeat / 4;
        const cx = sp.x + rx * xo * sx, cz = sp.y + rz * xo * sx;
        let y0 = -sp.z / Z + yo * sy;
        if (sp.cstat & 128) y0 -= h / 2;
        // items and enemies: the game drops them onto the floor when the level starts
        if ((sp.statnum === 3 || sp.statnum === 6) && sp.sectnum >= 0 && sp.sectnum < map.sectors.length) {
          const fl = -Geo().slopeZ(map, sp.sectnum, sp.x, sp.y, false) / Z;
          if (y0 < fl + 64) y0 = fl + yo * sy;
        }
        const y1 = y0 + h;
        let b = out.get(sp.picnum);
        if (!b) out.set(sp.picnum, b = { pic: sp.picnum, see: true, pos: [], uv: [], col: [], shade: [] });
        const fu = (sp.cstat & 4) ? [t.w - 0.01, 0] : [0, t.w - 0.01], fv = (sp.cstat & 8) ? [0, t.h - 0.01] : [t.h - 0.01, 0];
        const q = [[cx - rx * w / 2, y0, cz - rz * w / 2, fu[0], fv[0]], [cx + rx * w / 2, y0, cz + rz * w / 2, fu[1], fv[0]], [cx + rx * w / 2, y1, cz + rz * w / 2, fu[1], fv[1]], [cx - rx * w / 2, y1, cz - rz * w / 2, fu[0], fv[1]]];
        for (const k of [0, 1, 2, 0, 2, 3]) { const v = q[k]; b.pos.push(v[0], v[1], v[2]); b.uv.push(v[3], v[4]); b.col.push(1, 1, 1); b.shade.push(sp.shade); }
      });
      let first = 0; const list = [...out.values()];
      for (const b of list) { b.count = b.pos.length / 3; b.first = first; first += b.count; }
      return list;
    }
    function skyBatches(eye) {
      if (!textured) return [];
      const sec = map.sectors.find((s) => s.ceilingstat & 1);
      if (!sec) return [];
      const base = sec.ceilingpicnum, n = 1 << Math.min(5, map.header.skyBits || 0) || 1, R = 60000;
      const out = [];
      for (let i = 0; i < Math.max(n, 4); i++) {
        const off = map.skyOffsets[i % n] || 0, pic = G().tile(base + off) ? base + off : base, tile = G().tile(pic);
        if (!tile) continue;
        const a1 = (i / Math.max(n, 4)) * Math.PI * 2, a2 = ((i + 1) / Math.max(n, 4)) * Math.PI * 2;
        const p1 = [eye[0] + Math.cos(a1) * R, eye[2] + Math.sin(a1) * R], p2 = [eye[0] + Math.cos(a2) * R, eye[2] + Math.sin(a2) * R];
        const yb = eye[1] - R * 0.3, yt = eye[1] + R * 0.9;
        const b = { pic, see: false, pos: [], uv: [], col: [], shade: [] };
        const q = [[p1, yb, 0, tile.h - 0.01], [p2, yb, tile.w - 0.01, tile.h - 0.01], [p2, yt, tile.w - 0.01, 0], [p1, yt, 0, 0]];
        for (const k of [0, 1, 2, 0, 2, 3]) { const [p, y, u, v] = q[k]; b.pos.push(p[0], y, p[1]); b.uv.push(u, v); b.col.push(0, 0, 0); b.shade.push(0); }
        out.push(b);
      }
      let first = 0; for (const b of out) { b.count = 6; b.first = first; first += 6; }
      return out;
    }
    function drawBatches(list, bufs, uploadData, skipCeil) {
      const gl = r.gl;
      if (uploadData) uploadInto(r, bufs, flatten(list));
      bindBuffers(r, bufs);
      for (const b of list) {
        if (!b.count || (skipCeil && b.ceil)) continue;
        const t = textured ? tileTexture(r, b.pic) : null;
        if (t) {
          gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, t.tex);
          gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, r.palTex);
          if (r.lookTex) { gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, r.lookTex); }
          gl.uniform1i(r.u.mode, r.lookTex ? 1 : 2); gl.uniform2f(r.u.size, t.w, t.h); gl.uniform1f(r.u.see, b.see ? 1 : 0);
        } else { if (b.see) continue; gl.uniform1i(r.u.mode, 0); }
        gl.drawArrays(gl.TRIANGLES, b.first, b.count);
      }
    }

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
        gameplay();
        if (trig.tick(dtMs)) rebuild = true;
        eye = walker.eye();
        const st = walker.st;
        at = [eye[0] + Math.cos(st.ang) * Math.cos(st.pitch) * 100, eye[1] + Math.sin(st.pitch) * 100, eye[2] + Math.sin(st.ang) * Math.cos(st.pitch) * 100];
        info.textContent = st.sector >= 0 ? 'Sector ' + st.sector + (trig.secretTotal.n ? '  secrets ' + trig.found.size + '/' + trig.secretTotal.n : '') : '';
      } else {
        eye = [orbit.cx + Math.cos(orbit.yaw) * Math.cos(orbit.pitch) * orbit.dist, orbit.cy + Math.sin(orbit.pitch) * orbit.dist, orbit.cz + Math.sin(orbit.yaw) * Math.cos(orbit.pitch) * orbit.dist];
        at = [orbit.cx, orbit.cy, orbit.cz];
        info.textContent = map.sectors.length + ' sectors, ' + map.sprites.length + ' sprites';
      }
      usePressed = jumpPressed = false;
      if (rebuild) { rebuild = false; mesh = buildMesh(map, { wallSector: ws, hidden }); if (r) uploadInto(r, r.level, flatten(mesh.batches)); }
      if (r) {
        const gl = r.gl;
        gl.viewport(0, 0, cv.width, cv.height);
        gl.clearColor(0.09, 0.05, 0.06, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.disable(gl.CULL_FACE);
        const proj = perspective(mode === 'walk' ? 1.25 : 0.9, cv.width / Math.max(1, cv.height), mode === 'walk' ? 16 : 64, 600000);
        gl.uniformMatrix4fv(r.u.mvp, false, mul(proj, lookAt(eye, at, [0, 1, 0])));
        gl.uniform3fv(r.u.eye, eye);
        gl.uniform3fv(r.u.fog, mode === 'walk' ? [0.09, 0.05, 0.06] : [0.12, 0.07, 0.07]);
        gl.uniform1f(r.u.fogDist, mode === 'walk' ? 20000 : orbit.dist * 4);
        gl.uniform1f(r.u.vis, mode === 'walk' ? (map.header.visibility || 0) / 327680 : 0);
        if (mode === 'walk') { gl.disable(gl.DEPTH_TEST); drawBatches(skyBatches(eye), r.dyn, true, false); }
        gl.enable(gl.DEPTH_TEST);
        drawBatches(mesh.batches, r.level, false, mode !== 'walk');
        drawBatches(spriteBatches(eye), r.dyn, true, false);
      }
      requestAnimationFrame(frame);
    }
    setMode(mode);
    requestAnimationFrame(frame);
    return { setMode, walker, triggers: trig, textured, map, close };
  }

  root.View3D = { open, buildMesh, Walker, Triggers, wallSectors, PLAYER };
})(typeof window !== 'undefined' ? window : this);
