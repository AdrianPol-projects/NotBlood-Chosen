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

// Blood Extra Crispy's enemies (see becdudes.h): their types filled in from the mod's definitions, its
// AI states turned into Blood's, and the hooks Blood's AI and actor code call.
//
// The states: each of the mod's AISTATE_ blocks becomes one of Blood's AISTATEs (the same fields:
// animation, duration, enter/move/think functions, the animation's callback, the next state). They
// are kept in one array in the order the mod's files list them, so a saved game's pointers to them
// stay right as long as the mod's files are the same.

#include <string>
#include <unordered_map>
#include <vector>

#include "build.h"
#include "compat.h"
#include "common_game.h"
#include "actor.h"
#include "ai.h"
#include "becai.h"
#include "becdefs.h"
#include "becdudes.h"
#include "blood.h"
#include "choke.h"
#include "db.h"
#include "dude.h"
#include "eventq.h"
#include "gib.h"
#include "globals.h"
#include "levels.h"
#include "loadsave.h"
#include "osd.h"
#include "player.h"
#include "seq.h"
#include "sfx.h"
#include "trig.h"

uint8_t gBecFlags[kMaxSprites];

namespace
{

struct BECTYPE
{
    short nType;
    char const *pzActor;  // the mod's actor definition
};

BECTYPE const gTypes[] = {
    { kDudeBecZombieSkeletal, "DUDE_ZombieSkeletal" },
    { kDudeBecZombieSkeletalEarth, "DUDE_ZombieSkeletalEarth" },
    { kDudeBecZombieSkeletalSleep, "DUDE_SleepZombieSkeletal" },
    { kDudeBecCrimson, "DUDE_Crimson" },
    { kDudeBecCrimsonEarth, "DUDE_CrimsonEarth" },
    { kDudeBecZomHalf, "DUDE_ZomHalf" },
    { kDudeBecZomHead, "DUDE_ZomHead" },
    { kDudeBecZomSkeletalHead, "DUDE_ZomSkeletalHead" },
    { kDudeBecZomSlime, "DUDE_ZomSlime" },
    { kDudeBecDog, "DUDE_Dog" },
    { kDudeBecMonsterDog, "DUDE_MonsterDog" },
    { kDudeBecNecromancer, "DUDE_Necromancer" },
    { kDudeBecLich, "DUDE_Lich" },
    { kDudeBecNecroStone, "DUDE_NecroStoneRise" },
    { kDudeBecGoreTremor, "DUDE_GoreTremor" },
    { kDudeBecFleshTentacle, "DUDE_FleshTentacle" },
    { kDudeBecInferno, "DUDE_Inferno" },
    { kDudeBecSpirit, "DUDE_Spirit" },
    { kDudeBecWorm, "DUDE_Worm" },
};

struct TYPEINFO
{
    bool bOk;
    AISTATE *pSpawn;
    short nRepeatX, nRepeatY;
    bool bNoGravity;
};

TYPEINFO gInfo[kDudeBecMax - kDudeBecBase];

enum { kMaxStates = 768 };
AISTATE gStates[kMaxStates];
int nStates;
std::unordered_map<std::string, AISTATE *> *pStateIndex;

struct BECVECTOR
{
    bool bOk;
    VECTORDATA data;
};
std::unordered_map<std::string, BECVECTOR> *pVectors;

bool bActive;

// the mod's damage types (DMG_) in Blood's
DAMAGE_TYPE const gDamageTypes[] = {
    kDamageFall,    // DMG_PUMMEL
    kDamageBurn,    // DMG_FIRE
    kDamageBullet,  // DMG_HITSCAN
    kDamageExplode, // DMG_EXPLODE
    kDamageBurn,    // DMG_BURN
    kDamageSpirit,  // DMG_SPIRIT
    kDamageTesla,   // DMG_ELECTROCUTE
    kDamageDrown,   // DMG_DROWN
};

// a number of the mod's in 16.16 (it writes speeds and distances as fractions: 2.0, 106.6665)
int fixedValue(BECBLOCK const *pBlock, char const *pzKey, int nDefault)
{
    char const *pzValue = becValue(pBlock, pzKey);
    if (!pzValue)
        return nDefault;
    char *pEnd;
    double const f = strtod(pzValue, &pEnd);
    if (pEnd != pzValue)
        return (int)(f * 65536.0);
    return becDefine(pzValue, nDefault >> 16) << 16;
}

char const *findNoCase(char const *pzText, char const *pzWord)
{
    size_t const n = strlen(pzWord);
    for (char const *p = pzText; *p; p++)
        if (!Bstrncasecmp(p, pzWord, n))
            return p;
    return NULL;
}

int stateTypeFor(char const *pzName)
{
    if (findNoCase(pzName, "Idle") || findNoCase(pzName, "Sleep"))
        return kAiStateIdle;
    if (findNoCase(pzName, "Chase") || findNoCase(pzName, "Ponder"))
        return kAiStateChase;
    if (findNoCase(pzName, "Search"))
        return kAiStateSearch;
    if (findNoCase(pzName, "Goto") || findNoCase(pzName, "Dodge"))
        return kAiStateMove;
    if (findNoCase(pzName, "Recoil") || findNoCase(pzName, "Shock") || findNoCase(pzName, "Pain"))
        return kAiStateRecoil;
    return kAiStateOther;
}

int nMissing;  // functions of states that aren't ours (Blood's enemies' states, the mod's allies)

// Blood's AI functions for the mod's names of them; its own are in becai.cpp
void (*aiFunc(char const *pzCallback))(spritetype *, XSPRITE *)
{
    if (!pzCallback || !Bstrcmp(pzCallback, "0"))
        return NULL;
    BECCALLBACK const *pCallback = becFindCallback(pzCallback);
    if (pCallback && pCallback->pAiFunc)
        return pCallback->pAiFunc;
    nMissing++;
    return NULL;
}

void loadStates(void)
{
    nStates = 0;
    pStateIndex = new std::unordered_map<std::string, AISTATE *>;
    std::unordered_map<std::string, int> clients;  // a seq client for each of the animation callbacks
    int const nCount = min(becDefStateCount(), (int)kMaxStates);
    for (int i = 0; i < nCount; i++)
    {
        BECBLOCK const *pBlock = becDefStateAt(i);
        AISTATE *pState = &gStates[nStates++];
        (*pStateIndex)[pBlock->pzName] = pState;
        pState->stateType = stateTypeFor(pBlock->pzName);
        pState->seqId = becInt(pBlock, "state.seqID", -1);
        pState->stateTicks = max(becInt(pBlock, "state.ticks", 0), 0);
        pState->enterFunc = aiFunc(becValue(pBlock, "state.onEnter"));
        pState->moveFunc = aiFunc(becValue(pBlock, "state.onMove"));
        pState->thinkFunc = aiFunc(becValue(pBlock, "state.onThink"));
        pState->funcId = -1;
        char const *pzSeq = becValue(pBlock, "state.seqCallback");
        if (pzSeq && Bstrcmp(pzSeq, "0"))
        {
            auto it = clients.find(pzSeq);
            if (it != clients.end())
                pState->funcId = it->second;
            else
            {
                BECCALLBACK const *pCallback = becFindCallback(pzSeq);
                if (pCallback && pCallback->pSeqFunc)
                    pState->funcId = clients[pzSeq] = seqRegisterClient(pCallback->pSeqFunc);
                else
                    nMissing++;
            }
        }
        pState->nextState = NULL;
    }
    for (int i = 0; i < nStates; i++)
        if (char const *pzNext = becValue(becDefStateAt(i), "state.nextState"))
            gStates[i].nextState = becState(pzNext);
}

void loadTypes(void)
{
    for (auto const &t : gTypes)
    {
        TYPEINFO &info = gInfo[t.nType - kDudeBecBase];
        BECBLOCK const *pActor = becDefActor(t.pzActor);
        info.bOk = pActor != NULL;
        if (!pActor)
        {
            LOG_F(WARNING, "Blood Extra Crispy: no %s in its definitions", t.pzActor);
            continue;
        }
        DUDEINFO *pDude = &dudeInfo[t.nType - kDudeBase];
        memset(pDude, 0, sizeof(*pDude));
        pDude->seqStartID = becInt(pActor, "actor.spawnSeq", 0);
        pDude->startHealth = clamp(becInt(pActor, "actor.health", 10), 1, 4095);
        pDude->mass = becInt(pActor, "actor.mass", 100);
        pDude->clipdist = becInt(pActor, "sprite.clipdist", 32);
        pDude->eyeHeight = becInt(pActor, "actor.eyeHeight", 32);
        pDude->aimHeight = becInt(pActor, "actor.aimHeight", 0);
        pDude->hearDist = becInt(pActor, "ai.hearDist", 10240);
        pDude->seeDist = becInt(pActor, "ai.seeDist", 51200);
        pDude->periphery = becInt(pActor, "ai.periphery", 512);
        pDude->meleeDist = becInt(pActor, "ai.meleeDist", 0);
        pDude->fleeHealth = becInt(pActor, "ai.fleeHealth", 10);
        pDude->hinderDamage = becInt(pActor, "ai.hinderDamage", 10);
        pDude->changeTarget = becInt(pActor, "ai.changeTarget", 256);
        pDude->changeTargetKin = becInt(pActor, "ai.changeTargetKin", 16);
        pDude->alertChance = becInt(pActor, "ai.alertChance", 32768);
        pDude->lockOut = becInt(pActor, "actor.lockout", 0);
        pDude->frontSpeed = becInt(pActor, "ai.frontSpeed", 0);
        pDude->sideSpeed = becInt(pActor, "ai.sideSpeed", 0);
        pDude->backSpeed = becInt(pActor, "ai.backSpeed", 0);
        pDude->angSpeed = becInt(pActor, "ai.turnSpeed", 256);
        for (int i = 0; i < 3; i++)
        {
            char key[32];
            Bsnprintf(key, sizeof(key), "actor.gibParts_%d", i + 1);
            int const nGib = becInt(pActor, key, -1);
            pDude->nGibType[i] = nGib >= 0 && nGib < kGibMax ? nGib : -1;  // the mod's own gibs aren't Blood's
        }
        bool const bImmortal = becInt(pActor, "actor.health", 0) >= 9999;  // the spirits: nothing hurts them
        for (int i = 0; i < kDamageMax; i++)
        {
            char key[32];
            Bsnprintf(key, sizeof(key), "actor.damageShift_%d", i + 1);
            pDude->startDamage[i] = pDude->curDamage[i] = bImmortal ? 0 : becInt(pActor, key, 256);
        }
        info.pSpawn = becState(becValue(pActor, "ai.spawnState"));
        info.nRepeatX = becInt(pActor, "sprite.xrepeat", 0);
        info.nRepeatY = becInt(pActor, "sprite.yrepeat", 0);
        info.bNoGravity = !becInt(pActor, "actor.hasgravity", 1);
    }
}

bool loadVector(char const *pzName, VECTORDATA *pData)
{
    BECBLOCK const *pBlock = becDefVector(pzName);
    if (!pBlock)
        return false;
    memset(pData, 0, sizeof(*pData));
    pData->dmgType = gDamageTypes[clamp(becInt(pBlock, "damageType", 2), 0, (int)ARRAY_SIZE(gDamageTypes) - 1)];
    pData->dmg = becInt(pBlock, "damageValue", 0);
    pData->impulse = fixedValue(pBlock, "thrust", 0) >> 4;      // the mod's thrust is Blood's impulse / 4096
    pData->maxDist = fixedValue(pBlock, "maxDist", 0) >> 7;     // meters, 512 units each
    pData->fxChance = becInt(pBlock, "bloodSpatterChance", 0);
    pData->burnTime = becInt(pBlock, "burnAmount", 0);
    pData->bloodSplats = becInt(pBlock, "bloodCount", 0);
    pData->splatChance = becInt(pBlock, "bloodChance", 0);
    // its impacts are named after Blood's vectors (IMPACT_VectorData13_Flesh): those vectors' effects
    int nImpact = kVectorAxe;
    if (char const *pzImpact = becValue(pBlock, "impactDef_flesh"))
        if (char const *p = findNoCase(pzImpact, "VectorData"))
            nImpact = atoi(p + 10);
    if (nImpact < 0 || nImpact >= kVectorMax)
        nImpact = kVectorAxe;
    memcpy(pData->surfHit, gVectorData[nImpact].surfHit, sizeof(pData->surfHit));
    return true;
}

bool canSwap(XSPRITE *pXSprite)
{
    // the mod leaves enemies a level counts on alone: key holders, ones that send or get messages
    return pXSprite->key == 0 && pXSprite->rxID <= 0 && pXSprite->txID <= 0 && pXSprite->data1 != 397;
}

// Skill_EnemyCount in the mod: the difficulty, 0-4
int swapChance(void)
{
    return clamp<int>(gGameOptions.nDifficulty, 0, 4);
}

// the level start: the mod's map placeholders become its enemies, and some of Blood's become its
void levelSwaps(void)
{
    struct PLACEHOLDER { short nPicnum, nType; };
    static PLACEHOLDER const gPlaceholders[] = {
        { 5743, kDudeBecNecromancer },
        { 5888, kDudeBecInferno },
        { 5941, kDudeBecGoreTremor },
        { 5971, kDudeBecFleshTentacle },
        { 6369, kDudeBecSpirit },
        { 5679, kDudeBecCrimson },
        { 7424, kDudeBecZombieSkeletal },
    };
    int nPlaced = 0, nSwapped = 0;
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0;)
    {
        int const nNext = nextspritestat[nSprite];
        spritetype *pSprite = &sprite[nSprite];
        for (auto const &p : gPlaceholders)
        {
            if (pSprite->picnum != p.nPicnum || !gInfo[p.nType - kDudeBecBase].bOk)
                continue;
            int top, bottom;
            GetSpriteExtents(pSprite, &top, &bottom);
            if (becSpawnDude(pSprite, p.nType, pSprite->x, pSprite->y, bottom, pSprite->ang))
            {
                nPlaced++;
                DeleteSprite(nSprite);
            }
            break;
        }
        nSprite = nNext;
    }

