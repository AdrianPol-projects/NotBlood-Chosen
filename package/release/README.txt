NotBlood Chosen
===============

NotBlood (a Blood source port) with co-op saves, online lobbies, a live game dashboard, a
MODS menu and a LAN game finder. Every player in a network game needs the same version -
PC, Mac, iPad and Android builds of one version play together.


SETUP

1. Copy your Blood files into this NotBlood folder, next to the game:
   BLOOD.INI, BLOOD.RFF, GUI.RFF, SOUNDS.RFF, SURFACE.DAT, TABLES.DAT, VOXEL.DAT and
   TILES000.ART to TILES017.ART (from One Unit Whole Blood: CD, GOG or Steam).
2. Start the game:
   Windows:  notblood.exe
   Linux:    ./notblood   (needs SDL2: sudo apt install libsdl2-2.0-0)
   macOS:    see "macOS" below


FOLDERS

  movie/    the cutscenes: copy the files from the MOVIE folder of the Blood CD here
            (CS1.SMK ... CS6.SMK with their .WAV files, GTI.SMK, LOGO.SMK)
  addons/   add-on episodes, one folder each. Cryptic Passage: addons/cryptic with
            CRYPTIC.INI, its CP*.MAP maps and CPART07.AR_ / CPART15.AR_
  mods/     mods: a folder each (maps, .def texture packs, voxels, ...) or .zip/.grp/.pk3 files

  Pick add-ons and mods in the main menu: MODS (the game restarts with it).
  CD music: save the CD audio tracks as blood02.ogg, blood03.ogg, ... in this folder and turn on
  OPTIONS > SOUND > REDBOOK AUDIO.


CO-OP SAVES

  During a co-op game: Esc > SAVE CO-OP > pick a slot. Every player's machine saves.
  To continue, host a game, let everybody join, then NEW GAME > LOAD CO-OP (during a game the
  host also has Esc > LOAD CO-OP):
    CONTINUE WHERE YOU SAVED      everything as it was: positions, keys, enemies
    RESTART LEVEL WITH YOUR GEAR  the level starts over, everybody keeps weapons and items
  Players get their saves by their name; players who weren't in the save start with the
  pitchfork. When somebody leaves, the game is saved (slot "PLAYER LEFT").
  The saves are in the coopsaves folder: carryover/ and midlevel/.


ONLINE

  MULTIPLAYER > ONLINE: the server (for example blood.pi-mole.com) and its key, then
  HOST A LOBBY or FIND LOBBIES. Nobody needs to forward ports.
  Live stats of the games: open the server's address in a web browser.


LAN

  MULTIPLAYER > HOST A GAME shows your address. The others use FIND LAN GAMES
  (or JOIN A GAME with that address).


macOS

  1. Put this NotBlood folder anywhere in your home folder - no administrator needed. For
     example in "Applications" inside your home folder, or in Documents.
  2. Copy your Blood files into the NotBlood folder, next to NotBlood.app.
  3. The app isn't signed by Apple, so macOS blocks it at first. Open Terminal, type
        xattr -dr com.apple.quarantine 
     (with a space at the end), drag the NotBlood folder onto the Terminal window and press
     Return. This needs no administrator either.
  4. Double-click NotBlood.app. Settings and saves are kept in the same folder.


Source code and new versions: https://github.com/AdrianPol-projects/NotBlood-Chosen
NotBlood: https://github.com/clipmove/NotBlood - NotBlood Chosen is GPL 2 like NotBlood.
