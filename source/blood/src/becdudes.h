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
#include "actor.h"
#include "ai.h"
#include "db.h"

struct PLAYER;

// Blood Extra Crispy's enemies (only while the mod is chosen in MODS; see gorykill.h).
//
// Its new enemies get dude types of their own, after Blood's (kDudeMax leaves room for them). Their
// numbers (health, speeds, senses), animations and AI states come from the mod's definitions
// (becdefs.h); what the states do (the functions the mod's scripts have) is here and in becai.cpp.
// Some of its enemies are Blood's with another look or health: those stay Blood's types.
enum
{
    kDudeBecBase = 256,
    kDudeBecZombieSkeletal = kDudeBecBase,
    kDudeBecZombieSkeletalEarth,
    kDudeBecZombieSkeletalSleep,
    kDudeBecCrimson,
    kDudeBecCrimsonEarth,
    kDudeBecZomHalf,
    kDudeBecZomHead,
    kDudeBecZomSkeletalHead,
    kDudeBecZomSlime,
    kDudeBecDog,
    kDudeBecMonsterDog,
    kDudeBecNecromancer,
    kDudeBecLich,
    kDudeBecNecroStone,
    kDudeBecGoreTremor,
    kDudeBecFleshTentacle,
    kDudeBecInferno,
    kDudeBecSpirit,
    kDudeBecWorm,
    kDudeBecMax
};

static_assert((int)kDudeBecMax <= (int)kDudeMax, "kDudeMax needs room for Blood Extra Crispy's enemies");

// the mod's enemies are there (its definitions were read when the game started)
bool becDudesActive(void);
void becDudesInit(char const *pzKpf);

inline bool becIsDude(int nType) { return nType >= kDudeBecBase && nType < kDudeBecMax; }

// the mod doesn't count these as kills (its actor.nocountkill)
inline bool becCountsAsKill(int nType)
{
    return nType != kDudeBecSpirit && nType != kDudeBecWorm && nType != kDudeBecNecroStone;
}

// hooks into Blood's AI and actors; each one does nothing (and returns false) for Blood's own enemies
// unless the mod changes what they do
void becLevelStart(bool bSaveLoad);
bool becInitSprite(spritetype *pSprite, XSPRITE *pXSprite);
void becAfterInitSprite(spritetype *pSprite, XSPRITE *pXSprite);
bool becActivateDude(spritetype *pSprite, XSPRITE *pXSprite);
void becDamageDude(spritetype *pSprite, XSPRITE *pXSprite, int nSource, DAMAGE_TYPE nDmgType, int nDamage);
bool becRecoilDude(spritetype *pSprite, XSPRITE *pXSprite);
// at the start of actKillDude: true when the enemy lives on as something else (one of the burning ones)
bool becKillDudeFirst(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType, int nDamage);
// in place of Blood's death animation for the mod's enemies
enum
{
    kBecKillNo,     // not one of the mod's: Blood's death
    kBecKillCorpse, // its death animation plays, and it stays a corpse
    kBecKillGone,   // nothing is left of it
};
int becKillDude(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType, int nDamage);
void becAfterKill(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType);
void becProcessFrame(void);

// a mod enemy in place of a sprite (its type, health, size and animation as the mod has them)
void becMorphDude(spritetype *pSprite, int nType);
spritetype *becSpawnDude(spritetype *pSource, int nType, int x, int y, int z, int nAngle);

// the mod's AI state of that name (AISTATE_...), NULL if it has none
AISTATE *becState(char const *pzName);
void becNewState(spritetype *pSprite, XSPRITE *pXSprite, char const *pzName);
bool becInState(XSPRITE *pXSprite, char const *pzName);

// per sprite: what the mod made of one of Blood's enemies
enum
{
    kBecFlagLesser = 1,     // the Lesser Priest (a priest with the Lesser Beast in him)
    kBecFlagRatBomb = 2,    // a rat with dynamite on its back
    kBecFlagStone = 4,      // an Odious Headstone (a thing)
    kBecFlagHeadShot = 8,   // the last bullet hit its head (the mod's DD2_FLAG_HEADSHOT)
    kBecFlagBellyShot = 16, // or its belly
    kBecFlagLegShot = 32,   // or its legs
};
extern uint8_t gBecFlags[kMaxSprites];

// a vector hit a dude at that height: the mod's head and belly shots
void becVectorHit(spritetype *pSprite, DAMAGE_TYPE nDmgType, int nHitZ);

// the Skeletal Zombie on your face: Blood's hand choke (PLAYER::hand), with its picture and its release
extern bool gBecBite[kMaxPlayers];
bool becBiting(PLAYER *pPlayer);
void becChokeDraw(int x, int y);
void becDropBite(PLAYER *pPlayer);

// fires one of the mod's vectors (BV_ names in its vectorDef.txt)
void becFireVector(spritetype *pSprite, int a2, int a3, int dx, int dy, int dz, char const *pzVector);