    int nEnemies = 0;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pSprite = &sprite[nSprite];
        if (IsPlayerSprite(pSprite) || pSprite->extra <= 0 || pSprite->extra >= kMaxXSprites)
            continue;
        XSPRITE *pXSprite = &xsprite[pSprite->extra];
        nEnemies++;
        // for every 75 enemies a Fanatic (tommy gun cultist) becomes a Necromancer
        if (pSprite->type == kDudeCultistTommy && nEnemies >= 75 && nSprite > 450 && canSwap(pXSprite) && gInfo[kDudeBecNecromancer - kDudeBecBase].bOk)
        {
            nEnemies -= 75;
            becMorphDude(pSprite, kDudeBecNecromancer);
            nSwapped++;
            continue;
        }
        if (!canSwap(pXSprite))
            continue;
        int const r5 = Random(100), r6 = Random(100);
        int const nChance = 15 * swapChance();
        switch (pSprite->type)
        {
        case kDudeZombieAxeNormal:  // a third Skeletal Zombies, a third Crimson Innocents
            if (r5 <= 33)
                becMorphDude(pSprite, kDudeBecZombieSkeletal), nSwapped++;
            else if (r5 <= 66)
                becMorphDude(pSprite, kDudeBecCrimson), nSwapped++;
            break;
        case kDudeZombieAxeBuried:
        {
            int const r = Random(75);
            if (r <= 25)
                becMorphDude(pSprite, kDudeBecCrimsonEarth), nSwapped++;
            else if (r <= 50)
                becMorphDude(pSprite, kDudeBecZombieSkeletalEarth), nSwapped++;
            break;
        }
        case kDudeRat:
            if (r5 <= nChance)
                gBecFlags[nSprite] |= kBecFlagRatBomb, nSwapped++;
            break;
        case kDudeCultistTommy:
            if (r5 <= nChance && r6 > 30 && r6 <= 40)
            {
                pSprite->type = pSprite->inittype = kDudeCultistBeast;
                gBecFlags[nSprite] |= kBecFlagLesser, nSwapped++;
            }
            else if (r5 <= nChance && r6 > 40 && r6 <= 80)
                pSprite->type = pSprite->inittype = kDudeCultistTesla, nSwapped++;
            break;
        case kDudeCultistTesla:
            if (r5 <= nChance && r6 > 65)
            {
                pSprite->type = pSprite->inittype = kDudeCultistBeast;
                gBecFlags[nSprite] |= kBecFlagLesser, nSwapped++;
            }
            break;
        case kDudeInnocent:
            if (r5 <= nChance && r6 <= 50)
                becMorphDude(pSprite, kDudeBecCrimson), nSwapped++;
            break;
        }
    }
    LOG_F(INFO, "Blood Extra Crispy: %d enemies placed, %d swapped", nPlaced, nSwapped);
}

