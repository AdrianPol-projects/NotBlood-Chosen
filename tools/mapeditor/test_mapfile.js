// node test_mapfile.js FILE.MAP...: every map read and written again must give the same bytes
const fs = require('fs');
const F = require('./mapfile.js');
let bad = 0, n = 0;
for (const path of process.argv.slice(2)) {
  const data = new Uint8Array(fs.readFileSync(path));
  let out;
  try {
    const map = F.parseMap(data);
    out = F.writeMap(JSON.parse(JSON.stringify(map))); // through JSON too: the editor keeps maps that way
    n++;
    if (!map.crcOk) console.log(path + ': CRC was already wrong in the file');
  } catch (e) { bad++; console.log(path + ': ' + e.message); continue; }
  if (out.length !== data.length || out.some((b, i) => b !== data[i])) {
    bad++;
    let i = 0; while (i < data.length && out[i] === data[i]) i++;
    console.log(path + ': differs at byte ' + i + ' (' + out.length + ' vs ' + data.length + ' bytes)');
  }
}
console.log(bad ? bad + ' of ' + (n + bad) + ' maps failed' : 'all ' + n + ' maps came back byte for byte');
process.exit(bad ? 1 : 0);
