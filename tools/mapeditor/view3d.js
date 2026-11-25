// Blood Map Maker: 3D preview and walkthrough (WebGL, no libraries).
//
// The map is drawn from the same sectors and walls the .MAP export writes (MapCore.buildMap), so
// openings, steps and sky look like they will in the game. There are no Blood textures here:
// every tile number gets its own colour and a pattern, which shows the shapes and the scale.
// Walking uses rough Blood-like player sizes (see PLAYER); it's a quick check that rooms are
// reachable and steps are climbable, not the game's exact physics.
(function (root) {
  'use strict';
  const M = root.MapCore;

  const PLAYER = { radius: 128, height: 720, eye: 576, step: 256, jump: 5.3, gravity: 0.55, speed: 30, run: 56 }; // map units, per frame at 60 fps

  // ---- small matrix helpers (column-major, like WebGL wants) ---------------------------------
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

  // a colour for a tile number: the same tile always looks the same
  function tileColor(pic, kind) {
    let h = (pic * 2654435761) >>> 0;
    const hue = (h % 360) / 360, sat = 0.25 + ((h >> 9) % 30) / 100, val = kind === 'floor' ? 0.42 : kind === 'ceil' ? 0.3 : 0.52;
    const i = Math.floor(hue * 6), f = hue * 6 - i, p = val * (1 - sat), q = val * (1 - f * sat), t = val * (1 - (1 - f) * sat);
    return [[val, t, p], [q, val, p], [p, val, t], [p, q, val], [t, p, val], [val, p, q]][i % 6];
  }
  const THING_COLORS = { Starts: [0.44, 0.75, 0.45], Weapons: [0.94, 0.64, 0.29], Ammo: [0.85, 0.76, 0.42], Health: [0.94, 0.36, 0.29], Armor: [0.44, 0.66, 0.86], 'Power-ups': [0.76, 0.48, 0.63], Keys: [1, 1, 1] };

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
      if (!cut) break; // shouldn't happen for a valid room; draw what we have
    }
    if (idx.length === 3) tris.push(idx.slice());
    return tris;
  }

  // ---- the level as triangles -------------------------------------------------------------------
  // world: X = map x, Y = height (up), Z = map y
  function buildMesh(prj) {
    const pos = [], col = [], uvw = [];
    const ceil = []; // ceilings go last: the overview leaves them out to look inside
    const m = M.buildMap(prj, false);
    const rooms = prj.rooms;
    const floorOf = (s) => rooms[s].floor, ceilOf = (s) => rooms[s].floor + rooms[s].height;
    const lightOf = (s) => Math.max(0.25, 1 - (rooms[s].light || 0) / 40);
    function quad(a, b, y1, y2, c) { // wall from map point a to b, heights y1..y2
      if (y2 <= y1) return;
      const v = [[a[0], y1, a[1]], [b[0], y1, b[1]], [b[0], y2, b[1]], [a[0], y2, a[1]]];
      for (const k of [0, 1, 2, 0, 2, 3]) { pos.push(...v[k]); col.push(...c); uvw.push(1); }
    }
    m.walls.forEach((w) => {
      const s = w.sector, o = m.walls[w.point2], a = [w.x, w.y], b = [o.x, o.y];
      const r = rooms[s];
      // light the walls a little by which way they face, so corners show
      const dir = Math.atan2(b[1] - a[1], b[0] - a[0]);
      const shade = (0.78 + 0.22 * Math.cos(dir * 2)) * lightOf(s);
      const c = tileColor(r.wallPic, 'wall').map((x) => x * shade);
      if (w.nextsector < 0) { quad(a, b, floorOf(s), ceilOf(s), c); return; }
      const n = w.nextsector;
      if (floorOf(n) > floorOf(s)) quad(a, b, floorOf(s), floorOf(n), c);               // a step up
      if (ceilOf(n) < ceilOf(s) && !(r.sky && rooms[n].sky)) quad(a, b, ceilOf(n), ceilOf(s), c); // lower ceiling ahead
    });
    rooms.forEach((r, s) => {
      const tris = triangulate(r.pts);
      const lf = lightOf(s);
      const cf = tileColor(r.floorPic, 'floor').map((x) => x * lf), cc = tileColor(r.ceilPic, 'ceil').map((x) => x * lf);
      for (const t of tris) {
        for (const k of t) { pos.push(r.pts[k][0], r.floor, r.pts[k][1]); col.push(...cf); uvw.push(0); }
        if (!r.sky) for (const k of [t[0], t[2], t[1]]) ceil.push([r.pts[k][0], r.floor + r.height, r.pts[k][1], cc]);
      }
    });
    // things: small boxes standing on the floor
    prj.things.forEach((t) => {
      const s = M.roomAt([t.x, t.y], rooms);
      if (s < 0) return;
      const info = M.THING_BY_TYPE[t.type] || { group: '' };
      const c = THING_COLORS[info.group] || [0.7, 0.7, 0.7];
      const big = info.start ? 96 : 56, hgt = info.start ? 320 : 112, y0 = rooms[s].floor;
      const x1 = t.x - big, x2 = t.x + big, z1 = t.y - big, z2 = t.y + big, y1 = y0, y2 = y0 + hgt;
      const faces = [[[x1, z1], [x2, z1]], [[x2, z1], [x2, z2]], [[x2, z2], [x1, z2]], [[x1, z2], [x1, z1]]];
      faces.forEach(([a, b], i) => quad(a, b, y1, y2, c.map((v) => v * (0.75 + 0.08 * i))));
      for (const [x, z] of [[x1, z1], [x2, z1], [x2, z2], [x1, z1], [x2, z2], [x1, z2]]) { pos.push(x, y2, z); col.push(...c); uvw.push(2); }
      if (info.start) { // a nose showing which way the start faces
        const a = ((t.ang || 0) / 2048) * Math.PI * 2, nx = t.x + Math.cos(a) * 220, nz = t.y + Math.sin(a) * 220;
        quad([t.x - Math.sin(a) * 30, t.y + Math.cos(a) * 30], [nx, nz], y2 - 90, y2 - 30, [0.9, 1, 0.9]);
      }
    });
    const ceilStart = pos.length / 3;
    for (const [x, y, z, c] of ceil) { pos.push(x, y, z); col.push(...c); uvw.push(0); }
    return { pos: new Float32Array(pos), col: new Float32Array(col), uvw: new Float32Array(uvw), map: m, ceilStart };
  }

  const VS = `attribute vec3 aPos; attribute vec3 aCol; attribute float aKind;
uniform mat4 uMVP; uniform vec3 uEye;
varying vec3 vCol; varying vec3 vPos; varying float vKind; varying float vDist;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); vCol = aCol; vPos = aPos; vKind = aKind; vDist = distance(aPos, uEye); }`;
  const FS = `precision mediump float;
varying vec3 vCol; varying vec3 vPos; varying float vKind; varying float vDist;
uniform vec3 uFog; uniform float uFogDist;
void main() {
  // a 256-unit pattern (Blood's walls are mostly 64-pixel tiles at that size) so size and movement show
  vec3 p = vPos / 256.0;
  float g;
  if (vKind < 0.5) g = step(0.5, fract(p.x * 0.5 + floor(p.z) * 0.5)) * 0.06 + 0.97 - 0.06 * (step(fract(p.x), 0.03) + step(fract(p.z), 0.03));
  else if (vKind < 1.5) g = 0.97 - 0.07 * step(fract(p.y), 0.03) - 0.05 * step(fract(p.x + p.z), 0.03);
  else g = 1.0;
  vec3 c = vCol * g;
  float fog = clamp(vDist / uFogDist, 0.0, 0.85);
  gl_FragColor = vec4(mix(c, uFog, fog), 1.0);
}`;

  function makeGL(canvas) {
    const gl = canvas.getContext('webgl', { antialias: true }) || canvas.getContext('experimental-webgl');
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
    return {
      gl, prog,
      aPos: gl.getAttribLocation(prog, 'aPos'), aCol: gl.getAttribLocation(prog, 'aCol'), aKind: gl.getAttribLocation(prog, 'aKind'),
      uMVP: gl.getUniformLocation(prog, 'uMVP'), uEye: gl.getUniformLocation(prog, 'uEye'), uFog: gl.getUniformLocation(prog, 'uFog'), uFogDist: gl.getUniformLocation(prog, 'uFogDist'),
      bufs: [gl.createBuffer(), gl.createBuffer(), gl.createBuffer()], count: 0,
    };
  }

  function upload(r, mesh) {
    const gl = r.gl;
    [[mesh.pos, r.aPos, 3], [mesh.col, r.aCol, 3], [mesh.uvw, r.aKind, 1]].forEach(([data, loc, n], i) => {
      gl.bindBuffer(gl.ARRAY_BUFFER, r.bufs[i]);
      gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
      gl.enableVertexAttribArray(loc);
      gl.vertexAttribPointer(loc, n, gl.FLOAT, false, 0, 0);
    });
    r.count = mesh.pos.length / 3;
    r.ceilStart = mesh.ceilStart;
  }

  // ---- walking ------------------------------------------------------------------------------------
  // Like Build: the player is a circle on the map plus a height range. A wall stops it when it's
  // solid, the step up is too high, or the opening is too low for the player.
  function Walker(prj, map) {
    const rooms = prj.rooms;
    const floorOf = (s) => rooms[s].floor, ceilOf = (s) => rooms[s].floor + rooms[s].height;
    const edges = map.walls.map((w) => ({ a: [w.x, w.y], b: [map.walls[w.point2].x, map.walls[w.point2].y], s: w.sector, n: w.nextsector }));
    const st = { x: 0, y: 0, z: 0, vz: 0, ang: 0, pitch: 0, sector: -1, onGround: true };
    const start = prj.things.find((t) => t.type === 1) || prj.things.find((t) => t.type === 2);
    if (start) { st.x = start.x; st.y = start.y; st.ang = ((start.ang || 0) / 2048) * Math.PI * 2; }
    else if (rooms.length) { const c = M.centroidInside(rooms[0].pts) || rooms[0].pts[0]; st.x = c[0]; st.y = c[1]; }
    st.sector = M.roomAt([st.x, st.y], rooms);
    if (st.sector >= 0) st.z = floorOf(st.sector);

    function blocks(e, z) { // can a player standing at feet height z pass this edge?
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
    function crosses(p, q, a, b) {
      const c = (o, u, v) => (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0]);
      const d1 = c(a, b, p), d2 = c(a, b, q), d3 = c(p, q, a), d4 = c(p, q, b);
      return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
    }
    function tryMove(dx, dy) {
      let p = [st.x + dx, st.y + dy];
      // slide along whatever is in the way
      for (let it = 0; it < 3; it++) {
        for (const e of edges) {
          if (!blocks(e, st.z)) continue;
          const c = closest(p, e.a, e.b), ddx = p[0] - c[0], ddy = p[1] - c[1], d = Math.hypot(ddx, ddy);
          if (d < PLAYER.radius) {
            if (d > 0.001) { p[0] = c[0] + ddx / d * PLAYER.radius; p[1] = c[1] + ddy / d * PLAYER.radius; }
            else { p = [st.x, st.y]; }
          }
        }
      }
      for (const e of edges) if (blocks(e, st.z) && crosses([st.x, st.y], p, e.a, e.b)) return; // never through a wall
      const s = M.roomAt(p, rooms);
      if (s < 0) return;
      if (floorOf(s) - st.z > PLAYER.step) return;
      if (ceilOf(s) - Math.max(st.z, floorOf(s)) < PLAYER.height * 0.8) return;
      st.x = p[0]; st.y = p[1]; st.sector = s;
    }
    return {
      st, PLAYER,
      // fwd/side in -1..1, run: bool; dt in frames of 1/60 s
      step(fwd, side, run, jump, dt) {
        const sp = (run ? PLAYER.run : PLAYER.speed) * dt;
        const cs = Math.cos(st.ang), sn = Math.sin(st.ang);
        const dx = (cs * fwd - sn * side) * sp, dy = (sn * fwd + cs * side) * sp;
        // in small pieces, so fast moves don't skip a thin room
        const n = Math.max(1, Math.ceil(Math.hypot(dx, dy) / (PLAYER.radius / 2)));
        for (let i = 0; i < n; i++) tryMove(dx / n, dy / n);
        if (st.sector < 0) return;
        const fl = floorOf(st.sector), cl = ceilOf(st.sector);
        if (jump && st.onGround) { st.vz = PLAYER.jump * 16; st.onGround = false; }
        st.vz -= PLAYER.gravity * 16 * dt;
        st.z += st.vz * dt;
        if (st.z <= fl) { st.z = fl; st.vz = 0; st.onGround = true; } // landed, or stepped up
        else if (st.onGround && st.z - fl > 4 && st.vz <= 0) st.onGround = false;
        if (st.z + PLAYER.height > cl) { st.z = Math.max(fl, cl - PLAYER.height); if (st.vz > 0) st.vz = 0; }
      },
      eye() { return [st.x, st.z + PLAYER.eye, st.y]; },
    };
  }

  // ---- the viewer ------------------------------------------------------------------------------------
  function open(prj, overlay) {
    overlay.innerHTML = `
<canvas class="v3d"></canvas>
<div class="v3d-top"><button data-m="orbit">Look around</button><button data-m="walk">Walk</button><span class="v3d-info"></span><button data-m="close" class="on">Close</button></div>
<div class="v3d-stick"><div class="v3d-knob"></div></div>
<button class="v3d-jump">Jump</button><button class="v3d-run">Run</button>
<div class="v3d-help"></div>`;
    overlay.classList.add('open');
    const cv = overlay.querySelector('canvas');
    let r;
    try { r = makeGL(cv); } catch (e) { r = null; }
    const info = overlay.querySelector('.v3d-info'), help = overlay.querySelector('.v3d-help');
    if (!r) { help.textContent = 'This browser has no WebGL, so there is no 3D view.'; }
    const mesh = buildMesh(prj);
    if (r) upload(r, mesh);
    const walker = Walker(prj, mesh.map);
    // orbit camera around the middle of the map
    let x1 = Infinity, y1 = Infinity, x2 = -Infinity, y2 = -Infinity, top = 0;
    prj.rooms.forEach((rm) => { rm.pts.forEach((p) => { x1 = Math.min(x1, p[0]); y1 = Math.min(y1, p[1]); x2 = Math.max(x2, p[0]); y2 = Math.max(y2, p[1]); }); top = Math.max(top, rm.floor + rm.height); });
    if (!isFinite(x1)) { x1 = y1 = 0; x2 = y2 = 4096; }
    const orbit = { cx: (x1 + x2) / 2, cy: top / 2, cz: (y1 + y2) / 2, yaw: 0.7, pitch: 0.75, dist: Math.max(x2 - x1, y2 - y1) * 1.2 + 3000 };
    let mode = 'orbit', running = true, last = performance.now(), run = false;
    const keys = {}, stick = { x: 0, y: 0, id: null }, look = { id: null, x: 0, y: 0 };
    let jumpPressed = false;

    function setMode(m) {
      mode = m;
      overlay.querySelectorAll('[data-m]').forEach((b) => b.classList.toggle('on', b.dataset.m === m || b.dataset.m === 'close'));
      overlay.classList.toggle('walking', m === 'walk');
      help.textContent = m === 'walk'
        ? 'Left: move. Right: look. Keyboard: WASD or arrows, mouse drag, Space jumps, Shift runs. Sizes are close to Blood\'s, not exact.'
        : 'Drag to turn the map, pinch or scroll to zoom. Ceilings are left out here; Walk shows them.';
      help.classList.remove('gone'); clearTimeout(help.t); help.t = setTimeout(() => help.classList.add('gone'), 7000);
      if (m === 'walk' && walker.st.sector < 0) info.textContent = 'No start inside a room';
    }
    overlay.querySelectorAll('[data-m]').forEach((b) => b.onclick = () => {
      if (b.dataset.m === 'close') { running = false; overlay.classList.remove('open', 'walking'); overlay.innerHTML = ''; window.removeEventListener('keydown', kd); window.removeEventListener('keyup', ku); return; }
      setMode(b.dataset.m);
    });
    const jumpBtn = overlay.querySelector('.v3d-jump'), runBtn = overlay.querySelector('.v3d-run');
    jumpBtn.addEventListener('pointerdown', (e) => { e.preventDefault(); jumpPressed = true; });
    runBtn.onclick = () => { run = !run; runBtn.classList.toggle('on', run); };
    const kd = (e) => { keys[e.code] = true; if (e.code === 'Space') e.preventDefault(); };
    const ku = (e) => { keys[e.code] = false; };
    window.addEventListener('keydown', kd); window.addEventListener('keyup', ku);

    const stickEl = overlay.querySelector('.v3d-stick'), knob = overlay.querySelector('.v3d-knob');
    const pinch = new Map();
    let pinchD = 0;
    cv.addEventListener('pointerdown', (e) => {
      cv.setPointerCapture(e.pointerId);
      if (mode === 'walk') {
        const half = cv.getBoundingClientRect().width / 2;
        if (e.offsetX < half && stick.id === null) { stick.id = e.pointerId; stick.ox = e.offsetX; stick.oy = e.offsetY; stick.x = stick.y = 0; stickEl.style.left = (e.offsetX - 60) + 'px'; stickEl.style.top = (e.offsetY - 60) + 'px'; stickEl.classList.add('on'); }
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
        } else if (e.pointerId === look.id || (e.pointerType === 'mouse' && e.buttons)) {
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

    function frame(now) {
      if (!running) return;
      const dt = Math.min(3, (now - last) / (1000 / 60)); last = now;
      const dpr = window.devicePixelRatio || 1, w = cv.clientWidth, h = cv.clientHeight;
      if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
      let eye, at;
      if (mode === 'walk') {
        const fwd = -stick.y + ((keys.KeyW || keys.ArrowUp) ? 1 : 0) - ((keys.KeyS || keys.ArrowDown) ? 1 : 0);
        const side = stick.x + (keys.KeyD ? 1 : 0) - (keys.KeyA ? 1 : 0);
        if (keys.ArrowLeft) walker.st.ang -= 0.04 * dt;
        if (keys.ArrowRight) walker.st.ang += 0.04 * dt;
        walker.step(Math.max(-1, Math.min(1, fwd)), Math.max(-1, Math.min(1, side)), run || keys.ShiftLeft || keys.ShiftRight, jumpPressed || keys.Space, dt);
        jumpPressed = false;
        eye = walker.eye();
        const st = walker.st;
        at = [eye[0] + Math.cos(st.ang) * Math.cos(st.pitch) * 100, eye[1] + Math.sin(st.pitch) * 100, eye[2] + Math.sin(st.ang) * Math.cos(st.pitch) * 100];
        info.textContent = st.sector >= 0 ? 'Room ' + (st.sector + 1) : '';
      } else {
        eye = [orbit.cx + Math.cos(orbit.yaw) * Math.cos(orbit.pitch) * orbit.dist, orbit.cy + Math.sin(orbit.pitch) * orbit.dist, orbit.cz + Math.sin(orbit.yaw) * Math.cos(orbit.pitch) * orbit.dist];
        at = [orbit.cx, orbit.cy, orbit.cz];
        info.textContent = prj.rooms.length + ' rooms';
      }
      if (r) {
        const gl = r.gl;
        gl.viewport(0, 0, cv.width, cv.height);
        gl.clearColor(0.09, 0.05, 0.06, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        const proj = perspective(mode === 'walk' ? 1.25 : 0.9, cv.width / Math.max(1, cv.height), mode === 'walk' ? 16 : 64, 400000);
        gl.uniformMatrix4fv(r.uMVP, false, mul(proj, lookAt(eye, at, [0, 1, 0])));
        gl.uniform3fv(r.uEye, eye);
        gl.uniform3fv(r.uFog, mode === 'walk' ? [0.09, 0.05, 0.06] : [0.12, 0.07, 0.07]);
        gl.uniform1f(r.uFogDist, mode === 'walk' ? 14000 : orbit.dist * 4);
        gl.drawArrays(gl.TRIANGLES, 0, mode === 'walk' ? r.count : r.ceilStart); // the overview looks in from above
      }
      requestAnimationFrame(frame);
    }
    setMode('orbit');
    requestAnimationFrame(frame);
    return { setMode, walker, close: () => overlay.querySelector('[data-m=close]').click() };
  }

  root.View3D = { open, buildMesh, triangulate, Walker, PLAYER };
})(typeof window !== 'undefined' ? window : this);
