// node test_walk.js: the walkthrough's rules (openings, steps, walls) on small maps
global.window = global;
global.MapCore = require('./mapcore.js');
require('./view3d.js');
const M = global.MapCore, V = global.View3D;
let failed = 0;
const expect = (c, what) => { if (!c) { failed++; console.log('FAIL: ' + what); } };
const rect = (x1, y1, x2, y2) => M.cleanPoly([[x1, y1], [x2, y1], [x2, y2], [x1, y2]]);
function world(second) {
  const p = M.newProject();
  p.rooms.push(M.newRoom(rect(0, 0, 2048, 2048)));
  if (second) p.rooms.push(Object.assign(M.newRoom(rect(2048, 512, 4096, 1536)), second));
  p.things.push({ type: 1, x: 1024, y: 1024, ang: 0 }); // facing +x, towards room 2
  return p;
}
function walk(p, frames) {
  const w = V.Walker(p, M.buildMap(p, false));
  for (let i = 0; i < frames; i++) w.step(1, 0, false, false, 1);
  return w.st;
}
let st = walk(world({}), 120);
expect(st.sector === 1 && st.x > 2048, 'walks through an opening into the next room (x=' + Math.round(st.x) + ')');
st = walk(world({ floor: 128 }), 120);
expect(st.sector === 1 && st.z === 128, 'climbs a small step');
st = walk(world({ floor: 600 }), 120);
expect(st.sector === 0 && st.x < 2048 - 100, 'stopped by a high step (x=' + Math.round(st.x) + ')');
st = walk(world({ height: 400 }), 120);
expect(st.sector === 0, 'stopped by a low opening');
st = walk(world(null), 120);
expect(st.sector === 0 && st.x <= 2048 - V.PLAYER.radius + 1, 'stopped by a wall (x=' + Math.round(st.x) + ')');
st = walk(world({ floor: -800 }), 200);
expect(st.sector === 1 && st.z === -800, 'falls down into a lower room');
// a jump gets up a step that's too high to walk
const p = world({ floor: 340 });
const w = V.Walker(p, M.buildMap(p, false));
for (let i = 0; i < 40; i++) w.step(1, 0, false, false, 1);
for (let i = 0; i < 60; i++) w.step(1, 0, false, i === 0, 1);
expect(w.st.sector === 1, 'jumps up a 340 step');
const tris = V.triangulate(M.cleanPoly([[0, 0], [2048, 0], [2048, 2048], [1024, 2048], [1024, 1024], [0, 1024]]));
expect(tris.length === 4, 'L-shaped room makes 4 triangles (' + tris.length + ')');
const mesh = V.buildMesh(world({ floor: 128 }));
expect(mesh.pos.length > 0 && mesh.pos.length % 9 === 0, 'mesh has whole triangles');
console.log(failed ? failed + ' walk checks failed' : 'walk checks passed');
process.exit(failed ? 1 : 0);