class BecLoadSave : public LoadSave
{
    virtual void Load(void);
    virtual void Save(void);
};

void BecLoadSave::Load(void)
{
    Read(gBecFlags, sizeof(gBecFlags));
    Read(gBecBite, sizeof(gBecBite));
}

void BecLoadSave::Save(void)
{
    Write(gBecFlags, sizeof(gBecFlags));
    Write(gBecBite, sizeof(gBecBite));
}

BecLoadSave *pLoadSave;

// becspawn <name or number>: one of the mod's enemies appears in front of you (for trying them)
int osdcmd_becspawn(osdcmdptr_t parm)
{
    if (parm->numparms < 1 || !gGameStarted || !gMe || !gMe->pSprite)
    {
        for (int i = 0; i < (int)ARRAY_SIZE(gTypes); i++)
            OSD_Printf("%d %s%s\n", i, gTypes[i].pzActor, gInfo[gTypes[i].nType - kDudeBecBase].bOk ? "" : " (missing)");
        return OSDCMD_SHOWHELP;
    }
    int nType = -1;
    for (int i = 0; i < (int)ARRAY_SIZE(gTypes); i++)
        if (!Bstrcasecmp(parm->parms[0], gTypes[i].pzActor) || !Bstrcasecmp(parm->parms[0], gTypes[i].pzActor + 5))
            nType = gTypes[i].nType;
    if (nType < 0 && isdigit((unsigned char)parm->parms[0][0]))
    {
        int const n = atoi(parm->parms[0]);
        if (n >= 0 && n < (int)ARRAY_SIZE(gTypes))
            nType = gTypes[n].nType;
    }
    if (nType < 0 || !bActive || !gInfo[nType - kDudeBecBase].bOk)
        return OSDCMD_SHOWHELP;
    spritetype *pPlayer = gMe->pSprite;
    int const nDist = parm->numparms > 1 ? atoi(parm->parms[1]) : 1200;
    int top, bottom;
    GetSpriteExtents(pPlayer, &top, &bottom);
    spritetype *pDude = becSpawnDude(pPlayer, nType, pPlayer->x + mulscale30(nDist, Cos(pPlayer->ang)), pPlayer->y + mulscale30(nDist, Sin(pPlayer->ang)),
                                     bottom, (pPlayer->ang + 1024) & 2047);
    if (pDude && parm->numparms > 2)  // and that much health (to try what they do when hurt)
        xsprite[pDude->extra].health = clamp(atoi(parm->parms[2]), 1, 4095) << 4;
    if (pDude)
        OSD_Printf("%s: sprite %d, type %d, %d health, seq %d\n", parm->parms[0], (int)pDude->index, (int)pDude->type, xsprite[pDude->extra].health >> 4,
                   getDudeInfo(nType)->seqStartID);
    return OSDCMD_OK;
}

