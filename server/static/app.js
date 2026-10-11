"use strict";
// NotBlood Live dashboard: lobby list, live player stats, level map and event feed.

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

if (!window.isSecureContext && !/^(localhost|127\.|\[::1\])/.test(location.hostname)) {
  location.replace("https://" + location.host + location.pathname); // crypto.subtle needs https
}

const store = {
  get(k, d) { try { const v = localStorage.getItem(k); return v === null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch { /* private mode */ } },
  del(k) { try { localStorage.removeItem(k); } catch { /* ignore */ } },
};

const PLAYER_COLORS = ["#4da3ff", "#ff4b3e", "#4fd17a", "#f2d34b", "#c77dff", "#ff9f43", "#3fe0d0", "#ff7ad9"];
const WEAPONS = ["", "Pitchfork", "Flare Gun", "Shotgun", "Tommy Gun", "Napalm", "Dynamite", "Spray Can", "Tesla", "Life Leech", "Voodoo Doll", "Proximity", "Remote", "Beast"];
const KEYS = { 1: ["Skull", "#e9e2cf"], 2: ["Eye", "#6fb3ff"], 3: ["Fire", "#ff8a2b"], 4: ["Dagger", "#c9c9c9"], 5: ["Spider", "#b07be0"], 6: ["Moon", "#f2d34b"], 7: ["Key 7", "#9be37a"] };

// ---- settings -------------------------------------------------------------------------------

const SETTINGS = [
  ["Players", [
    ["health", "Health", true], ["armor", "Armor (total)", true], ["armor3", "Armor: fire / body / spirit", false],
    ["weapon", "Current weapon and ammo", true], ["weapons", "All weapons", true], ["keys", "Keys", true],
    ["items", "Inventory items", false], ["powerups", "Active power-ups", true], ["frags", "Frags and deaths", false],
  ]],
  ["Level", [
    ["level", "Level name and difficulty", true], ["time", "Level time", true], ["kills", "Enemies killed", true], ["secrets", "Secrets found", true],
  ]],
  ["Map", [
    ["map", "Show the map", true], ["names", "Player names on the map", true], ["enemies", "Enemies on the map", false],
    ["rotate", "Turn the map with the followed player", false], ["trail", "Player trails", false],
  ]],
  ["Screen", [
    ["feed", "Event list", true], ["autoopen", "Open the game automatically when only one is running", true],
  ]],
];
const settings = Object.assign(
  Object.fromEntries(SETTINGS.flatMap(([, items]) => items.map(([k, , d]) => [k, d]))),
  { textsize: "1" },
  store.get("nbl-settings", {}),
);
function saveSettings() { store.set("nbl-settings", settings); applySettings(); }
function applySettings() {
  document.documentElement.style.setProperty("--scale", settings.textsize);
  $("#feed-wrap").classList.toggle("hidden", !settings.feed);
  $(".game-grid").classList.toggle("no-map", !settings.map);
  $("#map-wrap").classList.toggle("hidden", !settings.map);
  render();
}
function buildSettings() {
  const body = $("#settings-body");
  body.innerHTML = "";
  for (const [title, items] of SETTINGS) {
    const fs = document.createElement("fieldset");
    fs.innerHTML = `<legend>${esc(title)}</legend>`;
    for (const [k, label] of items) {
      const l = document.createElement("label");
      l.innerHTML = `<input type="checkbox" ${settings[k] ? "checked" : ""}> ${esc(label)}`;
      l.querySelector("input").addEventListener("change", (e) => { settings[k] = e.target.checked; saveSettings(); });
      fs.appendChild(l);
    }
    body.appendChild(fs);
  }
  const fs = document.createElement("fieldset");
  fs.innerHTML = `<legend>Text size</legend><select aria-label="Text size">
    <option value="0.85">Small</option><option value="1">Normal</option><option value="1.3">Large (TV)</option><option value="1.7">Huge (far away TV)</option></select>`;
  const sel = fs.querySelector("select");
  sel.value = settings.textsize;
  sel.addEventListener("change", () => { settings.textsize = sel.value; saveSettings(); resizeMap(); });
  body.appendChild(fs);
}

// ---- connection -----------------------------------------------------------------------------

let ws = null;
let key = store.get("nbl-key", null) || sessionStorage.getItem("nbl-key");
let lobbies = [];
let history = [];
let current = null; // lobby name being watched
let userPicked = false;
const maps = {}; // lobby -> {id, walls: Int32Array-like, bounds}
const prevStats = {};
const feeds = {};
const tracks = {}; // lobby -> player id -> {x,y,ang,px,py,pang,t,trail[]}

async function hmacHex(k, msg) {
  const enc = new TextEncoder();
  const ck = await crypto.subtle.importKey("raw", enc.encode(k), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const sig = await crypto.subtle.sign("HMAC", ck, enc.encode(msg));
  return [...new Uint8Array(sig)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function showLogin(msg) {
  $("#app").classList.add("hidden");
  $("#login").classList.remove("hidden");
  $("#login-error").textContent = msg || "";
  $("#key").focus();
}

let retry = 0;
function connect() {
  if (!key) return showLogin();
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(`${proto}//${location.host}/dash`);
  let authed = false;
  ws.onmessage = async (ev) => {
    const data = ev.data;
    if (!authed) {
      const f = data.split("\t");
      if (f[0] === "challenge") {
        ws.send("auth\t" + await hmacHex(key, f[1]));
        return;
      }
      if (f[0] === "error") {
        key = null; store.del("nbl-key"); sessionStorage.removeItem("nbl-key");
        ws.onclose = null; ws.close();
        return showLogin(f[1] || "Wrong key");
      }
    }
    let msg;
    try { msg = JSON.parse(data); } catch { return; }
    if (msg.op === "ok") {
      authed = true; retry = 0;
      history = msg.history || [];
      $("#login").classList.add("hidden");
      $("#app").classList.remove("hidden");
      $("#conn").classList.add("on");
      if (current) watch(current);
      renderHistory();
      resizeMap();
    } else if (msg.op === "state") {
      onState(msg);
    } else if (msg.op === "map") {
      onMap(msg);
    } else if (msg.op === "history") {
      history = msg.history || []; renderHistory();
    }
  };
  ws.onclose = () => {
    $("#conn").classList.remove("on");
    if (!key) return;
    retry = Math.min(retry + 1, 10);
    setTimeout(connect, 500 * retry);
  };
}

$("#login-form").addEventListener("submit", (e) => {
  e.preventDefault();
  key = $("#key").value.trim();
  if (!key) return;
  if ($("#remember").checked) store.set("nbl-key", key); else sessionStorage.setItem("nbl-key", key);
  $("#login-error").textContent = "";
  if (ws) { ws.onclose = null; ws.close(); }
  connect();
});
$("#logout").addEventListener("click", () => {
  key = null; store.del("nbl-key"); sessionStorage.removeItem("nbl-key");
  if (ws) { ws.onclose = null; ws.close(); }
  $("#settings").classList.add("hidden");
  showLogin();
});

function watch(name) {
  current = name;
  if (ws && ws.readyState === 1) ws.send(JSON.stringify({ op: "watch", lobby: name }));
  render();
}

// ---- state ----------------------------------------------------------------------------------

function fmtTime(s) {
  s = Math.max(0, Math.floor(s || 0));
  const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = s % 60;
  return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(sec).padStart(2, "0");
}
function clock() { return new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" }); }

function addEvent(lobby, text, color) {
  const f = (feeds[lobby] ||= []);
  f.unshift({ t: clock(), text, color });
  f.length = Math.min(f.length, 60);
}

function diffEvents(l) {
  const s = l.stats, p = prevStats[l.name];
  prevStats[l.name] = s;
  if (!s) return;
  if (!p) { addEvent(l.name, `Watching ${s.level || "the game"}`); return; }
  if (s.level !== p.level) addEvent(l.name, `Level: ${s.level}`);
  if (s.secrets && p.secrets && s.secrets[0] > p.secrets[0]) addEvent(l.name, `Secret found (${s.secrets[0]}/${s.secrets[1]})`);
  const before = Object.fromEntries((p.players || []).map((x) => [x.id, x]));
  for (const pl of s.players || []) {
    const o = before[pl.id];
    const col = PLAYER_COLORS[pl.id % 8];
    if (!o) { addEvent(l.name, `${pl.name} is in the game`, col); continue; }
    if (o.alive && !pl.alive) addEvent(l.name, `${pl.name} died`, col);
    if (!o.alive && pl.alive) addEvent(l.name, `${pl.name} is back`, col);
    for (const k of pl.keys || []) if (!(o.keys || []).includes(k)) addEvent(l.name, `${pl.name} got the ${(KEYS[k] || ["Key"])[0]} key`, col);
    for (const w of pl.weapons || []) if (!(o.weapons || []).includes(w)) addEvent(l.name, `${pl.name} picked up the ${WEAPONS[w] || "weapon " + w}`, col);
    if ((pl.frags || 0) > (o.frags || 0)) addEvent(l.name, `${pl.name} scored a frag`, col);
  }
  for (const id in before) if (!(s.players || []).some((x) => String(x.id) === id)) addEvent(l.name, `${before[id].name} left`, PLAYER_COLORS[id % 8]);
}

function onState(msg) {
  const names = new Set(msg.lobbies.map((l) => l.name));
  for (const l of lobbies) if (!names.has(l.name)) {
    addEvent(l.name, "Game ended");
    if (ws && ws.readyState === 1) ws.send(JSON.stringify({ op: "history" }));
  }
  lobbies = msg.lobbies;
  const now = performance.now();
  for (const l of lobbies) {
    diffEvents(l);
    const tr = (tracks[l.name] ||= {});
    for (const p of (l.stats && l.stats.players) || []) {
      const t = (tr[p.id] ||= { x: p.x, y: p.y, ang: p.ang, px: p.x, py: p.y, pang: p.ang, t: now, trail: [] });
      if (t.x !== p.x || t.y !== p.y || t.ang !== p.ang) {
        const k = Math.min(1, (now - t.t) / 200);
        t.px = t.px + (t.x - t.px) * k; t.py = t.py + (t.y - t.py) * k; t.pang = lerpAng(t.pang, t.ang, k);
        t.x = p.x; t.y = p.y; t.ang = p.ang; t.t = now;
        const last = t.trail[t.trail.length - 1];
        if (!last || Math.hypot(last[0] - p.x, last[1] - p.y) > 256) { t.trail.push([p.x, p.y]); if (t.trail.length > 400) t.trail.shift(); }
      }
    }
  }
  if (!userPicked && settings.autoopen) {
    const live = lobbies.filter((l) => l.stats);
    if (live.length === 1 && current !== live[0].name) watch(live[0].name);
  }
  if (current && !names.has(current) && !userPicked) current = null;
  render();
}

function lerpAng(a, b, k) {
  let d = ((b - a + 3072) % 2048) - 1024;
  return a + d * k;
}

function onMap(msg) {
  const w = msg.walls || [];
  let minx = Infinity, miny = Infinity, maxx = -Infinity, maxy = -Infinity;
  for (let i = 0; i + 4 < w.length + 1; i += 5) {
    minx = Math.min(minx, w[i], w[i + 2]); maxx = Math.max(maxx, w[i], w[i + 2]);
    miny = Math.min(miny, w[i + 1], w[i + 3]); maxy = Math.max(maxy, w[i + 1], w[i + 3]);
  }
  maps[msg.lobby] = { id: msg.map_id, walls: w, bounds: [minx, miny, maxx, maxy] };
  const tr = tracks[msg.lobby]; if (tr) for (const id in tr) tr[id].trail = [];
  view.reset = true;
}

// ---- rendering ------------------------------------------------------------------------------

function render() {
  renderTabs();
  const l = lobbies.find((x) => x.name === current);
  $("#overview").classList.toggle("hidden", !!l);
  $("#game").classList.toggle("hidden", !l);
  if (!l) return renderOverview();
  renderLevel(l);
  renderPlayers(l);
  renderFeed(l);
  renderFollow(l);
}

function renderTabs() {
  const tabs = $("#tabs");
  const want = ["", ...lobbies.map((l) => l.name)];
  const have = [...tabs.children].map((b) => b.dataset.name);
  if (want.join("\n") !== have.join("\n")) {
    tabs.innerHTML = "";
    for (const n of want) {
      const b = document.createElement("button");
      b.dataset.name = n;
      b.textContent = n || "All games";
      b.addEventListener("click", () => { userPicked = true; if (n) watch(n); else { current = null; render(); } });
      tabs.appendChild(b);
    }
  }
  for (const b of tabs.children) b.classList.toggle("active", (b.dataset.name || null) === (current || null));
}

function lobbyState(l) {
  if (l.state === "waiting") return `<span class="badge waiting">Waiting for players ${l.players.length}/${l.max}</span>`;
  if (l.kind === "lan") return `<span class="badge playing">LAN game</span>`;
  return `<span class="badge playing">Playing</span>`;
}

function renderOverview() {
  const list = $("#lobby-list");
  list.innerHTML = "";
  $("#no-lobbies").classList.toggle("hidden", lobbies.length > 0);
  for (const l of lobbies) {
    const b = document.createElement("button");
    b.className = "lobby";
    const s = l.stats;
    b.innerHTML = `<span class="name">${esc(l.name)}</span>${lobbyState(l)}
      <span>${esc(s ? s.level : "Not started yet")}</span>
      <span class="muted">${l.players.map(esc).join(", ")}</span>
      ${l.mod ? `<span class="muted">Mod: ${esc(l.mod)}</span>` : ""}
      ${l.started ? `<span class="muted">Running for ${fmtTime(Date.now() / 1000 - l.started)}</span>` : ""}`;
    b.addEventListener("click", () => { userPicked = true; watch(l.name); });
    list.appendChild(b);
  }
}

function renderHistory() {
  const h = $("#history");
  h.innerHTML = "";
  if (!history.length) { h.innerHTML = `<p class="muted">Nothing yet.</p>`; return; }
  for (const g of [...history].reverse()) {
    const d = new Date(g.ended * 1000);
    const row = document.createElement("div");
    row.className = "row";
    row.innerHTML = `<b>${esc(g.name)}</b><span>${(g.players || []).map(esc).join(", ")}</span>
      <span class="muted">${esc((g.levels || []).join(" › "))}</span>
      <span class="muted">${fmtTime(g.ended - g.started)} · ${d.toLocaleDateString()} ${d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}</span>`;
    h.appendChild(row);
  }
}

function renderLevel(l) {
  const s = l.stats;
  const bar = $("#levelbar");
  if (!s) { bar.innerHTML = `<span class="lvl">${esc(l.name)}</span>${lobbyState(l)}<span class="stat">${l.players.map(esc).join(", ")}</span>`; return; }
  const parts = [];
  if (settings.level) parts.push(`<span class="lvl">${esc(s.level)}</span>`, `<span class="stat">${esc(s.diff || "")} · ${esc(s.mode || "")}</span>`);
  if (settings.time) parts.push(`<span class="stat">Time <b>${fmtTime(s.time)}</b></span>`);
  if (settings.kills && s.kills) parts.push(`<span class="stat">Kills <b>${s.kills[0]}/${s.kills[1]}</b></span>`);
  if (settings.secrets && s.secrets) parts.push(`<span class="stat">Secrets <b>${s.secrets[0]}/${s.secrets[1]}</b></span>`);
  if (l.mod) parts.push(`<span class="stat">Mod <b>${esc(l.mod)}</b></span>`);
  if (l.stats_age > 5) parts.push(`<span class="badge waiting">No update for ${Math.round(l.stats_age)} s</span>`);
  bar.innerHTML = parts.join("");
}

function renderPlayers(l) {
  const box = $("#players");
  const players = (l.stats && l.stats.players) || l.players.map((n, i) => ({ id: i, name: n }));
  const html = players.map((p) => {
    const col = PLAYER_COLORS[p.id % 8];
    const out = [`<div class="card ${p.alive === 0 ? "dead" : ""}" style="--pc:${col}">`,
      `<div class="head"><span class="pname">${esc(p.name)}</span>${settings.frags && p.frags !== undefined ? `<span class="frags">${p.frags} frags · ${p.deaths} deaths</span>` : ""}</div>`];
    if (p.health === undefined) return out.join("") + `<span class="muted">Waiting&hellip;</span></div>`;
    if (settings.health) {
      const hp = Math.max(0, p.health);
      out.push(`<div class="row2"><span class="lbl">Health</span><b>${p.alive === 0 ? "dead" : hp}</b></div>
        <div class="bar"><i style="width:${Math.min(100, hp)}%;background:${hp > 100 ? "#3fe0d0" : hp > 50 ? "var(--good)" : hp > 25 ? "var(--warn)" : "var(--bad)"}"></i></div>`);
    }
    const ar = p.armor || [0, 0, 0];
    if (settings.armor) {
      const tot = ar[0] + ar[1] + ar[2];
      out.push(`<div class="row2"><span class="lbl">Armor</span><b>${tot}</b></div><div class="bar armor"><i style="width:${Math.min(100, tot / 2)}%"></i></div>`);
    }
    if (settings.armor3) out.push(`<div class="armor3"><span title="Fire armor">🔥 ${ar[0]}</span><span title="Body armor">🛡 ${ar[1]}</span><span title="Spirit armor">✦ ${ar[2]}</span></div>`);
    if (settings.weapon) out.push(`<div class="row2"><span class="weapon">${esc(p.weapon || WEAPONS[p.wid] || "")}</span><span>${p.ammo >= 0 ? p.ammo + " ammo" : ""}</span></div>`);
    if (settings.weapons) {
      const own = new Set(p.weapons || []);
      out.push(`<div class="chips">${WEAPONS.slice(1, 13).map((w, i) => `<span class="chip ${own.has(i + 1) ? "on" : ""} ${p.wid === i + 1 ? "cur" : ""}">${w}</span>`).join("")}</div>`);
    }
    if (settings.keys && (p.keys || []).length) out.push(`<div class="chips">${p.keys.map((k) => `<span class="key" style="background:${(KEYS[k] || ["", "#888"])[1]}">${(KEYS[k] || ["Key " + k])[0]}</span>`).join("")}</div>`);
    if (settings.items && (p.items || []).length) out.push(`<div class="chips">${p.items.map(([n, a]) => `<span class="chip on">${esc(n)} ${a}%</span>`).join("")}</div>`);
    if (settings.powerups && (p.powerups || []).length) out.push(`<div class="chips">${p.powerups.map(([n, t]) => `<span class="chip cur">${esc(n)} ${t}s</span>`).join("")}</div>`);
    out.push("</div>");
    return out.join("");
  }).join("");
  if (box.dataset.html !== html) { box.innerHTML = html; box.dataset.html = html; }
}

function renderFeed(l) {
  const f = feeds[l.name] || [];
  const html = f.map((e) => `<li><time>${e.t}</time><span style="color:${e.color || "inherit"}">${esc(e.text)}</span></li>`).join("");
  const el = $("#feed");
  if (el.dataset.html !== html) { el.innerHTML = html; el.dataset.html = html; }
}

let followSel = store.get("nbl-follow", "");
function renderFollow(l) {
  const sel = $("#follow");
  const players = (l.stats && l.stats.players) || [];
  const opts = [["", "Whole level"], ...players.map((p) => [String(p.id), "Follow " + p.name])];
  const sig = opts.map((o) => o.join("=")).join("|");
  if (sel.dataset.sig !== sig) {
    sel.innerHTML = opts.map(([v, t]) => `<option value="${esc(v)}">${esc(t)}</option>`).join("");
    sel.dataset.sig = sig;
    sel.value = opts.some(([v]) => v === followSel) ? followSel : "";
  }
}
$("#follow").addEventListener("change", (e) => { followSel = e.target.value; store.set("nbl-follow", followSel); view.reset = true; });

// ---- map ------------------------------------------------------------------------------------

const canvas = $("#map");
const ctx = canvas.getContext("2d");
const view = { zoom: 1, panx: 0, pany: 0, reset: true };

function resizeMap() {
  const r = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(r.width * dpr));
  canvas.height = Math.max(1, Math.round(r.height * dpr));
}
window.addEventListener("resize", resizeMap);
new ResizeObserver(resizeMap).observe(canvas);

$("#zoom-in").addEventListener("click", () => { view.zoom *= 1.4; });
$("#zoom-out").addEventListener("click", () => { view.zoom /= 1.4; });
canvas.addEventListener("wheel", (e) => { e.preventDefault(); view.zoom *= e.deltaY < 0 ? 1.15 : 1 / 1.15; }, { passive: false });
let drag = null;
canvas.addEventListener("pointerdown", (e) => { drag = [e.clientX, e.clientY]; canvas.setPointerCapture(e.pointerId); });
canvas.addEventListener("pointermove", (e) => {
  if (!drag) return;
  const dpr = window.devicePixelRatio || 1;
  view.panx += (e.clientX - drag[0]) * dpr; view.pany += (e.clientY - drag[1]) * dpr;
  drag = [e.clientX, e.clientY];
});
canvas.addEventListener("pointerup", () => { drag = null; });

function drawMap() {
  requestAnimationFrame(drawMap);
  const l = lobbies.find((x) => x.name === current);
  if (!l || !settings.map || $("#game").classList.contains("hidden")) return;
  const m = maps[l.name];
  $("#map-empty").classList.toggle("hidden", !!m);
  const W = canvas.width, H = canvas.height;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.fillStyle = "#070606";
  ctx.fillRect(0, 0, W, H);
  if (!m) return;
  const [minx, miny, maxx, maxy] = m.bounds;
  const fit = Math.min(W / Math.max(1, maxx - minx), H / Math.max(1, maxy - miny)) * 0.92;
  const now = performance.now();
  const tr = tracks[l.name] || {};
  const pos = (id) => {
    const t = tr[id];
    if (!t) return null;
    const k = Math.min(1, (now - t.t) / 200);
    return [t.px + (t.x - t.px) * k, t.py + (t.y - t.py) * k, lerpAng(t.pang, t.ang, k)];
  };
  const followed = followSel !== "" ? pos(followSel) : null;
  if (view.reset) { view.zoom = followed ? 4 : 1; view.panx = view.pany = 0; view.reset = false; }
  const scale = fit * view.zoom;
  let cx = (minx + maxx) / 2, cy = (miny + maxy) / 2, rot = 0;
  if (followed) { cx = followed[0]; cy = followed[1]; if (settings.rotate) rot = -(followed[2] / 2048) * Math.PI * 2 - Math.PI / 2; }
  ctx.setTransform(1, 0, 0, 1, W / 2 + (followed ? 0 : view.panx), H / 2 + (followed ? 0 : view.pany));
  ctx.rotate(rot);
  ctx.scale(scale, scale);
  ctx.translate(-cx, -cy);
  const lw = 1.3 * (window.devicePixelRatio || 1) / scale;
  const w = m.walls;
  ctx.lineWidth = lw;
  ctx.strokeStyle = "rgba(200,40,40,.45)";
  ctx.beginPath();
  for (let i = 0; i + 4 < w.length + 1; i += 5) if (w[i + 4]) { ctx.moveTo(w[i], w[i + 1]); ctx.lineTo(w[i + 2], w[i + 3]); }
  ctx.stroke();
  ctx.strokeStyle = "rgba(236,228,224,.85)";
  ctx.beginPath();
  for (let i = 0; i + 4 < w.length + 1; i += 5) if (!w[i + 4]) { ctx.moveTo(w[i], w[i + 1]); ctx.lineTo(w[i + 2], w[i + 3]); }
  ctx.stroke();

  const s = l.stats || {};
  if (settings.enemies && s.enemies) {
    ctx.fillStyle = "#ff3b30";
    const r = 3.5 * (window.devicePixelRatio || 1) / scale;
    for (const e of s.enemies) { ctx.beginPath(); ctx.arc(e[0], e[1], r, 0, Math.PI * 2); ctx.fill(); }
  }
  const px = (window.devicePixelRatio || 1) / scale;
  for (const p of s.players || []) {
    const q = pos(p.id);
    if (!q) continue;
    const col = PLAYER_COLORS[p.id % 8];
    if (settings.trail && tr[p.id].trail.length > 1) {
      ctx.strokeStyle = col; ctx.globalAlpha = 0.35; ctx.lineWidth = 2 * px; ctx.beginPath();
      tr[p.id].trail.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
      ctx.lineTo(q[0], q[1]); ctx.stroke(); ctx.globalAlpha = 1;
    }
    const a = (q[2] / 2048) * Math.PI * 2;
    const sz = 11 * px;
    ctx.save();
    ctx.translate(q[0], q[1]);
    ctx.rotate(a);
    ctx.fillStyle = p.alive === 0 ? "#555" : col;
    ctx.strokeStyle = "#000"; ctx.lineWidth = 2 * px;
    ctx.beginPath(); ctx.moveTo(sz * 1.3, 0); ctx.lineTo(-sz, sz * 0.8); ctx.lineTo(-sz * 0.5, 0); ctx.lineTo(-sz, -sz * 0.8); ctx.closePath();
    ctx.stroke(); ctx.fill();
    ctx.restore();
    if (settings.names) {
      ctx.save();
      ctx.translate(q[0], q[1]);
      ctx.rotate(-rot);
      ctx.scale(px, px);
      ctx.font = `700 ${Math.round(13 * parseFloat(settings.textsize))}px system-ui, sans-serif`;
      ctx.textAlign = "center";
      ctx.lineWidth = 4; ctx.strokeStyle = "#000"; ctx.fillStyle = col;
      ctx.strokeText(p.name, 0, -18); ctx.fillText(p.name, 0, -18);
      ctx.restore();
    }
  }
}

// ---- buttons --------------------------------------------------------------------------------

$("#btn-settings").addEventListener("click", () => $("#settings").classList.toggle("hidden"));
$("#settings-close").addEventListener("click", () => $("#settings").classList.add("hidden"));
$("#btn-full").addEventListener("click", () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else document.documentElement.requestFullscreen?.().catch(() => {});
});
document.addEventListener("keydown", (e) => { if (e.key === "Escape") $("#settings").classList.add("hidden"); });

buildSettings();
applySettings();
requestAnimationFrame(drawMap);
setInterval(() => { if (!current) renderOverview(); }, 1000);
connect();
