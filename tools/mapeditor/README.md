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

**Full editor (any Blood map):** ☰ → *Open a .MAP (full editor)* opens any Blood map, Blood's own
included, and saves it back exactly (only what you change changes). Everything can be edited:
sectors (heights, tiles, shade, palette, slopes, panning, flags, type), walls (tile, masked tile, flags,
repeat, panning, type), sprites (Blood's 191 object types by name, tile, position, angle, size, flags),
and the extra data of all three: triggers, channels (tx/rx) and commands, timing, keys, door and lift
heights, light, panning, wind, water, enemy flags and each type's data fields with their meaning.
*Links* lists what an object sends to and hears from (tap to go there) and draws them as arrows.
Draw new sectors (they join walls they touch; inside another sector they become a sector within it),
split walls, move and remove corners, place, move, copy and delete sprites, undo. *3D / Walk* shows the
map with slopes, holes, masked walls and wall/floor sprites, and the walkthrough runs Blood's triggers:
using walls, sectors and switches, entering and leaving sectors, picking things up, keys, channels,
Z motion doors and lifts with their timing, secrets, messages and level exits. *This map in the full
editor* takes a simple project there. Names of the object types are from XMAPEDIT's OBJTYPES.INI
(blooddata.js, made by mkblooddata.py).

**Maps made by an AI:** [AI_PROMPT.md](AI_PROMPT.md) is a prompt for Claude (or another AI) that writes
a level as a project file; open it with ☰ → *Open project (.json)*, check it in 3D / Walk, fix and save.

Files: `mapfile.js` (any .MAP read and written without losses), `mapgeo.js` (geometry and edits), `fulleditor.js` (the full editor), `blooddata.js` (object names), `mapcore.js` (map model, checks, the `.MAP` writer, the same format the game saves),
`index.html` (the touch interface), `view3d.js` (3D view and walkthrough, WebGL), `gamefiles.js` (reads ART and RFF files), `build.py` (makes the one-file `BloodMapMaker.html` and the
relay's copy), `test_mapcore.js` + `check_structure.py` + `test_walk.js` (tests: `node test_mapcore.js /tmp/out &&
python3 check_structure.py /tmp/out/*.MAP && node test_walk.js && node test_gamefiles.js`).

Not there yet: in the walkthrough, enemies, sliding/rotating/path sectors, teleporters, water and
damage only change state or do nothing; editing in the 3D view; texture alignment tools; copying
several objects at once; joining or splitting existing sectors (other than drawing new ones).

For big projects on Windows there's [XMAPEDIT](https://github.com/NoOneBlood/xmapedit), the full Blood editor
(keyboard and mouse); maps from either editor work in NotBlood Chosen.
