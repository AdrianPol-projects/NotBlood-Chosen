// node test_mapgeo.js [BLOOD.MAP...]: editing operations keep the map whole
const fs = require('fs');
const F = require('./mapfile.js'), G = require('./mapgeo.js');
let failed = 0;
const expect = (c, what) => { if (!c) { failed++; console.log('FAIL: ' + what); } };
const rect = (x1, y1, x2, y2) => [[x1, y1], [x2, y1], [x2, y2], [x1, y2]];
const joined = (m) => m.walls.filter((w) => w.nextwall >= 0).length;
const roundTrip = (m) => { F.finish(m); const b = F.writeMap(m); const m2 = F.parseMap(b); return m2; };

let m = G.emptyMap();
G.addSector(m, rect(0, 0, 4096, 4096));
expect(m.sectors.length === 1 && m.walls.length === 4 && G.problems(m).length === 0, 'first sector');
G.addSector(m, rect(4096, 1024, 6144, 2048));        // against part of the first one's wall: a T-junction
expect(joined(m) === 2 && G.problems(m).length === 0, 'next to it, joined (' + joined(m) + ' joined walls)');
G.addSector(m, rect(1024, 1024, 2048, 2048));        // inside the first: a sector within a sector
expect(m.sectors.length === 3 && joined(m) === 10 && G.problems(m).length === 0, 'a sector inside a sector (' + joined(m) + ' joined walls)');
expect(G.loopsOf(m, 0).length === 2, 'the outer sector got an inner loop');
expect(G.sectorAt(m, 1500, 1500) === 2 && G.sectorAt(m, 500, 500) === 0 && G.sectorAt(m, 5000, 1500) === 1, 'finding sectors');
let threw = ''; try { G.addSector(m, rect(1500, 1500, 3000, 3000)); } catch (e) { threw = e.message; }
expect(/crosses|overlaps/.test(threw), 'an overlapping sector is refused (' + threw + ')');
threw = ''; try { G.addSector(m, rect(-1000, -1000, 8000, 8000)); } catch (e) { threw = e.message; }
expect(threw, 'a sector around others is refused (' + threw + ')');
expect(G.triangulateSector(m, 0).length >= 8, 'a sector with a hole makes triangles (' + G.triangulateSector(m, 0).length + ')');
const m2 = roundTrip(m);
expect(m2.walls.length === m.walls.length && G.problems(m2).length === 0, 'written and read back');
G.splitWall(m, m.walls.findIndex((w) => w.nextwall >= 0));
expect(G.problems(m).length === 0 && joined(m) === 12, 'splitting a joined wall keeps both sides joined (' + joined(m) + ')');
G.deleteSector(m, 2);
expect(m.sectors.length === 2 && G.problems(m).length === 0 && G.loopsOf(m, 0).length === 2, 'deleting the inner sector leaves a pillar');
m.sprites.push({ x: 500, y: 500, z: 0, cstat: 0, picnum: 2522, shade: 0, pal: 0, clipdist: 32, blend: 0, xrepeat: 64, yrepeat: 64, xoffset: 0, yoffset: 0, sectnum: 1, statnum: 0, ang: 0, owner: -1, xvel: 0, yvel: 0, zvel: 0, type: 1, flags: 0, extra: -1 });
G.deleteSector(m, 0);
expect(m.sectors.length === 1 && m.sprites.length === 1 && m.sprites[0].sectnum === 0 && G.problems(m).length === 0, 'deleting renumbers the others');

// real maps: the graph round trip changes nothing, and edits keep them whole
for (const path of process.argv.slice(2)) {
  const data = new Uint8Array(fs.readFileSync(path));
  const map = F.parseMap(data);
  G.fromGraph(G.toGraph(map));
  const out = F.writeMap(map);
  expect(out.length === data.length && out.every((b, i) => b === data[i]), path + ': unchanged through the edit graph');
  expect(G.problems(map).length === 0, path + ': no problems found in an official map (' + G.problems(map).slice(0, 2) + ')');
  let tris = 0;
  for (let s = 0; s < map.sectors.length; s++) tris += G.triangulateSector(map, s).length;
  expect(tris > map.sectors.length, path + ': triangles for every sector');
  G.splitWall(map, map.walls.findIndex((w) => w.nextwall >= 0));
  G.deleteSector(map, map.sectors.length - 1);
  expect(G.problems(map).length === 0, path + ': still whole after a split and a delete');
}
// sliding doors keep their marker sprites when other sprites and sectors are deleted
for (const path of process.argv.slice(2)) {
  const map = F.parseMap(new Uint8Array(fs.readFileSync(path)));
  const markers = () => map.sectors.map((s) => s.ext && s.ext.marker0 !== 65535 && [616, 614].includes(s.type) ? map.sprites[s.ext.marker0] : null);
  const before = markers().map((sp) => sp && sp.x + ',' + sp.y + ',' + sp.type);
  if (!before.some(Boolean)) continue;
  // markers whose owner isn't the sector that uses them (a few official maps have some)
  const oddOwners = (m) => m.sprites.filter((sp, i) => sp.type >= 3 && sp.type <= 5 && sp.owner >= 0 && !(m.sectors[sp.owner] && m.sectors[sp.owner].ext && (m.sectors[sp.owner].ext.marker0 === i || m.sectors[sp.owner].ext.marker1 === i))).length;
  const odd0 = oddOwners(map);
  const w0 = []; G.problems(map, w0);
  // delete the first sprite that isn't a marker, and a sector without markers in it
  G.deleteSprite(map, map.sprites.findIndex((sp) => sp.type < 3 || sp.type > 8));
  const victim = map.sectors.findIndex((s, i) => !(s.ext && s.ext.marker0 !== 65535) && !map.sprites.some((sp) => sp.sectnum === i && sp.type >= 3 && sp.type <= 8) && !map.sectors.some((o) => o.ext && map.sprites[o.ext.marker0] && map.sprites[o.ext.marker0].sectnum === i));
  const nb = before.slice(0, victim).concat(before.slice(victim + 1));
  G.deleteSector(map, victim);
  const after = markers().map((sp) => sp && sp.x + ',' + sp.y + ',' + sp.type);
  expect(JSON.stringify(after) === JSON.stringify(nb), path + ': sliding doors keep their markers after deletions');
  expect(oddOwners(map) === odd0, path + ': markers still name their sectors (' + odd0 + ' odd in the original, ' + oddOwners(map) + ' now)');
  const w1 = []; G.problems(map, w1);
  expect(w1.filter((t) => /marker/.test(t)).length === w0.filter((t) => /marker/.test(t)).length, path + ': no markers lost');
}
console.log(failed ? failed + ' geometry checks failed' : 'geometry checks passed');
process.exit(failed ? 1 : 0);
