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

// What Blood Extra Crispy's enemies do (see becai.h). Each function says which of the mod's script
// functions it is (scripts/ai/ in its BloodEX.kpf); they're written the way Blood's own AI is
// (aizomba.cpp and the others), with the mod's numbers. Its distances are meters (512 units each);
// its angles are Blood's (85 is the "facing it" of Blood's zombies too).
//
// Its sounds, animations and states are found by name in its definitions (SFX_, SEQ_, AISTATE_), so
// nothing here depends on the numbers its files give them.

#include "build.h"
#include "compat.h"
#include "common_game.h"
#include "actor.h"
#include "ai.h"
#include "aiburn.h"
#include "becai.h"
#include "becdefs.h"
#include "becdudes.h"
#include "blood.h"
#include "db.h"
#include "dude.h"
#include "endgame.h"
#include "eventq.h"
#include "mmulti.h"
#include "fx.h"
#include "gib.h"
#include "globals.h"
#include "levels.h"
#include "loadsave.h"
#include "player.h"
#include "seq.h"
#include "sfx.h"
#include "trig.h"

extern int nDudeToGibClient1;
extern int gDudeSlope[kMaxXSprites];

namespace
{

enum { kMeter = 512 };

// a state, sound or animation of the mod's by name, looked up once
#define STATE(name) ([]() { static AISTATE *p = becState(name); return p; }())
#define SFX(name) ([]() { static int n = becDefine(name, -1); return n; }())
#define SEQ(name) ([]() { static int n = becDefine(name, -1); return n; }())

void newState(spritetype *pSprite, XSPRITE *pXSprite, AISTATE *pState)
{
    if (pState)
        aiNewState(pSprite, pXSprite, pState);
}

void sound(spritetype *pSprite, int nSfx)
{
    if (nSfx >= 0)
        sfxPlay3DSound(pSprite, nSfx, -1, 0);
}

// a sound from a set of the mod's numbered ones (SFX_X + Random(n))
void soundOf(spritetype *pSprite, int nFirst, int nCount)
{
    if (nFirst >= 0)
        sfxPlay3DSound(pSprite, nFirst + (nCount > 1 ? (int)Random(nCount) : 0), -1, 0);
}

void playSeq(spritetype *pSprite, int nSeq, int nCallback = -1)
{
    if (nSeq >= 0 && pSprite->extra > 0 && gSysRes.Lookup(nSeq, "SEQ"))
        seqSpawn(nSeq, 3, pSprite->extra, nCallback);
}

bool targetHidden(spritetype *pTarget)
{
    if (!IsPlayerSprite(pTarget))
        return false;
    PLAYER *pPlayer = &gPlayer[pTarget->type - kDudePlayer1];
    return powerupCheck(pPlayer, kPwUpShadowCloak) > 0 || powerupCheck(pPlayer, kPwUpDeathMaskUseless) > 0;
}

// the mod's chase checks (its A_AI...ThinkChase): what became of the target
enum { kLookGone, kLookLost, kLookSeen };

struct LOOK
{
    spritetype *pTarget;
    int nDist, nAngle;
};

int look(spritetype *pSprite, XSPRITE *pXSprite, LOOK &l)
{
    if (pXSprite->target < 0 || pXSprite->target >= kMaxSprites)
        return kLookGone;
    spritetype *pTarget = &sprite[pXSprite->target];
    if (pTarget->extra <= 0 || pTarget->extra >= kMaxXSprites)
        return kLookGone;
    XSPRITE *pXTarget = &xsprite[pTarget->extra];
    int const dx = pTarget->x - pSprite->x, dy = pTarget->y - pSprite->y;
    int const nAngle = getangle(dx, dy);
    aiChooseDirection(pSprite, pXSprite, nAngle);
    if (pXTarget->health == 0 || targetHidden(pTarget))
        return kLookGone;
    DUDEINFO *pDudeInfo = getDudeInfo(pSprite->type);
    l.pTarget = pTarget;
    l.nDist = approxDist(dx, dy);
    l.nAngle = klabs(((nAngle + 1024 - pSprite->ang) & 2047) - 1024);
    if (l.nDist > pDudeInfo->seeDist)
        return kLookLost;
    int const nHeight = (pDudeInfo->eyeHeight * pSprite->yrepeat) << 2;
    if (!cansee(pTarget->x, pTarget->y, pTarget->z, pTarget->sectnum, pSprite->x, pSprite->y, pSprite->z - nHeight, pSprite->sectnum))
        return kLookLost;
    if (l.nAngle > pDudeInfo->periphery)
        return kLookLost;
    aiSetTarget(pXSprite, pXSprite->target);
    // the aim (the mod's SetAimSlope)
    if (l.nDist > 0)
    {
        DUDEINFO *pTargetInfo = getDudeInfo(pTarget->type);
        int const nTargetZ = pTarget->z - ((pTargetInfo->eyeHeight * pTarget->yrepeat) << 2);
        gDudeSlope[pSprite->extra] = divscale10(nTargetZ - (pSprite->z - nHeight), l.nDist);
    }
    return kLookSeen;
}

// the mod's A_AI...ThinkGoto: walk to where the target was, search when there
void thinkGoto(spritetype *pSprite, XSPRITE *pXSprite, AISTATE *pSearch)
{
    int const dx = pXSprite->targetX - pSprite->x, dy = pXSprite->targetY - pSprite->y;
    int const nAngle = getangle(dx, dy);
    aiChooseDirection(pSprite, pXSprite, nAngle);
    int const nDelta = klabs(((nAngle + 1024 - pSprite->ang) & 2047) - 1024);
    if (approxDist(dx, dy) <= kMeter && nDelta < getDudeInfo(pSprite->type)->periphery)
        newState(pSprite, pXSprite, pSearch);
    aiThinkTarget(pSprite, pXSprite);
}

void lostTarget(spritetype *pSprite, XSPRITE *pXSprite, AISTATE *pGoto)
{
    newState(pSprite, pXSprite, pGoto);
    pXSprite->target = -1;
}

// a straight jump forward (the mod adds to the velocity: speed 20 is 20 << 16, its z up is Blood's down)
void leap(spritetype *pSprite, int nSpeed, int nUp)
{
    xvel[pSprite->index] += mulscale30(nSpeed << 16, Cos(pSprite->ang));
    yvel[pSprite->index] += mulscale30(nSpeed << 16, Sin(pSprite->ang));
    zvel[pSprite->index] -= nUp << 16;
}

// a vector at the target (the mod's FireVector at the target's eyes)
void fireAtTarget(spritetype *pSprite, XSPRITE *pXSprite, char const *pzVector)
{
    int dz = 0;
    if (pXSprite->target >= 0 && pXSprite->target < kMaxSprites)
    {
        spritetype *pTarget = &sprite[pXSprite->target];
        dz = ((pSprite->yrepeat * getDudeInfo(pSprite->type)->eyeHeight) << 2) - ((pTarget->yrepeat * getDudeInfo(pTarget->type)->eyeHeight) << 2);
    }
    int const nAngle = getangle(pXSprite->targetX - pSprite->x, pXSprite->targetY - pSprite->y);
    becFireVector(pSprite, 0, 0, Cos(nAngle) >> 16, Sin(nAngle) >> 16, dz, pzVector);
}

spritetype *spriteOf(int nXSprite)
{
    if (nXSprite <= 0 || nXSprite >= kMaxXSprites)
        return NULL;
    int const nSprite = xsprite[nXSprite].reference;
    return nSprite >= 0 && nSprite < kMaxSprites ? &sprite[nSprite] : NULL;
}

// the mod's ACTOR_PukeBlood: blobs of red puke (Blood's puke missile with the mod's look)
void pukeBlobs(spritetype *pSprite, XSPRITE *pXSprite, int nCount, int nZ)
{
    for (int i = 0; i < nCount; i++)
    {
        int const nAngle = pSprite->ang + Random2(64);
        spritetype *pMissile = actFireMissile(pSprite, 0, nZ, Cos(nAngle) >> 16, Sin(nAngle) >> 16,
                                              gDudeSlope[pSprite->extra] + Random2(2000), kMissilePukeGreen);
        if (pMissile)
        {
            pMissile->xrepeat = pMissile->yrepeat = 16;
            pMissile->pal = 2;
            int const nSpeed = 0x10000 + Random(0x30000);  // the mod's 250000-1000000, roughly
            xvel[pMissile->index] = mulscale16(xvel[pMissile->index], nSpeed);
            yvel[pMissile->index] = mulscale16(yvel[pMissile->index], nSpeed);
            if (pMissile->extra > 0)
                playSeq(pMissile, SEQ("SEQ_REDPUKE"));
        }
    }
    UNREFERENCED_PARAMETER(pXSprite);
}

// an explosion right here (the mod's single dynamite, or spray can, going off)
void explodeHere(spritetype *pSprite, int nThing = kThingArmedTNTStick)
{
    spritetype *pTNT = actSpawnThing(pSprite->sectnum, pSprite->x, pSprite->y, pSprite->z, nThing);
    if (!pTNT)
        return;
    actPropagateSpriteOwner(pTNT, pSprite);
    actExplodeSprite(pTNT);
}

// blood, the mod's blood plume and some gibs, for the gory deaths
void gore(spritetype *pSprite, int nBlood, bool bGibs)
{
    for (int i = 0; i < nBlood; i++)
        fxSpawnBlood(pSprite, 0);
    if (bGibs)
    {
        GibSprite(pSprite, GIBTYPE_7, NULL, NULL);
        GibSprite(pSprite, GIBTYPE_7, NULL, NULL);
    }
}

// worms crawling out of a body (the mod's ACTOR_SpawnWorm: they become worms)
void spawnWorms(spritetype *pSprite, int nCount)
{
    if (!becDudesActive())
        return;
    for (int i = 0; i < nCount; i++)
    {
        int const nAngle = Random(2048);
        int const nDist = 64 + Random(256);
        int x = pSprite->x + mulscale30(nDist, Cos(nAngle)), y = pSprite->y + mulscale30(nDist, Sin(nAngle));
        int16_t nSector = pSprite->sectnum;
        updatesector(x, y, &nSector);
        if (nSector < 0)
            x = pSprite->x, y = pSprite->y;
        spritetype *pWorm = becSpawnDude(pSprite, kDudeBecWorm, x, y, pSprite->z, nAngle);
        if (pWorm)
        {
            xvel[pWorm->index] = mulscale30(0x20000 + Random(0x30000), Cos(nAngle));
            yvel[pWorm->index] = mulscale30(0x20000 + Random(0x30000), Sin(nAngle));
            zvel[pWorm->index] = -(0x20000 + Random(0x30000));
        }
    }
}

// a corpse the Necromancer can raise: Blood's death animation of these enemies, still lying there
struct REVIVABLE
{
    short nType;
    char const *pzSeq;      // the mod's revival animation (its ACTOR_RevivedCorpse plays it)
};

REVIVABLE const gRevivable[] = {
    { kDudeCultistTommy, "SEQ_RCULT1D1" },
    { kDudeCultistShotgun, "SEQ_RCULT2D1" },
    { kDudeCultistTesla, "SEQ_RCULT3D1" },
    { kDudeCultistTNT, "SEQ_RCULT4D1" },
    { kDudeZombieAxeNormal, "SEQ_RZOMB1D1" },
    { kDudeZombieButcher, "SEQ_RZOMB2D1" },
    { kDudeGargoyleFlesh, "SEQ_RGARG1D1" },
    { kDudeHellHound, "SEQ_RHELL1D1" },
    { kDudeGillBeast, "SEQ_RGILL1D1" },
    { kDudeBecCrimson, "SEQ_RCRIMSD1" },
    { kDudeBecZombieSkeletal, "SEQ_RZOMBSD1" },
};

enum { kReviveTime = 2 * 120 };  // the raised corpse stands up, then fights

int gReviveTime[kMaxSprites];    // when a corpse being raised stands up, 0 when none
int gDeathTimer[kMaxSprites];    // a dead Necromancer's chance at being a Lich, a dog's turning
int gAllyTime[kMaxSprites];      // when an ally is gone again (gFrameClock), 0 for never
int nNextStone;                  // the next Odious Headstone (gFrameClock), 0 for none planned

// Blood's death animation of a type: its seqStartID + 1 is a corpse lying there
REVIVABLE const *revivableFor(int nType)
{
    for (auto const &r : gRevivable)
        if (r.nType == nType)
            return &r;
    return NULL;
}

bool underwater(int nSector)
{
    int const nXSector = sector[nSector].extra;
    return nXSector > 0 && xsector[nXSector].Underwater;
}

// the mod's G_Resurrect: the nearest corpse it can see stands up again
bool resurrect(spritetype *pSprite, XSPRITE *pXSprite, AISTATE *pReviveState)
{
    int nBest = -1, nBestDist = 1000 * 16;  // its 1000 units
    for (int nSprite = headspritestat[kStatThing]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pCorpse = &sprite[nSprite];
        if (pCorpse->type != kThingBloodChunks || pCorpse->extra <= 0 || gReviveTime[nSprite])
            continue;
        if (!revivableFor(pCorpse->inittype) || underwater(pCorpse->sectnum))
            continue;
        if (seqGetStatus(3, pCorpse->extra) >= 0)  // still falling down
            continue;
        int const nDist = approxDist(pCorpse->x - pSprite->x, pCorpse->y - pSprite->y);
        if (nDist >= nBestDist || !cansee(pSprite->x, pSprite->y, pSprite->z - (32 << 8), pSprite->sectnum, pCorpse->x, pCorpse->y, pCorpse->z - (8 << 8), pCorpse->sectnum))
            continue;
        nBest = nSprite, nBestDist = nDist;
    }
    if (nBest < 0)
        return false;
    spritetype *pCorpse = &sprite[nBest];
    newState(pSprite, pXSprite, pReviveState);
    sound(pSprite, SFX("SFX_NECREVI"));
    soundOf(pSprite, SFX("SFX_NMATTA1"), 4);
    if (REVIVABLE const *pRevive = revivableFor(pCorpse->inittype))
        playSeq(pCorpse, becDefine(pRevive->pzSeq, -1));
    gReviveTime[nBest] = (int)gFrameClock + kReviveTime;
    return true;
}

// the corpse is up: Blood's enemy again, full of life
void reviveNow(int nSprite)
{
    gReviveTime[nSprite] = 0;
    spritetype *pCorpse = &sprite[nSprite];
    if (pCorpse->statnum != kStatThing || pCorpse->type != kThingBloodChunks || pCorpse->extra <= 0)
        return;
    int const nType = pCorpse->inittype;
    if (!revivableFor(nType))
        return;
    XSPRITE *pXCorpse = &xsprite[pCorpse->extra];
    ChangeSpriteStat(nSprite, kStatDude);
    pCorpse->type = nType;
    pCorpse->flags = 15;
    pCorpse->cstat |= CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN;
    pCorpse->clipdist = getDudeInfo(nType)->clipdist;
    pXCorpse->health = getDudeInfo(nType)->startHealth << 4;
    pXCorpse->burnTime = 0;
    pXCorpse->key = 0;
    pXCorpse->triggerOnce = 0;
    pXCorpse->isTriggered = 0;
    pXCorpse->locked = 0;
    pXCorpse->respawnPending = 0;
    if (becIsAlly(nSprite))  // raised by Caleb (the Vile Anathema): it fights for him
    {
        int const nOwner = pCorpse->owner;
        aiInitSprite(pCorpse);
        if (nOwner >= 0 && nOwner < kMaxSprites)
            becMakeAlly(pCorpse, &sprite[nOwner], 0);
        return;
    }
    gKillMgr.AddCount(pCorpse);  // one more to kill
    aiInitSprite(pCorpse);
    aiActivateDude(pCorpse, pXCorpse);
}

// the mod's G_SpawnZombies: one rises from the ground in front of it
void spawnZombie(spritetype *pSprite, XSPRITE *pXSprite, AISTATE *pReviveState)
{
    int const r = Random(100);
    int const nType = r <= 30 ? (int)kDudeZombieAxeBuried : r <= 60 ? kDudeBecZombieSkeletalEarth : r <= 90 ? kDudeBecCrimsonEarth : (int)kDudePhantasm;
    int const nDist = pSprite->clipdist * 16;
    int x = pSprite->x + mulscale30(nDist, Cos(pSprite->ang)), y = pSprite->y + mulscale30(nDist, Sin(pSprite->ang));
    int16_t nSector = pSprite->sectnum;
    updatesector(x, y, &nSector);
    if (nSector < 0 || underwater(nSector))
        return;
    newState(pSprite, pXSprite, pReviveState);
    sound(pSprite, SFX("SFX_VOOINVI"));
    soundOf(pSprite, SFX("SFX_NMATTA1"), 4);
    int ceilZ, floorZ;
    getzsofslope(nSector, x, y, &ceilZ, &floorZ);
    spritetype *pZombie = becSpawnDude(pSprite, nType, x, y, floorZ, pSprite->ang);
    if (!pZombie)
        return;
    gKillMgr.AddCount(pZombie);
    XSPRITE *pXZombie = &xsprite[pZombie->extra];
    aiSetTarget(pXZombie, pXSprite->target >= 0 ? pXSprite->target : pZombie->index);
    aiActivateDude(pZombie, pXZombie);  // the buried ones rise
}

//
// generic
//

void thinkSearch(spritetype *pSprite, XSPRITE *pXSprite)
{
    aiChooseDirection(pSprite, pXSprite, pXSprite->goalAng);
    aiThinkTarget(pSprite, pXSprite);
}

void thinkTarget(spritetype *pSprite, XSPRITE *pXSprite)
{
    aiThinkTarget(pSprite, pXSprite);
}

//
// small ones that bite (ai_rat.txt): the crawling torso, the living heads
//

AISTATE *ratState(spritetype *pSprite, char const *pzWhat)
{
    char name[64];
    char const *pzFamily = pSprite->type == kDudeBecZomHalf ? "ZHalf" : pSprite->type == kDudeBecZomHead ? "ZHead" : "ZSHead";
    Bsnprintf(name, sizeof(name), "AISTATE_%s%s", pzFamily, pzWhat);
    return becState(name);
}

void ratThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, ratState(pSprite, "Search"));
}

void ratThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    int const nLook = look(pSprite, pXSprite, l);
    if (nLook == kLookGone)
    {
        newState(pSprite, pXSprite, ratState(pSprite, "Search"));
        return;
    }
    if (nLook == kLookLost)
    {
        lostTarget(pSprite, pXSprite, ratState(pSprite, "Goto"));
        return;
    }
    bool const bOnGround = (pSprite->flags & kPhysFalling) == 0;
    if (pSprite->type == kDudeBecZomHalf)
    {
        if (l.nDist < (int)(1.4 * kMeter) && l.nAngle < 85)
            newState(pSprite, pXSprite, ratState(pSprite, "Bite"));
        return;
    }
    // the heads hop at you, and bounce on toward you from further away
    if (!bOnGround)
        return;
    if (l.nDist < (int)(3.5 * kMeter) && l.nAngle < 85)
    {
        leap(pSprite, 20, 12);
        soundOf(pSprite, SFX("SFX_DEMMEL1"), 1);
        newState(pSprite, pXSprite, ratState(pSprite, "Bite"));
    }
    else if (l.nDist >= (int)(3.5 * kMeter))
    {
        leap(pSprite, 10, 8);
        sound(pSprite, SFX("SFX_PUNT"));
        newState(pSprite, pXSprite, ratState(pSprite, "Search"));
    }
}

void ratBite(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    if (pSprite->type == kDudeBecZomHalf)
    {
        sound(pSprite, SFX("SFX_AZOMSWIP"));
        fireAtTarget(pSprite, &xsprite[nXSprite], "BV_ZOMBIEHALF");
    }
    else
        fireAtTarget(pSprite, &xsprite[nXSprite], "BV_ZOMBIEHEAD");
}

//
// the Skeletal Zombie (ai_zombieskeletal.txt)
//

bool playerBusy(spritetype *pTarget)
{
    // the mod doesn't grab you when you're held already, in a Gory Kill or untouchable
    if (!IsPlayerSprite(pTarget))
        return true;
    PLAYER *pPlayer = &gPlayer[pTarget->type - kDudePlayer1];
    return pPlayer->hand || powerupCheck(pPlayer, kPwUpDeathMask) > 0;
}

void skeletalAttack(spritetype *pSprite, XSPRITE *pXSprite, LOOK const &l, bool bPonder)
{
    int const r = Random(100);
    bool const bWounded = pXSprite->health <= 200;  // primed for a Gory Kill: it goes for your throat
    if (l.nDist < kMeter && l.nAngle < 85 && bWounded)
    {
        if (!playerBusy(l.pTarget))
            newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalLeap"));
        else
        {
            if (r <= 20)
                sound(pSprite, SFX("SFX_SZOMATK"));
            newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalHack"));
        }
        return;
    }
    if (l.nDist < 2 * kMeter && l.nDist > kMeter && !bWounded)
    {
        if (l.nAngle < 85)
        {
            if (r <= 20)
                sound(pSprite, SFX("SFX_SZOMATK"));
            newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalHack"));
        }
        if (bPonder)
            return;
    }
    if (r <= 20 && l.nDist < 12 * kMeter && l.nDist > 5 * kMeter && l.nAngle < 85)
    {
        sound(pSprite, SFX("SFX_SZOMATK"));
        newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalThrow"));
    }
}

void skeletalThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalSearch"));
}

void skeletalThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    switch (look(pSprite, pXSprite, l))
    {
    case kLookGone:
        newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalSearch"));
        return;
    case kLookLost:
        lostTarget(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalGoto"));
        return;
    }
    skeletalAttack(pSprite, pXSprite, l, false);
}

void skeletalThinkPonder(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    int const nLook = look(pSprite, pXSprite, l);
    if (nLook == kLookGone)
    {
        newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalSearch"));
        return;
    }
    if (nLook == kLookSeen)
    {
        AISTATE *pBefore = pXSprite->aiState;
        skeletalAttack(pSprite, pXSprite, l, true);
        if (pXSprite->aiState != pBefore)
            return;
    }
    newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalChase"));
}

void skeletalEarthRisen(spritetype *pSprite, XSPRITE *pXSprite)
{
    pSprite->cstat |= CSTAT_SPRITE_BLOCK;
    pSprite->flags |= 1;
    pSprite->type = kDudeBecZombieSkeletal;
    newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalSearch"));
}

void skeletalRecoil(spritetype *pSprite, XSPRITE *pXSprite)
{
    sound(pSprite, SFX("SFX_AZOMPAIN"));
    if (pSprite->type == kDudeBecZombieSkeletalEarth || pSprite->type == kDudeBecZombieSkeletalSleep)
        skeletalEarthRisen(pSprite, pXSprite);
    if (pXSprite->health <= (60 / 3))
        newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalRecoil2"));
}

void earthEntry(spritetype *pSprite, XSPRITE *pXSprite)
{
    sound(pSprite, SFX("SFX_AZOMRISE"));
    pSprite->ang = getangle(pXSprite->targetX - pSprite->x, pXSprite->targetY - pSprite->y);
    // the mod throws up dirt (its fx 85); Blood's zombies rise without it
}

void skeletalHack(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    sound(pSprite, SFX("SFX_AZOMSWIP"));
    fireAtTarget(pSprite, &xsprite[nXSprite], "BV_ZOMBIEHALF");
}

// a chunk of meat thrown at you (the mod's ACTOR_GibProjectile: Blood's thrown bone with its picture)
void skeletalThrow(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite || xsprite[nXSprite].health <= 0)
        return;
    sound(pSprite, SFX("SFX_AZOMSWIP"));
    spritetype *pThing = actFireThing(pSprite, 0, 0, gDudeSlope[nXSprite] - 7500, kThingBone, 0xEEEEE);
    if (pThing)
        pThing->picnum = 4955;
}

// it grabs you and bites (the mod's SZBiteDamage: Blood's hand choke, with its own picture)
void skeletalLatch(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    XSPRITE *pXSprite = &xsprite[nXSprite];
    if (!pSprite || pXSprite->target < 0 || pXSprite->target >= kMaxSprites)
        return;
    spritetype *pTarget = &sprite[pXSprite->target];
    if (!IsPlayerSprite(pTarget))
        return;
    PLAYER *pPlayer = &gPlayer[pTarget->type - kDudePlayer1];
    if (pPlayer->hand || !pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
        return;
    if (approxDist(pTarget->x - pSprite->x, pTarget->y - pSprite->y) > 2 * kMeter)
        return;
    pPlayer->hand = 1;  // Blood's choke: USE shakes it off (becDropBite)
    gBecBite[pPlayer->nPlayer] = true;
    actPostSprite(pSprite->index, kStatFree);
}

//
// the Crimson Innocent (ai_crimson.txt)
//

void crimsonAttack(spritetype *pSprite, XSPRITE *pXSprite, LOOK const &l, bool bPonder)
{
    int const r = Random(100);
    if (r <= 25 && l.nDist > 3 * kMeter && l.nDist < 10 * kMeter)
    {
        if (l.nAngle < 85)
            newState(pSprite, pXSprite, STATE("AISTATE_CrimsonPuke"));
        if (bPonder)
            return;
    }
    if (l.nDist < (int)(1.5 * kMeter) && l.nAngle < 85)
    {
        if (bPonder)
            sound(pSprite, SFX("SFX_AZOMSWIP"));
        newState(pSprite, pXSprite, STATE("AISTATE_CrimsonHack"));
    }
}

void crimsonThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, STATE("AISTATE_CrimsonSearch"));
}

void crimsonThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    switch (look(pSprite, pXSprite, l))
    {
    case kLookGone:
        newState(pSprite, pXSprite, STATE("AISTATE_CrimsonSearch"));
        return;
    case kLookLost:
        lostTarget(pSprite, pXSprite, STATE("AISTATE_CrimsonGoto"));
        return;
    }
    crimsonAttack(pSprite, pXSprite, l, false);
}

void crimsonThinkPonder(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    int const nLook = look(pSprite, pXSprite, l);
    if (nLook == kLookGone)
    {
        newState(pSprite, pXSprite, STATE("AISTATE_CrimsonSearch"));
        return;
    }
    if (nLook == kLookSeen)
    {
        AISTATE *pBefore = pXSprite->aiState;
        crimsonAttack(pSprite, pXSprite, l, true);
        if (pXSprite->aiState != pBefore)
            return;
    }
    newState(pSprite, pXSprite, STATE("AISTATE_CrimsonChase"));
}

void crimsonRecoil(spritetype *pSprite, XSPRITE *pXSprite)
{
    UNREFERENCED_PARAMETER(pXSprite);
    sound(pSprite, SFX("SFX_CRIPAIN"));
}

void crimsonHack(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    sound(pSprite, SFX("SFX_AZOMSWIP"));
    fireAtTarget(pSprite, &xsprite[nXSprite], "BV_HELLHANDPUNCH");
}

void crimsonPuke(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    sound(pSprite, SFX("SFX_CRIPUKE"));
    int const nHeight = (getDudeInfo(pSprite->type)->eyeHeight * pSprite->yrepeat) << 2;
    pukeBlobs(pSprite, &xsprite[nXSprite], 6, -nHeight + (8 << 8));
}

void crimsonEarthRisen(spritetype *pSprite, XSPRITE *pXSprite)
{
    pSprite->cstat |= CSTAT_SPRITE_BLOCK;
    pSprite->flags |= 1;
    pSprite->type = kDudeBecCrimson;
    newState(pSprite, pXSprite, STATE("AISTATE_CrimsonSearch"));
}

//
// the Slime Zombie (ai_slime.txt)
//

void slimeThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, STATE("AISTATE_SlimeSearch"));
}

void slimeThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    switch (look(pSprite, pXSprite, l))
    {
    case kLookGone:
        newState(pSprite, pXSprite, STATE("AISTATE_SlimeSearch"));
        return;
    case kLookLost:
        lostTarget(pSprite, pXSprite, STATE("AISTATE_SlimeGoto"));
        return;
    }
    if (Random(100) <= 25 && l.nDist < 10 * kMeter && l.nAngle < 85)
        newState(pSprite, pXSprite, STATE("AISTATE_SlimeFire"));
}

void slimeThinkPonder(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    int const nLook = look(pSprite, pXSprite, l);
    if (nLook == kLookGone)
    {
        newState(pSprite, pXSprite, STATE("AISTATE_SlimeSearch"));
        return;
    }
    if (nLook == kLookSeen && Random(100) <= 25 && l.nDist < 10 * kMeter)
    {
        if (l.nAngle < 85)
            newState(pSprite, pXSprite, STATE("AISTATE_SlimeFire"));
        return;
    }
    newState(pSprite, pXSprite, STATE("AISTATE_SlimeChase"));
}

void slimeRecoil(spritetype *pSprite, XSPRITE *pXSprite)
{
    UNREFERENCED_PARAMETER(pXSprite);
    sound(pSprite, SFX("SFX_CRIPAIN"));
}

void slimeFire(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    pukeBlobs(pSprite, &xsprite[nXSprite], 6, -(2000 << 2));
    // and a hard smack for whoever stands right in front of it
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        spritetype *pPlayer = gPlayer[p].pSprite;
        if (!pPlayer)
            continue;
        int const dx = pPlayer->x - pSprite->x, dy = pPlayer->y - pSprite->y;
        int const nDelta = klabs(((getangle(dx, dy) + 1024 - pSprite->ang) & 2047) - 1024);
        if (approxDist(dx, dy) < 100 * 16 && nDelta < 228)  // its 100 units and 40 degrees
        {
            actDamageSprite(pSprite->index, pPlayer, kDamageBullet, 300);
            fxSpawnBlood(pPlayer, 0);
        }
    }
}

//
// the attack dog and the Monster Dog (ai_dog.txt, ai_monsterdog.txt)
//

void dogThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, pSprite->type == kDudeBecMonsterDog ? STATE("AISTATE_MonsterDogSearch") : STATE("AISTATE_DogSearch"));
}

void dogThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    bool const bMonster = pSprite->type == kDudeBecMonsterDog;
    LOOK l;
    switch (look(pSprite, pXSprite, l))
    {
    case kLookGone:
        newState(pSprite, pXSprite, bMonster ? STATE("AISTATE_MonsterDogSearch") : STATE("AISTATE_DogSearch"));
        return;
    case kLookLost:
        lostTarget(pSprite, pXSprite, bMonster ? STATE("AISTATE_MonsterDogGoto") : STATE("AISTATE_DogGoto"));
        return;
    }
    if (l.nDist < (int)(3.5 * kMeter) && l.nDist > (int)(1.2 * kMeter) && l.nAngle < 85)
    {
        leap(pSprite, 20, 8);
        soundOf(pSprite, SFX("SFX_DEMMEL1"), 1);
        sound(pSprite, bMonster ? SFX("SFX_MOGATT2") : SFX("SFX_DOGATK"));
        newState(pSprite, pXSprite, bMonster ? STATE("AISTATE_MonsterDogLeap") : STATE("AISTATE_DogLeap"));
    }
    else if (l.nDist < (int)(1.2 * kMeter) && l.nAngle < 85)
    {
        sound(pSprite, bMonster ? SFX("SFX_MOGATT1") : SFX("SFX_DOGATK"));
        newState(pSprite, pXSprite, bMonster ? STATE("AISTATE_MonsterDogBite") : STATE("AISTATE_DogBite"));
    }
}

void dogBite(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite || xsprite[nXSprite].target < 0 || !IsPlayerSprite(&sprite[xsprite[nXSprite].target]))
        return;
    fireAtTarget(pSprite, &xsprite[nXSprite], "BV_HOUNDBITE");
    if (pSprite->type == kDudeBecMonsterDog)  // twice the damage
        fireAtTarget(pSprite, &xsprite[nXSprite], "BV_HOUNDBITE");
}

//
// the Necromancer and the Lich (ai_necro.txt, ai_lich.txt)
//

// it vanishes in a puff and pops up somewhere close (its teleport is a quick hop)
void necroTeleport(spritetype *pSprite, spritetype *pTarget, bool bLich)
{
    sound(pSprite, SFX("SFX_NECTELE"));
    playSeq(pSprite, bLich ? SEQ("SEQ_LICHT1") : SEQ("SEQ_NECROT1"));
    int const nAngle = Random(100) <= 75 ? (pTarget->ang + 1024) & 2047 : pTarget->ang;
    xvel[pSprite->index] += mulscale30(50 << 16, Cos(nAngle));
    yvel[pSprite->index] += mulscale30(50 << 16, Sin(nAngle));
}

void necroFireSkulls(spritetype *pSprite, XSPRITE *pXSprite, int nExtra)
{
    int const nDifficulty = clamp<int>(gGameOptions.nDifficulty, 0, 4);
    int const nSpread = 3731 - 750 * nDifficulty;
    for (int i = 0; i <= nDifficulty + nExtra; i++)
    {
        int dx = Cos(pSprite->ang) >> 16, dy = Sin(pSprite->ang) >> 16, dz = gDudeSlope[pSprite->extra];
        if (i > 0)
            dx += Random2(nSpread), dy += Random2(nSpread), dz += Random2(nSpread);
        spritetype *pMissile = actFireMissile(pSprite, 0, -(25 << 8), dx, dy, dz, kMissileEctoSkull);
        if (pMissile)
            pMissile->picnum = 5798;
    }
    UNREFERENCED_PARAMETER(pXSprite);
}

void necroThink(spritetype *pSprite, XSPRITE *pXSprite, bool bPonder)
{
    bool const bLich = pSprite->type == kDudeBecLich;
    AISTATE *pSearch = bLich ? STATE("AISTATE_LichSearch") : STATE("AISTATE_NecroSearch");
    AISTATE *pRevive = bLich ? STATE("AISTATE_LichRevive") : STATE("AISTATE_NecroRevive");
    AISTATE *pFire = bLich ? STATE("AISTATE_LichFire") : STATE("AISTATE_NecroFire");
    LOOK l;
    int const nLook = look(pSprite, pXSprite, l);
    if (nLook == kLookGone)
    {
        newState(pSprite, pXSprite, pSearch);
        return;
    }
    if (nLook == kLookLost)
    {
        if (bPonder)
            newState(pSprite, pXSprite, bLich ? STATE("AISTATE_LichChase") : STATE("AISTATE_NecroChase"));
        else
            lostTarget(pSprite, pXSprite, bLich ? STATE("AISTATE_LichGoto") : STATE("AISTATE_NecroGoto"));
        return;
    }
    int const r = Random(100);
    if (l.nDist < (int)(3.5 * kMeter))  // too close: it gets away
    {
        necroTeleport(pSprite, l.pTarget, bLich);
        return;
    }
    bool const bNear = l.nDist < (int)(6.5 * kMeter), bFar = l.nDist > (int)(10.5 * kMeter);
    if ((bNear || bFar) && r <= 10)
    {
        resurrect(pSprite, pXSprite, pRevive);
        return;
    }
    if ((bNear || bFar) && r > 10 && r <= 15)
    {
        spawnZombie(pSprite, pXSprite, pRevive);
        if (bLich)  // the Lich raises a few at once
            for (int i = 0; i < 2; i++)
                spawnZombie(pSprite, pXSprite, pRevive);
        return;
    }
    if (r <= 25 && !bNear && !bFar && l.nAngle < 85)
    {
        sound(pSprite, bLich ? SFX("SFX_LICHA1") : SFX("SFX_NSKULLF"));
        newState(pSprite, pXSprite, pFire);
        return;
    }
    if (bPonder)
        newState(pSprite, pXSprite, bLich ? STATE("AISTATE_LichChase") : STATE("AISTATE_NecroChase"));
}

void necroThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, pSprite->type == kDudeBecLich ? STATE("AISTATE_LichSearch") : STATE("AISTATE_NecroSearch"));
}

void necroThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    necroThink(pSprite, pXSprite, false);
}

void necroThinkPonder(spritetype *pSprite, XSPRITE *pXSprite)
{
    necroThink(pSprite, pXSprite, true);
}

void necroRecoil(spritetype *pSprite, XSPRITE *pXSprite)
{
    UNREFERENCED_PARAMETER(pXSprite);
    if (pSprite->type == kDudeBecLich)
        sound(pSprite, SFX("SFX_LICHR1"));
    else
        soundOf(pSprite, SFX("SFX_NMPAIN1"), 2);
}

void necroFire(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (pSprite)
        necroFireSkulls(pSprite, &xsprite[nXSprite], pSprite->type == kDudeBecLich ? 3 : 0);
}

void lichSpawn(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (pSprite)
        sound(pSprite, SFX("SFX_LICHSPO"));
}

