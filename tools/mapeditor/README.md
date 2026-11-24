# Blood Map Maker

A map editor for NotBlood Chosen that runs in a web browser, on a phone too. Draw rooms (they
join where they touch), set floor and ceiling heights, textures, light and sky, place player
starts, weapons, ammo, health, armor, power-ups and keys, and save a Blood `.MAP` the game loads.

- **Use it:** open `BloodMapMaker.html` from the `mapeditor-v*` release (download it, then open it
  in Chrome, Firefox or Safari), or `http://<your NotBlood server>/mapmaker`.
- **Play the map:** put the `.MAP` in the game folder (Android: `NotBlood` in the main storage) and
  pick it under `< USER MAP >` or MULTIPLAYER > USER MAP. Online, the other players get it from the
  server by themselves.

Files: `mapcore.js` (map model, checks, the `.MAP` writer, the same format the game saves),
`index.html` (the touch interface), `build.py` (makes the one-file `BloodMapMaker.html` and the
relay's copy), `test_mapcore.js` + `check_structure.py` (tests: `node test_mapcore.js /tmp/out &&
python3 check_structure.py /tmp/out/*.MAP`).

Not there yet: sloped floors, doors, lifts and switches, water, enemies, several outlines per room
(pillars), editing existing Blood maps, tile pictures (textures are tile numbers).
