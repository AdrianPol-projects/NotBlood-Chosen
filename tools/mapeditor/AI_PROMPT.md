# Prompt: a Blood map as a Blood Map Maker project

Copy everything below the line into Claude (or another AI), then write what you want at the end.
Save the answer as a `.json` file and open it in Blood Map Maker (☰ → *Open project (.json)*).
Look at it in **3D / Walk**, fix what you don't like, then ☰ → *Save .MAP for the game*.

---

You design levels for **Blood** (the 1997 FPS, played in NotBlood Chosen). You write the level as a
JSON project file for the "Blood Map Maker" editor. Answer with **only the JSON** in one code block:
no comments inside it, no text after it. Before the JSON, write a short plan (5–15 lines): the rooms,
how they connect, where the player starts, where the doors, keys, lifts and secrets are.

## The file

```
{
  "version": 2,
  "name": "CRYPT1",            // the file name: A–Z, 0–9, _ or -, at most 8 characters
  "visibility": 800,           // darkness with distance: 0 none, 400 a little, 800 like Blood, 1600 a lot
  "skyPic": 3678,              // the sky's tile (3678 or 2500)
  "rooms": [ ROOM, ... ],
  "things": [ THING, ... ]
}
```

**ROOM** (a floor area with walls around it; every field is required):
```
{
  "pts": [[x, y], [x, y], ...],  // the outline's corners in order, at least 3, a simple shape (it must not cross itself)
  "floor": 0,                  // floor height; UP is MORE (a step up of 64 is floor 64)
  "height": 2048,              // floor to ceiling (for a door: its height when open)
  "floorPic": 270, "ceilPic": 568, "wallPic": 449,   // tile numbers, see TEXTURES
  "sky": false,                // true: open sky instead of a ceiling (outdoors)
  "light": 0,                  // -8 bright, 0 normal, 8 dim, 16 dark, 24 very dark
  "kind": "room",              // "room", "door" or "lift"
  "secret": false,             // true: entering it counts as a secret found (only for kind "room")
  "doorPic": 302,              // door: the texture on its faces
  "speed": 10,                 // door/lift: tenths of a second to open or rise (5, 10, 20 or 40)
  "wait": 30,                  // door/lift: tenths of a second before it goes back; 0 = stays
  "key": 0,                    // door: 0 none, 1 skull, 2 eye, 3 fire, 4 dagger, 5 spider, 6 moon
  "rise": 512                  // lift: how far its floor goes up
}
```

**THING** (one object): `{ "type": 41, "x": 1024, "y": 2048, "ang": 0 }`
`ang` is the direction it faces: 0 east (+x), 512 south (+y), 1024 west, 1536 north. It matters for
starts and enemies.

## Coordinates and sizes

- The map is a flat grid seen from above: **x grows to the east (right), y grows to the south (down)**.
  Use numbers between −30000 and 30000. Put **every room corner on a multiple of 128**, better 256.
- Blood's sizes, in map units: the player is about 720 tall and 256 wide. A corridor is 512–768 wide.
  A normal room is 1536–4096 across; a big hall up to 8192. Room height 1536–2560; halls up to 4096.
- **Steps:** the player walks up a floor difference of up to about 256. Up to about 400 needs a jump;
  more than that needs a lift or stairs. Stairs are a row of small rooms (each 256–384 deep), each one
  128–192 higher than the one before. Walking **down** any height is fine.
- A ceiling has to be at least 800 above the floor where the player walks.

## How rooms connect (the important part)

- Two rooms connect where their outlines **share an edge, along the same line**: you can walk across
  that shared part. The shared part can be only part of an edge (a narrow room against a long wall),
  as long as both lie on exactly the same line, for example both have x = 4096 there.
- Where rooms don't touch, there is wall. There is no other way to make an opening.
- **Rooms must never overlap**, not even a little. One room inside another doesn't work either: make
  a pillar or a pit by surrounding it with several rooms instead.
- Check every connection: write down the shared line (for example "x = 4096, from y 1536 to 2560") and
  make sure both rooms have an edge exactly there.
- Every room has to be reachable from the start. Rooms that only connect diagonally (corner to corner)
  are **not** connected.

## Doors, lifts, secrets, keys

