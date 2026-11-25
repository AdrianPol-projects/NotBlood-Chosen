// Blood Map Maker: the full editor, for any Blood map (MapFile's model, edited with MapGeo).
//
// Every field of sectors, walls and sprites and of their extra data can be changed; links between
// objects (tx/rx channels) are listed and drawn; sectors are drawn and joined like in a Build editor
// (also inside other sectors), walls split, corners moved and removed, sprites placed from Blood's
// whole list of object types (names from XMAPEDIT, see blooddata.js). Saving writes the map back
// exactly, apart from what was changed.
(function (root) {
  'use strict';
  const F = () => root.MapFile, G = () => root.MapGeo, B = () => root.BloodData, UI = () => root.MapMakerUI;
  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const Z = 16;

  // ---- what the fields mean -------------------------------------------------------------------------
  const BITS = {
    sectorStat: [[1, 'sky (parallax)'], [2, 'sloped'], [4, 'swap x/y'], [8, 'double size'], [16, 'flip x'], [32, 'flip y'], [64, 'align to first wall'], [128, 'masked'], [256, 'translucent']],
    wall: [[1, 'blocks the player'], [2, 'lower part from the other side'], [4, 'align to floor'], [8, 'flip x'], [16, 'masked (in the opening)'], [32, 'one-way'], [64, 'blocks shots'], [128, 'translucent'], [256, 'flip y'], [512, 'more translucent']],
    sprite: [[1, 'blocks the player'], [2, 'translucent'], [4, 'flip x'], [8, 'flip y'], [16, 'stands along a wall'], [32, 'lies on the floor'], [64, 'one-sided'], [128, 'centered'], [256, 'blocks shots'], [512, 'more translucent'], [8192, 'no shade'], [32768, 'invisible']],
  };
  const LABELS = {
    state: 'state (on)', txID: 'sends to channel (tx)', rxID: 'listens on channel (rx)', command: 'command it sends', triggerOn: 'sends when turned on', triggerOff: 'sends when turned off',
    triggerOnce: 'only once', isTriggered: 'already used', decoupled: 'decoupled (only sends)', locked: 'locked', Key: 'needs key', key: 'needs key', Push: 'use (push)',
    Wallpush: 'use its walls (from outside)', Enter: 'entering', Exit: 'leaving', Vector: 'shot at', Impact: 'hit (explosion)', Pickup: 'picked up', Touch: 'touched', Sight: 'seen',
    Proximity: 'something near', triggerPush: 'use (push)', triggerVector: 'shot at', triggerTouch: 'touched', dudeLockout: 'enemies can\'t use it', DudeLockout: 'enemies can\'t use it',
    busyTimeA: 'time to turn on (1/10 s)', busyTimeB: 'time to turn off (1/10 s)', waitTimeA: 'wait, then off (1/10 s)', waitTimeB: 'wait, then on (1/10 s)', reTriggerA: 'turn off by itself',
    reTriggerB: 'turn on by itself', busyTime: 'busy time (1/10 s)', waitTime: 'wait time (1/10 s)', restState: 'rest state', interruptable: 'can be reversed while moving', Interrutable: 'can be reversed',
    offCeilZ: 'ceiling when off (z)', onCeilZ: 'ceiling when on (z)', offFloorZ: 'floor when off (z)', onFloorZ: 'floor when on (z)', busyWaveA: 'motion curve on', busyWaveB: 'motion curve off',
    data: 'data', data1: 'data 1', data2: 'data 2', data3: 'data 3', data4: 'data 4', Underwater: 'under water', Depth: 'water depth', Crush: 'crushes', damageType: 'damage type',
    lS: 'not in single player', lB: 'not in bloodbath', lC: 'not in co-op', lT: 'not in teams', lSkill: 'not at difficulties (bits)', respawn: 'respawn', health: 'health',
    dudeDeaf: 'deaf', dudeAmbush: 'ambush', dudeGuard: 'guard (doesn\'t move)', dropMsg: 'drops item', lockMsg: 'locked message', panVel: 'panning speed', panAngle: 'panning angle',
    panAlways: 'always pan', panFloor: 'pan floor', panCeiling: 'pan ceiling', Drag: 'drag things along', amplitude: 'light amplitude', freq: 'light speed', phase: 'light phase',
    wave: 'light wave', shadeAlways: 'always shade', shadeFloor: 'shade floor', shadeCeiling: 'shade ceiling', shadeWalls: 'shade walls', shade: 'shade', color: 'coloured light',
    windVel: 'wind speed', windAng: 'wind angle', windAlways: 'always wind', bobZRange: 'bob height', bobSpeed: 'bob speed', bobTheta: 'bob phase', bobAlways: 'always bob',
    bobFloor: 'bob floor', bobCeiling: 'bob ceiling', bobRotate: 'bob rotates', ceilpal: 'ceiling palette', floorpal: 'floor palette', marker0: 'marker 0', marker1: 'marker 1',
    stopOn: 'stop when on', stopOff: 'stop when off', panXVel: 'pan x speed', panYVel: 'pan y speed', goalAng: 'goal angle', medium: 'medium', target: 'target',
  };
  const GROUPS = [
    ['Trigger', ['state', 'rxID', 'txID', 'command', 'triggerOn', 'triggerOff', 'triggerOnce', 'isTriggered', 'decoupled', 'locked', 'Key', 'key', 'Push', 'Wallpush', 'triggerPush', 'Enter', 'Exit',
      'Vector', 'triggerVector', 'Impact', 'Pickup', 'Touch', 'triggerTouch', 'Sight', 'Proximity', 'dudeLockout', 'DudeLockout']],
    ['Timing', ['busyTimeA', 'busyTimeB', 'busyTime', 'waitTimeA', 'waitTimeB', 'waitTime', 'reTriggerA', 'reTriggerB', 'restState', 'interruptable', 'Interrutable', 'busyWaveA', 'busyWaveB', 'stopOn', 'stopOff']],
    ['Data', ['data', 'data1', 'data2', 'data3', 'data4']],
    ['Motion (doors, lifts)', ['offCeilZ', 'onCeilZ', 'offFloorZ', 'onFloorZ', 'marker0', 'marker1', 'Crush']],
    ['Game modes', ['lS', 'lB', 'lC', 'lT', 'lSkill', 'respawn']],
    ['Enemy', ['health', 'dudeDeaf', 'dudeAmbush', 'dudeGuard', 'dudeFlag4', 'goalAng', 'dropMsg', 'lockMsg']],
    ['Light', ['amplitude', 'freq', 'phase', 'wave', 'shadeAlways', 'shadeFloor', 'shadeCeiling', 'shadeWalls', 'shade', 'color', 'ceilpal', 'floorpal']],
    ['Panning, wind, bobbing', ['panVel', 'panAngle', 'panAlways', 'panFloor', 'panCeiling', 'Drag', 'panXVel', 'panYVel', 'windVel', 'windAng', 'windAlways', 'bobZRange', 'bobSpeed', 'bobTheta', 'bobAlways', 'bobFloor', 'bobCeiling', 'bobRotate']],
    ['Water, damage', ['Underwater', 'Depth', 'damageType', 'medium']],
  ];
  const HIDE = new Set(['reference', 'busy', 'unused1', 'unused2', 'unused3', 'unused4', 'Reserved', 'respawnPending', 'dodgeDir', 'targetX', 'targetY', 'targetZ', 'burnTime', 'burnSource',
    'height', 'stateTimer', 'ceilXPanFrac', 'ceilYPanFrac', 'floorXPanFrac', 'floorYPanFrac', 'xpanFrac', 'ypanFrac', 'isTriggered']);
  const KEYS = ['none', 'skull', 'eye', 'fire', 'dagger', 'spider', 'moon', 'key 7'];

  function specOf(kind) { return kind === 'sector' ? F().XSECTOR : kind === 'wall' ? F().XWALL : F().XSPRITE; }
  function bitsOf(spec, name) { const f = spec.find((x) => x[0] === name); return f ? f[1] : 0; }
  function typeName(kind, type) { const t = B()[kind][type]; return t ? t.name : (type ? 'type ' + type : 'Normal'); }

  // ---- the editor ---------------------------------------------------------------------------------
  let st = null; // { map, name, view, tool, sel, ... }

  function html() {
    return `
<header><span class="title">Full map</span><input id="feName" type="text" maxlength="8" spellcheck="false" aria-label="Map file name"><button id="feMenuBtn" class="small">☰</button></header>
<div id="feWrap" class="wrap"><canvas id="feCv"></canvas><div id="feHint" class="hint"></div><button id="feBadge" class="small badge">Check</button></div>
<div id="feSheet" class="sheet"></div>
<div id="feTools" class="tools">
  <button data-t="pan">Move</button><button data-t="select">Select</button><button data-t="points">Points</button><button data-t="draw">✎ Sector</button>
  <button data-t="sprite">+ Sprite</button><button data-t="erase">Delete</button><button id="feUndo">Undo</button><button id="feFit">Fit</button><button id="fe3d">3D / Walk</button>
</div>
<div id="feMenu" class="menu">
  <button id="feSave" class="on">Save .MAP</button>
  <button id="feOpen">Open a .MAP…</button>
  <button id="feNew">New empty map</button>
  <label class="row" style="margin:0"><span>Grid</span><select id="feGrid"><option>16</option><option>64</option><option>128</option><option selected>256</option><option>512</option><option>1024</option></select></label>
  <label class="row" style="margin:0"><span>Links</span><select id="feLinks"><option value="sel">of the selected</option><option value="all">all</option><option value="none">none</option></select></label>
  <button id="feSettings">Map settings</button>
  <button id="feBack">Back to the simple editor</button>
  <input id="feFile" type="file" accept=".map,.MAP" hidden>
</div>`;
  }

  function open(map, name) {
    const host = $('full');
    host.innerHTML = html();
    host.classList.add('open');
    st = { map, name: (name || 'MYMAP').toUpperCase().replace(/[^A-Z0-9_-]/g, '').slice(0, 8) || 'MYMAP', view: { x: 0, y: 0, scale: 0.05 }, tool: 'select', sel: null,
      grid: 256, undo: [], drawPts: [], spriteType: 0, links: 'sel', paths: null, pointers: new Map(), gesture: null, vert: null, moving: null };
    $('feName').value = st.name;
    $('feName').oninput = (e) => { st.name = e.target.value.toUpperCase().replace(/[^A-Z0-9_-]/g, '').slice(0, 8); autosave(); };
    document.querySelectorAll('#feTools [data-t]').forEach((b) => b.onclick = () => setTool(b.dataset.t));
    $('feUndo').onclick = undo;
    $('feFit').onclick = fit;
    $('fe3d').onclick = open3d;
    $('feMenuBtn').onclick = () => $('feMenu').classList.toggle('open');
    $('feSave').onclick = save;
    $('feOpen').onclick = () => $('feFile').click();
    $('feFile').onchange = async (e) => { const f = e.target.files[0]; e.target.value = ''; $('feMenu').classList.remove('open'); if (f) openFile(f); };
    $('feNew').onclick = () => { $('feMenu').classList.remove('open'); if (confirm('Start an empty map? (Undo brings this one back)')) { snapshot(); st.map = newMap(); st.name = 'MYMAP'; $('feName').value = st.name; st.sel = null; changed(); fit(); } };
    $('feGrid').onchange = (e) => { st.grid = +e.target.value; draw(); };
    $('feLinks').onchange = (e) => { st.links = e.target.value; draw(); };
    $('feSettings').onclick = () => { $('feMenu').classList.remove('open'); settingsPanel(); };
    $('feBack').onclick = () => { $('feMenu').classList.remove('open'); close(); };
    $('feBadge').onclick = checkPanel;
    const cv = $('feCv');
    cv.addEventListener('pointerdown', down); cv.addEventListener('pointermove', move); cv.addEventListener('pointerup', up);
    cv.addEventListener('pointercancel', (e) => { st.pointers.delete(e.pointerId); st.gesture = null; });
    cv.addEventListener('wheel', wheel, { passive: false });
    window.addEventListener('resize', resize);
    resize();
    setTool('select');
    fit();
    changed(true);
  }
  function close() {
    $('full').classList.remove('open');
    $('full').innerHTML = '';
    window.removeEventListener('resize', resize);
    st = null;
    if (UI() && UI().onFullClosed) UI().onFullClosed();
  }

  function newMap() {
    const map = G().emptyMap();
    G().addSector(map, [[0, 0], [4096, 0], [4096, 4096], [0, 4096]]);
    map.sprites.push(spriteOf(1, 1024, 2048, 0, 0));
    return map;
  }

  // ---- keeping it -----------------------------------------------------------------------------------
  function bytesOf(map) { F().finish(map); return F().writeMap(map); }
  function snapshot() { st.undo.push(bytesOf(st.map)); if (st.undo.length > 40) st.undo.shift(); }
  function undo() {
    if (!st.undo.length) { toast('Nothing to undo'); return; }
    st.map = F().parseMap(st.undo.pop()); st.sel = null; closeSheet(); changed();
  }
  let saveTimer = 0;
  function autosave() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(async () => { try { await idbPut('fullmap', { name: st.name, bytes: bytesOf(st.map) }); } catch (e) {} }, 800);
  }
  function idb() { return new Promise((res, rej) => { const rq = indexedDB.open('bloodmapmaker', 1); rq.onupgradeneeded = () => rq.result.createObjectStore('files'); rq.onsuccess = () => res(rq.result); rq.onerror = () => rej(rq.error); }); }
  async function idbPut(k, v) { const d = await idb(); await new Promise((res, rej) => { const t = d.transaction('files', 'readwrite'); t.objectStore('files').put(v, k); t.oncomplete = res; t.onerror = () => rej(t.error); }); d.close(); }
  async function idbGet(k) { const d = await idb(); const v = await new Promise((res, rej) => { const rq = d.transaction('files').objectStore('files').get(k); rq.onsuccess = () => res(rq.result); rq.onerror = () => rej(rq.error); }); d.close(); return v; }

  function changed(noSave) {
    st.paths = null;
    if (!noSave) autosave();
    const p = G().problems(st.map);
    const b = $('feBadge');
    b.textContent = p.length ? p.length + ' to fix' : st.map.sectors.length + ' sectors';
    b.style.borderColor = p.length ? 'var(--err)' : 'var(--line)';
    draw();
  }
  function toast(t) { if (UI()) UI().toast(t); }

  async function openFile(file) {
    try {
      const map = F().parseMap(new Uint8Array(await file.arrayBuffer()));
      if (st) { snapshot(); st.map = map; st.name = file.name.replace(/\.map$/i, '').toUpperCase().replace(/[^A-Z0-9_-]/g, '').slice(0, 8); $('feName').value = st.name; st.sel = null; closeSheet(); changed(); fit(); }
      else open(map, file.name.replace(/\.map$/i, ''));
      toast(file.name + ': ' + map.sectors.length + ' sectors, ' + map.walls.length + ' walls, ' + map.sprites.length + ' sprites' + (map.crcOk ? '' : ' (its check sum was wrong)'));
    } catch (e) { toast(file.name + ': ' + e.message); }
  }
  function save() {
    $('feMenu').classList.remove('open');
    const p = G().problems(st.map);
    if (p.length) { checkPanel(); toast('Fix the problems first'); return; }
    const bytes = bytesOf(st.map);
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
    a.download = (st.name || 'MYMAP') + '.MAP';
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 5000);
    toast(a.download + ' saved (' + Math.round(bytes.length / 1024) + ' KB)');
  }
  async function restore() {
    try { const v = await idbGet('fullmap'); if (v && v.bytes) return { map: F().parseMap(new Uint8Array(v.bytes)), name: v.name }; } catch (e) {}
    return null;
  }

  // ---- view -------------------------------------------------------------------------------------------
  function resize() {
    if (!st) return;
    const cv = $('feCv'), r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    cv.width = Math.round(r.width * dpr); cv.height = Math.round(r.height * dpr);
    cv.getContext('2d').setTransform(dpr, 0, 0, dpr, 0, 0);
    draw();
  }
  const W = () => $('feCv').getBoundingClientRect().width, H = () => $('feCv').getBoundingClientRect().height;
  const toScreen = (x, y) => [(x - st.view.x) * st.view.scale + W() / 2, (y - st.view.y) * st.view.scale + H() / 2];
  const toMap = (sx, sy) => [(sx - W() / 2) / st.view.scale + st.view.x, (sy - H() / 2) / st.view.scale + st.view.y];
  function snap(p) {
    // corners snap to existing corners near the finger first, else to the grid
    const r = 14 / st.view.scale;
    let best = null, bd = r * r;
    for (const w of st.map.walls) { const d = (w.x - p[0]) ** 2 + (w.y - p[1]) ** 2; if (d < bd) { bd = d; best = [w.x, w.y]; } }
    if (best) return best;
    return [Math.round(p[0] / st.grid) * st.grid, Math.round(p[1] / st.grid) * st.grid];
  }
  function fit() {
    const m = st.map;
    if (!m.walls.length) { st.view = { x: 0, y: 0, scale: 0.05 }; draw(); return; }
    let x1 = Infinity, y1 = Infinity, x2 = -Infinity, y2 = -Infinity;
    for (const w of m.walls) { x1 = Math.min(x1, w.x); y1 = Math.min(y1, w.y); x2 = Math.max(x2, w.x); y2 = Math.max(y2, w.y); }
    st.view.x = (x1 + x2) / 2; st.view.y = (y1 + y2) / 2;
    st.view.scale = Math.min(W() / (x2 - x1 + 1024), H() / (y2 - y1 + 1024));
    draw();
  }
  function sectorPaths() {
    if (st.paths) return st.paths;
    const m = st.map, geo = G();
    st.paths = m.sectors.map((s, i) => {
      const loops = geo.loopsOf(m, i);
      return loops.map((l) => l.map((w) => [m.walls[w].x, m.walls[w].y]));
    });
    return st.paths;
  }
  function objPos(kind, i) {
    const m = st.map;
    if (kind === 'sprite') return [m.sprites[i].x, m.sprites[i].y];
    if (kind === 'wall') { const a = m.walls[i], b = m.walls[a.point2]; return [(a.x + b.x) / 2, (a.y + b.y) / 2]; }
    const s = m.sectors[i]; let x = 0, y = 0;
    for (let w = s.wallptr; w < s.wallptr + s.wallnum; w++) { x += m.walls[w].x; y += m.walls[w].y; }
    return [x / s.wallnum, y / s.wallnum];
  }
  function spriteColor(sp) {
    const t = B().sprite[sp.type], g = t && t.group;
    if (sp.type === 1 || sp.type === 2) return '#6fbf73';
    if (g === 'Enemy' || (sp.type >= 200 && sp.type < 260)) return '#e04030';
    if (sp.type >= 40 && sp.type < 51) return '#f0a24a';
    if (sp.type >= 60 && sp.type < 81) return '#d9c36a';
    if (sp.type >= 100 && sp.type < 150) return '#6fa8dc';
    if (sp.type >= 20 && sp.type < 30) return '#4fd1d1';
    if (sp.type >= 400 && sp.type < 500) return '#c27ba0';
    if (sp.type >= 700 && sp.type < 800) return '#9b8cf0';
    return '#b0a090';
  }
  function draw() {
    if (!st) return;
    const cv = $('feCv'), ctx = cv.getContext('2d'), w = W(), h = H(), m = st.map;
    ctx.fillStyle = '#0d0707'; ctx.fillRect(0, 0, w, h);
    // grid
    let g = st.grid; while (g * st.view.scale < 10) g *= 2;
    const [mx1, my1] = toMap(0, 0), [mx2, my2] = toMap(w, h);
    ctx.lineWidth = 1; ctx.strokeStyle = '#1b1111';
    ctx.beginPath();
    for (let x = Math.floor(mx1 / g) * g; x <= mx2; x += g) { const sx = toScreen(x, 0)[0]; ctx.moveTo(sx, 0); ctx.lineTo(sx, h); }
    for (let y = Math.floor(my1 / g) * g; y <= my2; y += g) { const sy = toScreen(0, y)[1]; ctx.moveTo(0, sy); ctx.lineTo(w, sy); }
    ctx.stroke();
    // sectors
    const paths = sectorPaths();
    paths.forEach((loops, i) => {
      ctx.beginPath();
      for (const l of loops) l.forEach((p, k) => { const s = toScreen(p[0], p[1]); k ? ctx.lineTo(s[0], s[1]) : ctx.moveTo(s[0], s[1]); });
      const sec = m.sectors[i], hgt = Math.max(-1, Math.min(1, -sec.floorz / 16 / 2048));
      const isSel = st.sel && st.sel.kind === 'sector' && st.sel.i === i;
      ctx.fillStyle = isSel ? 'rgba(240,162,74,.35)' : sec.type >= 600 ? 'hsla(30,55%,28%,.75)' : (sec.ceilingstat & 1) ? 'hsla(210,35%,22%,.7)' : 'hsla(' + (20 - hgt * 40) + ',45%,' + (18 + hgt * 8) + '%,.75)';
      ctx.fill('evenodd');
    });
    // walls
    for (let i = 0; i < m.walls.length; i++) {
      const a = m.walls[i], b = m.walls[a.point2];
      if (!b || (a.nextwall >= 0 && a.nextwall < i && !(a.cstat & 16))) continue;
      const s = toScreen(a.x, a.y), t = toScreen(b.x, b.y);
      if ((s[0] < 0 && t[0] < 0) || (s[1] < 0 && t[1] < 0) || (s[0] > w && t[0] > w) || (s[1] > h && t[1] > h)) continue;
      ctx.strokeStyle = (a.cstat & 16) ? '#6fa8dc' : a.nextwall >= 0 ? '#a03030' : '#d8cfc8';
      ctx.lineWidth = a.ext ? 2.2 : 1.2;
      ctx.beginPath(); ctx.moveTo(s[0], s[1]); ctx.lineTo(t[0], t[1]); ctx.stroke();
    }
    if (st.sel && st.sel.kind === 'wall') {
      const a = m.walls[st.sel.i], b = a && m.walls[a.point2];
      if (b) { const s = toScreen(a.x, a.y), t = toScreen(b.x, b.y); ctx.strokeStyle = '#f0a24a'; ctx.lineWidth = 4; ctx.beginPath(); ctx.moveTo(s[0], s[1]); ctx.lineTo(t[0], t[1]); ctx.stroke(); }
    }
    // corners
    if (st.tool === 'points' || st.tool === 'draw') {
      ctx.fillStyle = '#d8cfc8';
      for (const wl of m.walls) { const s = toScreen(wl.x, wl.y); if (s[0] > -4 && s[1] > -4 && s[0] < w + 4 && s[1] < h + 4) ctx.fillRect(s[0] - 2, s[1] - 2, 4, 4); }
      if (st.vert) { const s = toScreen(st.vert[0], st.vert[1]); ctx.fillStyle = '#f0a24a'; ctx.fillRect(s[0] - 5, s[1] - 5, 10, 10); }
    }
    // sprites
    const rs = Math.max(3, Math.min(9, 48 * st.view.scale));
    m.sprites.forEach((sp, i) => {
      const s = toScreen(sp.x, sp.y);
      if (s[0] < -10 || s[1] < -10 || s[0] > w + 10 || s[1] > h + 10) return;
      const isSel = st.sel && st.sel.kind === 'sprite' && st.sel.i === i;
      ctx.fillStyle = spriteColor(sp);
      ctx.beginPath(); ctx.arc(s[0], s[1], isSel ? rs + 3 : rs, 0, Math.PI * 2); ctx.fill();
      if (isSel) { ctx.strokeStyle = '#fff'; ctx.lineWidth = 2; ctx.stroke(); }
      if (rs > 5 || isSel) {
        const a = (sp.ang & 2047) / 2048 * Math.PI * 2;
        ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = 1.5;
        ctx.beginPath(); ctx.moveTo(s[0], s[1]); ctx.lineTo(s[0] + Math.cos(a) * rs * 2, s[1] + Math.sin(a) * rs * 2); ctx.stroke();
      }
    });
    // links
    drawLinks(ctx);
    // the sector being drawn
    if (st.drawPts.length) {
      ctx.strokeStyle = '#f0a24a'; ctx.fillStyle = '#f0a24a'; ctx.lineWidth = 2;
      ctx.beginPath();
      st.drawPts.forEach((p, k) => { const s = toScreen(p[0], p[1]); k ? ctx.lineTo(s[0], s[1]) : ctx.moveTo(s[0], s[1]); });
      ctx.stroke();
      st.drawPts.forEach((p) => { const s = toScreen(p[0], p[1]); ctx.beginPath(); ctx.arc(s[0], s[1], 5, 0, Math.PI * 2); ctx.fill(); });
    }
    hint();
  }
  // tx -> rx arrows: of the selected object, or all of them
  function linkObjects() {
    const m = st.map, out = [];
    m.sectors.forEach((o, i) => { if (o.ext) out.push({ kind: 'sector', i, x: o.ext }); });
    m.walls.forEach((o, i) => { if (o.ext) out.push({ kind: 'wall', i, x: o.ext }); });
    m.sprites.forEach((o, i) => { if (o.ext) out.push({ kind: 'sprite', i, x: o.ext }); });
    return out;
  }
  function drawLinks(ctx) {
    if (st.links === 'none') return;
    const objs = linkObjects();
    const byRx = new Map();
    for (const o of objs) if (o.x.rxID) { if (!byRx.has(o.x.rxID)) byRx.set(o.x.rxID, []); byRx.get(o.x.rxID).push(o); }
    const sel = st.sel;
    const isSel = (o) => sel && o.kind === sel.kind && o.i === sel.i;
    ctx.lineWidth = 1.5;
    for (const o of objs) {
      if (!o.x.txID || o.x.txID < 3 || !byRx.has(o.x.txID)) continue;
      for (const r of byRx.get(o.x.txID)) {
        if (r === o) continue;
        if (st.links === 'sel' && !isSel(o) && !isSel(r)) continue;
        const a = toScreen(...objPos(o.kind, o.i)), b = toScreen(...objPos(r.kind, r.i));
        ctx.strokeStyle = ctx.fillStyle = isSel(o) || isSel(r) ? '#ffe066' : 'rgba(255,224,102,.45)';
        ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
        const ang = Math.atan2(b[1] - a[1], b[0] - a[0]);
        ctx.beginPath(); ctx.moveTo(b[0], b[1]); ctx.lineTo(b[0] - Math.cos(ang - 0.4) * 10, b[1] - Math.sin(ang - 0.4) * 10); ctx.lineTo(b[0] - Math.cos(ang + 0.4) * 10, b[1] - Math.sin(ang + 0.4) * 10); ctx.fill();
      }
    }
  }
  function hint() {
    const t = {
      pan: 'Drag to move, pinch to zoom.',
      select: 'Tap a sprite, a wall or a sector. Drag a selected sprite to move it.',
      points: st.vert ? 'Drag the corner to move it, or use the panel.' : 'Tap a corner to pick it; drag it to move it.',
      draw: st.drawPts.length ? 'Tap the next corner (it snaps to existing corners); tap the first one to finish.' : 'Tap the first corner of a new sector (inside a sector makes a sector within it).',
      sprite: 'Tap where to put: ' + typeName('sprite', st.spriteType),
      erase: 'Tap a sprite or a sector to delete it.',
    }[st.tool];
    $('feHint').textContent = t || '';
  }

  // ---- picking ----------------------------------------------------------------------------------------
  function spriteAt(p) {
    const r = 16 / st.view.scale; let best = -1, bd = r * r;
    st.map.sprites.forEach((sp, i) => { const d = (sp.x - p[0]) ** 2 + (sp.y - p[1]) ** 2; if (d <= bd) { bd = d; best = i; } });
    return best;
  }
  function wallAt(p) {
    // the nearest wall within reach; of the two sides of an opening, the one whose sector the tap is in
    const m = st.map, r = 10 / st.view.scale;
    let best = -1, bd = Infinity;
    for (let i = 0; i < m.walls.length; i++) {
      const a = m.walls[i], b = m.walls[a.point2];
      if (!b) continue;
      const dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
      const t = l ? Math.max(0, Math.min(1, ((p[0] - a.x) * dx + (p[1] - a.y) * dy) / l)) : 0;
      const d = Math.hypot(p[0] - (a.x + dx * t), p[1] - (a.y + dy * t));
      if (d > r) continue;
      const side = dx * (p[1] - a.y) - dy * (p[0] - a.x); // > 0: on its sector's side
      const score = d + (side >= 0 ? 0 : r); // the wrong side only if nothing else is near
      if (score < bd) { bd = score; best = i; }
    }
    return best;
  }
  function cornerAt(p) {
    const r = 14 / st.view.scale; let best = null, bd = r * r;
    for (const w of st.map.walls) { const d = (w.x - p[0]) ** 2 + (w.y - p[1]) ** 2; if (d < bd) { bd = d; best = [w.x, w.y]; } }
    return best;
  }

  // ---- touch ------------------------------------------------------------------------------------------
  function down(e) {
    const cv = $('feCv');
    cv.setPointerCapture(e.pointerId);
    st.pointers.set(e.pointerId, { x: e.offsetX, y: e.offsetY, x0: e.offsetX, y0: e.offsetY });
    if (st.pointers.size === 2) {
      const [a, b] = [...st.pointers.values()];
      st.gesture = { kind: 'pinch', d: Math.hypot(a.x - b.x, a.y - b.y), cx: (a.x + b.x) / 2, cy: (a.y + b.y) / 2, view: { ...st.view } };
      st.moving = null;
      return;
    }
    const p = toMap(e.offsetX, e.offsetY);
    st.gesture = { kind: st.tool === 'pan' ? 'pan' : 'tap', moved: false, view: { ...st.view } };
    // dragging a selected sprite, or a corner
    if (st.tool === 'select' && st.sel && st.sel.kind === 'sprite' && spriteAt(p) === st.sel.i) st.moving = { kind: 'sprite', i: st.sel.i };
    else if (st.tool === 'points') { const c = cornerAt(p); if (c) { st.vert = c; st.moving = { kind: 'corner', from: c.slice(), at: c.slice() }; draw(); } }
  }
  function move(e) {
    const p0 = st.pointers.get(e.pointerId);
    if (!p0) return;
    p0.x = e.offsetX; p0.y = e.offsetY;
    if (st.gesture && st.gesture.kind === 'pinch' && st.pointers.size >= 2) {
      const [a, b] = [...st.pointers.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y), cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2, v = st.gesture.view;
      const scale = Math.max(0.002, Math.min(4, v.scale * d / st.gesture.d));
      const mx = (st.gesture.cx - W() / 2) / v.scale + v.x, my = (st.gesture.cy - H() / 2) / v.scale + v.y;
      st.view = { scale, x: mx - (cx - W() / 2) / scale, y: my - (cy - H() / 2) / scale };
      draw(); return;
    }
    if (!st.gesture || st.pointers.size !== 1) return;
    const dx = p0.x - p0.x0, dy = p0.y - p0.y0;
    if (Math.hypot(dx, dy) > 8) st.gesture.moved = true;
    if (!st.gesture.moved) return;
    if (st.moving) {
      const p = toMap(p0.x, p0.y);
      if (st.moving.kind === 'sprite') {
        if (!st.moving.saved) { snapshot(); st.moving.saved = true; }
        const sp = st.map.sprites[st.moving.i];
        const q = [Math.round(p[0] / 16) * 16, Math.round(p[1] / 16) * 16];
        const s = G().sectorAt(st.map, q[0], q[1], sp.sectnum);
        if (s >= 0) { sp.x = q[0]; sp.y = q[1]; if (s !== sp.sectnum) { sp.sectnum = s; } }
        draw();
      } else {
        const q = [Math.round(p[0] / Math.min(st.grid, 64)) * Math.min(st.grid, 64), Math.round(p[1] / Math.min(st.grid, 64)) * Math.min(st.grid, 64)];
        if (!st.moving.saved) { snapshot(); st.moving.saved = true; }
        G().moveCorner(st.map, st.moving.at[0], st.moving.at[1], q[0], q[1]);
        st.moving.at = q; st.vert = q; st.paths = null; draw();
      }
      return;
    }
    st.view.x = st.gesture.view.x - dx / st.view.scale; st.view.y = st.gesture.view.y - dy / st.view.scale; draw();
  }
  function up(e) {
    const p0 = st.pointers.get(e.pointerId);
    const wasTap = p0 && st.gesture && st.gesture.kind === 'tap' && !st.gesture.moved && st.pointers.size === 1;
    st.pointers.delete(e.pointerId);
    if (st.moving && st.moving.saved) {
      if (st.moving.kind === 'corner') {
        // a corner that made walls cross goes back
        if (G().problems(st.map).length || crossing()) { G().moveCorner(st.map, st.moving.at[0], st.moving.at[1], st.moving.from[0], st.moving.from[1]); st.vert = st.moving.from; toast('Walls would cross there'); st.undo.pop(); }
        else cornerPanel();
      } else if (st.sel) spritePanel(st.sel.i);
      st.moving = null; changed(); return;
    }
    st.moving = null;
    if (!st.pointers.size) st.gesture = null;
    if (wasTap) tap(p0.x, p0.y);
  }
  function wheel(e) {
    e.preventDefault();
    const before = toMap(e.offsetX, e.offsetY);
    st.view.scale = Math.max(0.002, Math.min(4, st.view.scale * Math.exp(-e.deltaY * 0.0015)));
    const after = toMap(e.offsetX, e.offsetY);
    st.view.x += before[0] - after[0]; st.view.y += before[1] - after[1];
    draw();
  }
  // any two walls crossing (after moving a corner)
  function crossing() {
    const m = st.map, near = (st.vert || [0, 0]);
    const segs = m.walls.map((a) => { const b = m.walls[a.point2]; return [[a.x, a.y], [b.x, b.y]]; });
    const touched = segs.filter((s) => (s[0][0] === near[0] && s[0][1] === near[1]) || (s[1][0] === near[0] && s[1][1] === near[1]));
    const cr = (o, u, v) => (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0]);
    for (const t of touched) for (const s of segs) {
      const d1 = cr(s[0], s[1], t[0]), d2 = cr(s[0], s[1], t[1]), d3 = cr(t[0], t[1], s[0]), d4 = cr(t[0], t[1], s[1]);
      if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return true;
    }
    return false;
  }

  // a phone sends a click after the touch: it mustn't land on the panel that the tap just opened
  let lastTap = 0, lastTapXY = [0, 0];
  document.addEventListener('click', (e) => {
    if (Date.now() - lastTap < 450 && Math.hypot(e.clientX - lastTapXY[0], e.clientY - lastTapXY[1]) < 40 && e.target && e.target.id !== 'feCv'
      && e.target.closest && e.target.closest('#full')) { e.stopPropagation(); e.preventDefault(); }
  }, true);
  function tap(sx, sy) {
    lastTap = Date.now(); { const r = document.getElementById('feCv').getBoundingClientRect(); lastTapXY = [r.left + sx, r.top + sy]; }
    const m = st.map, p = toMap(sx, sy);
    if (st.tool === 'select' || st.tool === 'erase') {
      const sp = spriteAt(p);
      if (sp >= 0) {
        if (st.tool === 'erase') { snapshot(); m.sprites.splice(sp, 1); st.sel = null; closeSheet(); changed(); return; }
        st.sel = { kind: 'sprite', i: sp }; spritePanel(sp); draw(); return;
      }
      const wl = st.tool === 'select' ? wallAt(p) : -1;
      if (wl >= 0) { st.sel = { kind: 'wall', i: wl }; wallPanel(wl); draw(); return; }
      const s = G().sectorAt(m, p[0], p[1], st.sel && st.sel.kind === 'sector' ? st.sel.i : -1);
      if (s < 0) { st.sel = null; closeSheet(); draw(); return; }
      if (st.tool === 'erase') {
        const n = m.sprites.filter((x) => x.sectnum === s).length;
        if (n && !confirm('Delete sector ' + s + ' and its ' + n + ' sprites?')) return;
        snapshot(); G().deleteSector(m, s); st.sel = null; closeSheet(); changed(); return;
      }
      st.sel = { kind: 'sector', i: s }; sectorPanel(s); draw(); return;
    }
    if (st.tool === 'points') {
      const c = cornerAt(p);
      st.vert = c; if (c) cornerPanel(); else closeSheet(); draw(); return;
    }
    if (st.tool === 'draw') {
      const q = snap(p), pts = st.drawPts;
      if (pts.length >= 3 && q[0] === pts[0][0] && q[1] === pts[0][1]) { finishDraw(); return; }
      if (!pts.length || q[0] !== pts[pts.length - 1][0] || q[1] !== pts[pts.length - 1][1]) pts.push(q);
      drawPanel(); draw(); return;
    }
    if (st.tool === 'sprite') {
      const q = [Math.round(p[0] / 16) * 16, Math.round(p[1] / 16) * 16];
      const s = G().sectorAt(m, q[0], q[1], -1);
      if (s < 0) { toast('Put it inside a sector'); return; }
      snapshot();
      const z = Math.round(G().slopeZ(m, s, q[0], q[1], false));
      m.sprites.push(spriteOf(st.spriteType, q[0], q[1], z, s));
      st.sel = { kind: 'sprite', i: m.sprites.length - 1 };
      changed(); spritePanel(st.sel.i);
    }
  }
  function spriteOf(type, x, y, z, s) {
    const t = B().sprite[type] || {};
    const sp = { x, y, z, cstat: 0, picnum: t.tile >= 0 ? t.tile : 0, shade: 0, pal: t.pal > 0 ? t.pal : 0, clipdist: 32, blend: 0, xrepeat: t.xr > 0 ? t.xr : 64, yrepeat: t.yr > 0 ? t.yr : 64,
      xoffset: 0, yoffset: 0, sectnum: s, statnum: 0, ang: 0, owner: -1, xvel: 0, yvel: 0, zvel: 0, type, flags: 0, extra: -1 };
    if (type === 1 || type === 2) { sp.picnum = 2522; sp.cstat = 128; sp.xrepeat = sp.yrepeat = 64; }
    else if (type >= 200 && type < 260) { sp.statnum = 6; sp.cstat = 256 | 128; }
    else if ((type >= 40 && type < 51) || (type >= 60 && type < 81) || (type >= 100 && type < 150)) { sp.statnum = 3; sp.cstat = 128; }
    else if (type >= 400 && type < 500) { sp.statnum = 4; sp.cstat = 1 | 256; }
    else if (type >= 700 && type < 800) { sp.statnum = 7; sp.cstat = 0x8000; }
    else if (type >= 3 && type < 20) { sp.statnum = 10; sp.cstat = 0x8000; }
    if (type) sp.ext = {};
    return sp;
  }

  function setTool(t) {
    st.tool = t; st.drawPts = []; st.vert = null;
    document.querySelectorAll('#feTools [data-t]').forEach((b) => b.classList.toggle('on', b.dataset.t === t));
    if (t === 'sprite') spriteChooser(); else if (t !== 'select') closeSheet();
    draw();
  }

  // ---- panels ----------------------------------------------------------------------------------------
  function openSheet(h) { const s = $('feSheet'); s.innerHTML = h; s.classList.add('open'); requestAnimationFrame(resize); }
  function closeSheet() { const s = $('feSheet'); if (!s) return; s.classList.remove('open'); s.innerHTML = ''; requestAnimationFrame(resize); }
  let uid = 0;
  // a form field bound to obj[key]; kinds: num, bits (with list), bool, select (with options), tile
  function field(label, obj, key, kind, extra) {
    const id = 'f' + (++uid);
    const v = obj[key] === undefined ? 0 : obj[key];
    let input;
    if (kind === 'bool') input = '<input type="checkbox" id="' + id + '"' + (v ? ' checked' : '') + '>';
    else if (kind === 'select') input = '<select id="' + id + '">' + extra.map(([k, n]) => '<option value="' + k + '"' + (+k === +v ? ' selected' : '') + '>' + esc(n) + '</option>').join('') + (extra.some(([k]) => +k === +v) ? '' : '<option value="' + v + '" selected>' + v + '</option>') + '</select>';
    else if (kind === 'tile') input = '<div class="picrow"><canvas class="tileprev" id="' + id + 'Img" width="40" height="40"></canvas><input id="' + id + '" type="number" value="' + v + '" style="flex:1"><button id="' + id + 'Pick" class="small">Pick</button></div>';
    else input = '<input id="' + id + '" type="number" value="' + v + '">';
    binders.push(() => {
      const el = $(id); if (!el) return;
      const set = (nv) => { snapshot(); obj[key] = nv; changed(); if (extra && extra.after) extra.after(); };
      if (kind === 'bool') el.onchange = () => set(el.checked ? 1 : 0);
      else if (kind === 'select') el.onchange = () => set(parseInt(el.value, 10));
      else el.onchange = () => { const n = parseInt(el.value, 10); if (!isNaN(n)) set(n); };
      if (kind === 'tile') {
        const show = () => UI() && UI().drawTile($(id + 'Img'), obj[key]);
        show();
        el.addEventListener('change', show);
        $(id + 'Pick').onclick = () => UI().openTiles(obj[key], null, (n) => { el.value = n; set(n); show(); });
      }
    });
    return '<div class="row"><label for="' + id + '">' + esc(label) + '</label>' + input + '</div>';
  }
  function bitsField(label, obj, key, list) {
    const id = 'f' + (++uid);
    binders.push(() => {
      document.querySelectorAll('[data-bits="' + id + '"]').forEach((el) => el.onchange = () => {
        snapshot(); const b = +el.dataset.bit; obj[key] = el.checked ? (obj[key] | b) : (obj[key] & ~b); changed();
      });
    });
    return '<details><summary>' + esc(label) + '</summary><div class="chips">' + list.map(([b, n]) =>
      '<label class="chk"><input type="checkbox" data-bits="' + id + '" data-bit="' + b + '"' + ((obj[key] & b) ? ' checked' : '') + '> ' + esc(n) + '</label>').join('') + '</div></details>';
  }
  // the height of a z value, in map units with up being more (what the simple editor shows)
  function heightField(label, obj, key) {
    const id = 'f' + (++uid);
    binders.push(() => { const el = $(id); if (el) el.onchange = () => { const n = parseFloat(el.value); if (!isNaN(n)) { snapshot(); obj[key] = Math.round(-n * Z); changed(); } }; });
    return '<div class="row"><label for="' + id + '">' + esc(label) + '</label><input id="' + id + '" type="number" step="16" value="' + Math.round(-obj[key] / Z) + '"></div>';
  }
  let binders = [];
  function bind() { const b = binders; binders = []; b.forEach((f) => f()); }

  // the extra data of any object, grouped; tx/rx with their channel names and a "free channel" helper
  function extraSection(kind, obj, rerender) {
    if (!obj.ext) {
      binders.push(() => { const b = $('feAddX'); if (b) b.onclick = () => { snapshot(); obj.ext = {}; changed(); rerender(); }; });
      return '<details><summary>Extra data (triggers, timing, …)</summary><p><small>This ' + kind + ' has none.</small></p><button id="feAddX" class="small">Add extra data</button></details>';
    }
    const spec = specOf(kind), x = obj.ext;
    const known = new Set();
    let out = '';
    const label = (k) => LABELS[k] || k;
    const one = (k) => {
      known.add(k);
      if (HIDE.has(k) || !spec.some((f) => f[0] === k)) return '';
      const bits = bitsOf(spec, k);
      if (k === 'command') return field(label(k), x, k, 'select', Object.entries(B().commands).filter(([c]) => +c < 70 || +x[k] === +c).map(([c, n]) => [c, c + ': ' + n]));
      if (k === 'Key' || k === 'key') return field(label(k), x, k, 'select', KEYS.map((n, i) => [i, n]));
      if (k === 'rxID' || k === 'txID') {
        const ch = B().channels[x[k]];
        return field(label(k) + (ch ? ' — ' + ch : ''), x, k, 'num');
      }
      if (bits === 1) return field(label(k), x, k, 'bool');
      if (kind === 'sprite' && /^data[1-4]$/.test(k)) {
        const t = B().sprite[obj.type], n = t && t.data && t.data[+k.slice(4) - 1];
        return field((n ? n.replace(/^\*/, '') + ' (' + label(k) + ')' : label(k)), x, k, 'num');
      }
      return field(label(k), x, k, 'num');
    };
    for (const [g, keys] of GROUPS) {
      const inner = keys.filter((k) => spec.some((f) => f[0] === k)).map(one).join('');
      if (inner) out += '<details' + (g === 'Trigger' ? ' open' : '') + '><summary>' + g + '</summary>' + inner + '</details>';
    }
    const rest = spec.map((f) => f[0]).filter((k) => !known.has(k) && !HIDE.has(k));
    if (rest.length) out += '<details><summary>Other</summary>' + rest.map(one).join('') + '</details>';
    binders.push(() => {
      const fr = $('feFreeCh'); if (fr) fr.onclick = () => { const c = freeChannel(); toast('Channel ' + c + ' is free'); };
      const rm = $('feRmX'); if (rm) rm.onclick = () => { snapshot(); delete obj.ext; changed(); rerender(); };
    });
    return '<details open><summary>Extra data</summary>' + out + '<div class="btns"><button id="feFreeCh" class="small">A free channel?</button><button id="feRmX" class="small">Remove extra data</button></div></details>';
  }
  function freeChannel() {
    const used = new Set();
    for (const o of linkObjects()) { used.add(o.x.rxID); used.add(o.x.txID); }
    let c = 100; while (used.has(c)) c++;
    return c;
  }
  // who this object sends to and hears from
  function linksSection(kind, i, obj) {
    if (!obj.ext) return '';
    const x = obj.ext, objs = linkObjects();
    const to = x.txID ? objs.filter((o) => o.x.rxID === x.txID && !(o.kind === kind && o.i === i)) : [];
    const from = x.rxID ? objs.filter((o) => o.x.txID === x.rxID && !(o.kind === kind && o.i === i)) : [];
    const name = (o) => o.kind + ' ' + o.i + (o.kind === 'sprite' ? ' (' + typeName('sprite', st.map.sprites[o.i].type) + ')' : o.kind === 'sector' && st.map.sectors[o.i].type ? ' (' + typeName('sector', st.map.sectors[o.i].type) + ')' : '');
    const list = (arr, dir) => arr.slice(0, 30).map((o, k) => '<button class="small" data-go="' + dir + k + '">' + esc(name(o)) + '</button>').join('');
    binders.push(() => {
      to.slice(0, 30).forEach((o, k) => { const b = document.querySelector('[data-go="t' + k + '"]'); if (b) b.onclick = () => selectObj(o.kind, o.i); });
      from.slice(0, 30).forEach((o, k) => { const b = document.querySelector('[data-go="f' + k + '"]'); if (b) b.onclick = () => selectObj(o.kind, o.i); });
    });
    const special = B().channels[x.txID];
    return '<details open><summary>Links</summary>'
      + (x.txID ? '<p><small>Sends "' + esc(B().commands[x.command] || x.command) + '" to channel ' + x.txID + (special ? ' (' + special + ')' : '') + ':</small></p><div class="chips">' + (list(to, 't') || '<small>nobody listens there</small>') + '</div>' : '')
      + (x.rxID ? '<p><small>Listens on channel ' + x.rxID + (B().channels[x.rxID] ? ' (' + B().channels[x.rxID] + ')' : '') + '; it hears:</small></p><div class="chips">' + (list(from, 'f') || '<small>nobody sends there</small>') + '</div>' : '')
      + (!x.txID && !x.rxID ? '<p><small>No channels set (see Trigger above).</small></p>' : '') + '</details>';
  }
  function selectObj(kind, i) {
    st.sel = { kind, i };
    const p = objPos(kind, i); st.view.x = p[0]; st.view.y = p[1];
    if (kind === 'sector') sectorPanel(i); else if (kind === 'wall') wallPanel(i); else spritePanel(i);
    draw();
  }

  function sectorPanel(s) {
    const sec = st.map.sectors[s];
    const types = Object.entries(B().sector).map(([k, v]) => [k, k + ': ' + v.name]);
    const re = () => sectorPanel(s);
    openSheet('<h3>Sector ' + s + ' <small>' + esc(typeName('sector', sec.type)) + '</small></h3>'
      + field('Type', sec, 'type', 'select', types)
      + heightField('Floor height', sec, 'floorz') + heightField('Ceiling height', sec, 'ceilingz')
      + field('Floor tile', sec, 'floorpicnum', 'tile') + field('Ceiling tile', sec, 'ceilingpicnum', 'tile')
      + '<details><summary>Floor: shade, palette, slope, panning</summary>' + field('Floor shade', sec, 'floorshade', 'num') + field('Floor palette', sec, 'floorpal', 'num')
      + field('Floor slope', sec, 'floorheinum', 'num') + field('Floor x panning', sec, 'floorxpanning', 'num') + field('Floor y panning', sec, 'floorypanning', 'num') + '</details>'
      + bitsField('Floor flags', sec, 'floorstat', BITS.sectorStat)
      + '<details><summary>Ceiling: shade, palette, slope, panning</summary>' + field('Ceiling shade', sec, 'ceilingshade', 'num') + field('Ceiling palette', sec, 'ceilingpal', 'num')
      + field('Ceiling slope', sec, 'ceilingheinum', 'num') + field('Ceiling x panning', sec, 'ceilingxpanning', 'num') + field('Ceiling y panning', sec, 'ceilingypanning', 'num') + '</details>'
      + bitsField('Ceiling flags (sky, slope, …)', sec, 'ceilingstat', BITS.sectorStat)
      + '<details><summary>More</summary>' + field('Visibility', sec, 'visibility', 'num') + field('Hitag', sec, 'hitag', 'num') + '</details>'
      + extraSection('sector', sec, re) + linksSection('sector', s, sec)
      + '<div class="btns"><button id="feWalls" class="small">Wall tile for all its walls…</button><button id="feWalkHere" class="small">Walk from here</button><button id="feDelS" class="small">Delete sector</button><button id="feDone" class="small on">Done</button></div>');
    bind();
    $('feWalls').onclick = () => UI().openTiles(st.map.walls[sec.wallptr].picnum, null, (n) => { snapshot(); for (let w = sec.wallptr; w < sec.wallptr + sec.wallnum; w++) st.map.walls[w].picnum = n; changed(); });
    $('feWalkHere').onclick = () => { const p = objPos('sector', s); open3d({ x: p[0], y: p[1], sector: s, z: -sec.floorz / Z, ang: 0, pitch: 0 }); };
    $('feDelS').onclick = () => { snapshot(); G().deleteSector(st.map, s); st.sel = null; closeSheet(); changed(); };
    $('feDone').onclick = () => { st.sel = null; closeSheet(); draw(); };
  }
  function wallPanel(w) {
    const wall = st.map.walls[w];
    const types = Object.entries(B().wall).map(([k, v]) => [k, k + ': ' + v.name]);
    const re = () => wallPanel(w);
    const o = st.map.walls[wall.point2];
    openSheet('<h3>Wall ' + w + ' <small>' + (wall.nextsector >= 0 ? 'opening to sector ' + wall.nextsector : 'solid') + ', ' + Math.round(Math.hypot(o.x - wall.x, o.y - wall.y)) + ' long</small></h3>'
      + field('Tile', wall, 'picnum', 'tile') + (wall.nextsector >= 0 ? field('Masked tile (in the opening)', wall, 'overpicnum', 'tile') : '')
      + bitsField('Flags (blocking, aligned, masked, …)', wall, 'cstat', BITS.wall)
      + '<details><summary>Shade, palette, size, panning</summary>' + field('Shade', wall, 'shade', 'num') + field('Palette', wall, 'pal', 'num')
      + field('X repeat', wall, 'xrepeat', 'num') + field('Y repeat', wall, 'yrepeat', 'num') + field('X panning', wall, 'xpanning', 'num') + field('Y panning', wall, 'ypanning', 'num') + '</details>'
      + field('Type', wall, 'type', 'select', types)
      + '<details><summary>More</summary>' + field('Hitag', wall, 'hitag', 'num') + '</details>'
      + extraSection('wall', wall, re) + linksSection('wall', w, wall)
      + '<div class="btns"><button id="feSplit" class="small">Split (add a corner)</button>' + (wall.nextsector >= 0 ? '<button id="feOther" class="small">Other side</button>' : '') + '<button id="feDone" class="small on">Done</button></div>');
    bind();
    $('feSplit').onclick = () => { snapshot(); G().splitWall(st.map, w); st.sel = null; closeSheet(); changed(); toast('Split: a new corner in the middle (Points to move it)'); };
    if ($('feOther')) $('feOther').onclick = () => selectObj('wall', wall.nextwall);
    $('feDone').onclick = () => { st.sel = null; closeSheet(); draw(); };
  }
  function spritePanel(i) {
    const sp = st.map.sprites[i];
    if (!sp) { closeSheet(); return; }
    const re = () => spritePanel(i);
    const t = B().sprite[sp.type];
    openSheet('<h3>Sprite ' + i + ' <small>' + esc(typeName('sprite', sp.type)) + ', sector ' + sp.sectnum + '</small></h3>'
      + '<div class="row"><label>Type</label><button id="feType" class="small">' + esc(sp.type + ': ' + typeName('sprite', sp.type)) + '</button></div>'
      + field('Tile', sp, 'picnum', 'tile')
      + field('Facing (0 east, 512 south, 1024 west, 1536 north)', sp, 'ang', 'num')
      + heightField('Height (z)', sp, 'z')
      + bitsField('Flags (blocking, wall/floor sprite, invisible, …)', sp, 'cstat', BITS.sprite)
      + '<details><summary>Position, size, shade, palette</summary>' + field('X', sp, 'x', 'num') + field('Y', sp, 'y', 'num') + field('X repeat (width)', sp, 'xrepeat', 'num') + field('Y repeat (height)', sp, 'yrepeat', 'num')
      + field('Shade', sp, 'shade', 'num') + field('Palette', sp, 'pal', 'num') + field('Clip distance', sp, 'clipdist', 'num') + field('X offset', sp, 'xoffset', 'num') + field('Y offset', sp, 'yoffset', 'num')
      + field('Status (statnum)', sp, 'statnum', 'num') + field('Flags (hitag)', sp, 'flags', 'num') + '</details>'
      + extraSection('sprite', sp, re) + linksSection('sprite', i, sp)
      + '<div class="btns"><button id="feTurnL" class="small">⟲</button><button id="feTurnR" class="small">⟳</button><button id="feFloor" class="small">Onto the floor</button><button id="feDup" class="small">Copy</button><button id="feDelP" class="small">Delete</button><button id="feDone" class="small on">Done</button></div>'
      + (t && t.caption ? '' : ''));
    bind();
    $('feType').onclick = () => spriteChooser((type) => { snapshot(); const fresh = spriteOf(type, sp.x, sp.y, sp.z, sp.sectnum); Object.assign(sp, { type, picnum: fresh.picnum, xrepeat: fresh.xrepeat, yrepeat: fresh.yrepeat, statnum: fresh.statnum, pal: fresh.pal }); if (!sp.ext && type) sp.ext = {}; changed(); spritePanel(i); });
    $('feTurnL').onclick = () => { snapshot(); sp.ang = (sp.ang - 256 + 2048) & 2047; changed(); };
    $('feTurnR').onclick = () => { snapshot(); sp.ang = (sp.ang + 256) & 2047; changed(); };
    $('feFloor').onclick = () => { snapshot(); sp.z = Math.round(G().slopeZ(st.map, sp.sectnum, sp.x, sp.y, false)); changed(); spritePanel(i); };
    $('feDup').onclick = () => { snapshot(); const c = JSON.parse(JSON.stringify(sp)); c.x += 128; c.extra = -1; st.map.sprites.push(c); st.sel = { kind: 'sprite', i: st.map.sprites.length - 1 }; changed(); spritePanel(st.sel.i); };
    $('feDelP').onclick = () => { snapshot(); st.map.sprites.splice(i, 1); st.sel = null; closeSheet(); changed(); };
    $('feDone').onclick = () => { st.sel = null; closeSheet(); draw(); };
  }
  function cornerPanel() {
    if (!st.vert) return;
    const [x, y] = st.vert;
    const n = st.map.walls.filter((w) => w.x === x && w.y === y).length;
    openSheet('<h3>Corner</h3><div class="row"><label>X</label><input id="feCx" type="number" value="' + x + '"></div><div class="row"><label>Y</label><input id="feCy" type="number" value="' + y + '"></div>'
      + '<p><small>' + n + ' wall' + (n === 1 ? '' : 's') + ' start here. Drag the corner to move it.</small></p>'
      + '<div class="btns"><button id="feCdel" class="small">Remove corner</button><button id="feDone" class="small on">Done</button></div>');
    const mv = () => {
      const nx = parseInt($('feCx').value, 10), ny = parseInt($('feCy').value, 10);
      if (isNaN(nx) || isNaN(ny)) return;
      snapshot(); G().moveCorner(st.map, st.vert[0], st.vert[1], nx, ny); const old = st.vert; st.vert = [nx, ny];
      if (crossing()) { G().moveCorner(st.map, nx, ny, old[0], old[1]); st.vert = old; st.undo.pop(); toast('Walls would cross there'); }
      changed();
    };
    $('feCx').onchange = mv; $('feCy').onchange = mv;
    $('feCdel').onclick = () => { try { snapshot(); G().deleteCorner(st.map, x, y); st.vert = null; closeSheet(); changed(); } catch (e) { st.undo.pop(); toast(e.message); } };
    $('feDone').onclick = () => { st.vert = null; closeSheet(); draw(); };
  }
  function drawPanel() {
    openSheet('<h3>New sector</h3><p><small>' + st.drawPts.length + ' corners. Tap the first corner again, or Finish. Corners snap to existing corners: put them on a wall\'s ends (or on a wall) to join the new sector to it.</small></p>'
      + '<div class="btns"><button id="feFin" class="small on">Finish</button><button id="feBackPt" class="small">Remove last corner</button><button id="feCancel" class="small">Cancel</button></div>');
    $('feFin').onclick = finishDraw;
    $('feBackPt').onclick = () => { st.drawPts.pop(); drawPanel(); draw(); };
    $('feCancel').onclick = () => { st.drawPts = []; closeSheet(); draw(); };
  }
  function finishDraw() {
    const pts = st.drawPts;
    if (pts.length < 3) { toast('A sector needs at least 3 corners'); return; }
    // looks like the sector it's in or next to (or the selected one)
    const m = st.map;
    const cx = pts.reduce((a, p) => a + p[0], 0) / pts.length, cy = pts.reduce((a, p) => a + p[1], 0) / pts.length;
    let like = G().sectorAt(m, cx, cy, -1);
    if (like < 0 && st.lastSector >= 0 && st.lastSector < m.sectors.length) like = st.lastSector;
    const src = like >= 0 ? m.sectors[like] : null;
    const fields = src ? { floorz: src.floorz, ceilingz: src.ceilingz, floorpicnum: src.floorpicnum, ceilingpicnum: src.ceilingpicnum, floorshade: src.floorshade, ceilingshade: src.ceilingshade, ceilingstat: src.ceilingstat & 1 } : null;
    const wallFields = src ? { picnum: m.walls[src.wallptr].picnum, overpicnum: m.walls[src.wallptr].picnum } : null;
    try {
      snapshot();
      const s = G().addSector(m, pts, fields, wallFields);
      st.drawPts = []; st.lastSector = s; st.sel = { kind: 'sector', i: s };
      changed(); sectorPanel(s);
      toast('Sector ' + s + ' added' + (like >= 0 ? ' (looks like sector ' + like + ')' : ''));
    } catch (e) { st.undo.pop(); toast(e.message); }
  }
  // Blood's object types, grouped like XMAPEDIT groups them, with a search box
  function spriteChooser(pick) {
    const types = Object.entries(B().sprite).map(([k, v]) => ({ type: +k, ...v }));
    const groupOf = (t) => t.type === 0 ? 'Decoration' : t.type < 20 ? 'Markers' : t.type < 40 ? 'Switches and triggers' : t.type < 60 ? 'Weapons' : t.type < 100 ? 'Ammo' : t.type < 150 ? 'Items, keys, armor' : t.type < 260 ? 'Enemies' : t.type < 400 ? 'Other' : t.type < 500 ? 'Things (barrels, traps, …)' : t.type < 700 ? 'Other' : 'Generators, sounds';
    const groups = {};
    for (const t of types) (groups[groupOf(t)] = groups[groupOf(t)] || []).push(t);
    const render = (q) => {
      let h = '<h3>' + (pick ? 'Sprite type' : 'Place a sprite') + '</h3><input id="feQ" type="text" placeholder="Search (e.g. switch, cultist, key)" value="' + esc(q || '') + '">';
      for (const g in groups) {
        const list = groups[g].filter((t) => !q || (t.name + ' ' + t.type).toLowerCase().includes(q.toLowerCase()));
        if (!list.length) continue;
        h += '<div><small>' + g + '</small><div class="chips">' + list.map((t) => '<button class="small' + (t.type === st.spriteType && !pick ? ' on' : '') + '" data-ty="' + t.type + '">' + esc(t.name) + '</button>').join('') + '</div></div>';
      }
      openSheet(h);
      const qi = $('feQ'); qi.oninput = () => { render(qi.value); const n = $('feQ'); n.focus(); n.setSelectionRange(n.value.length, n.value.length); };
      document.querySelectorAll('[data-ty]').forEach((b) => b.onclick = () => { const ty = +b.dataset.ty; if (pick) pick(ty); else { st.spriteType = ty; render(qi.value); hint(); } });
    };
    render('');
  }
  function settingsPanel() {
    const h = st.map.header;
    openSheet('<h3>Map settings</h3>' + field('Darker with distance (visibility)', h, 'visibility', 'num') + field('Parallax type', h, 'parallaxType', 'num')
      + '<p><small>Version ' + (st.map.version >> 8) + ', ' + st.map.sectors.length + ' sectors, ' + st.map.walls.length + ' walls, ' + st.map.sprites.length + ' sprites.</small></p>'
      + '<div class="btns"><button id="feDone" class="small on">Done</button></div>');
    bind();
    $('feDone').onclick = closeSheet;
  }
  function checkPanel() {
    const warn = [], p = G().problems(st.map, warn);
    const starts = st.map.sprites.filter((s) => s.type === 1).length, dm = st.map.sprites.filter((s) => s.type === 2).length;
    if (!starts) warn.push('There is no single player start (sprite type 1).');
    if (dm < 2) warn.push('Bloodbath needs bloodbath starts (sprite type 2): ' + dm + ' now.');
    openSheet('<h3>Check</h3><div class="problems">' + p.map((t) => '<div class="error">✗ ' + esc(t) + '</div>').join('') + warn.slice(0, 40).map((t) => '<div class="warning">• ' + esc(t) + '</div>').join('')
      + (!p.length && !warn.length ? '<div style="color:var(--ok)">Everything looks right.</div>' : '') + '</div><div class="btns"><button id="feDone" class="small on">Close</button></div>');
    $('feDone').onclick = closeSheet;
  }

  function open3d(at) {
    if (!root.View3D) return;
    const copy = F().parseMap(bytesOf(st.map)); // the walkthrough moves doors: on a copy
    root.View3D.open(copy, $('v3d'), at && at.sector !== undefined ? { at, walk: true } : {});
  }

  root.FullEditor = { open, openFile, restore, close, isOpen: () => !!st, current: () => st && st.map, newMap,
    // for tests: where a map point is on the screen, and moving the view
    screenOf: (x, y) => { const r = $('feCv').getBoundingClientRect(), p = toScreen(x, y); return [r.left + p[0], r.top + p[1]]; },
    look: (x, y, scale) => { st.view = { x, y, scale }; draw(); } };
})(typeof window !== 'undefined' ? window : this);