// the Odious Headstone comes out of the ground (ai_common.txt A_NecroStoneRise): it becomes a stone
// that raises zombies (Necromancer.txt A_NecroStone) until it's blown up
void necroStoneRise(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    spritetype *pStone = actSpawnThing(pSprite->sectnum, pSprite->x, pSprite->y, pSprite->z, kThingObjectExplode);
    if (pStone && pStone->extra > 0)
    {
        pStone->picnum = 7326;
        pStone->xrepeat = pStone->yrepeat = 48;
        pStone->clipdist = 48;
        pStone->shade = 4;
        pStone->cstat |= CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN;
        pStone->flags &= ~(kPhysGravity | kPhysFalling);
        pStone->ang = pSprite->ang;
        xsprite[pStone->extra].data1 = 300;
        xsprite[pStone->extra].health = 100 << 4;
        gBecFlags[pStone->index] |= kBecFlagStone;
    }
    actPostSprite(pSprite->index, kStatFree);
}

//
// the Gore Tremor (ai_gore.txt): it travels underground and comes up to bite or spit
//

void goreThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, STATE("AISTATE_GoreSearch"));
}

void goreSurface(spritetype *pSprite)
{
    xvel[pSprite->index] = yvel[pSprite->index] = 0;
    sound(pSprite, SFX("SFX_GOREXIT"));
    int top, bottom;
    GetSpriteExtents(pSprite, &top, &bottom);
    gFX.fxSpawn(FX_27, pSprite->sectnum, pSprite->x, pSprite->y, bottom, 0);  // Blood's dirt bits
}

void goreThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    switch (look(pSprite, pXSprite, l))
    {
    case kLookGone:
        newState(pSprite, pXSprite, STATE("AISTATE_GoreSearch"));
        return;
    case kLookLost:
        lostTarget(pSprite, pXSprite, STATE("AISTATE_GoreGoto"));
        return;
    }
    if (l.nDist < (int)(1.8 * kMeter) && l.nAngle < 85)
    {
        goreSurface(pSprite);
        newState(pSprite, pXSprite, STATE("AISTATE_GoreBite"));
    }
    else if (l.nDist < 10 * kMeter && l.nAngle < 85 && Random(100) <= 5)
    {
        goreSurface(pSprite);
        newState(pSprite, pXSprite, STATE("AISTATE_GoreSpit"));
    }
}

void goreBite(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    sound(pSprite, SFX("SFX_GORBITE"));
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        spritetype *pPlayer = gPlayer[p].pSprite;
        if (pPlayer && approxDist(pPlayer->x - pSprite->x, pPlayer->y - pSprite->y) < 100 * 16)
        {
            actDamageSprite(pSprite->index, pPlayer, kDamageBullet, 600);
            fxSpawnBlood(pPlayer, 0);
        }
    }
}

void goreSpit(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    XSPRITE *pXSprite = &xsprite[nXSprite];
    sound(pSprite, SFX("SFX_GORSPIT"));
    if (pXSprite->target >= 0 && pXSprite->target < kMaxSprites)
        pSprite->ang = getangle(sprite[pXSprite->target].x - pSprite->x, sprite[pXSprite->target].y - pSprite->y);
    spawnWorms(pSprite, 4 + Random(4));
    actFireMissile(pSprite, 0, -(8 << 8), Cos(pSprite->ang) >> 16, Sin(pSprite->ang) >> 16, gDudeSlope[nXSprite], kMissilePukeGreen);
}

//
// the Flesh Tentacle (ai_fleshtentacle.txt): it shoots from where it grows, and bursts when you're close
//

void fleshThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, STATE("AISTATE_FleshTSearch"));
}

void fleshThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    switch (look(pSprite, pXSprite, l))
    {
    case kLookGone:
        newState(pSprite, pXSprite, STATE("AISTATE_FleshTSearch"));
        return;
    case kLookLost:
        lostTarget(pSprite, pXSprite, STATE("AISTATE_FleshTGoto"));
        return;
    }
    if (l.nDist < 30 * kMeter && l.nDist > 3 * kMeter && l.nAngle < 85 && Random(100) <= 25)
        newState(pSprite, pXSprite, STATE("AISTATE_FleshTFire"));
    else if (l.nDist <= 3 * kMeter && pXSprite->health > 0)
    {
        // it bursts in a ball of fire (the mod's exploding spray can)
        explodeHere(pSprite, kThingArmedSpray);
        actDamageSprite(pSprite->index, pSprite, kDamageExplode, pXSprite->health);
    }
}

void fleshFire(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    XSPRITE *pXSprite = &xsprite[nXSprite];
    if (pXSprite->target < 0 || !IsPlayerSprite(&sprite[pXSprite->target]))
        return;
    sound(pSprite, SFX("SFX_LALTFR1"));
    for (int i = 0; i < 3; i++)
        actFireMissile(pSprite, 0, -(60 << 8), (Cos(pSprite->ang) >> 16) + Random2(1500), (Sin(pSprite->ang) >> 16) + Random2(1500),
                       gDudeSlope[nXSprite] + Random2(1500), kMissileLifeLeechRegular);
}

//
// the Inferno (ai_inferno.txt): a fire demon, fire in its fists and napalm from afar
//

void infernoThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, STATE("AISTATE_InfernoSearch"));
}

void infernoThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    switch (look(pSprite, pXSprite, l))
    {
    case kLookGone:
        newState(pSprite, pXSprite, STATE("AISTATE_InfernoSearch"));
        return;
    case kLookLost:
        lostTarget(pSprite, pXSprite, STATE("AISTATE_InfernoGoto"));
        return;
    }
    if (Random(100) <= 25 && l.nDist > 4 * kMeter && l.nDist < 20 * kMeter && l.nAngle < 85)
        newState(pSprite, pXSprite, STATE("AISTATE_InfernoFire"));
    if (l.nDist < 4 * kMeter && l.nAngle < 85)
        newState(pSprite, pXSprite, STATE("AISTATE_InfernoAttack"));
}

void infernoAttack(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    soundOf(pSprite, SFX("SFX_DEMMEL1"), 1);
    actFireMissile(pSprite, 0, (25 << 8), Cos(pSprite->ang) >> 16, Sin(pSprite->ang) >> 16, gDudeSlope[nXSprite], kMissileFlameSpray);
}

void infernoFire(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (!pSprite)
        return;
    sound(pSprite, SFX("SFX_BLASTER"));
    actFireMissile(pSprite, 30, -(20 << 8), Cos(pSprite->ang) >> 16, Sin(pSprite->ang) >> 16, gDudeSlope[nXSprite], kMissileFireballNapalm);
}

//
// the worm (ai_worm.txt): out of the gory bodies, it bites your feet
//

void wormThinkGoto(spritetype *pSprite, XSPRITE *pXSprite)
{
    thinkGoto(pSprite, pXSprite, STATE("AISTATE_WormSearch"));
}

void wormThinkChase(spritetype *pSprite, XSPRITE *pXSprite)
{
    LOOK l;
    switch (look(pSprite, pXSprite, l))
    {
    case kLookGone:
        newState(pSprite, pXSprite, STATE("AISTATE_WormSearch"));
        return;
    case kLookLost:
        lostTarget(pSprite, pXSprite, STATE("AISTATE_WormGoto"));
        return;
    }
    if (l.nDist < (int)(1.8 * kMeter) && l.nAngle < 85)
        newState(pSprite, pXSprite, STATE("AISTATE_WormBite"));
}

void wormBite(int, int nXSprite)
{
    spritetype *pSprite = spriteOf(nXSprite);
    if (pSprite)
        fireAtTarget(pSprite, &xsprite[nXSprite], "BV_WORM");
}

BECCALLBACK const gCallbacks[] = {
    { "CALLBACK_THINKTARGET", thinkTarget, NULL },
    { "CALLBACK_AI_MOVEFORWARD", aiMoveForward, NULL },
    { "CALLBACK_AI_MOVETURN", aiMoveTurn, NULL },
    { "CALLBACK_AI_MOVEDODGE", aiMoveDodge, NULL },
    { "CALLBACK_AI_SEARCH", thinkSearch, NULL },
    { "CALLBACK_AI_RAT_THINK_SEARCH", thinkSearch, NULL },
    { "CALLBACK_AI_RAT_THINK_GOTO", ratThinkGoto, NULL },
    { "CALLBACK_AI_RAT_THINK_CHASE", ratThinkChase, NULL },
    { "CALLBACK_AI_RAT_BITE", NULL, ratBite },
    { "CALLBACK_AI_ZOMBIESKELETAL_THINK_GOTO", skeletalThinkGoto, NULL },
    { "CALLBACK_AI_ZOMBIESKELETAL_THINK_CHASE", skeletalThinkChase, NULL },
    { "CALLBACK_AI_ZOMBIESKELETAL_THINK_PONDER", skeletalThinkPonder, NULL },
    { "CALLBACK_AI_ZOMBIESKELETAL_RECOIL", skeletalRecoil, NULL },
    { "CALLBACK_AI_ZOMBIESKELETAL_EARTH_ENTRY", earthEntry, NULL },
    { "CALLBACK_AI_ZOMBIESKELETAL_EARTH_RISEN", skeletalEarthRisen, NULL },
    { "CALLBACK_AI_ZOMBIESKELETAL_HACK", NULL, skeletalHack },
    { "CALLBACK_AI_ZOMBIESKELETAL_THROW", NULL, skeletalThrow },
    { "CALLBACK_AI_ZOMBIESKELETAL_LATCH", NULL, skeletalLatch },
    { "CALLBACK_AI_CRIMSON_THINK_GOTO", crimsonThinkGoto, NULL },
    { "CALLBACK_AI_CRIMSON_THINK_CHASE", crimsonThinkChase, NULL },
    { "CALLBACK_AI_CRIMSON_THINK_PONDER", crimsonThinkPonder, NULL },
    { "CALLBACK_AI_CRIMSON_RECOIL", crimsonRecoil, NULL },
    { "CALLBACK_AI_CRIMSON_EARTH_ENTRY", earthEntry, NULL },
    { "CALLBACK_AI_CRIMSON_EARTH_RISEN", crimsonEarthRisen, NULL },
    { "CALLBACK_AI_CRIMSON_HACK", NULL, crimsonHack },
    { "CALLBACK_AI_CRIMSON_PUKE", NULL, crimsonPuke },
    { "CALLBACK_AI_SLIME_THINK_GOTO", slimeThinkGoto, NULL },
    { "CALLBACK_AI_SLIME_THINK_CHASE", slimeThinkChase, NULL },
    { "CALLBACK_AI_SLIME_THINK_PONDER", slimeThinkPonder, NULL },
    { "CALLBACK_AI_SLIME_RECOIL", slimeRecoil, NULL },
    { "CALLBACK_AI_SLIME_FIRE", NULL, slimeFire },
    { "CALLBACK_AI_DOG_GOTO", dogThinkGoto, NULL },
    { "CALLBACK_AI_DOG_CHASE", dogThinkChase, NULL },
    { "CALLBACK_AI_DOG_BITE", NULL, dogBite },
    { "CALLBACK_AI_MONSTERDOG_GOTO", dogThinkGoto, NULL },
    { "CALLBACK_AI_MONSTERDOG_CHASE", dogThinkChase, NULL },
    { "CALLBACK_AI_MONSTERDOG_BITE", NULL, dogBite },
    { "CALLBACK_AI_NECRO_THINK_GOTO", necroThinkGoto, NULL },
    { "CALLBACK_AI_NECRO_THINK_CHASE", necroThinkChase, NULL },
    { "CALLBACK_AI_NECRO_THINK_PONDER", necroThinkPonder, NULL },
    { "CALLBACK_AI_NECRO_RECOIL", necroRecoil, NULL },
    { "CALLBACK_AI_NECRO_FIRE", NULL, necroFire },
    { "CALLBACK_AI_LICH_THINK_GOTO", necroThinkGoto, NULL },
    { "CALLBACK_AI_LICH_THINK_CHASE", necroThinkChase, NULL },
    { "CALLBACK_AI_LICH_THINK_PONDER", necroThinkPonder, NULL },
    { "CALLBACK_AI_LICH_RECOIL", necroRecoil, NULL },
    { "CALLBACK_AI_LICH_FIRE", NULL, necroFire },
    { "CALLBACK_AI_LICH_SPAWN", NULL, lichSpawn },
    { "CALLBACK_NECROSTONERISE", NULL, necroStoneRise },
    { "CALLBACK_AI_GORE_THINK_GOTO", goreThinkGoto, NULL },
    { "CALLBACK_AI_GORE_THINK_CHASE", goreThinkChase, NULL },
    { "CALLBACK_AI_GORE_BITE", NULL, goreBite },
    { "CALLBACK_AI_GORE_SPIT", NULL, goreSpit },
    { "CALLBACK_AI_FLESHT_THINK_GOTO", fleshThinkGoto, NULL },
    { "CALLBACK_AI_FLESHT_THINK_CHASE", fleshThinkChase, NULL },
    { "CALLBACK_AI_FLESHT_FIRE", NULL, fleshFire },
    { "CALLBACK_AI_INFERNO_THINK_GOTO", infernoThinkGoto, NULL },
    { "CALLBACK_AI_INFERNO_THINK_CHASE", infernoThinkChase, NULL },
    { "CALLBACK_AI_INFERNO_ATTACK", NULL, infernoAttack },
    { "CALLBACK_AI_INFERNO_FIRE", NULL, infernoFire },
    { "CALLBACK_AI_WORM_THINK_SEARCH", thinkTarget, NULL },
    { "CALLBACK_AI_WORM_THINK_GOTO", wormThinkGoto, NULL },
    { "CALLBACK_AI_WORM_THINK_CHASE", wormThinkChase, NULL },
    { "CALLBACK_AI_WORM_BITE", NULL, wormBite },
};

