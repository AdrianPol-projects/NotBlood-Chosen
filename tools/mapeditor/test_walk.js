// node test_walk.js: the walkthrough (walking, doors, lifts, keys, switches, secrets) on small maps
global.window = global;
global.MapCore = require('./mapcore.js');
global.MapFile = require('./mapfile.js');
global.MapGeo = require('./mapgeo.js');
require('./view3d.js');
const M = global.MapCore, F = global.MapFile, V = global.View3D;
let failed = 0;
const expect = (c, what) => { if (!c) { failed++; console.log('FAIL: ' + what); } };
const rect = (x1, y1, x2, y2) => M.cleanPoly([[x1, y1], [x2, y1], [x2, y2], [x1, y2]]);
function world(second, third) {
  const p = M.newProject();
  p.rooms.push(M.newRoom(rect(0, 0, 2048, 2048)));
  if (second) p.rooms.push(Object.assign(M.newRoom(rect(2048, 512, 4096, 1536)), second));
  if (third) p.rooms.push(Object.assign(M.newRoom(rect(4096, 512, 6144, 1536)), third));
  p.things.push({ type: 1, x: 1024, y: 1024, ang: 0 }); // facing +x
  return p;
}
const mapOf = (p) => F.parseMap(M.exportMap(p, true));
function setup(p) {
  const map = mapOf(p), w = V.Walker(map), says = [];
  const t = V.Triggers(map, { say: (s) => says.push(s) });
  t.start();
  return { map, w, t, says };
}
function walk(env, frames, fwd, opts) {
  for (let i = 0; i < frames; i++) {
    const was = env.w.st.sector;
    env.t.tick(16); env.w.step(fwd === undefined ? 1 : fwd, 0, false, opts && opts.jump && i === 0, 1);
    if (env.w.st.sector !== was) env.t.moved(was, env.w.st.sector);
  }
  return env.w.st;
}
// use, like the viewer does (walls and the sector behind them, or the sector it stands in)
function use(env) {
  const ray = env.w.useRay(), map = env.map;
  if (ray.wall >= 0) {
    const n = map.walls[ray.wall].nextsector;
    if (n >= 0 && map.sectors[n].ext && map.sectors[n].ext.Wallpush) return env.t.trigger({ kind: 'sector', i: n }, 40);
  }
  const s = env.w.st.sector;
  if (s >= 0 && map.sectors[s].ext && map.sectors[s].ext.Push) return env.t.trigger({ kind: 'sector', i: s }, 40);
  return false;
}
let e = setup(world({}));
walk(e, 120);
expect(e.w.st.sector === 1 && e.w.st.x > 2048, 'walks through an opening (x=' + Math.round(e.w.st.x) + ')');
e = setup(world({ floor: 128 })); walk(e, 120);
expect(e.w.st.sector === 1 && Math.abs(e.w.st.z - 128) < 1, 'climbs a small step');
e = setup(world({ floor: 600 })); walk(e, 120);
expect(e.w.st.sector === 0 && e.w.st.x < 2048 - 100, 'stopped by a high step');
e = setup(world({ height: 400 })); walk(e, 120);
expect(e.w.st.sector === 0, 'stopped by a low opening');
e = setup(world(null)); walk(e, 120);
expect(e.w.st.sector === 0 && e.w.st.x <= 2048 - V.PLAYER.radius + 1, 'stopped by a wall');
e = setup(world({ floor: -800 })); walk(e, 200);
expect(e.w.st.sector === 1 && Math.abs(e.w.st.z + 800) < 1, 'falls into a lower room');
e = setup(world({ floor: 340 })); walk(e, 40); walk(e, 60, 1, { jump: true });
expect(e.w.st.sector === 1, 'jumps up a 340 step');

// a door: shut it stops the player; used, it opens, lets them through and shuts again
e = setup(world({ kind: 'door', wait: 10, speed: 5 }, {}));
walk(e, 90);
expect(e.w.st.sector === 0, 'a shut door stops the player');
expect(use(e), 'use reaches the door');
walk(e, 40, 0);
walk(e, 200);
expect(e.w.st.sector === 2, 'walked through the opened door (' + e.w.st.sector + ')');
walk(e, 200, 0);
expect(e.map.sectors[1].ceilingz === e.map.sectors[1].floorz, 'the door shut again after its wait');

// a locked door
e = setup(world({ kind: 'door', key: 1 }, {}));
walk(e, 90); use(e); walk(e, 40, 0);
expect(e.says.some((s) => /skull/.test(s)) && e.map.sectors[1].ceilingz === e.map.sectors[1].floorz, 'a locked door says which key it needs');
e.t.keys.add(1); use(e); walk(e, 60, 0);
expect(e.map.sectors[1].ceilingz < e.map.sectors[1].floorz, 'with the key it opens');

// a lift carries the player up to the room above
e = setup(world({ kind: 'lift', rise: 600, wait: 0 }, { floor: 600 }));
walk(e, 60);
expect(e.w.st.sector === 1, 'walked onto the lift');
walk(e, 150);
expect(e.w.st.sector === 1, 'the upper room is out of reach before the lift goes up');
expect(use(e), 'use on the lift');
walk(e, 150, 0);
expect(Math.abs(e.w.st.z - 600) < 1, 'the lift carried the player up (z=' + Math.round(e.w.st.z) + ')');
walk(e, 150);
expect(e.w.st.sector === 2, 'stepped off the lift into the upper room');

// a secret room says so, once
e = setup(world({ secret: true }));
walk(e, 150);
expect(e.says.filter((s) => /secret/.test(s)).length === 1 && /1 of 1/.test(e.says[0]), 'a secret room counts (' + e.says + ')');

// a switch (sprite with Push) opens a door elsewhere through a channel
e = setup(world({ kind: 'door', wait: 0 }, {}));
const door = e.map.sectors[1];
door.ext.Wallpush = 0; door.ext.rxID = 100;
e.map.sprites.push({ ...e.map.sprites[0], type: 20, picnum: 1072, x: 1500, y: 1024, ext: { state: 0, txID: 100, command: 3, triggerOn: 1, triggerOff: 1, Push: 1 } });
const t2 = V.Triggers(e.map, { say: (s) => e.says.push(s) });
t2.trigger({ kind: 'sprite', i: e.map.sprites.length - 1 }, 30);
for (let i = 0; i < 100; i++) t2.tick(16);
expect(door.ceilingz < door.floorz, 'a switch opened the door through channel 100');
t2.trigger({ kind: 'sprite', i: e.map.sprites.length - 1 }, 30);
for (let i = 0; i < 100; i++) t2.tick(16);
expect(door.ceilingz === door.floorz, 'and shut it again');

const mesh = V.buildMesh(mapOf(world({ floor: 128 })));
expect(mesh.count > 0 && mesh.count % 3 === 0 && mesh.batches.every((b) => b.pos.length === b.count * 3 && b.uv.length === b.count * 2), 'mesh has whole triangles');
console.log(failed ? failed + ' walk checks failed' : 'walk checks passed');
process.exit(failed ? 1 : 0);
