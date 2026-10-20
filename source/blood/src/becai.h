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

#include "ai.h"
#include "becdudes.h"

// What Blood Extra Crispy's enemies do (becai.cpp): the functions its AI states name (CALLBACK_...),
// and what happens when one of them wakes up, is hurt or dies. Written after the mod's scripts
// (scripts/ai/ in its BloodEX.kpf), with Blood's own code where the mod uses Blood's.

struct BECCALLBACK
{
    char const *pzName;                       // the mod's CALLBACK_ name
    void (*pAiFunc)(spritetype *, XSPRITE *); // an enter, move or think function
    void (*pSeqFunc)(int, int);               // or a function the animation calls (seqCallback)
};

BECCALLBACK const *becFindCallback(char const *pzName);

// for each of the mod's enemies
void becAiActivate(spritetype *pSprite, XSPRITE *pXSprite);
void becAiDamage(spritetype *pSprite, XSPRITE *pXSprite, int nSource, DAMAGE_TYPE nDmgType, int nDamage);
bool becAiRecoil(spritetype *pSprite, XSPRITE *pXSprite);
bool becAiKillFirst(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType, int nDamage);
int becAiKill(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType, int nDamage);
void becAiAfterKill(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType);
void becAiInit(spritetype *pSprite, XSPRITE *pXSprite);
void becAiFrame(void);
void becAiLevelStart(void);

// helpers shared with becdudes.cpp
spritetype *becTarget(XSPRITE *pXSprite);
int becSfx(char const *pzName);  // a sound of the mod's (SFX_ name), -1 if it has none
int becSeq(char const *pzName);  // an animation of the mod's (SEQ_ name)