char const *stateName(AISTATE *pState)
{
    if (pState >= gStates && pState < gStates + nStates)
        return becDefStateAt((int)(pState - gStates))->pzName;
    return "(Blood's)";
}

// becinfo: what the mod's enemies are doing
int osdcmd_becinfo(osdcmdptr_t UNUSED(parm))
{
    UNREFERENCED_CONST_PARAMETER(parm);
    if (!gMe || !gMe->pSprite)
        return OSDCMD_OK;
    spritetype *pPlayer = gMe->pSprite;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pSprite = &sprite[nSprite];
        if (IsPlayerSprite(pSprite) || pSprite->extra <= 0)
            continue;
        int const dx = pPlayer->x - pSprite->x, dy = pPlayer->y - pSprite->y;
        int const nDist = approxDist(dx, dy);
        if (nDist > 8000)
            continue;
        XSPRITE *pXSprite = &xsprite[pSprite->extra];
        DUDEINFO *pInfo = getDudeInfo(pSprite->type);
        bool const bSees = cansee(pPlayer->x, pPlayer->y, pPlayer->z, pPlayer->sectnum, pSprite->x, pSprite->y,
                                  pSprite->z - ((pInfo->eyeHeight * pSprite->yrepeat) << 2), pSprite->sectnum);
        OSD_Printf("sprite %d type %d: %s, health %d, target %d, seq %d (%d), dist %d, sees %d, flags %d\n", nSprite, (int)pSprite->type,
                   stateName(pXSprite->aiState), pXSprite->health >> 4, pXSprite->target, seqGetID(3, pSprite->extra),
                   seqGetStatus(3, pSprite->extra), nDist, (int)bSees, gBecFlags[nSprite]);
    }
    for (int nSprite = headspritestat[kStatThing]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pSprite = &sprite[nSprite];
        if (pSprite->type != kThingBloodChunks || pSprite->extra <= 0 || approxDist(pPlayer->x - pSprite->x, pPlayer->y - pSprite->y) > 8000)
            continue;
        OSD_Printf("corpse %d of type %d: seq %d (%d), picnum %d\n", nSprite, (int)pSprite->inittype, seqGetID(3, pSprite->extra),
                   seqGetStatus(3, pSprite->extra), (int)pSprite->picnum);
    }
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])  // what the dead left
    {
        spritetype *pSprite = &sprite[nSprite];
        int const nPicnum = pSprite->picnum;
        if ((nPicnum == 5481 || nPicnum == 5558 || nPicnum == 7305 || nPicnum == 7353) && approxDist(pPlayer->x - pSprite->x, pPlayer->y - pSprite->y) <= 8000)
            OSD_Printf("item %d: picnum %d, dist %d\n", nSprite, nPicnum, approxDist(pPlayer->x - pSprite->x, pPlayer->y - pSprite->y));
    }
    return OSDCMD_OK;
}

