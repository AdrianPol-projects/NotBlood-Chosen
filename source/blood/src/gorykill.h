//-------------------------------------------------------------------------
/*
This file is part of NotBlood Chosen.

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

// Gory Kills, as in the Blood Extra Crispy mod (by VGames): walk up to a nearly dead enemy, press USE,
// and Caleb finishes it in first person. The animations, art and gore sounds are the mod's own and
// come from the player's copy of it (its BEC_1.2 folder in the game folder, or one named
// extracrispy); nothing of the mod is part of NotBlood. See gorykill.cpp.

#pragma once

struct PLAYER;

extern int32_t gGoryKills;  // the option (off by default)

// startup, after the game's RFF files: finds the mod's files and takes what the kills need
void goryKillInit(void);

// the mod's files are there (the option can be switched on)
bool goryKillAvailable(void);

// a kill is playing for this player: no moving, firing or damage
bool goryKillActive(PLAYER *pPlayer);

// USE was pressed: starts a kill on a nearly dead enemy in front, if there is one
bool goryKillTryStart(PLAYER *pPlayer);

// once a game frame: the kills' timelines and the thrown bodies
void goryKillFrame(void);

// draws the kill instead of the weapon (true when it did), and the blood on the screen after it
bool goryKillDraw(PLAYER *pPlayer, int nShade, int x, int y, int nPal);
void goryKillDrawScreenBlood(PLAYER *pPlayer);

// a new level or a loaded game: nothing is playing
void goryKillReset(void);
