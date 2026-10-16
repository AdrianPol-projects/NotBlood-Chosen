**Test build of NotBlood Chosen with bots** for Windows, Linux, macOS, iPad and Android. It doesn't replace the normal [latest](https://github.com/AdrianPol-projects/NotBlood-Chosen/releases/tag/latest) downloads. Windows, Linux and macOS: unzip it into its own folder and copy your Blood files there (macOS: run the `xattr` step from the README first). iPad: sideload the IPA like the normal one. Android: the APK installs over NotBlood Chosen and keeps using the `NotBlood` folder; to go back, uninstall it first and install the normal APK (your files in `NotBlood` stay).

### Playing against bots

MULTIPLAYER > **PLAY WITH BOTS**

- **BOTS**: how many (1-7)
- **DIFFICULTY**: easy, normal, hard, nightmare - how well they aim, how fast they react, how far they hear and how often they use tactics
- **PERSONALITY**:
  - *rusher* charges in
  - *camper* waits in good spots
  - *trickster* ambushes you at doorways and sneaks round to come from the other side
  - *coward* backs off and goes for health
  - *jumper* hops around in fights
  - *balanced* a bit of everything
  - *mixed* gives every bot a different one
- **SET EACH BOT**: a different difficulty or personality for single bots
- **FINE TUNING**: aim, reaction time, aggression, jumping, ambushes, flanking, camping and high ground in percent of the difficulty's/personality's own value; taunts; cultist look; how much damage bots take (shown next to their names when not normal); showing what every bot is doing
- **MATCH SETTINGS**: the multiplayer setup - bloodbath or teams, map, frag and time limits, weapons, items, mutators
- **START MATCH**. ESC pauses the match; NEW GAME there sets up the next bot match (bots and match settings); END GAME goes back to normal single player.

The difficulties and personalities include the tuning that played best in testing (slower reactions, more aggression, jumping and ambushes, less camping); the FINE TUNING sliders start again at 100% once.

Before each decision a bot sizes up the situation like a player: its health, armor and weapon against the enemies it knows are close (their weapons, powerups, how hurt they must be from the hits it saw land, whether they have seen it), the score and how close the frag limit is, and whether it just got a kill or just got hurt. Then it weighs fighting, getting away, getting up high, going round, ambushing, chasing, collecting and camping against each other. When two others fight each other and neither has noticed it, it may wait and then finish off whoever is left.

The bots only know what a player could: they see what's in front of them and not behind walls, hear shots, jumps and running, and remember where they last saw you. They take a moment to react, their aim wobbles and settles while they track you, and they pause their shooting now and then. On easy and normal they don't use autoaim. They open doors and sliding walls (from the side the button is on), never press a level exit, and when they're lost they look around for a way out. They ride lifts: they call them, ride them up and down and walk over to a lift's switch when that's what works it, so they climb out of pits too.

On every map they work out the good high places: how much you can see from there, how far you look down on it, and weapons close by. Depending on their personality and the HIGH GROUND setting they go up there, hold it, don't step off ledges, shoot down at you instead of jumping after you, and go for splash weapons (napalm, TNT, flares) when you have the high ground.

Console: `bot_match [frag limit] [time limit]` starts a match with the menu's settings; all settings are `bot_*` variables (type `bot_` and press TAB).

### Match types and mutators (with bots, real players, or both)

MULTIPLAYER match settings (or MATCH SETTINGS in the bots menu) > GAME > **MODE**:

- **Bloodbath**: normal, *gun game* (every kill moves you on to the next of 10 weapons; a pitchfork kill with the last one wins), *king of the hill* (a point a second for holding the flag-marked area alone; it moves every 90 seconds), *juggernaut* (whoever kills the juggernaut becomes it: 200 health, armor, double guns; only kills by or of the juggernaut score), *last man standing* (LIVES EACH; the last one with lives left wins)
- **Teams**: capture the flag (the bots carry, return and defend flags), king of the hill
- **Co-op**: normal, *survival* (waves of monsters that get bigger and nastier, ammo between waves, shared lives (0: endless), your best wave per map is kept)

SET MUTATORS > **MORE MUTATORS**: low or moon gravity, vampire (hits heal you), one-hit kills, and weapon sets (a random weapon at every spawn, or one weapon only).

During a match ESC shows **MATCH STATS** (frags, deaths, aim, best streak, favourite weapon, your nemesis and favourite victim; also shown when a match ends). Everybody in a network game needs the same version (1.1).

### More with bots

- **BOT ORDERS** (ESC during a bot match): follow me, hold this spot, go ahead - for the bots on your side in co-op and teams. In co-op they follow you by default and fight the monsters with you through the episodes.
- **CAREER** (bots menu): a ladder of 10 matches against named bots, getting harder and mixing the match types; win a stage to unlock the next. Your record, kills, deaths, best streak and nemesis are kept.
- **WATCH BOTS** (bots menu): a match of bots only; the camera follows the action (the see-co-op-view key switches bots).
- FINE TUNING: **GRUDGES** (a bot you kill a lot hunts you and taunts you when it gets you back) and **ADAPTIVE DIFFICULTY** (bots slowly get better or worse to keep it close).

### Feedback

Set FINE TUNING > SHOW WHAT BOTS DO, or type `bot_debug 2` in the console: every bot's plan is shown on screen, and every 2 seconds it is written to `notblood.log` in the game folder, with places where bots got stuck. Send that file along with what felt wrong.

**Known issue:** if the game folder has Blood's demo files (`*.DEM`, the gameplay that plays behind the main menu), the game closes when a bot match starts (seen on Windows). Remove the `.DEM` files from the game folder (or start the game with `-nodemo`) and it works. Not fixed yet.

Not yet: online bots (next step), capturing flags in teams, smart co-op partners (in co-op they only fight monsters).
