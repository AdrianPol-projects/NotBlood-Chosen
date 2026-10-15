**Test build of NotBlood Chosen with bots** for Windows and Android. It doesn't replace the normal [latest](https://github.com/AdrianPol-projects/NotBlood-Chosen/releases/tag/latest) downloads. Windows: unzip it into its own folder and copy your Blood files there. Android: the APK installs over NotBlood Chosen and keeps using the `NotBlood` folder; to go back, uninstall it first and install the normal APK (your files in `NotBlood` stay).

### Playing against bots

NEW GAME > **< BLOODBATH WITH BOTS >**

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
- **FINE TUNING**: aim, reaction time, aggression, jumping, ambushes, flanking and camping in percent of the difficulty's/personality's own value; taunts; cultist look; showing what every bot is doing
- **MATCH SETTINGS**: the multiplayer setup - bloodbath or teams, map, frag and time limits, weapons, items, mutators
- **START MATCH**. ESC pauses the match. END GAME goes back to normal single player.

The bots only know what a player could: they see what's in front of them and not behind walls, hear shots, jumps and running, and remember where they last saw you. They take a moment to react and their aim wobbles and settles while they track you.

Console: `bot_match [frag limit] [time limit]` starts a match with the menu's settings; all settings are `bot_*` variables (type `bot_` and press TAB).

### Feedback

Set FINE TUNING > SHOW WHAT BOTS DO, or type `bot_debug 2` in the console: every bot's plan is shown on screen, and every 2 seconds it is written to `notblood.log` in the game folder, with places where bots got stuck. Send that file along with what felt wrong.

Not yet: online bots (next step), capturing flags in teams, smart co-op partners (in co-op they only fight monsters).