// beckill <bullet|explode|burn|spirit|tesla|fall> [head|belly]: the nearest enemy in front of you dies that way
int osdcmd_beckill(osdcmdptr_t parm)
{
    if (parm->numparms < 1 || !gGameStarted || !gMe || !gMe->pSprite)
        return OSDCMD_SHOWHELP;
    static char const *const gNames[] = { "fall", "burn", "bullet", "explode", "drown", "spirit", "tesla" };
    int nDmgType = -1;
    for (int i = 0; i < (int)ARRAY_SIZE(gNames); i++)
        if (!Bstrcasecmp(parm->parms[0], gNames[i]))
            nDmgType = i;
    if (nDmgType < 0)
        return OSDCMD_SHOWHELP;
    spritetype *pPlayer = gMe->pSprite;
    int nBest = -1, nBestDist = INT32_MAX;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pSprite = &sprite[nSprite];
        if (IsPlayerSprite(pSprite) || pSprite->extra <= 0 || xsprite[pSprite->extra].health <= 0)
            continue;
        int const nDist = approxDist(pSprite->x - pPlayer->x, pSprite->y - pPlayer->y);
        if (nDist < nBestDist)
            nBest = nSprite, nBestDist = nDist;
    }
    if (nBest < 0)
        return OSDCMD_OK;
    spritetype *pSprite = &sprite[nBest];
    gBecFlags[nBest] &= ~(kBecFlagHeadShot | kBecFlagBellyShot | kBecFlagLegShot);
    if (parm->numparms > 1 && !Bstrcasecmp(parm->parms[1], "head"))
        gBecFlags[nBest] |= kBecFlagHeadShot;
    if (parm->numparms > 1 && !Bstrcasecmp(parm->parms[1], "belly"))
        gBecFlags[nBest] |= kBecFlagBellyShot;
    if (parm->numparms > 1 && !Bstrcasecmp(parm->parms[1], "legs"))
        gBecFlags[nBest] |= kBecFlagLegShot;
    OSD_Printf("beckill: sprite %d type %d by %s\n", nBest, (int)pSprite->type, gNames[nDmgType]);
    for (int i = 0; i < 20 && pSprite->statnum == kStatDude && pSprite->extra > 0 && xsprite[pSprite->extra].health > 0; i++)
        actDamageSprite(pPlayer->index, pSprite, (DAMAGE_TYPE)nDmgType, 1000 << 4);
    return OSDCMD_OK;
}

} // namespace

void BecLoadSaveConstruct(void)
{
    void BecAiLoadSaveConstruct(void);
    void BecWeaponsLoadSaveConstruct(void);
    pLoadSave = new BecLoadSave();
    BecAiLoadSaveConstruct();
    BecWeaponsLoadSaveConstruct();
}

void becVectorHit(spritetype *pSprite, DAMAGE_TYPE nDmgType, int nHitZ)
{
    if (!bActive || !IsDudeSprite(pSprite) || IsPlayerSprite(pSprite))
        return;
    uint8_t &flags = gBecFlags[pSprite->index];
    flags &= ~(kBecFlagHeadShot | kBecFlagBellyShot | kBecFlagLegShot);
    if (nDmgType != kDamageBullet)
        return;
    // the mod measures from the top of the sprite: its head, then a gap, then its belly
    int top, bottom;
    GetSpriteExtents(pSprite, &top, &bottom);
    int const nHeight = bottom - top;
    if (nHeight <= 0)
        return;
    int const nFrom = scale(nHitZ - top, 100, nHeight);  // percent from the top
    if (nFrom < 18)
        flags |= kBecFlagHeadShot;
    else if (nFrom >= 35 && nFrom <= 55)
        flags |= kBecFlagBellyShot;
    else if (nFrom > 55)
        flags |= kBecFlagLegShot;
}

bool gBecBite[kMaxPlayers];

bool becBiting(PLAYER *pPlayer)
{
    return pPlayer->hand && gBecBite[pPlayer->nPlayer];
}

static CChoke gBecChoke;

void becChokeDraw(int x, int y)
{
    if (!gBecChoke.hQav)
    {
        int const nQav = becDefine("QAV_SKEZGRAB", -1);
        if (nQav < 0 || !gSysRes.Lookup(nQav, "QAV"))
            return;
        gBecChoke.Init(nQav, NULL);
    }
    gBecChoke.Draw(x, y);
}