//
// waking up (ai_common.txt A_AIActivate)
//

struct ACTIVATION
{
    short nType;
    char const *pzSearch, *pzChase;
    char const *pzSpotSfx;
    int nSpotChance;  // of 0x10000
};

ACTIVATION const gActivations[] = {
    { kDudeBecZombieSkeletal, "AISTATE_ZombieSkeletalSearch", "AISTATE_ZombieSkeletalChase", "SFX_SZOMSEE", 0x10000 - 20480 },
    { kDudeBecCrimson, "AISTATE_CrimsonSearch", "AISTATE_CrimsonChase", "SFX_CRISPOT", 0x10000 - 20480 },
    { kDudeBecZomSlime, "AISTATE_SlimeSearch", "AISTATE_SlimeChase", "SFX_CRISPOT", 0x10000 - 20480 },
    { kDudeBecZomHalf, "AISTATE_ZHalfSearch", "AISTATE_ZHalfChase", "SFX_AZOMSPOT", 0x10000 },
    { kDudeBecZomHead, "AISTATE_ZHeadSearch", "AISTATE_ZHeadChase", "SFX_AZOMSPOT", 0x10000 / 20 },
    { kDudeBecZomSkeletalHead, "AISTATE_ZSHeadSearch", "AISTATE_ZSHeadChase", "SFX_SZOMSEE", 0x10000 / 20 },
    { kDudeBecDog, "AISTATE_DogSearch", "AISTATE_DogChase", "SFX_DOGSEE", 0x10000 },
    { kDudeBecMonsterDog, "AISTATE_MonsterDogSearch", "AISTATE_MonsterDogChase", "SFX_MOGSEE", 0x10000 },
    { kDudeBecNecromancer, "AISTATE_NecroSearch", "AISTATE_NecroChase", "SFX_NMSPOT1", 0x2000 },
    { kDudeBecLich, "AISTATE_LichSearch", "AISTATE_LichChase", "SFX_LICHSPO", 0x2000 },
    { kDudeBecGoreTremor, "AISTATE_GoreSearch", "AISTATE_GoreChase", NULL, 0 },
    { kDudeBecFleshTentacle, "AISTATE_FleshTSearch", "AISTATE_FleshTChase", NULL, 0 },
    { kDudeBecInferno, "AISTATE_InfernoSearch", "AISTATE_InfernoChase", "SFX_INFSPOT", 0x10000 },
    { kDudeBecSpirit, "AISTATE_SpiritSearch", "AISTATE_SpiritSearch", NULL, 0 },
    { kDudeBecWorm, "AISTATE_WormSearch", "AISTATE_WormChase", NULL, 0 },
};

//
// dying
//

int deathSfx(int nType)
{
    switch (nType)
    {
    case kDudeBecZombieSkeletal:
    case kDudeBecZombieSkeletalEarth:
    case kDudeBecZombieSkeletalSleep:
        return SFX("SFX_SZOMDIE");
    case kDudeBecCrimson:
    case kDudeBecCrimsonEarth:
    case kDudeBecZomSlime:
    {
        int const n = SFX("SFX_CRIDIE1");
        return n >= 0 ? n + Random(3) : -1;
    }
    case kDudeBecDog:
        return SFX("SFX_DOGDIE");
    case kDudeBecMonsterDog:
        return SFX("SFX_MOGDIE");
    case kDudeBecNecromancer:
        return SFX("SFX_NMDIE");
    case kDudeBecLich:
        return SFX("SFX_LICHD1");
    case kDudeBecZomHalf:
    case kDudeBecZomHead:
        return SFX("SFX_AZOMDIE1");
    }
    return -1;
}

// Blood's bodies turn into gibs when shot (nDudeToGibClient1); the mod's do too (its CALLBACK_DUDE_TO_GIB)
void corpseSeq(spritetype *pSprite, int nSeq)
{
    playSeq(pSprite, nSeq, nDudeToGibClient1);
}

//
// allies: the gargoyles of the Mourning Codex, the dead the Vile Anathema raises
//

bool isEnemy(spritetype *pSprite)
{
    return pSprite->statnum == kStatDude && !IsPlayerSprite(pSprite) && !becIsAlly(pSprite->index) && pSprite->extra > 0
        && xsprite[pSprite->extra].health > 0;
}

// the nearest enemy it can see, -1 for none
int nearestEnemy(spritetype *pSprite, int nRange)
{
    int nBest = -1, nBestDist = nRange;
    int const nEye = pSprite->z - (24 << 8);
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pOther = &sprite[nSprite];
        if (pOther == pSprite || !isEnemy(pOther))
            continue;
        int const nDist = approxDist(pOther->x - pSprite->x, pOther->y - pSprite->y);
        if (nDist >= nBestDist || !cansee(pSprite->x, pSprite->y, nEye, pSprite->sectnum, pOther->x, pOther->y, pOther->z - (24 << 8), pOther->sectnum))
            continue;
        nBest = nSprite, nBestDist = nDist;
    }
    return nBest;
}

void allyThink(spritetype *pSprite, XSPRITE *pXSprite)
{
    int const nSprite = pSprite->index;
    if (pXSprite->health <= 0)
        return;
    if (gAllyTime[nSprite] && (int)gFrameClock >= gAllyTime[nSprite])  // its time is up: back where it came from
    {
        gAllyTime[nSprite] = 0;
        gBecFlags[nSprite] &= ~kBecFlagAlly;
        sound(pSprite, SFX("SFX_NECREVI"));
        gFX.fxSpawn(FX_29, pSprite->sectnum, pSprite->x, pSprite->y, pSprite->z, 0);
        seqKill(3, pSprite->extra);
        actPostSprite(nSprite, kStatFree);
        return;
    }
    if ((nSprite + gFrame) & 7)  // a look around now and then
        return;
    spritetype *pTarget = becTarget(pXSprite);
    if (pTarget && isEnemy(pTarget))
        return;
    int const nEnemy = nearestEnemy(pSprite, 40 * kMeter);
    if (nEnemy >= 0)
    {
        aiSetTarget(pXSprite, nEnemy);
        aiActivateDude(pSprite, pXSprite);
        return;
    }
    // none about: after Caleb
    if (pSprite->owner < 0 || pSprite->owner >= kMaxSprites)
        return;
    spritetype *pOwner = &sprite[pSprite->owner];
    if (approxDist(pOwner->x - pSprite->x, pOwner->y - pSprite->y) > 6 * kMeter)
    {
        aiSetTarget(pXSprite, pOwner->x, pOwner->y, pOwner->z);
        aiActivateDude(pSprite, pXSprite);
    }
}

// what's left of a dead enemy: one of the mod's things, picked up with USE (becweapons.cpp)
void dropItem(spritetype *pSprite, int nPicnum, int nRepeat)
{
    int ceilZ, floorZ;
    getzsofslope(pSprite->sectnum, pSprite->x, pSprite->y, &ceilZ, &floorZ);
    spritetype *pItem = actSpawnSprite(pSprite->sectnum, pSprite->x + Random2(64), pSprite->y + Random2(64), floorZ, kStatDecoration, 0);
    if (!pItem)
        return;
    pItem->picnum = nPicnum;
    pItem->xrepeat = pItem->yrepeat = nRepeat;
    pItem->ang = Random(2048);
    pItem->cstat = 0;
    pItem->type = 0;
    gBecFlags[pItem->index] = 0;
}

} // namespace

void becMakeAlly(spritetype *pSprite, spritetype *pOwner, int nTicks)
{
    gBecFlags[pSprite->index] |= kBecFlagAlly;
    pSprite->owner = pOwner->index;
    gAllyTime[pSprite->index] = nTicks > 0 ? (int)gFrameClock + nTicks : 0;
    if (pSprite->extra > 0)
    {
        XSPRITE *pXSprite = &xsprite[pSprite->extra];
        int const nEnemy = nearestEnemy(pSprite, 40 * kMeter);
        if (nEnemy >= 0)
            aiSetTarget(pXSprite, nEnemy);
        else
            aiSetTarget(pXSprite, pOwner->x, pOwner->y, pOwner->z);
        aiActivateDude(pSprite, pXSprite);
    }
}

bool becIgnoreAttacker(spritetype *pSprite, int nSource)
{
    if (!becIsAlly(pSprite->index) || nSource < 0 || nSource >= kMaxSprites)
        return false;
    spritetype *pSource = &sprite[nSource];
    return IsPlayerSprite(pSource) || (becIsAlly(nSource) && pSource->statnum == kStatDude);
}

int becRaiseCorpses(spritetype *pCaster, int nRange)
{
    int nCount = 0;
    for (int nSprite = headspritestat[kStatThing]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pCorpse = &sprite[nSprite];
        if (pCorpse->type != kThingBloodChunks || pCorpse->extra <= 0 || gReviveTime[nSprite])
            continue;
        REVIVABLE const *pRevive = revivableFor(pCorpse->inittype);
        if (!pRevive || underwater(pCorpse->sectnum) || seqGetStatus(3, pCorpse->extra) >= 0)
            continue;
        if (approxDist(pCorpse->x - pCaster->x, pCorpse->y - pCaster->y) > nRange
            || !cansee(pCaster->x, pCaster->y, pCaster->z - (32 << 8), pCaster->sectnum, pCorpse->x, pCorpse->y, pCorpse->z - (8 << 8), pCorpse->sectnum))
            continue;
        playSeq(pCorpse, becDefine(pRevive->pzSeq, -1));
        gFX.fxSpawn(FX_29, pCorpse->sectnum, pCorpse->x, pCorpse->y, pCorpse->z, 0);
        gBecFlags[nSprite] = kBecFlagAlly;
        pCorpse->owner = pCaster->index;
        gReviveTime[nSprite] = (int)gFrameClock + kReviveTime;
        nCount++;
    }
    return nCount;
}

void becCorpseSeq(spritetype *pSprite, char const *pzSeq)
{
    int const nSeq = becDefine(pzSeq, -1);
    if (nSeq >= 0 && pSprite->extra > 0)
        corpseSeq(pSprite, nSeq);
}

BECCALLBACK const *becFindCallback(char const *pzName)
{
    for (auto const &c : gCallbacks)
        if (!Bstrcasecmp(c.pzName, pzName))
            return &c;
    return NULL;
}

int becSfx(char const *pzName)
{
    return becDefine(pzName, -1);
}

int becSeq(char const *pzName)
{
    return becDefine(pzName, -1);
}

spritetype *becTarget(XSPRITE *pXSprite)
{
    return pXSprite->target >= 0 && pXSprite->target < kMaxSprites ? &sprite[pXSprite->target] : NULL;
}

void becAiInit(spritetype *pSprite, XSPRITE *pXSprite)
{
    switch (pSprite->type)
    {
    case kDudeBecZombieSkeletalEarth:
        newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalEarthIdle"));
        pSprite->cstat &= ~CSTAT_SPRITE_BLOCK;
        pSprite->flags = 7;
        break;
    case kDudeBecCrimsonEarth:
        newState(pSprite, pXSprite, STATE("AISTATE_CrimsonEarthIdle"));
        pSprite->cstat &= ~CSTAT_SPRITE_BLOCK;
        pSprite->flags = 7;
        break;
    case kDudeBecZombieSkeletalSleep:
        newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalSleep"));
        pSprite->flags = 7;
        break;
    case kDudeBecNecroStone:
        pSprite->cstat &= ~(CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);
        break;
    case kDudeBecSpirit:  // it flies through, untouchable
        pSprite->cstat &= ~(CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);
        pSprite->cstat |= CSTAT_SPRITE_TRANSLUCENT;
        break;
    }
}

