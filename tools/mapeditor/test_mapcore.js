// node test_mapcore.js OUTDIR: writes test maps (check them with server/mapcheck.py and check_structure.py)
const fs = require('fs');
const path = require('path');
const M = require('./mapcore.js');
const out = process.argv[2] || '.';
let failed = 0;
function expect(cond, what) { if (!cond) { failed++; console.log('FAIL: ' + what); } }

function rect(x1, y1, x2, y2) { return M.cleanPoly([[x1, y1], [x2, y1], [x2, y2], [x1, y2]]); }
function add(prj, pts, extra) {
  const why = M.roomProblem(pts, prj.rooms, -1);
  if (why) throw new Error(why);
  const r = Object.assign(M.newRoom(pts), extra || {});
  prj.rooms.push(r);
  return r;
}
function starts(prj, pts) {
  prj.things.push({ type: 1, x: pts[0][0], y: pts[0][1], ang: 0 });
  pts.forEach((p, i) => prj.things.push({ type: 2, x: p[0], y: p[1], ang: (i * 256) & 2047 }));
}

// 1: one room
let p = M.newProject(); p.name = 'ONE';
add(p, rect(0, 0, 4096, 4096));
starts(p, [[1024, 1024], [3072, 3072]]);
p.things.push({ type: 41, x: 2048, y: 2048 });
fs.writeFileSync(path.join(out, 'ONE.MAP'), M.exportMap(p));

// 2: rooms sharing parts of edges (T-junctions), a raised one, a sky one, an L-shape
p = M.newProject(); p.name = 'JOINED';
add(p, rect(0, 0, 4096, 4096));
add(p, rect(4096, 1024, 6144, 2048), { floor: 32, height: 1536 });
add(p, rect(1024, -2048, 2048, 0), { sky: true });
add(p, M.cleanPoly([[4096, 2048], [6144, 2048], [6144, 6144], [5120, 6144], [5120, 3072], [4096, 3072]]), { floor: -64 });
starts(p, [[512, 512], [3500, 3500], [5000, 1500], [1500, -1000], [5600, 5600]]);
for (const t of M.THINGS) if (!t.start) p.things.push({ type: t.type, x: 200 + (t.type * 37) % 3600, y: 200 + (t.type * 53) % 3600 });
fs.writeFileSync(path.join(out, 'JOINED.MAP'), M.exportMap(p));

// 3: a door, a lift, a secret room and enemies
p = M.newProject(); p.name = 'FEATURES';
add(p, rect(0, 0, 4096, 4096));
add(p, rect(4096, 1536, 4352, 2560), { kind: 'door', doorPic: 302, key: 1, height: 1536 }); // door into the secret room
add(p, rect(4352, 1024, 6144, 3072), { secret: true });
add(p, rect(1024, -1024, 2048, 0), { kind: 'lift', rise: 768 });
add(p, rect(1024, -3072, 2048, -1024), { floor: 768 });                                  // where the lift goes
starts(p, [[512, 512], [3500, 3500]]);
p.things.push({ type: 100, x: 2000, y: 2000 });
for (const t of M.THINGS) if (t.enemy) p.things.push({ type: t.type, x: 300 + (t.type * 61) % 3400, y: 2600 + (t.type * 29) % 1300 });
expect(!M.checkProject(p).some((x) => x.level === 'error'), 'features map has no errors: ' + JSON.stringify(M.checkProject(p)));
fs.writeFileSync(path.join(out, 'FEATURES.MAP'), M.exportMap(p));
const q = M.newProject(); q.rooms.push(Object.assign(M.newRoom(rect(0, 0, 512, 512)), { kind: 'door', secret: true }));
expect(M.checkProject(q).some((x) => /secret/.test(x.text)), 'a secret door is refused');

// checks that must refuse
p = M.newProject();
add(p, rect(0, 0, 1024, 1024));
expect(M.roomProblem(rect(512, 512, 2048, 2048), p.rooms, -1), 'overlap refused');
expect(M.roomProblem(rect(0, 0, 1024, 1024), p.rooms, -1), 'same room twice refused');
expect(M.roomProblem(rect(256, 256, 512, 512), p.rooms, -1), 'room inside a room refused');
expect(!M.roomProblem(rect(1024, 0, 2048, 512), p.rooms, -1), 'touching room allowed');
expect(M.roomProblem(M.cleanPoly([[0, 0], [1024, 1024], [1024, 0], [0, 1024]]), [], -1), 'bow tie refused');
let threw = false;
try { M.exportMap(M.newProject()); } catch (e) { threw = true; }
expect(threw, 'empty project refused');
p = M.newProject(); add(p, rect(0, 0, 1024, 1024)); p.things.push({ type: 1, x: 5000, y: 5000 });
expect(M.checkProject(p).some((x) => x.level === 'error'), 'start outside rooms refused');
console.log(failed ? failed + ' checks failed' : 'mapcore checks passed');
process.exit(failed ? 1 : 0);
