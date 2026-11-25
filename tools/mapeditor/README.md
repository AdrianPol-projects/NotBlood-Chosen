# Blood Map Maker

A map editor for NotBlood Chosen that runs in a web browser, on a phone too. Draw rooms (they
join where they touch), set floor and ceiling heights, textures, light and sky, place player
starts, weapons, ammo, health, armor, power-ups and keys, and save a Blood `.MAP` the game loads.
**3D / Walk** shows the map from above (ceilings left out) or lets you walk through it from the single
player start, with steps, jumps, openings and falls (player sizes close to Blood's, not exact), to
try a map without starting the game.

- **Use it:** open `BloodMapMaker.html` from the `mapeditor-v*` release (download it, then open it
  in Chrome, Firefox or Safari), or `http://<your NotBlood server>/mapmaker`.
- **Play the map:** put the `.MAP` in the game folder (Android: `NotBlood` in the main storage) and
  pick it under `< USER MAP >` or MULTIPLAYER > USER MAP. Online, the other players get it from the
  server by themselves.

Files: `mapcore.js` (map model, checks, the `.MAP` writer, the same format the game saves),
`index.html` (the touch interface), `view3d.js` (3D view and walkthrough, WebGL), `build.py` (makes the one-file `BloodMapMaker.html` and the
relay's copy), `test_mapcore.js` + `check_structure.py` + `test_walk.js` (tests: `node test_mapcore.js /tmp/out &&
python3 check_structure.py /tmp/out/*.MAP && node test_walk.js`).

Not there yet: sloped floors, doors, lifts and switches, water, enemies, several outlines per room
(pillars), editing existing Blood maps, tile pictures (textures are tile numbers; the 3D view gives each tile number its own colour).

For big projects on Windows there's [XMAPEDIT](https://github.com/NoOneBlood/xmapedit), the full Blood editor
(keyboard and mouse); maps from either editor work in NotBlood Chosen.