void becAiActivate(spritetype *pSprite, XSPRITE *pXSprite)
{
    switch (pSprite->type)
    {
    case kDudeBecZombieSkeletalEarth:
        if (becInState(pXSprite, "AISTATE_ZombieSkeletalEarthIdle"))
            newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalEarthRise"));
        return;
    case kDudeBecCrimsonEarth:
        if (becInState(pXSprite, "AISTATE_CrimsonEarthIdle"))
            newState(pSprite, pXSprite, STATE("AISTATE_CrimsonEarthRise"));
        return;
    case kDudeBecZombieSkeletalSleep:
        if (becInState(pXSprite, "AISTATE_ZombieSkeletalSleep"))
            newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalWoke"));
        return;
    case kDudeBecNecroStone:
        return;
    }
    for (auto const &a : gActivations)
    {
        if (a.nType != pSprite->type)
            continue;
        if (pXSprite->target == -1)
            newState(pSprite, pXSprite, becState(a.pzSearch));
        else
        {
            if (a.pzSpotSfx && Chance(a.nSpotChance))
                sound(pSprite, becDefine(a.pzSpotSfx, -1));
            newState(pSprite, pXSprite, becState(a.pzChase));
        }
        return;
    }
}

// what being hurt does to them (ai_common.txt A_AIOnDamage); after Blood's own reactions
void becAiDamage(spritetype *pSprite, XSPRITE *pXSprite, int nSource, DAMAGE_TYPE nDmgType, int nDamage)
{
    UNREFERENCED_PARAMETER(nSource);
    UNREFERENCED_PARAMETER(nDamage);
    if (pXSprite->health == 0)
        return;
    int const r = Random(100);
    int const nSprite = pSprite->index;
    // the Lesser Priest's beast is the Lesser Beast
    if ((gBecFlags[nSprite] & kBecFlagLesser) && pSprite->type == kDudeBeast)
    {
        gBecFlags[nSprite] &= ~kBecFlagLesser;
        BECBLOCK const *pBeast2 = becDefActor("DUDE_Beast2");
        pXSprite->health = clamp(becInt(pBeast2, "actor.health", 50), 1, 4095) << 4;
        if (r <= 50)
            sound(pSprite, SFX("SFX_CULTRAN"));
    }
    if (!becIsDude(pSprite->type))
        return;
    DUDEINFO *pDudeInfo = getDudeInfo(pSprite->type);
    // burning: they become Blood's burning ones
    if (nDmgType == kDamageBurn && pXSprite->health <= (unsigned int)pDudeInfo->fleeHealth << 4)
    {
        int nBurning = -1;
        switch (pSprite->type)
        {
        case kDudeBecZombieSkeletal:
        case kDudeBecZombieSkeletalEarth:
        case kDudeBecZombieSkeletalSleep:
            nBurning = kDudeBurningZombieAxe;
            sound(pSprite, SFX("SFX_SZOMPAI"));
            break;
        case kDudeBecNecromancer:
            nBurning = kDudeBurningCultist;
            break;
        case kDudeBecCrimson:
        case kDudeBecCrimsonEarth:
        case kDudeBecZomSlime:
            nBurning = kDudeBurningInnocent;
            sound(pSprite, SFX("SFX_CRIFIRE"));
            break;
        }
        if (nBurning >= 0)
        {
            sfxPlay3DSound(pSprite, 361, -1, 0);
            pSprite->type = nBurning;
            pSprite->cstat |= CSTAT_SPRITE_BLOCK;
            aiNewState(pSprite, pXSprite, nBurning == kDudeBurningZombieAxe ? &zombieABurnGoto : &cultistBurnGoto);
            actHealDude(pXSprite, dudeInfo[nBurning - kDudeBase].startHealth, dudeInfo[nBurning - kDudeBase].startHealth);
            gDudeExtra[pSprite->extra].clock = (int)gFrameClock + 360;
            evKill(nSprite, 3, kCallbackFXFlameLick);
            return;
        }
    }
    switch (pSprite->type)
    {
    case kDudeBecGoreTremor:
        xvel[nSprite] = yvel[nSprite] = zvel[nSprite] = 0;
        break;
    case kDudeBecDog:
        if (r <= 50)
        {
            sound(pSprite, SFX("SFX_DOGPAI"));
            newState(pSprite, pXSprite, STATE("AISTATE_DogIdle"));
            xvel[nSprite] = yvel[nSprite] = 0;
        }
        break;
    case kDudeBecMonsterDog:
        if (r <= 25)
        {
            newState(pSprite, pXSprite, STATE("AISTATE_MonsterDogPain"));
            xvel[nSprite] = yvel[nSprite] = 0;
        }
        break;
    case kDudeBecZombieSkeletal:
        if (nDmgType == kDamageTesla && r <= 75)
            newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalShock")), xvel[nSprite] = yvel[nSprite] = 0;
        else if (nDmgType == kDamageBullet && r <= 50)
        {
            newState(pSprite, pXSprite, STATE("AISTATE_ZombieSkeletalRecoil"));
            sound(pSprite, SFX("SFX_SZOMPAI"));
            xvel[nSprite] = yvel[nSprite] = 0;
        }
        break;
    case kDudeBecCrimson:
        if (nDmgType == kDamageTesla && r <= 75)
            newState(pSprite, pXSprite, STATE("AISTATE_CrimsonShock")), xvel[nSprite] = yvel[nSprite] = 0;
        else if (nDmgType == kDamageBullet && r <= 50)
            newState(pSprite, pXSprite, STATE("AISTATE_CrimsonRecoil")), xvel[nSprite] = yvel[nSprite] = 0;
        break;
    case kDudeBecZomSlime:
        if (nDmgType == kDamageBullet && r <= 50)
            newState(pSprite, pXSprite, STATE("AISTATE_SlimeRecoil")), xvel[nSprite] = yvel[nSprite] = 0;
        break;
    case kDudeBecNecromancer:
    case kDudeBecLich:
    {
        bool const bLich = pSprite->type == kDudeBecLich;
        if (nDmgType == kDamageBullet && r <= (bLich ? 25 : 50))
            newState(pSprite, pXSprite, bLich ? STATE("AISTATE_LichRecoil") : STATE("AISTATE_NecroRecoil")), xvel[nSprite] = yvel[nSprite] = 0;
        else if (nDmgType != kDamageSpirit && r <= 3 && becTarget(pXSprite))
            necroTeleport(pSprite, becTarget(pXSprite), bLich);
        break;
    }
    }
}

bool becAiRecoil(spritetype *pSprite, XSPRITE *pXSprite)
{
    // the mod's recoil (its ai.recoilState / ai.shockState), with Blood's hit sounds
    char const *pzRecoil = NULL, *pzShock = NULL;
    switch (pSprite->type)
    {
    case kDudeBecZombieSkeletal: pzRecoil = "AISTATE_ZombieSkeletalRecoil", pzShock = "AISTATE_ZombieSkeletalShock"; break;
    case kDudeBecCrimson: pzRecoil = "AISTATE_CrimsonRecoil", pzShock = "AISTATE_CrimsonShock"; break;
    case kDudeBecZomSlime: pzRecoil = "AISTATE_SlimeRecoil", pzShock = "AISTATE_SlimeShock"; break;
    case kDudeBecZomHalf: pzRecoil = "AISTATE_ZHalfRecoil"; break;
    case kDudeBecZomHead: pzRecoil = "AISTATE_ZHeadRecoil"; break;
    case kDudeBecZomSkeletalHead: pzRecoil = "AISTATE_ZSHeadRecoil"; break;
    case kDudeBecNecromancer: pzRecoil = "AISTATE_NecroRecoil"; break;
    case kDudeBecLich: pzRecoil = "AISTATE_LichRecoil"; break;
    case kDudeBecMonsterDog: pzRecoil = "AISTATE_MonsterDogPain"; break;
    case kDudeBecZombieSkeletalEarth:
    case kDudeBecCrimsonEarth:
    case kDudeBecZombieSkeletalSleep:
    case kDudeBecNecroStone:
    case kDudeBecGoreTremor:
    case kDudeBecFleshTentacle:
    case kDudeBecInferno:
    case kDudeBecSpirit:
    case kDudeBecWorm:
    case kDudeBecDog:
        return true;  // no recoil
    }
    char const *pzState = gDudeExtra[pSprite->extra].teslaHit && pzShock ? pzShock : pzRecoil;
    if (pzState)
        newState(pSprite, pXSprite, becState(pzState));
    return true;
}

bool becAiKillFirst(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType, int nDamage)
{
    UNREFERENCED_PARAMETER(nKiller);
    UNREFERENCED_PARAMETER(nDamage);
    if (!becIsDude(pSprite->type) || nDmgType != kDamageBurn || pXSprite->medium != kMediumNormal)
        return false;
    // burnt to death: the human ones run around burning first, like Blood's
    int nBurning;
    switch (pSprite->type)
    {
    case kDudeBecZombieSkeletal:
    case kDudeBecZombieSkeletalEarth:
    case kDudeBecZombieSkeletalSleep:
        nBurning = kDudeBurningZombieAxe;
        break;
    case kDudeBecNecromancer:
        nBurning = kDudeBurningCultist;
        break;
    case kDudeBecCrimson:
    case kDudeBecCrimsonEarth:
    case kDudeBecZomSlime:
        nBurning = kDudeBurningInnocent;
        break;
    default:
        return false;
    }
    pSprite->type = nBurning;
    aiNewState(pSprite, pXSprite, nBurning == kDudeBurningZombieAxe ? &zombieABurnGoto : nBurning == kDudeBurningInnocent ? &innocentBurnGoto : &cultistBurnGoto);
    actHealDude(pXSprite, dudeInfo[nBurning - kDudeBase].startHealth, dudeInfo[nBurning - kDudeBase].startHealth);
    return true;
}