// pushed off: it lands in front of you, a Skeletal Zombie again (SZBiteDamage.txt CheckRelease)
void becDropBite(PLAYER *pPlayer)
{
    pPlayer->hand = 0;
    gBecBite[pPlayer->nPlayer] = false;
    spritetype *pPlayerSprite = pPlayer->pSprite;
    int top, bottom;
    GetSpriteExtents(pPlayerSprite, &top, &bottom);
    spritetype *pZombie = becSpawnDude(pPlayerSprite, kDudeBecZombieSkeletal, pPlayerSprite->x + mulscale30(pPlayerSprite->clipdist << 1, Cos(pPlayerSprite->ang)),
                                       pPlayerSprite->y + mulscale30(pPlayerSprite->clipdist << 1, Sin(pPlayerSprite->ang)), bottom, (pPlayerSprite->ang + 1024) & 2047);
    if (!pZombie)
        return;
    int const x = Cos(pPlayerSprite->ang) >> 16, y = Sin(pPlayerSprite->ang) >> 16;
    xvel[pZombie->index] = xvel[pPlayerSprite->index] + mulscale14(0x155555, x);
    yvel[pZombie->index] = yvel[pPlayerSprite->index] + mulscale14(0x155555, y);
    zvel[pZombie->index] = zvel[pPlayerSprite->index];
}

bool becDudesActive(void)
{
    return bActive;
}

static char szKpf[BMAX_PATH];  // the mod's BloodEX.kpf

// the mod's voxels (3D models): its defs/extraVoxels.txt gives one for each of these tiles (switches,
// levers, valves, bottles, signs, lamps, pickups, its Maxim gun and cannons), the .kvx files are in its
// BloodEX.kpf (a zip, mounted for them). Each level loads only the ones its sprites use, and makes
// their meshes while it loads: all of them (510 files) took too much memory and time on phones.
// bec_models: 0 none, 1 switches, pickups and the mod's own things, 2 all (wall decorations, props too)
int gBecModels = 1;

namespace
{

enum { kVoxGroupOther, kVoxGroupKey, kVoxGroupAlways };  // which bec_models setting shows it

struct BECVOXFILE
{
    std::string name;
    int nVoxel;     // its voxel slot (kept for the whole game)
    int nGroup;
    bool bRotate;   // pickups turn
    bool bLoaded;
};
struct BECVOXTILE { int nTile, nFile; };

std::vector<BECVOXFILE> gVoxFiles;
std::vector<BECVOXTILE> gVoxTiles;

int voxGroup(char const *pzName)
{
    // EXTRAC/KVX/<group>/<file>, or EXTRAC/KVX/<file> for the mod's own things
    char const *p = pzName + (Bstrncasecmp(pzName, "EXTRAC/KVX/", 11) ? 0 : 11);
    if (!Bstrncasecmp(p, "PICKUPS/", 8))
        return kVoxGroupAlways;  // they can turn up later (drops, respawns)
    if (!Bstrncasecmp(p, "SWITCHES/", 9))
        return kVoxGroupKey;
    if (strchr(p, '/'))
        return kVoxGroupOther;  // wall decorations, props, gibs, trees, lamps
    // the mod's own things: what can turn up later (the dead's drops, spells, keys and guns dropped),
    // the others only where the map has them
    static char const *const gLater[] = { "ORB.", "VILE.", "WATCH.", "AXE.", "NECROSTONE", "DBELL", "CANNONBALL.",
                                          "TOMMYGUNS.", "SHOTGUNS.", "FLAREGUNS.", "KEY", "ARMOR" };
    for (char const *pzLater : gLater)
        if (!Bstrncasecmp(p, pzLater, strlen(pzLater)))
            return kVoxGroupAlways;
    return kVoxGroupKey;
}

} // namespace

void becVoxelsInit(void)
{
    gVoxFiles.clear();
    gVoxTiles.clear();
    if (!bActive || !szKpf[0])
        return;
    if (initgroupfile(szKpf) < 0)
    {
        LOG_F(WARNING, "Blood Extra Crispy: %s can't be opened for its 3D models", szKpf);
        return;
    }
    buildvfs_kfd h = kopen4load("defs/extraVoxels.txt", 0);
    if (h == buildvfs_kfd_invalid)
        return;
    int const nLen = kfilelength(h);
    std::string text(max(nLen, 0), '\0');
    if (nLen > 0)
        kread(h, &text[0], nLen);
    kclose(h);
    std::unordered_map<std::string, int> files;
    for (size_t p = 0; p < text.size();)
    {
        size_t eol = text.find('\n', p);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(p, eol - p);
        p = eol + 1;
        size_t const nComment = line.find("//");
        if (nComment != std::string::npos)
            line.resize(nComment);
        // Voxel <tile> { filename "<file>" [rotate TRUE] }
        int nTile;
        char name[256];
        if (sscanf(line.c_str(), " Voxel %d { filename \"%255[^\"]\"", &nTile, name) != 2 || nTile < 0 || nTile >= MAXTILES)
            continue;
        auto it = files.find(name);
        int nFile;
        if (it != files.end())
            nFile = it->second;
        else
        {
            while (nextvoxid < MAXVOXELS && (voxflags[nextvoxid] & VF_RESERVE))  // Blood's own voxels
                nextvoxid++;
            if (nextvoxid >= MAXVOXELS)
                break;
            BECVOXFILE f;
            f.name = name;
            f.nVoxel = nextvoxid++;
            f.nGroup = voxGroup(name);
            f.bRotate = line.find("rotate TRUE") != std::string::npos;
            f.bLoaded = false;
            nFile = files[name] = (int)gVoxFiles.size();
            gVoxFiles.push_back(f);
        }
        gVoxTiles.push_back({ nTile, nFile });
    }
    LOG_F(INFO, "Blood Extra Crispy: 3D models for %d tiles (%d files), loaded with the levels that use them",
          (int)gVoxTiles.size(), (int)gVoxFiles.size());
}

