# Blood Map Maker

A map editor for NotBlood Chosen that runs in a web browser, on a phone too. Draw rooms (they
join where they touch), set floor and ceiling heights, textures, light and sky, place player
starts, weapons, ammo, health, armor, power-ups and keys, and save a Blood `.MAP` the game loads.
Rooms can be **doors** (thin rooms between rooms that open upwards when used, optionally needing a
key), **lifts** (the floor rises when used) and **secrets** (walking in counts as a secret found);
**enemies** (cultists, zombies, gargoyles, hounds, spiders, ...) can be placed like items. These are
written the way Blood's own maps do them (Z motion sectors, secret channel 2, the secret counter on
channel 7 → 1).

**3D / Walk** shows the map from above (ceilings left out) or lets you walk through it from the single
player start: steps, jumps, falls, doors and lifts (Use), keys, secrets (player sizes close to Blood's,
not exact; enemies stand still), to try a map without starting the game.

**Real textures:** ☰ → *Blood files for textures*, then pick `TILES000.ART`–`TILES017.ART` and
`BLOOD.RFF` from your own game folder (they stay in the browser). The tile picker then shows pictures,
and the 3D view uses Blood's tiles, palette and shading table, sprites and the parallax sky.

- **Use it:** open `BloodMapMaker.html` from the `mapeditor-v*` release (download it, then open it
  in Chrome, Firefox or Safari), or `http://<your NotBlood server>/mapmaker`.
- **Play the map:** put the `.MAP` in the game folder (Android: `NotBlood` in the main storage) and
  pick it under `< USER MAP >` or MULTIPLAYER > USER MAP. Online, the other players get it from the
  server by themselves.

Files: `mapcore.js` (map model, checks, the `.MAP` writer, the same format the game saves),
`index.html` (the touch interface), `view3d.js` (3D view and walkthrough, WebGL), `gamefiles.js` (reads ART and RFF files), `build.py` (makes the one-file `BloodMapMaker.html` and the
relay's copy), `test_mapcore.js` + `check_structure.py` + `test_walk.js` (tests: `node test_mapcore.js /tmp/out &&
python3 check_structure.py /tmp/out/*.MAP && node test_walk.js && node test_gamefiles.js`).

Not there yet: sloped floors, switches and other triggers, sliding or swinging doors, water, sounds,
level exits, several outlines per room (pillars), editing existing Blood maps, enemies that move in
the walkthrough.

For big projects on Windows there's [XMAPEDIT](https://github.com/NoOneBlood/xmapedit), the full Blood editor
(keyboard and mouse); maps from either editor work in NotBlood Chosen.