int becAiKill(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType, int nDamage)
{
    UNREFERENCED_PARAMETER(nKiller);
    UNREFERENCED_PARAMETER(nDamage);
    int const nType = pSprite->type;
    int const nBase = getDudeInfo(nType)->seqStartID;
    int const r = Random(100);
    // where the last bullet hit (becVectorHit) counts for a death by bullets
    bool const bHead = nDmgType == kDamageBullet && (gBecFlags[pSprite->index] & kBecFlagHeadShot) != 0;
    bool const bBelly = nDmgType == kDamageBullet && (gBecFlags[pSprite->index] & kBecFlagBellyShot) != 0;
    sound(pSprite, deathSfx(nType));
    switch (nType)
    {
    case kDudeBecZombieSkeletal:
    case kDudeBecZombieSkeletalEarth:
    case kDudeBecZombieSkeletalSleep:
        if (nDmgType == kDamageSpirit)
            corpseSeq(pSprite, SEQ("SEQ_ZOMBSD5"));
        else if (nDmgType == kDamageExplode)
        {
            corpseSeq(pSprite, nBase + 2);
            soundOf(pSprite, SFX("SFX_BODEXP1"), 2);
            gore(pSprite, 4, true);
        }
        else if (bHead)
        {
            corpseSeq(pSprite, SEQ("SEQ_ZOMBSD4"));
            sound(pSprite, SFX("SFX_BLDSPRY1"));
            gore(pSprite, 3, false);
            // its head flies off and, a quarter of the time, keeps biting
            if (r <= 25 && becDudesActive())
            {
                int top, bottom;
                GetSpriteExtents(pSprite, &top, &bottom);
                spritetype *pHead = becSpawnDude(pSprite, kDudeBecZomSkeletalHead, pSprite->x, pSprite->y, top, pSprite->ang);
                if (pHead)
                {
                    gKillMgr.AddCount(pHead);
                    zvel[pHead->index] = -0x60000;
                }
            }
        }
        else
            corpseSeq(pSprite, nBase + 1);
        return kBecKillCorpse;
    case kDudeBecCrimson:
    case kDudeBecCrimsonEarth:
        if (nDmgType == kDamageExplode || (nDmgType == kDamageSpirit && r > 50))
        {
            corpseSeq(pSprite, SEQ("SEQ_CRIMSD2"));
            soundOf(pSprite, SFX("SFX_BODEXP1"), 2);
            gore(pSprite, 6, true);
            if (Random(100) <= 50)
                spawnWorms(pSprite, Random(10));
        }
        else if (nDmgType == kDamageSpirit || bHead)
        {
            corpseSeq(pSprite, SEQ("SEQ_CRIMSHS"));
            sound(pSprite, SFX("SFX_GORHEAD"));
            gore(pSprite, Random(10), false);
        }
        else if (bBelly)
        {
            corpseSeq(pSprite, SEQ("SEQ_CRIMSBS"));
            soundOf(pSprite, SFX("SFX_GORRAN1"), 2);
            gore(pSprite, 3, false);
        }
        else
            corpseSeq(pSprite, SEQ("SEQ_CRIMSD1"));
        return kBecKillCorpse;
    case kDudeBecZomHalf:
        if (nDmgType == kDamageBurn)
            corpseSeq(pSprite, SEQ("SEQ_HAZOMD3"));
        else if (nDmgType == kDamageSpirit)
            corpseSeq(pSprite, SEQ("SEQ_HAZOMSD"));
        else if (nDmgType == kDamageExplode)
        {
            gore(pSprite, 4, true);
            return kBecKillGone;
        }
        else if (bHead)
        {
            corpseSeq(pSprite, SEQ("SEQ_HAZOMD1"));
            sound(pSprite, SFX("SFX_GORHEAD"));
            gore(pSprite, 2, false);
        }
        else
            corpseSeq(pSprite, SEQ("SEQ_HAZOMD2"));
        return kBecKillCorpse;
    case kDudeBecZomHead:
        if (nDmgType == kDamageBurn)
            corpseSeq(pSprite, SEQ("SEQ_HEZOMD2"));
        else if (nDmgType == kDamageSpirit)
            corpseSeq(pSprite, SEQ("SEQ_HEZOMSD"));
        else if (nDmgType == kDamageBullet || nDmgType == kDamageFall)
        {
            sound(pSprite, SFX("SFX_GORHEAD"));
            gore(pSprite, Random(3), false);
            corpseSeq(pSprite, SEQ("SEQ_HEZOMD1"));
        }
        else
        {
            sound(pSprite, SFX("SFX_GORHEAD"));
            gore(pSprite, Random(3), false);
            return kBecKillGone;
        }
        return kBecKillCorpse;
    case kDudeBecZomSkeletalHead:
        sound(pSprite, SFX("SFX_GORHEAD"));
        gore(pSprite, Random(3), false);
        return kBecKillGone;
    case kDudeBecZomSlime:
        if (nDmgType != kDamageExplode)
        {
            corpseSeq(pSprite, SEQ("SEQ_ZOMSLIMD"));
            return kBecKillCorpse;
        }
        pukeBlobs(pSprite, pXSprite, 12, -(16 << 8));
        gore(pSprite, 6, true);
        return kBecKillGone;
    case kDudeBecDog:
        if (nDmgType == kDamageBullet || nDmgType == kDamageFall || nDmgType == kDamageTesla)
            corpseSeq(pSprite, r <= 50 ? SEQ("SEQ_DOGDIE1") : SEQ("SEQ_DOGDIE1B"));
        else if (nDmgType == kDamageBurn)
            corpseSeq(pSprite, r <= 50 ? SEQ("SEQ_DOGDIE2") : SEQ("SEQ_DOGDIE2B"));
        else if (nDmgType == kDamageSpirit)  // it turns into a Monster Dog
        {
            playSeq(pSprite, SEQ("SEQ_DOGTURN"));
            gDeathTimer[pSprite->index] = 200;
        }
        else
        {
            gore(pSprite, 4, true);
            return kBecKillGone;
        }
        return kBecKillCorpse;
    case kDudeBecMonsterDog:
        if (r <= 50)
            pSprite->cstat |= CSTAT_SPRITE_XFLIP;
        else
            pSprite->cstat &= ~CSTAT_SPRITE_XFLIP;
        corpseSeq(pSprite, SEQ("SEQ_MOGDIE"));
        return kBecKillCorpse;
    case kDudeBecNecromancer:
        // the Necromancer's gifts: the Vile Anathema or the Bastion Chronometer (not here yet), and its
        // headstones crumble
        for (int nSprite = headspritestat[kStatThing]; nSprite >= 0; nSprite = nextspritestat[nSprite])
            if (gBecFlags[nSprite] & kBecFlagStone)
                actDamageSprite(pSprite->index, &sprite[nSprite], kDamageExplode, 1000 << 4);
        if (nDmgType == kDamageExplode)
        {
            corpseSeq(pSprite, SEQ("SEQ_NECROD2"));
            soundOf(pSprite, SFX("SFX_BODEXP1"), 2);
            gore(pSprite, 4, true);
        }
        else
            corpseSeq(pSprite, SEQ("SEQ_NECROD1"));
        if (!underwater(pSprite->sectnum))
            gDeathTimer[pSprite->index] = 125;  // 40% of them come back as a Lich
        return kBecKillCorpse;
    case kDudeBecLich:
        for (int nSprite = headspritestat[kStatThing]; nSprite >= 0; nSprite = nextspritestat[nSprite])
            if (gBecFlags[nSprite] & kBecFlagStone)
                actDamageSprite(pSprite->index, &sprite[nSprite], kDamageExplode, 1000 << 4);
        corpseSeq(pSprite, SEQ("SEQ_LICHD1"));
        return kBecKillCorpse;
    case kDudeBecGoreTremor:
        soundOf(pSprite, SFX("SFX_BODEXP1"), 2);
        sound(pSprite, SFX("SFX_GORDIED"));
        gore(pSprite, 6, true);
        if (Random(100) <= 50)
            spawnWorms(pSprite, Random(10));
        return kBecKillGone;
    case kDudeBecFleshTentacle:
        corpseSeq(pSprite, SEQ("SEQ_FLESHTED"));
        soundOf(pSprite, SFX("SFX_BODEXP1"), 2);
        gore(pSprite, 4, true);
        return kBecKillCorpse;
    case kDudeBecInferno:
        corpseSeq(pSprite, SEQ("SEQ_INFERD1"));
        sound(pSprite, SFX("SFX_INFSPOT"));
        return kBecKillCorpse;
    case kDudeBecWorm:
        soundOf(pSprite, SFX("SFX_WORMD1"), 2);
        gore(pSprite, Random(3), false);
        return kBecKillGone;
    case kDudeBecNecroStone:
    case kDudeBecSpirit:
        return kBecKillGone;
    }
    corpseSeq(pSprite, nBase + 1);
    return kBecKillCorpse;
}

namespace
{

// the mod's deaths for Blood's enemies (sharedCallbacks.txt A_KillThing): shot in the head, the belly
// (half the cultists lie there suffering) or the legs, melted by spirit damage, fried by tesla
struct EXTRADEATH
{
    short nType;
    char const *pzHead, *pzBelly, *pzSuffer, *pzLegs, *pzSpirit, *pzTesla;
};

EXTRADEATH const gExtraDeaths[] = {
    { kDudeCultistTommy, "SEQ_CULT1HS", "SEQ_CULT1BS", "SEQ_CU1SUF1", "SEQ_CULT1LS", "SEQ_CULT1SD", "SEQ_EDCULT1" },
    { kDudeCultistShotgun, "SEQ_CULT2HS", "SEQ_CULT2BS", "SEQ_CU2SUF1", "SEQ_CULT2LS", "SEQ_CULT2SD", "SEQ_EDCULT1" },
    { kDudeCultistTesla, "SEQ_CULT3HS", "SEQ_CULT3BS", "SEQ_CU3SUF1", "SEQ_CULT3LS", "SEQ_CULT3SD", "SEQ_EDCULT1" },
    { kDudeCultistTNT, "SEQ_CULT4HS", "SEQ_CULT4BS", "SEQ_CU4SUF1", "SEQ_CULT4LS", "SEQ_CULT4SD", "SEQ_EDCULT1" },
    { kDudeZombieAxeNormal, "SEQ_ZOMB1HS", NULL, NULL, NULL, NULL, "SEQ_EDZOMB1" },
    { kDudeZombieAxeBuried, "SEQ_ZOMB1HS", NULL, NULL, NULL, NULL, "SEQ_EDZOMB1" },
    { kDudeZombieButcher, "SEQ_ZOMB2HS", NULL, NULL, NULL, "SEQ_ZOMB2D5", "SEQ_EDZOMB2" },
    { kDudeGillBeast, "SEQ_GILL1HS", NULL, NULL, NULL, "SEQ_GILL1SD", "SEQ_EDGILL1" },
    { kDudeInnocent, "SEQ_PRIS1HS", NULL, NULL, NULL, "SEQ_PRIS1HS", "SEQ_EDPRIS1" },
    { kDudeGargoyleFlesh, "SEQ_GARG1HS", NULL, NULL, NULL, NULL, "SEQ_EDGARG1" },
    { kDudeHellHound, NULL, NULL, NULL, NULL, "SEQ_HELL1SD", "SEQ_EDHELL1" },
    { kDudeBeast, "SEQ_BEST1HS", NULL, NULL, NULL, "SEQ_BEST1SD", NULL },
    { kDudePhantasm, NULL, NULL, NULL, NULL, "SEQ_GOST1SD", NULL },
    { kDudeTinyCaleb, NULL, NULL, NULL, NULL, "SEQ_CALB1SD", NULL },
    { kDudeSpiderBrown, NULL, NULL, NULL, NULL, NULL, "SEQ_EDSPID1" },
    { kDudeSpiderRed, NULL, NULL, NULL, NULL, NULL, "SEQ_EDSPID2" },
    { kDudeSpiderBlack, NULL, NULL, NULL, NULL, NULL, "SEQ_EDSPID3" },
    { kDudePodGreen, NULL, NULL, NULL, NULL, NULL, "SEQ_EDPOD1" },
    { kDudePodFire, NULL, NULL, NULL, NULL, NULL, "SEQ_EDPOD1" },
};

void extraDeath(spritetype *pSprite, DAMAGE_TYPE nDmgType)
{
    EXTRADEATH const *pDeath = NULL;
    for (auto const &d : gExtraDeaths)
        if (d.nType == pSprite->type)
            pDeath = &d;
    if (!pDeath || pSprite->extra <= 0)
        return;
    uint8_t const flags = gBecFlags[pSprite->index];
    char const *pzSeq = NULL;
    if (nDmgType == kDamageSpirit)
        pzSeq = pDeath->pzSpirit;
    else if (nDmgType == kDamageTesla && Random(100) <= 75)
    {
        pzSeq = pDeath->pzTesla;
        if (pzSeq)
            sound(pSprite, SFX("SFX_ORBEXPL"));
    }
    else if (nDmgType == kDamageBullet || nDmgType == kDamageFall)
    {
        if ((flags & kBecFlagHeadShot) && pDeath->pzHead)
        {
            pzSeq = pDeath->pzHead;
            sound(pSprite, SFX("SFX_GORHEAD"));
            gore(pSprite, 2, false);
        }
        else if ((flags & kBecFlagBellyShot) && pDeath->pzBelly)
        {
            pzSeq = pDeath->pzSuffer && Random(100) <= 50 ? pDeath->pzSuffer : pDeath->pzBelly;
            soundOf(pSprite, SFX("SFX_GORRAN1"), 2);
            gore(pSprite, 2, false);
        }
        else if ((flags & kBecFlagLegShot) && pDeath->pzLegs)
        {
            pzSeq = pDeath->pzLegs;
            sound(pSprite, SFX("SFX_GORLEGS"));
        }
    }
    if (pzSeq)
        corpseSeq(pSprite, becDefine(pzSeq, -1));
}

} // namespace

