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
#pragma once

#include "common_game.h"
#include "db.h"

struct PLAYER;

// Blood Extra Crispy's weapons (only while the mod is chosen in MODS; see gorykill.h).
//
// They're held in Caleb's hands next to Blood's: on the number keys of the slots the mod gives them
// (the Gatling Gun on 7 with the spray can, the Trench Gun on 3 with the shotgun...), pressing the key
// again goes on to the next one in that slot. While one of them is in hand, Blood's weapon is put away
// (its curWeapon is none) and this draws and fires the mod's: its QAVs (from its EXTRAC.RFF), its
// sounds, its projectiles. They come from the items the mod turns into them and the pickups its maps
// place.

void becWeaponsInit(void);
bool becWeaponsActive(void);

// in WeaponProcess, first: true while one of the mod's weapons is in hand (Blood's weapon code waits)
bool becWeaponProcess(PLAYER *pPlayer);
// in place of WeaponDraw
bool becWeaponDraw(PLAYER *pPlayer, int nShade, int x, int y, int nPal);
// the ammo the status bar shows while one is in hand, -1 when none is
int becWeaponAmmo(PLAYER *pPlayer);
// an item picked up: one of the mod's weapons in its place, if it is one (the text for the message)
bool becWeaponPickUp(PLAYER *pPlayer, spritetype *pItem, char *pzMessage, int nMessageSize);

void becWeaponsPlayerReset(PLAYER *pPlayer);  // Blood's weapons reset: none of the mod's either
void becWeaponsPlayerStart(PLAYER *pPlayer);  // a new level or life: none in hand
bool becImpactMissile(spritetype *pMissile);  // one of the mod's rockets hits
// USE: the mod's world weapons in front of Caleb (a lawn mower, a Maxim gun on its stand), or off the gun
bool becWeaponUse(PLAYER *pPlayer);
// behind the Maxim gun: Caleb stays where he is; JUMP rips it off its stand (the mod: RUN, which auto-run holds)
bool becWeaponMounted(PLAYER *pPlayer);
void becWeaponMountedInput(PLAYER *pPlayer, bool bRun);

void becWeaponsLevelStart(bool bSaveLoad);
void becWeaponsFrame(void);

// for the weapon switch list: the mod's weapon in hand (its name and picture), NULL for none
char const *becWeaponName(PLAYER *pPlayer, int *pnTile);

// for the bots (bot.cpp), as its own weapon table: how good each of the mod's weapons is, and at what
// distance (Build units); angles in Blood's
struct BECBOTWEAPON
{
    int nPower;
    int nMinDist, nBestDist, nMaxDist;
    int nCone;       // fire when the aim is this close
    int nLeadSpeed;  // aim ahead for slow shots (0: hitscan)
    int nAltNear, nAltFar;  // alt fire when the enemy is nearer / further than this (0: never)
};
int becBotWeaponCount(void);
bool becBotWeaponInfo(int nWeapon, BECBOTWEAPON *pInfo);  // false: not one for bots to choose
bool becBotUsable(PLAYER *pPlayer, int nWeapon);           // in its pack, with ammo
int becBotActive(PLAYER *pPlayer);                         // the mod's weapon in hand or coming up, -1 none
int becBotSlot(int nWeapon);                               // its number key
// an item gives one of the mod's weapons Caleb hasn't got (a second gun of his: the pair)
bool becBotItemGivesWeapon(PLAYER *pPlayer, int nItemType);