// the level's: the tiles its sprites show (and their animations, and a switch's other tile)
void becVoxelsLevel(void)
{
    if (gVoxFiles.empty())
        return;
    static uint8_t used[MAXTILES];
    memset(used, 0, sizeof(used));
    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum >= kMaxStatus)
            continue;
        int const nPic = sprite[i].picnum;
        int const nFrames = picanm[nPic].num;
        for (int t = nPic; t <= nPic + nFrames + 1 && t < MAXTILES; t++)
            used[t] = 1;
    }
    int const nLevel = usevoxels ? clamp(gBecModels, 0, 2) : 0;
    std::vector<uint8_t> want(gVoxFiles.size(), 0);
    for (auto const &t : gVoxTiles)
    {
        BECVOXFILE const &f = gVoxFiles[t.nFile];
        bool const bShown = nLevel >= 2 || (nLevel == 1 && f.nGroup >= kVoxGroupKey);
        if (bShown && (used[t.nTile] || f.nGroup == kVoxGroupAlways))
            want[t.nFile] = 1;
    }
    int nLoaded = 0, nFreed = 0, nFailed = 0;
    for (size_t i = 0; i < gVoxFiles.size(); i++)
    {
        BECVOXFILE &f = gVoxFiles[i];
        if (want[i] && !f.bLoaded)
        {
            if (qloadkvx(f.nVoxel, f.name.c_str()))
            {
                nFailed++;
                continue;
            }
            if (f.bRotate)
                voxflags[f.nVoxel] |= VF_ROTATE;
            f.bLoaded = true;
            nLoaded++;
        }
        else if (!want[i] && f.bLoaded)
        {
            vox_clearid(f.nVoxel);
            f.bLoaded = false;
            nFreed++;
        }
    }
    int nTiles = 0;
    for (auto const &t : gVoxTiles)
    {
        bool const bOn = gVoxFiles[t.nFile].bLoaded;
        tiletovox[t.nTile] = bOn ? gVoxFiles[t.nFile].nVoxel : -1;
        nTiles += bOn;
    }
    vox_buildmeshes();  // now, while the level loads, not when they first come into view
    LOG_F(INFO, "Blood Extra Crispy: 3D models for %d tiles in this level (%d loaded, %d freed, %d failed; bec_models %d)",
          nTiles, nLoaded, nFreed, nFailed, nLevel);
}

void becDudesInit(char const *pzKpf)
{
    bActive = false;
    Bstrncpyz(szKpf, pzKpf ? pzKpf : "", sizeof(szKpf));
    OSD_RegisterFunction("becspawn", "becspawn <name or number> [distance] [health]: one of Blood Extra Crispy's enemies appears in front of you", osdcmd_becspawn);
    OSD_RegisterFunction("becinfo", "becinfo: what Blood Extra Crispy's enemies are doing", osdcmd_becinfo);
    static osdcvardata_t cvModels = { "bec_models", "Blood Extra Crispy's 3D models (0: none, 1: switches, pickups and its own things, 2: all, next level)",
                                      (void *)&gBecModels, CVAR_INT, 0, 2 };
    OSD_RegisterCvar(&cvModels, osdcmd_cvar_set);
    OSD_RegisterFunction("beckill", "beckill <bullet|explode|burn|spirit|tesla|fall> [head|belly]: the nearest enemy dies that way", osdcmd_beckill);
    if (!pzKpf || !becDefsLoad(pzKpf))
    {
        LOG_F(WARNING, "Blood Extra Crispy: its BloodEX.kpf can't be read, so it has none of its new enemies");
        return;
    }
    loadStates();
    loadTypes();
    pVectors = new std::unordered_map<std::string, BECVECTOR>;
    int nTypes = 0;
    for (auto const &t : gTypes)
        nTypes += gInfo[t.nType - kDudeBecBase].bOk;
    bActive = nTypes > 0;
    LOG_F(INFO, "Blood Extra Crispy: %d of %d new enemies, %d AI states (%d functions of others' states not used)", nTypes,
          (int)ARRAY_SIZE(gTypes), nStates, nMissing);
}

AISTATE *becState(char const *pzName)
{
    if (!pStateIndex || !pzName)
        return NULL;
    auto it = pStateIndex->find(pzName);
    return it != pStateIndex->end() ? it->second : NULL;
}

void becNewState(spritetype *pSprite, XSPRITE *pXSprite, char const *pzName)
{
    if (AISTATE *pState = becState(pzName))
        aiNewState(pSprite, pXSprite, pState);
    else
        LOG_F(WARNING, "Blood Extra Crispy: no AI state %s", pzName);
}

bool becInState(XSPRITE *pXSprite, char const *pzName)
{
    AISTATE *pState = becState(pzName);
    return pState && pXSprite->aiState == pState;
}