// what the mod adds to Blood's own enemies' deaths
void becAiAfterKill(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType)
{
    UNREFERENCED_PARAMETER(nKiller);
    int const nSprite = pSprite->index;
    if (!becIsDude(pSprite->type))
        extraDeath(pSprite, nDmgType);
    // the rat with dynamite on its back
    if (gBecFlags[nSprite] & kBecFlagRatBomb)
    {
        gBecFlags[nSprite] &= ~kBecFlagRatBomb;
        explodeHere(pSprite);
    }
    // an axe zombie shot in the belly keeps crawling after you (ai_zombie.txt A_AIZombieDeath)
    int const nWas = pSprite->inittype;
    if ((nWas == kDudeZombieAxeNormal || nWas == kDudeZombieAxeBuried) && nDmgType == kDamageBullet
        && (gBecFlags[nSprite] & kBecFlagBellyShot) && becDudesActive())
    {
        spritetype *pTorso = becSpawnDude(pSprite, kDudeBecZomHalf, pSprite->x, pSprite->y, pSprite->z - (30 << 8), pSprite->ang);
        if (pTorso)
        {
            pTorso->pal = pSprite->pal;
            gKillMgr.AddCount(pTorso);
            gore(pSprite, 3, false);
            soundOf(pSprite, SFX("SFX_GORRAN1"), 2);
            if (gMe && Random(100) <= 10)
                sound(gMe->pSprite, SFX("SFX_VOHALF"));
        }
        // its legs stay where it stood
        playSeq(pSprite, SEQ("SEQ_ZOMB1TS"), nDudeToGibClient1);
    }
    // an axe zombie's head shot off keeps biting, a quarter of the time
    if ((nWas == kDudeZombieAxeNormal || nWas == kDudeZombieAxeBuried) && nDmgType == kDamageBullet
        && (gBecFlags[nSprite] & kBecFlagHeadShot) && Random(100) <= 25 && becDudesActive())
    {
        int top, bottom;
        GetSpriteExtents(pSprite, &top, &bottom);
        spritetype *pHead = becSpawnDude(pSprite, kDudeBecZomHead, pSprite->x, pSprite->y, top, pSprite->ang);
        if (pHead)
        {
            gKillMgr.AddCount(pHead);
            zvel[pHead->index] = -0x60000;
            sound(pSprite, SFX("SFX_GORHEAD"));
            gore(pSprite, 3, false);
        }
    }
    // what they leave for Caleb to pick up (USE): a Necromancer its Vile Anathema or Bastion Chronometer
    // (ai_necro.txt), a Phantasm its orb, an axe zombie its axe now and then
    if (becDudesActive() && !becIsAlly(nSprite))
    {
        if (nWas == kDudeBecNecromancer)
            dropItem(pSprite, Random(100) < 50 ? 7305 : 7353, 24);
        else if (nWas == kDudePhantasm)
            dropItem(pSprite, 5558, 24);
        else if ((nWas == kDudeZombieAxeNormal || nWas == kDudeZombieAxeBuried || nWas == kDudeZombieAxeLaying) && Random(100) < 25)
            dropItem(pSprite, 5481, 32);
    }
    gBecFlags[nSprite] &= ~(kBecFlagHeadShot | kBecFlagBellyShot | kBecFlagLegShot | kBecFlagAlly);
    gAllyTime[nSprite] = 0;
    UNREFERENCED_PARAMETER(pXSprite);
}

void becAiLevelStart(void)
{
    memset(gReviveTime, 0, sizeof(gReviveTime));
    memset(gDeathTimer, 0, sizeof(gDeathTimer));
    memset(gAllyTime, 0, sizeof(gAllyTime));
    nNextStone = 0;
    // attack dogs: next to some of the cultists (ai_cultist.txt A_AISpawnDog)
    int nDogs = 0;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pSprite = &sprite[nSprite];
        if (pSprite->type != kDudeCultistTNT && pSprite->type != kDudeCultistShotgun)
            continue;
        if (pSprite->extra <= 0 || underwater(pSprite->sectnum))
            continue;
        int const nChance = pSprite->type == kDudeCultistTNT ? 15 : 30;
        if ((int)Random(100) > nChance)
            continue;
        int const nDist = pSprite->clipdist * 3;
        int x = pSprite->x + mulscale30(nDist, Cos(pSprite->ang)), y = pSprite->y + mulscale30(nDist, Sin(pSprite->ang));
        int16_t nSector = pSprite->sectnum;
        updatesector(x, y, &nSector);
        if (nSector < 0 || !cansee(pSprite->x, pSprite->y, pSprite->z - (16 << 8), pSprite->sectnum, x, y, pSprite->z - (16 << 8), nSector))
            continue;
        if (becSpawnDude(pSprite, kDudeBecDog, x, y, pSprite->z, pSprite->ang))
            nDogs++;
    }
    if (nDogs)
        LOG_F(INFO, "Blood Extra Crispy: %d attack dogs", nDogs);
}

// the Odious Headstones while a Necromancer or Lich lives (every 3-6 minutes, near you), the corpses the
// Necromancer raises, the Lich, the Monster Dog, rats with dynamite, worms underfoot
void becAiFrame(void)
{
    int const nNow = (int)gFrameClock;
    bool bNecro = false;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (!gPlayer[p].hand)  // let go of (or never had): a hand that grabs next is Blood's
            gBecBite[p] = false;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pSprite = &sprite[nSprite];
        if (pSprite->extra <= 0)
            continue;
        XSPRITE *pXSprite = &xsprite[pSprite->extra];
        if (becIsAlly(nSprite))
        {
            allyThink(pSprite, pXSprite);
            continue;
        }
        if ((pSprite->type == kDudeBecNecromancer || pSprite->type == kDudeBecLich) && pXSprite->health > 0)
            bNecro = true;
        // the spirits start drifting about when you're near (BEC_Players.txt)
        if (pSprite->type == kDudeBecSpirit && becInState(pXSprite, "AISTATE_SpiritIdle") && gMe && gMe->pSprite
            && approxDist(gMe->pSprite->x - pSprite->x, gMe->pSprite->y - pSprite->y) < 30 * kMeter)
            newState(pSprite, pXSprite, STATE("AISTATE_SpiritSearch"));
        // a rat with dynamite runs at you and goes off
        if ((gBecFlags[nSprite] & kBecFlagRatBomb) && pSprite->type == kDudeRat && pXSprite->health > 0 && becTarget(pXSprite))
        {
            spritetype *pTarget = becTarget(pXSprite);
            if (IsPlayerSprite(pTarget) && approxDist(pTarget->x - pSprite->x, pTarget->y - pSprite->y) < (int)(2.5 * kMeter))
                actDamageSprite(nSprite, pSprite, kDamageExplode, pXSprite->health);
        }
        // worms get stepped on
        if (pSprite->type == kDudeBecWorm && pXSprite->health > 0)
        {
            for (int p = connecthead; p >= 0; p = connectpoint2[p])
            {
                spritetype *pPlayer = gPlayer[p].pSprite;
                if (!pPlayer || (xvel[pPlayer->index] == 0 && yvel[pPlayer->index] == 0))
                    continue;
                if (approxDist(pPlayer->x - pSprite->x, pPlayer->y - pSprite->y) < 32 && klabs(pPlayer->z - pSprite->z) < (48 << 8))
                {
                    sound(pPlayer, SFX("SFX_PODSQUIS"));
                    actDamageSprite(pPlayer->index, pSprite, kDamageBullet, 1000);
                }
            }
        }
    }
    for (int nSprite = headspritestat[kStatThing]; nSprite >= 0;)
    {
        int const nNext = nextspritestat[nSprite];
        spritetype *pSprite = &sprite[nSprite];
        if (gReviveTime[nSprite] && nNow >= gReviveTime[nSprite])
            reviveNow(nSprite);
        else if (gDeathTimer[nSprite] > 0 && pSprite->type == kThingBloodChunks && pSprite->extra > 0)
        {
            gDeathTimer[nSprite] -= kTicsPerFrame / 2;  // the mod's timer: 1 a tick of its, 2 of ours
            if (gDeathTimer[nSprite] <= 0)
            {
                gDeathTimer[nSprite] = 0;
                if (pSprite->inittype == kDudeBecNecromancer && Random(100) <= 40)
                {
                    // the Lich bursts out of the Necromancer's body
                    spritetype *pLich = becSpawnDude(pSprite, kDudeBecLich, pSprite->x, pSprite->y, pSprite->z, pSprite->ang);
                    if (pLich)
                    {
                        gKillMgr.AddCount(pLich);
                        soundOf(pSprite, SFX("SFX_BODEXP1"), 2);
                        gore(pSprite, 6, true);
                        actPostSprite(nSprite, kStatFree);
                    }
                }
                else if (pSprite->inittype == kDudeBecDog)
                {
                    spritetype *pMog = becSpawnDude(pSprite, kDudeBecMonsterDog, pSprite->x, pSprite->y, pSprite->z, pSprite->ang);
                    if (pMog)
                    {
                        gKillMgr.AddCount(pMog);
                        newState(pMog, &xsprite[pMog->extra], STATE("AISTATE_MonsterDogIdle"));
                        actPostSprite(nSprite, kStatFree);
                    }
                }
            }
        }
        // an Odious Headstone raises a zombie every 10 seconds while you're near
        else if ((gBecFlags[nSprite] & kBecFlagStone) && pSprite->extra > 0 && xsprite[pSprite->extra].health > 0)
        {
            XSPRITE *pXStone = &xsprite[pSprite->extra];
            bool bNear = false;
            for (int p = connecthead; p >= 0; p = connectpoint2[p])
            {
                spritetype *pPlayer = gPlayer[p].pSprite;
                if (pPlayer && approxDist(pPlayer->x - pSprite->x, pPlayer->y - pSprite->y) < 1000 * 16
                    && cansee(pSprite->x, pSprite->y, pSprite->z - (16 << 8), pSprite->sectnum, pPlayer->x, pPlayer->y, pPlayer->z, pPlayer->sectnum))
                    bNear = true;
            }
            if (bNear)
                pXStone->data1 = ClipLow(pXStone->data1 - kTicsPerFrame / 2, 0);
            if (pXStone->data1 == 290)
                pSprite->picnum = 7326;
            if (pXStone->data1 <= 0)
            {
                pSprite->picnum = 7327;
                sound(pSprite, SFX("SFX_VOOINVI"));
                int const r = Random(110);
                int const nType = r <= 40 ? (int)kDudeZombieAxeBuried : r <= 70 ? kDudeBecCrimsonEarth : kDudeBecZombieSkeletalEarth;
                int const nDist = pSprite->clipdist * 16;
                int x = pSprite->x + mulscale30(nDist, Cos(pSprite->ang)), y = pSprite->y + mulscale30(nDist, Sin(pSprite->ang));
                int16_t nSector = pSprite->sectnum;
                updatesector(x, y, &nSector);
                if (nSector >= 0)
                {
                    int ceilZ, floorZ;
                    getzsofslope(nSector, x, y, &ceilZ, &floorZ);
                    spritetype *pZombie = becSpawnDude(pSprite, nType, x, y, floorZ, pSprite->ang);
                    if (pZombie)
                    {
                        gKillMgr.AddCount(pZombie);
                        if (gMe && gMe->pSprite)
                            aiSetTarget(&xsprite[pZombie->extra], gMe->pSprite->index);
                        aiActivateDude(pZombie, &xsprite[pZombie->extra]);
                    }
                }
                pXStone->data1 = 300;
            }
        }
        nSprite = nNext;
    }
    // a new headstone near you now and then, while a Necromancer or Lich is about
    if (!bNecro)
        nNextStone = 0;
    else if (!nNextStone)
        nNextStone = nNow + (180 + (int)Random(180)) * kTicRate;
    else if (nNow >= nNextStone && gMe && gMe->pSprite && gMe->pXSprite && gMe->pXSprite->health > 0)
    {
        nNextStone = 0;
        spritetype *pPlayer = gMe->pSprite;
        int const nAngle = pPlayer->ang + Random2(256);
        int const nDist = 3 * kMeter;
        int x = pPlayer->x + mulscale30(nDist, Cos(nAngle)), y = pPlayer->y + mulscale30(nDist, Sin(nAngle));
        int16_t nSector = pPlayer->sectnum;
        updatesector(x, y, &nSector);
        if (nSector >= 0 && !underwater(nSector) && cansee(pPlayer->x, pPlayer->y, pPlayer->z, pPlayer->sectnum, x, y, pPlayer->z, nSector))
        {
            int ceilZ, floorZ;
            getzsofslope(nSector, x, y, &ceilZ, &floorZ);
            becSpawnDude(pPlayer, kDudeBecNecroStone, x, y, floorZ, (nAngle + 1024) & 2047);
        }
    }
}

class BecAiLoadSave : public LoadSave
{
    virtual void Load(void)
    {
        Read(gReviveTime, sizeof(gReviveTime));
        Read(gDeathTimer, sizeof(gDeathTimer));
        Read(&nNextStone, sizeof(nNextStone));
        Read(gAllyTime, sizeof(gAllyTime));
    }
    virtual void Save(void)
    {
        Write(gReviveTime, sizeof(gReviveTime));
        Write(gDeathTimer, sizeof(gDeathTimer));
        Write(&nNextStone, sizeof(nNextStone));
        Write(gAllyTime, sizeof(gAllyTime));
    }
};

void BecAiLoadSaveConstruct(void)
{
    new BecAiLoadSave();
}