- A **door** is a thin room (kind "door", 128–256 thick, 512–1024 wide) placed **between two rooms**:
  one long side shares an edge with room A, the other with room B. It starts shut; the player opens
  it by using it. Give it `"height"` (open height, at most the lower ceiling beside it) and `"doorPic"`.
  The door's floor is the same as the rooms beside it.
- A **locked door** has `"key"` 1–6. Put the matching key (type 100–105) somewhere the player can get
  to **before** that door, never behind it.
- A **lift** (kind "lift") is a small room (512–1024 across) whose floor rises by `"rise"` when the
  player uses it. Its `"floor"` is the LOW position, at the same height as the room it's reached from.
  Next to it, the upper room's floor is at lift floor + rise. Its `"height"` must be bigger than
  `rise` + 800.
- A **secret** is a normal room with `"secret": true`, usually small, behind a door or a lift, or
  reached by a jump, with a reward inside (armor, a power-up, ammo).
- Don't put things inside a door room.

## Things

**Starts (needed):**
- type **1** single player start: exactly one is needed (the game starts there). For co-op, add one
  per player (up to 8); the first is player 1.
- type **2** bloodbath start: add 8, spread over the map, facing into the room.

**Weapons:** 43 flare gun, 41 sawed-off shotgun, 42 tommy gun, 46 napalm launcher, 49 TNT,
48 spray can, 45 tesla cannon, 50 life leech, 44 voodoo doll.
**Ammo:** 76 flares, 67 shells (few), 68 shells (box), 69 bullets (few), 72 tommy drum,
79 gasoline, 62 TNT bundle, 63 TNT box, 64 proximity bombs, 65 remote bombs, 60 spray can,
73 tesla charge, 66 trapped soul.
**Health:** 109 life essence (small), 110 life seed (big), 107 doctor's bag, 108 medicine pouch.
**Armor:** 140 basic, 141 body, 142 fire, 143 spirit, 144 super.
**Power-ups:** 117 guns akimbo, 113 invisibility, 114 death mask, 124 reflective shots,
115 jump boots, 125 beast vision, 118 diving suit, 121 crystal ball.
**Keys:** 100 skull, 101 eye, 102 fire, 103 dagger, 104 spider, 105 moon.
**Enemies:** 201 cultist (tommy gun), 202 cultist (shotgun), 248 cultist (TNT), 247 cultist (tesla),
203 axe zombie, 205 axe zombie in the ground, 204 butcher, 206 flesh gargoyle, 207 stone gargoyle,
210 phantasm, 211 hell hound, 212 hand, 213 brown spider, 214 red spider, 215 black spider,
216 mother spider, 217 gill beast (needs water: avoid), 218 bone eel (needs water: avoid), 219 bat,
220 rat, 221 green pod, 223 fire pod, 227 cerberus (boss), 245 innocent.

- Put every thing **inside** a room, at least 128 away from its walls, never in a door room.
- Balance like Blood: the first rooms have weak enemies (cultists, zombies, rats, bats) and the flare
  gun or shotgun nearby; tougher enemies (gargoyles, hounds, butchers, phantasms) come later with
  better weapons. Put ammo for the weapons you give, health after hard fights, armor in secrets.
  Enemies face the way the player comes from. Don't put enemies within 1536 of the start.

## Textures (tile numbers from Blood)

- Walls: 449, 556, 458, 568, 273, 448, 80, 20, 255
- Floors: 270, 287, 253, 448, 568, 255, 438, 274
- Ceilings: 568, 449, 255, 448, 556
- Doors (doorPic): 5, 302, 2453, 495, 195, 22
- Sky (skyPic): 3678, 2500

Use the same few tiles for one area so it looks planned; change them between areas (a crypt, a
chapel, a yard). Outdoor areas use `"sky": true` and are usually bigger and lighter.

## Before you answer, check

1. Every room: at least 3 corners, corners on multiples of 128, doesn't cross itself.
2. No two rooms overlap (compare their x and y ranges; for rooms that touch, the touching side is on
   exactly the same line).
3. Every room connects to the start through shared edges, doors and lifts; no floor step on the way is
   more than 256 up without a lift or stairs.
4. Every door has a room on both sides; every key is reachable before its door.
5. One type 1 start, eight type 2 starts, every thing inside a room and not in a door.
6. All fields are there, numbers are plain whole numbers, the JSON is valid (no comments, no
   trailing commas).

## Example (a small, complete level)

A start room, a door (skull key) into a hall, a lift up to a balcony, a small secret room.

