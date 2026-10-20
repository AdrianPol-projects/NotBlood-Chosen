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

// Blood Extra Crispy's text definitions, read from the player's own copy of the mod (BloodEX.kpf, a zip
// made for Nightdive's KEX engine). Only the plain data is used: #define numbers, Actor blocks (an
// enemy's health, speeds, senses, animations) and AI state blocks (animation, duration, the functions
// it calls by name, the next state). The functions themselves are NotBlood's (becdudes.cpp).

struct BECBLOCK
{
    char const *pzName;
    int nFirst, nCount;  // its keys and values (becKey/becValue)
};

// reads the definitions from the mod's BloodEX.kpf; false if it isn't there or can't be read
bool becDefsLoad(char const *pzKpf);
void becDefsFree(void);
bool becDefsLoaded(void);

// a #define of the mod's (SEQ_, SFX_, DUDE_, CALLBACK_ ...); nDefault when it has none
int becDefine(char const *pzName, int nDefault = -1);

BECBLOCK const *becDefActor(char const *pzName);
BECBLOCK const *becDefState(char const *pzName);
BECBLOCK const *becDefVector(char const *pzName);
int becDefStateCount(void);
BECBLOCK const *becDefStateAt(int nIndex);

// a block's value for a key, as text (NULL when it has none) or as a number: numbers, TRUE/FALSE and
// the mod's #define names; fractions (0.9) are given in 16.16
char const *becValue(BECBLOCK const *pBlock, char const *pzKey);
int becInt(BECBLOCK const *pBlock, char const *pzKey, int nDefault = 0);
int becNumber(char const *pzText, int nDefault = 0);