void becFireVector(spritetype *pSprite, int a2, int a3, int dx, int dy, int dz, char const *pzVector)
{
    if (!pVectors)
        return;
    auto it = pVectors->find(pzVector);
    if (it == pVectors->end())
    {
        BECVECTOR v;
        v.bOk = loadVector(pzVector, &v.data);
        it = pVectors->emplace(pzVector, v).first;
        if (!v.bOk)
            LOG_F(WARNING, "Blood Extra Crispy: no vector %s", pzVector);
    }
    if (!it->second.bOk)
        return;
    // fired as Blood's unused vector 18, which is the mod's for the moment
    VECTORDATA const backup = gVectorData[VECTOR_TYPE_18];
    gVectorData[VECTOR_TYPE_18] = it->second.data;
    actFireVector(pSprite, a2, a3, dx, dy, dz, VECTOR_TYPE_18);
    gVectorData[VECTOR_TYPE_18] = backup;
}

void becMorphDude(spritetype *pSprite, int nType)
{
    if (pSprite->extra <= 0 || pSprite->extra >= kMaxXSprites)
        return;
    XSPRITE *pXSprite = &xsprite[pSprite->extra];
    DUDEINFO *pInfo = getDudeInfo(nType);
    pSprite->type = pSprite->inittype = nType;
    pSprite->clipdist = pInfo->clipdist;
    pXSprite->health = pInfo->startHealth << 4;
    if (becIsDude(nType))
    {
        TYPEINFO const &info = gInfo[nType - kDudeBecBase];
        if (info.nRepeatX > 0)
            pSprite->xrepeat = info.nRepeatX;
        if (info.nRepeatY > 0)
            pSprite->yrepeat = info.nRepeatY;
    }
    if (gSysRes.Lookup(pInfo->seqStartID, "SEQ"))
        seqSpawn(pInfo->seqStartID, 3, pSprite->extra, -1);
    aiInitSprite(pSprite);
}

spritetype *becSpawnDude(spritetype *pSource, int nType, int x, int y, int z, int nAngle)
{
    if (becIsDude(nType) && !gInfo[nType - kDudeBecBase].bOk)
        return NULL;
    spritetype *pSprite = actSpawnDude(pSource, nType, -1, 0);
    if (!pSprite)
        return NULL;
    if (IsPlayerSprite(pSource))  // Blood's enemies never go for their owner
        pSprite->owner = -1;
    pSprite->cstat &= ~CSTAT_SPRITE_YCENTER;  // from the source (the player's is centred): its z is its feet
    vec3_t pos = { x, y, z };
    setsprite(pSprite->index, &pos);
    pSprite->ang = nAngle;
    if (becIsDude(nType))
    {
        TYPEINFO const &info = gInfo[nType - kDudeBecBase];
        if (info.nRepeatX > 0)
            pSprite->xrepeat = info.nRepeatX;
        if (info.nRepeatY > 0)
            pSprite->yrepeat = info.nRepeatY;
    }
    // not under the floor (its z is its feet)
    int ceilZ, floorZ;
    getzsofslope(pSprite->sectnum, pSprite->x, pSprite->y, &ceilZ, &floorZ);
    if (pSprite->z > floorZ)
        pSprite->z = floorZ;
    return pSprite;
}

void becLevelStart(bool bSaveLoad)
{
    becVoxelsLevel();  // the 3D models this level shows (a saved game's too)
    if (bSaveLoad)
        return;
    memset(gBecFlags, 0, sizeof(gBecFlags));
    if (!bActive)
        return;
    if (gGameOptions.nMonsterSettings == 0)  // no monsters (Bloodbath): Blood deletes them all next
        return;
    levelSwaps();
    becAiLevelStart();
}

bool becInitSprite(spritetype *pSprite, XSPRITE *pXSprite)
{
    if (!becIsDude(pSprite->type))
        return false;
    TYPEINFO const &info = gInfo[pSprite->type - kDudeBecBase];
    if (info.pSpawn)
        aiNewState(pSprite, pXSprite, info.pSpawn);
    else
        aiNewState(pSprite, pXSprite, &genIdle);
    return true;
}

void becAfterInitSprite(spritetype *pSprite, XSPRITE *pXSprite)
{
    if (!becIsDude(pSprite->type))
        return;
    if (gInfo[pSprite->type - kDudeBecBase].bNoGravity)
        pSprite->flags |= 9;
    becAiInit(pSprite, pXSprite);
}

bool becActivateDude(spritetype *pSprite, XSPRITE *pXSprite)
{
    if (!becIsDude(pSprite->type))
        return false;
    becAiActivate(pSprite, pXSprite);
    return true;
}

void becDamageDude(spritetype *pSprite, XSPRITE *pXSprite, int nSource, DAMAGE_TYPE nDmgType, int nDamage)
{
    if (!bActive)
        return;
    becAiDamage(pSprite, pXSprite, nSource, nDmgType, nDamage);
}

bool becRecoilDude(spritetype *pSprite, XSPRITE *pXSprite)
{
    if (!becIsDude(pSprite->type))
        return false;
    return becAiRecoil(pSprite, pXSprite);
}

bool becKillDudeFirst(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType, int nDamage)
{
    if (!bActive)
        return false;
    return becAiKillFirst(pSprite, pXSprite, nKiller, nDmgType, nDamage);
}

int becKillDude(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType, int nDamage)
{
    if (!becIsDude(pSprite->type))
        return kBecKillNo;
    return becAiKill(pSprite, pXSprite, nKiller, nDmgType, nDamage);
}

void becAfterKill(spritetype *pSprite, XSPRITE *pXSprite, int nKiller, DAMAGE_TYPE nDmgType)
{
    if (!bActive)
        return;
    becAiAfterKill(pSprite, pXSprite, nKiller, nDmgType);
}

void becProcessFrame(void)
{
    if (!bActive || !gGameStarted)
        return;
    becAiFrame();
}