```json
{
  "version": 2, "name": "EXAMPLE", "visibility": 800, "skyPic": 3678,
  "rooms": [
    {"pts": [[0,0],[3072,0],[3072,3072],[0,3072]], "floor": 0, "height": 2048, "floorPic": 270, "ceilPic": 568, "wallPic": 449, "sky": false, "light": 0, "kind": "room", "secret": false, "doorPic": 302, "speed": 10, "wait": 30, "key": 0, "rise": 512},
    {"pts": [[3072,1024],[3328,1024],[3328,2048],[3072,2048]], "floor": 0, "height": 1536, "floorPic": 270, "ceilPic": 568, "wallPic": 449, "sky": false, "light": 0, "kind": "door", "secret": false, "doorPic": 302, "speed": 10, "wait": 30, "key": 1, "rise": 512},
    {"pts": [[3328,-1024],[7424,-1024],[7424,4096],[3328,4096]], "floor": 0, "height": 3072, "floorPic": 287, "ceilPic": 556, "wallPic": 556, "sky": false, "light": 8, "kind": "room", "secret": false, "doorPic": 302, "speed": 10, "wait": 30, "key": 0, "rise": 512},
    {"pts": [[7424,1536],[8192,1536],[8192,2304],[7424,2304]], "floor": 0, "height": 2048, "floorPic": 448, "ceilPic": 448, "wallPic": 448, "sky": false, "light": 0, "kind": "lift", "secret": false, "doorPic": 302, "speed": 20, "wait": 0, "key": 0, "rise": 768},
    {"pts": [[8192,0],[10240,0],[10240,4096],[8192,4096]], "floor": 768, "height": 2048, "floorPic": 253, "ceilPic": 568, "wallPic": 458, "sky": true, "light": -8, "kind": "room", "secret": false, "doorPic": 302, "speed": 10, "wait": 30, "key": 0, "rise": 512},
    {"pts": [[10240,1536],[11264,1536],[11264,2560],[10240,2560]], "floor": 768, "height": 1536, "floorPic": 255, "ceilPic": 255, "wallPic": 255, "sky": false, "light": 16, "kind": "room", "secret": true, "doorPic": 302, "speed": 10, "wait": 30, "key": 0, "rise": 512}
  ],
  "things": [
    {"type": 1, "x": 512, "y": 1536, "ang": 0},
    {"type": 2, "x": 512, "y": 512, "ang": 256}, {"type": 2, "x": 2560, "y": 2560, "ang": 1280},
    {"type": 2, "x": 4096, "y": -512, "ang": 512}, {"type": 2, "x": 6656, "y": 3584, "ang": 1536},
    {"type": 2, "x": 5376, "y": 1536, "ang": 0}, {"type": 2, "x": 9728, "y": 512, "ang": 1024},
    {"type": 2, "x": 9728, "y": 3584, "ang": 1024}, {"type": 2, "x": 8704, "y": 2048, "ang": 0},
    {"type": 43, "x": 1536, "y": 1536, "ang": 0}, {"type": 76, "x": 1536, "y": 2048, "ang": 0},
    {"type": 100, "x": 2560, "y": 512, "ang": 0},
    {"type": 203, "x": 2560, "y": 2560, "ang": 1024},
    {"type": 41, "x": 4096, "y": 1536, "ang": 0}, {"type": 68, "x": 4096, "y": 2560, "ang": 0},
    {"type": 202, "x": 6400, "y": 512, "ang": 1024}, {"type": 201, "x": 6400, "y": 3072, "ang": 1024},
    {"type": 109, "x": 5376, "y": 3584, "ang": 0},
    {"type": 211, "x": 9472, "y": 2048, "ang": 1024}, {"type": 110, "x": 9984, "y": 3584, "ang": 0},
    {"type": 141, "x": 10752, "y": 2048, "ang": 0}
  ]
}
```

How the example connects: the start room and the door share x = 3072 (y 1024–2048); the door and
the hall share x = 3328 (same y range); the hall and the lift share x = 7424 (y 1536–2304); the lift
and the yard share x = 8192 (the yard's floor, 768, is the lift's floor + rise); the yard and the secret
share x = 10240 (y 1536–2560). The skull key is in the start room, before its door.

## The level I want

(Write here what you want: size, theme, number of rooms, enemies, difficulty, secrets, bloodbath or
single player, anything else.)
