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

// Blood Extra Crispy as a mod, and its Gory Kills (see gorykill.h).
//
// Chosen in the MODS menu, the mod's folder gives the game its art (tiles001-030.art: its title,
// menu background, sprites and new tiles), its SOUNDS.RFF and SURFACE.DAT. Its maps, BLOOD.INI and
// scripts aren't used: they are made for Nightdive's KEX engine (enemy types and texts only it has).
//
// The mod (for Nightdive's Refreshed Supply) scripts its kills in BEC_Players.txt: USE near a
// suffering enemy removes it, plays a first-person QAV over the screen and runs a timeline (a timer
// counting down by 2 a game frame) of sounds, a heal, and a body kicked away with a gory SEQ that
// stays as a corpse. That timeline is in the table below, timer values and all. The QAVs, SEQs, gore
// sounds and tiles (tiles018-030.art) come from the player's copy of the mod: EXTRAC.RFF is opened
// next to the game's own files and only what the kills use is taken from it.

#include "build.h"
#include "compat.h"
#include "common_game.h"
#include "actor.h"
#include "becai.h"
#include "becdefs.h"
#include "becdudes.h"
#include "becweapons.h"
#include "blood.h"
#include "config.h"
#include "db.h"
#include "fx.h"
#include "gib.h"
#include "globals.h"
#include "gorykill.h"
#include "levels.h"
#include "player.h"
#include "qav.h"
#include "resource.h"
#include "seq.h"
#include "sfx.h"
#include "sound.h"
#include "mmulti.h"
#include "osd.h"
#include "trig.h"

namespace
{

enum
{
    kEvSound = 0,  // a sound (plus a random 0..rnd-1)
    kEvLaugh,      // Caleb heals 10 (up to 200) and laughs now and then; blood on the screen
    kEvBody,       // the body flies away from Caleb with the kill's SEQ and stays as a corpse
    kEvKick,       // the quick kick lands: three short hits that push hard (the mod's BV_QUICKKICK)
    kEvWipe,       // Caleb's hand wipes the puke off his face
    kEvEnd = -1,
};

struct GKEVENT
{
    short nTime;  // the mod's timer: starts at the kill's length, 2 less a game frame
    signed char nType;
    signed char nRand;
    int nSound;
};

struct GKDEF
{
    short nDude[4];  // enemy types (0: none)
    short nHealth;   // the enemy's health (x16, as in xsprite) must be at most this; -1: any
    short nDist;     // how near it must be (Build units)
    short nLength;   // the timer's start
    short nQav, nQavCloak;  // the QAV, and the one for an invisible Caleb (shadow cloak)
    int nBodySeq;    // the body's SEQ (0: no body)
    GKEVENT const *pEvents;
    bool bKick;      // the quick kick: Caleb can move meanwhile, and gets hurt
};

// sounds: original Blood's, and the mod's gore (in EXTRAC.RFF)
enum
{
    kSfxCultDie1 = 1018, kSfxPunt = 357, kSfxAZomPain = 1106, kSfxAZomDie1 = 1107, kSfxAZomSwip = 1101,
    kSfxFZomPain = 1202, kSfxFZomDie1 = 1204, kSfxGargPain = 1402, kSfxGargDie1 = 1403, kSfxGillPain = 1702,
    kSfxGillDie1 = 1703, kSfxIDonHme = 7002, kSfxIScream1 = 7007, kSfxPFlesh = 503, kSfxCerbPain = 1302,
    kSfxCerbDie1 = 1303, kSfxPhanSpot = 1600, kSfxPhanDie1 = 1603, kSfxPlay1109 = 734,
    kSfxGorBone = 22161, kSfxGorHead = 22162, kSfxGorRan1 = 22164, kSfxGorRip1 = 22167, kSfxDemMel1 = 22112,
    kSfxCriDie1 = 22146, kSfxDogDie = 22249, kSfxDogPai = 22251, kSfxSZomDie = 22462, kSfxSZomPai = 22463,
};

#define SND(t, s) { t, kEvSound, 0, s }
#define SNDR(t, s, r) { t, kEvSound, r, s }
#define LAUGH(t) { t, kEvLaugh, 0, 0 }
#define BODY(t) { t, kEvBody, 0, 0 }
#define KICK(t) { t, kEvKick, 0, 0 }
#define WIPE(t) { t, kEvWipe, 0, 0 }
#define END { 0, kEvEnd, 0, 0 }

// cultists (the four have the same timeline; tesla and TNT add a punt at the start)
GKEVENT const gEvCultist[] = {
    SNDR(110, kSfxCultDie1, 2), SND(110, kSfxGorHead), LAUGH(110),
    SND(34, kSfxDemMel1), SND(28, kSfxPunt), BODY(18), END };
GKEVENT const gEvCultistTesla[] = {
    SND(114, kSfxDemMel1), SNDR(110, kSfxCultDie1, 2), SND(110, kSfxGorHead), SND(110, kSfxPunt), LAUGH(110),
    SND(34, kSfxDemMel1), SND(28, kSfxPunt), BODY(18), END };
GKEVENT const gEvCultistTNT[] = {
    SNDR(110, kSfxCultDie1, 2), SND(110, kSfxGorHead), SND(110, kSfxPunt), LAUGH(110),
    SND(34, kSfxDemMel1), SND(28, kSfxPunt), BODY(18), END };
GKEVENT const gEvAxeZombie[] = {
    SND(100, kSfxAZomPain), SND(90, kSfxAZomPain), SNDR(80, kSfxAZomDie1, 2), SND(80, kSfxGorRip1), LAUGH(80),
    SND(26, kSfxDemMel1), SND(20, kSfxPunt), BODY(10), END };
GKEVENT const gEvFatZombie[] = {
    SND(130, kSfxAZomSwip), SND(124, kSfxFZomPain), SND(124, kSfxGorBone), SND(100, kSfxAZomSwip),
    SNDR(94, kSfxFZomDie1, 2), SND(94, kSfxGorRip1), SND(60, kSfxAZomSwip), SND(54, kSfxPunt), SND(54, kSfxGorHead),
    LAUGH(54), SND(20, kSfxDemMel1), SND(14, kSfxPunt), BODY(4), END };
GKEVENT const gEvGargoyle[] = {
    SND(120, kSfxGorBone), SND(110, kSfxAZomSwip), SND(100, kSfxPunt), SND(100, kSfxGargPain), SND(100, kSfxGorHead),
    SND(84, kSfxAZomSwip), SND(74, kSfxPunt), SND(74, kSfxGargPain), SND(74, kSfxGorHead), SND(60, kSfxAZomSwip),
    SND(50, kSfxPunt), SND(50, kSfxGargPain), SND(50, kSfxGorHead), SND(30, kSfxDemMel1), SND(24, kSfxPunt),
    SNDR(24, kSfxGargDie1, 2), SNDR(24, kSfxGorRan1, 2), LAUGH(24), BODY(14), END };
GKEVENT const gEvGillBeast[] = {
    SND(80, kSfxGillPain), SND(64, kSfxGillPain), SNDR(52, kSfxGillDie1, 2), SNDR(52, kSfxGorRan1, 2), LAUGH(52),
    SND(30, kSfxDemMel1), SND(24, kSfxPunt), BODY(14), END };
GKEVENT const gEvInnocent[] = {
    SND(100, kSfxIDonHme), SND(90, kSfxGorBone), SNDR(80, kSfxIScream1, 2), SNDR(70, kSfxIScream1, 2),
    SNDR(60, kSfxIScream1, 2), SNDR(50, kSfxIScream1, 2), SND(50, kSfxGorRip1), LAUGH(50),
    SND(30, kSfxDemMel1), SND(24, kSfxPunt), BODY(14), END };
GKEVENT const gEvHellHound[] = {
    SND(100, kSfxAZomSwip), SND(90, kSfxPFlesh), SND(90, kSfxCerbPain), LAUGH(90), SND(80, kSfxAZomSwip),
    SND(70, kSfxPFlesh), SND(70, kSfxCerbPain), SND(60, kSfxAZomSwip), SND(50, kSfxPFlesh), SNDR(50, kSfxCerbDie1, 2),
    SND(40, kSfxDemMel1), SND(34, kSfxPunt), BODY(24), END };
GKEVENT const gEvPhantasm[] = {
    SND(120, kSfxPhanSpot), SND(100, kSfxPhanSpot), SND(56, kSfxAZomSwip), SND(50, kSfxGorHead),
    SNDR(50, kSfxPhanDie1, 2), LAUGH(50), END };
// the mod's own enemies (BEC_Players.txt HUDQAVType 15, 500, 67, 68, 670, 70)
GKEVENT const gEvCrimson[] = {
    SND(120, kSfxGorBone), SNDR(90, kSfxCriDie1, 2), SND(90, kSfxGorHead), LAUGH(90),
    SND(36, kSfxDemMel1), SND(30, kSfxPunt), BODY(20), END };
GKEVENT const gEvSkeletal[] = {
    SND(100, kSfxSZomPai), SND(90, kSfxSZomPai), SND(80, kSfxSZomDie), SND(80, kSfxGorRip1), LAUGH(80),
    SND(26, kSfxDemMel1), SND(20, kSfxPunt), BODY(10), END };
GKEVENT const gEvZomHead[] = {
    SND(60, kSfxAZomPain), SND(50, kSfxAZomPain), SNDR(30, kSfxAZomDie1, 2), SND(30, kSfxGorHead), LAUGH(30), END };
GKEVENT const gEvZomHalf[] = {
    SND(60, kSfxAZomPain), SND(50, kSfxAZomPain), SNDR(30, kSfxAZomDie1, 2), SND(30, kSfxGorHead), LAUGH(30), BODY(30), END };
GKEVENT const gEvSkeletalHead[] = {
    SND(60, kSfxSZomPai), SND(50, kSfxSZomPai), SND(30, kSfxSZomDie), SND(30, kSfxGorHead), LAUGH(30), END };
GKEVENT const gEvDog[] = {
    SND(90, kSfxDogPai), SND(90, kSfxGorHead), SND(60, kSfxDogPai), SND(60, kSfxGorHead), SND(30, kSfxDogDie),
    SND(30, kSfxGorHead), LAUGH(30), BODY(20), END };

// the mod's distances: 128 (most) and 85 of its units, 16 Build units each
enum { kNear = 2048, kNearer = 1360 };

// USE near an enemy that isn't hurt enough for a Gory Kill: Caleb kicks it back (QAV_QKICK), at most
// once a second (the mod's KickDelay)
GKEVENT const gEvKick[] = { SND(14, kSfxDemMel1), KICK(8), END };
GKDEF const gKick = { { 0 }, -1, kNearer, 24, 622, 669, 0, gEvKick, true };
// another player (Bloodbath, the other team): kicked, or finished off when nearly dead as a cultist is
// (his body stays his own)
GKDEF const gPlayerKill = { { 0 }, 200, kNear, 140, 626, 689, 0, gEvCultist };
enum { kKickDelay = 60 };

// always kicked (no Gory Kill for them): burning ones and beasts, as in the mod's list
short const gAlwaysKicked[] = { kDudeBurningInnocent, kDudeBurningCultist, kDudeBurningZombieAxe, kDudeBurningZombieButcher,
                                kDudeBurningBeast, kDudeBeast, kDudeCultistBeast,
                                // and the mod's new ones that walk about
                                kDudeBecNecromancer, kDudeBecLich, kDudeBecMonsterDog, kDudeBecZomSlime, kDudeBecInferno };

GKDEF const gKills[] = {
    { { kDudeCultistTommy, kDudeCultistTommyProne }, 200, kNear, 140, 626, 689, 15034, gEvCultist },
    { { kDudeCultistShotgun, kDudeCultistShotgunProne }, 200, kNear, 140, 627, 690, 15035, gEvCultist },
    { { kDudeCultistTesla }, 200, kNear, 140, 628, 691, 15036, gEvCultistTesla },
    { { kDudeCultistTNT }, 200, kNear, 140, 629, 692, 15037, gEvCultistTNT },
    { { kDudeZombieAxeNormal, kDudeZombieAxeLaying }, 200, kNearer, 130, 630, 688, 4359, gEvAxeZombie },
    { { kDudeZombieButcher }, 200, kNearer, 140, 631, 687, 13816, gEvFatZombie },
    { { kDudeGargoyleFlesh }, 200, kNearer, 140, 632, 686, 13821, gEvGargoyle },
    { { kDudeGillBeast }, 200, kNearer, 140, 633, 685, 7176, gEvGillBeast },
    { { kDudeInnocent }, -1, kNearer, 140, 640, 683, 13830, gEvInnocent },
    { { kDudeHellHound }, 200, kNear, 140, 662, 682, 5633, gEvHellHound },
    { { kDudePhantasm }, 200, kNearer, 140, 663, 681, 0, gEvPhantasm },
    { { kDudeBecCrimson, kDudeBecCrimsonEarth }, 200, kNearer, 140, 693, 694, 14858, gEvCrimson },
    { { kDudeBecZombieSkeletal, kDudeBecZombieSkeletalEarth, kDudeBecZombieSkeletalSleep }, 200, kNearer, 130, 857, 858, 14994, gEvSkeletal },
    { { kDudeBecZomHalf }, -1, kNear, 80, 787, 788, 13825, gEvZomHalf },
    { { kDudeBecZomHead }, -1, kNear, 80, 787, 788, 0, gEvZomHead },
    { { kDudeBecZomSkeletalHead }, -1, kNear, 80, 859, 860, 0, gEvSkeletalHead },
    { { kDudeBecDog }, -1, kNear, 120, 875, 876, 15030, gEvDog },
};

enum { kScreenBloodTime = 90 };

// puke on Caleb's face (BEC_Players.txt ScreenPuke): it stays until he wipes it off with USE (QAV_WIPE, the
// mod's HUDQAVType 62) or a while has gone by; green from fat zombies, the red blood from Crimson Innocents
enum { kPukeTime = 600 / 2, kBloodPukeTime = 900 / 2 };
GKEVENT const gEvWipe[] = { WIPE(30), END };
GKDEF const gWipe = { { 0 }, -1, 0, 50, 713, 714, 0, gEvWipe, true };

struct GKSTATE
{
    GKDEF const *pDef;  // the kill playing, NULL for none
    QAV *pQav;
    int nTimer;
    int nScreenBlood, nScreenBloodTile;
    int nKickDelay;
    int nKickTarget;
    int nPuke;      // frames left of puke on the screen
    bool bBloodPuke;
};

Resource gBecRes;  // the mod's EXTRAC.RFF
bool bAvailable;
char szDir[BMAX_PATH];  // the mod's folder (from the game folder), "" when it isn't chosen
GKSTATE gState[kMaxPlayers];

bool fileExists(const char *pzPath)
{
    int h = kopen4load(pzPath, 0);
    if (h < 0)
        return false;
    kclose(h);
    return true;
}

// everything new in the mod's EXTRAC.RFF: its animations (SEQ, QAV) and sounds (SFX and their samples),
// read from there when they're used; the game's own files of the same ids stay
void importAll(int &nSeqs, int &nQavs, int &nSounds)
{
    nSeqs = nQavs = nSounds = 0;
    for (unsigned int i = 0; i < gBecRes.count; i++)
    {
        DICTNODE *n = &gBecRes.dict[i];
        if (!n->type || n->id == (unsigned int)-1)
            continue;
        char name[16];
        Bsnprintf(name, sizeof(name), "BEC%u", n->id);  // the mod reuses names for different ids
        if (!Bstrcasecmp(n->type, "SEQ"))
            nSeqs += gSysRes.AddFromResource(gBecRes, n, name, (int)n->id);
        else if (!Bstrcasecmp(n->type, "QAV"))
            nQavs += gSysRes.AddFromResource(gBecRes, n, name, (int)n->id);
        else if (!Bstrcasecmp(n->type, "SFX") && gBecRes.Size(n) >= (int)sizeof(SFX) && !gSoundRes.Lookup(n->id, "SFX"))
        {
            SFX sfx;
            gBecRes.Read(n, &sfx);
            sfx.rawName[8] = '\0';
            if (!gSoundRes.Lookup(sfx.rawName, "RAW"))
            {
                DICTNODE *hRaw = gBecRes.Lookup(sfx.rawName, "RAW");
                if (!hRaw || !gSoundRes.AddFromResource(gBecRes, hRaw, sfx.rawName))
                    continue;
            }
            nSounds += gSoundRes.AddFromResource(gBecRes, n, name, (int)n->id);
        }
    }
}

// another player Caleb can kick and kill (not in co-op, not his own team)
bool playerTarget(PLAYER *pPlayer, spritetype *pTarget)
{
    return IsPlayerSprite(pTarget) && gGameOptions.nGameType != kGameTypeSinglePlayer && gGameOptions.nGameType != kGameTypeCoop
        && !IsTargetTeammate(pPlayer, pTarget);
}

GKDEF const *killFor(int nType)
{
    for (auto const &k : gKills)
        for (short nDude : k.nDude)
            if (nDude && nDude == nType)
                return &k;
    return NULL;
}

// the body stays where it lands, a corpse like any other (the mod's CALLBACK_DUDE_TO_GIB only makes it
// one that gibs when shot, as Blood's corpses do)
void throwBody(PLAYER *pPlayer, GKDEF const *pDef)
{
    if (!pDef->nBodySeq || !gSysRes.Lookup(pDef->nBodySeq, "SEQ"))
        return;
    spritetype *pThing = actFireThing(pPlayer->pSprite, 0, 0, -7500, kThingBloodChunks, 1200000);
    if (!pThing || pThing->extra <= 0)
        return;
    seqSpawn(pDef->nBodySeq, 3, pThing->extra, -1);
    for (int i = 0; i < 3; i++)
        fxSpawnBlood(pThing, 0);
}

// the things the mod's kick also takes (BEC_Players.txt Iteration3): severed heads and other gibs fly
// off, an Odious Headstone gets the hard kick, the soul gib (2400) lets out a life essence
enum { kKickGib = 1, kKickStone, kKickSoul };

int kickKind(spritetype *pThing)
{
    if (pThing->statnum != kStatThing || (pThing->cstat & CSTAT_SPRITE_INVISIBLE))
        return 0;
    if (gBecFlags[pThing->index] & kBecFlagStone)
        return pThing->extra > 0 && xsprite[pThing->extra].health > 0 ? kKickStone : 0;
    switch (pThing->picnum)
    {
    case 1098: case 2548: case 2447:
        return kKickGib;
    case 2400:
        return kKickSoul;
    }
    return 0;
}

void kickThing(PLAYER *pPlayer, spritetype *pThing)
{
    spritetype *pSprite = pPlayer->pSprite;
    int const nKind = kickKind(pThing);
    int const dx = pThing->x - pSprite->x, dy = pThing->y - pSprite->y;
    if (!nKind || approxDist(dx, dy) > kNearer + (pThing->clipdist << 2))
        return;
    int const nAngle = getangle(dx, dy);
    sfxPlay3DSound(pThing, kSfxPunt, -1, 0);
    switch (nKind)
    {
    case kKickGib:  // off it goes
        xvel[pThing->index] += mulscale30(0x140000, Cos(nAngle));
        yvel[pThing->index] += mulscale30(0x140000, Sin(nAngle));
        zvel[pThing->index] -= 0x60000;
        break;
    case kKickStone:  // the hard kick: three of them break it
        actDamageSprite(pSprite->index, pThing, kDamageExplode, 34 << 4);
        break;
    case kKickSoul:  // the soul goes free, and leaves its essence
        actDropObject(pThing, kItemHealthLifeEssense);
        sfxPlay3DSound(pThing, kSfxPhanDie1, -1, 0);
        actPostSprite(pThing->index, kStatFree);
        break;
    }
}

// the kick lands on the enemy it was aimed at: the mod's three BV_QUICKKICK hits, each 4 damage (pummel)
// and a hard push (its thrust 250.33; the pitchfork's 42.67 is Blood's 174762), if it's still in reach
void kick(PLAYER *pPlayer, int nTarget)
{
    if (nTarget < 0 || nTarget >= kMaxSprites)
        return;
    spritetype *pSprite = pPlayer->pSprite;
    spritetype *pDude = &sprite[nTarget];
    if (pDude->statnum == kStatThing)
    {
        kickThing(pPlayer, pDude);
        return;
    }
    if (pDude->statnum != kStatDude || pDude->extra <= 0 || xsprite[pDude->extra].health <= 0)
        return;
    int const dx = pDude->x - pSprite->x, dy = pDude->y - pSprite->y;
    if (approxDist(dx, dy) > kNearer + (pDude->clipdist << 2))  // it got away meanwhile
        return;
    int const nAngle = getangle(dx, dy);
    int const nMass = getDudeInfo(pDude->type)->mass;
    sfxPlay3DSound(pDude, kSfxPunt, -1, 0);
    for (int i = 0; i < 3 && pDude->extra > 0 && xsprite[pDude->extra].health > 0; i++)
    {
        actDamageSprite(pSprite->index, pDude, kDamageFall, 4 << 4);
        if (nMass > 0)
        {
            int const nPush = divscale8(1025365, nMass);
            xvel[nTarget] += mulscale16(Cos(nAngle) >> 16, nPush);
            yvel[nTarget] += mulscale16(Sin(nAngle) >> 16, nPush);
        }
    }
}

void runEvent(PLAYER *pPlayer, GKSTATE *pState, GKEVENT const *pEv)
{
    spritetype *pSprite = pPlayer->pSprite;
    switch (pEv->nType)
    {
    case kEvSound:
        sfxPlay3DSound(pSprite, pEv->nSound + (pEv->nRand > 1 ? (int)Random(pEv->nRand) : 0), -1, 0);
        break;
    case kEvLaugh:
        actHealDude(pPlayer->pXSprite, 10, 200, 1);
        if (Random(100) < 50)
            sfxPlay3DSound(pSprite, kSfxPlay1109 + Random(7), -1, 0);
        if (pState->nScreenBlood <= 0)
        {
            pState->nScreenBlood = kScreenBloodTime;
            pState->nScreenBloodTile = 5532 + Random(3);
        }
        break;
    case kEvBody:
        throwBody(pPlayer, pState->pDef);
        break;
    case kEvKick:
        kick(pPlayer, pState->nKickTarget);
        break;
    case kEvWipe:
        pState->nPuke = 0;
        break;
    }
}

// gktest <n>: a nearly dead enemy for kill n appears in front of Caleb (for trying the kills)
int osdcmd_gktest(osdcmdptr_t parm)
{
    if (parm->numparms < 1 || !gGameStarted || !gMe || !gMe->pSprite)
        return OSDCMD_SHOWHELP;
    if (!Bstrcasecmp(parm->parms[0], "kick"))  // a healthy cultist: USE kicks it
    {
        spritetype *pDude = actSpawnDude(gMe->pSprite, kDudeCultistTommy, 600, 0);
        if (pDude)
            OSD_Printf("Kick test: enemy type %d, %d health\n", pDude->type, xsprite[pDude->extra].health >> 4);
        return OSDCMD_OK;
    }
    int const n = atoi(parm->parms[0]);
    if (n < 0 || n >= (int)ARRAY_SIZE(gKills))
        return OSDCMD_SHOWHELP;
    spritetype *pDude = actSpawnDude(gMe->pSprite, gKills[n].nDude[0], 600, 0);
    if (!pDude)
        return OSDCMD_OK;
    xsprite[pDude->extra].health = gKills[n].nHealth >= 0 ? gKills[n].nHealth / 2 : 160;
    OSD_Printf("Gory Kill test: enemy type %d, %d health\n", pDude->type, xsprite[pDude->extra].health >> 4);
    return OSDCMD_OK;
}

} // namespace

void goryKillSetModDir(const char *pzDir)
{
    Bstrncpyz(szDir, pzDir ? pzDir : "", sizeof(szDir));
}

const char *goryKillModFile(const char *pzName)
{
    static char path[BMAX_PATH];
    if (!szDir[0])
        return NULL;
    BDIR *dir = Bopendir(szDir);
    if (!dir)
        return NULL;
    bool bFound = false;
    while (Bdirent *d = Breaddir(dir))
    {
        if (!Bstrcasecmp(d->name, pzName))
        {
            Bsnprintf(path, sizeof(path), "%s/%s", szDir, d->name);
            bFound = true;
            break;
        }
    }
    Bclosedir(dir);
    return bFound ? path : NULL;
}

void goryKillInit(void)
{
    OSD_RegisterFunction("gktest", "gktest <0-10>: a nearly dead enemy for that Gory Kill appears in front of you", osdcmd_gktest);

    bAvailable = false;
    if (!szDir[0])
        return;
    char const *pzExtra = goryKillModFile("EXTRAC.RFF");
    if (!pzExtra || !fileExists(pzExtra))
    {
        LOG_F(ERROR, "Blood Extra Crispy: no EXTRAC.RFF in %s", szDir);
        return;
    }
    char path[BMAX_PATH];
    Bstrncpyz(path, pzExtra, sizeof(path));
    gBecRes.Init(path);
    // the mod's files have ids, but not the flag Blood looks for before it indexes them
    for (unsigned int i = 0; i < gBecRes.count; i++)
        gBecRes.dict[i].flags |= DICT_ID;
    gBecRes.Reindex();
    bAvailable = true;

    // the mod's art comes first: its own title, menus and sprites, and its new tiles
    artSetExtraDir(szDir, 1);

    int nSeqs, nQavs, nSounds;
    importAll(nSeqs, nQavs, nSounds);
    int nKills = 0;
    for (auto const &k : gKills)
        nKills += !!gSysRes.Lookup(k.nQav, "QAV");
    LOG_F(INFO, "Blood Extra Crispy: files in %s (%d new animations, %d new weapon animations, %d new sounds; %d of %d kills)",
          szDir, nSeqs, nQavs, nSounds, nKills, (int)ARRAY_SIZE(gKills));

    // its new enemies (their definitions are in its BloodEX.kpf)
    becDudesInit(goryKillModFile("BloodEX.kpf"));
    becWeaponsInit();
}

// in Blood's level preload (PreloadTiles): the mod's animations this level needs, loaded with it, not
// the first time they're used (the kills' hands for the enemies there, the kick, the extra deaths, the
// mod's weapons carried)
void goryKillPrecache(void)
{
    if (!bAvailable)
        return;
    auto precacheQav = [](int nQav) {
        if (DICTNODE *hQav = gBecRes.Lookup(nQav, "QAV"))
            ((QAV *)gBecRes.Lock(hQav))->Precache();
    };
    precacheQav(gKick.nQav);
    for (auto const &k : gKills)
    {
        bool bThere = false;
        for (int nSprite = headspritestat[kStatDude]; nSprite >= 0 && !bThere; nSprite = nextspritestat[nSprite])
            for (short nDude : k.nDude)
                bThere |= nDude && nDude == sprite[nSprite].type;
        if (!bThere)
            continue;
        precacheQav(k.nQav);
        if (k.nBodySeq)
            seqPrecacheId(k.nBodySeq);
    }
    becAiPrecache();
    becWeaponsPrecache();
}

bool goryKillAvailable(void)
{
    return bAvailable;
}

bool goryKillActive(PLAYER *pPlayer)
{
    return pPlayer && gState[pPlayer->nPlayer].pDef != NULL && !gState[pPlayer->nPlayer].pDef->bKick;
}

bool goryKillBusy(PLAYER *pPlayer)
{
    return pPlayer && gState[pPlayer->nPlayer].pDef != NULL;
}

bool goryKillTryStart(PLAYER *pPlayer)
{
    if (!bAvailable || !pPlayer || goryKillBusy(pPlayer))
        return false;
    spritetype *pSprite = pPlayer->pSprite;
    XSPRITE *pXSprite = pPlayer->pXSprite;
    GKSTATE *pWiping = &gState[pPlayer->nPlayer];
    if (pWiping->nPuke > 0 && pXSprite && pXSprite->health > 0)  // puke on his face: wiped off first
    {
        DICTNODE *hQav = gBecRes.Lookup(powerupCheck(pPlayer, kPwUpShadowCloak) ? gWipe.nQavCloak : gWipe.nQav, "QAV");
        if (!hQav)
        {
            pWiping->nPuke = 0;
            return false;
        }
        pWiping->pQav = (QAV *)gBecRes.Lock(hQav);
        pWiping->pQav->nSprite = -1;
        pWiping->pQav->Precache();
        pWiping->pDef = &gWipe;
        pWiping->nTimer = gWipe.nLength;
        return false;  // USE still opens what's in front
    }
    if (!pSprite || !pXSprite || pXSprite->health <= 0 || pPlayer->posture == kPostureSwim || pXSprite->height > 16)
        return false;

    // the nearest enemy in front for a Gory Kill; failing that, the nearest one to kick
    int nBest = -1, nBestDist = INT32_MAX, nKick = -1, nKickDist = INT32_MAX;
    GKDEF const *pBestDef = NULL;
    GKSTATE *pState = &gState[pPlayer->nPlayer];
    // the sector Caleb's eyes are in (his sprite's can be the one below a step he stands over)
    int16_t nEyeSect = pSprite->sectnum;
    updatesectorz(pSprite->x, pSprite->y, pPlayer->zView, &nEyeSect);
    if (nEyeSect < 0)
        nEyeSect = pSprite->sectnum;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pDude = &sprite[nSprite];
        if (pDude == pSprite || pDude->extra <= 0 || pDude->extra >= kMaxXSprites)
            continue;
        bool const bPlayer = IsPlayerSprite(pDude);
        if (bPlayer && !playerTarget(pPlayer, pDude))
            continue;
        XSPRITE *pXDude = &xsprite[pDude->extra];
        if (pXDude->health <= 0)
            continue;
        GKDEF const *pDef = bPlayer ? &gPlayerKill : killFor(pDude->type);
        bool const bGory = pDef && (pDef->nHealth < 0 || pXDude->health <= pDef->nHealth);
        bool bKickable = pDef && !bGory;
        for (short nType : gAlwaysKicked)
            bKickable |= nType == pDude->type;
        if (!bGory && !bKickable)
            continue;
        int const nRange = bGory ? pDef->nDist : (int)kNearer;
        int const dx = pDude->x - pSprite->x, dy = pDude->y - pSprite->y;
        int const nDist = approxDist(dx, dy);
        if (nDist > nRange || klabs(pDude->z - pSprite->z) > (nRange << 4))
            continue;
        int const nAngle = (getangle(dx, dy) - pSprite->ang) & 2047;
        if (nAngle > 512 && nAngle < 1536)  // behind Caleb
            continue;
        int nTop, nBottom;
        GetSpriteExtents(pDude, &nTop, &nBottom);
        int const nDudeZ = (nTop + nBottom) >> 1;  // its middle, as Blood's AI looks
        // reachable: a straight line from Caleb's eyes hits it (as aiming does; cansee() stops at some
        // one-way walls Blood draws through)
        bool bReach = cansee(pSprite->x, pSprite->y, pPlayer->zView, nEyeSect, pDude->x, pDude->y, nDudeZ, pDude->sectnum);
        if (!bReach && (dx || dy))
            bReach = HitScan(pSprite, pPlayer->zView, dx, dy, (nDudeZ - pPlayer->zView) >> 4, CLIPMASK1, 0) == 3
                     && gHitInfo.hitsprite == nSprite;
        if (!bReach)
            continue;
        if (bGory && nDist < nBestDist)
        {
            nBest = nSprite;
            nBestDist = nDist;
            pBestDef = pDef;
        }
        else if (!bGory && nDist < nKickDist)
        {
            nKick = nSprite;
            nKickDist = nDist;
        }
    }
    // things: a cultist lying there shot in the belly, suffering (the mod's kill of that cultist), and
    // what can be kicked
    bool bCorpse = false;
    for (int nSprite = headspritestat[kStatThing]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pThing = &sprite[nSprite];
        GKDEF const *pDef = NULL;
        if (pThing->type == kThingBloodChunks && pThing->extra > 0 && xsprite[pThing->extra].data1 != 8)
        {
            static char const *const gSuffer[4] = { "SEQ_CU1SUF1", "SEQ_CU2SUF1", "SEQ_CU3SUF1", "SEQ_CU4SUF1" };
            int const nSeq = seqGetID(3, pThing->extra);
            for (int i = 0; i < 4 && nSeq > 0; i++)
                if (nSeq == becDefine(gSuffer[i], -1))
                    pDef = &gKills[i];
        }
        bool const bKickable = !pDef && kickKind(pThing) != 0;
        if (!pDef && !bKickable)
            continue;
        int const dx = pThing->x - pSprite->x, dy = pThing->y - pSprite->y;
        int const nDist = approxDist(dx, dy);
        if (nDist > kNearer || klabs(pThing->z - pSprite->z) > (kNearer << 4))
            continue;
        int const nAngle = (getangle(dx, dy) - pSprite->ang) & 2047;
        if (nAngle > 512 && nAngle < 1536)
            continue;
        int nTop, nBottom;
        GetSpriteExtents(pThing, &nTop, &nBottom);
        if (!cansee(pSprite->x, pSprite->y, pPlayer->zView, nEyeSect, pThing->x, pThing->y, (nTop + nBottom) >> 1, pThing->sectnum))
            continue;
        if (pDef && nDist < nBestDist)
            nBest = nSprite, nBestDist = nDist, pBestDef = pDef, bCorpse = true;
        else if (bKickable && nDist < nKickDist)
            nKick = nSprite, nKickDist = nDist;
    }
    if (nBest < 0 && (nKick < 0 || pState->nKickDelay > 0))
        return false;
    GKDEF const *pStart = nBest >= 0 ? pBestDef : &gKick;

    int const nQav = powerupCheck(pPlayer, kPwUpShadowCloak) ? pStart->nQavCloak : pStart->nQav;
    DICTNODE *hQav = gBecRes.Lookup(nQav, "QAV");
    if (!hQav)
        hQav = gBecRes.Lookup(pStart->nQav, "QAV");
    if (!hQav)
        return false;

    pState->pQav = (QAV *)gBecRes.Lock(hQav);
    pState->pQav->nSprite = -1;
    pState->pQav->Precache();
    pState->pDef = pStart;
    pState->nTimer = pStart->nLength;
    if (pStart->bKick)
    {
        pState->nKickDelay = kKickDelay;
        pState->nKickTarget = nKick;
        return true;
    }

    spritetype *pDude = &sprite[nBest];
    if (bCorpse)  // the suffering cultist: no more of him
    {
        actPostSprite(nBest, kStatFree);
        return true;
    }
    if (IsPlayerSprite(pDude))  // another player: dies (Caleb's frag), his body stays
    {
        LOG_F(INFO, "Gory Kill: player %d finished off player %d", pPlayer->nPlayer, pDude->type - kDudePlayer1);
        actDamageSprite(pSprite->index, pDude, kDamageFall, xsprite[pDude->extra].health + (16 << 4));
        return true;
    }
    // the enemy is gone: it's in Caleb's hands now (the QAV); it counts as killed, its drops stay
    xsprite[pDude->extra].health = 0;
    actKillDude(pSprite->index, pDude, kDamageFall, 16);
    actPostSprite(nBest, kStatFree);
    return true;
}

void goryKillFrame(void)
{
    for (int i = connecthead; i >= 0; i = connectpoint2[i])
    {
        PLAYER *pPlayer = &gPlayer[i];
        GKSTATE *pState = &gState[i];
        if (pState->nScreenBlood > 0)
            pState->nScreenBlood--;
        if (pState->nPuke > 0 && (pPlayer->pXSprite == NULL || pPlayer->pXSprite->health <= 0))
            pState->nPuke = 0;
        else if (pState->nPuke > 0)
            pState->nPuke--;
        if (pState->nKickDelay > 0)
            pState->nKickDelay -= 2;
        if (!pState->pDef)
            continue;
        if (!pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
        {
            pState->pDef = NULL;
            continue;
        }
        int const nBefore = pState->nTimer;
        pState->nTimer -= 2;
        for (GKEVENT const *pEv = pState->pDef->pEvents; pEv->nType != kEvEnd; pEv++)
            if (pEv->nTime < nBefore && pEv->nTime >= pState->nTimer)
                runEvent(pPlayer, pState, pEv);
        if (pState->nTimer <= 0)
            pState->pDef = NULL;
    }
}

bool goryKillDraw(PLAYER *pPlayer, int nShade, int x, int y, int nPal)
{
    if (!goryKillBusy(pPlayer))
        return false;
    GKSTATE *pState = &gState[pPlayer->nPlayer];
    QAV *pQav = pState->pQav;
    if (!pQav)
        return false;
    // the timer goes 60 a second, QAV ticks 120
    int const nTicks = ClipRange((pState->pDef->nLength - pState->nTimer) * 2, 0, pQav->at10 - 1);
    pQav->x = x;
    pQav->y = y;
    int nFlags = 2 | kQavOrientationQ16;
    if (powerupCheck(pPlayer, kPwUpShadowCloak))
        nFlags |= 1;
    pQav->Draw(nTicks, nFlags, nShade, nPal, 0);
    return true;
}

void goryKillScreenPuke(PLAYER *pPlayer, bool bBlood)
{
    if (!bAvailable || !pPlayer)
        return;
    GKSTATE *pState = &gState[pPlayer->nPlayer];
    pState->nPuke = bBlood ? kBloodPukeTime : kPukeTime;
    pState->bBloodPuke = bBlood;
}

void goryKillDrawScreenBlood(PLAYER *pPlayer)
{
    if (!pPlayer)
        return;
    becWeaponsHudDraw(pPlayer);  // the mod's HUD pictures, under the blood and puke
    GKSTATE *pState = &gState[pPlayer->nPlayer];
    if (pState->nPuke > 0)
    {
        // a translucent layer and a solid one, as the mod's two HUD pictures (5536+5617 green, 5615+5616 red)
        int const nBack = pState->bBloodPuke ? 5615 : 5536, nFront = pState->bBloodPuke ? 5616 : 5617;
        if (tilesiz[nBack].x > 0)
            rotatesprite(160 << 16, 100 << 16, 65536, 0, nBack, 0, 0, RS_AUTO | RS_STRETCH | RS_TRANS1, 0, 0, xdim - 1, ydim - 1);
        if (tilesiz[nFront].x > 0)
            rotatesprite(160 << 16, 100 << 16, 65536, 0, nFront, 0, 0, RS_AUTO | RS_STRETCH, 0, 0, xdim - 1, ydim - 1);
    }
    if (pState->nScreenBlood <= 0 || tilesiz[pState->nScreenBloodTile].x <= 0)
        return;
    // translucent, more so as it fades
    int const nFlags = RS_AUTO | RS_STRETCH | RS_TRANS1 | (pState->nScreenBlood < kScreenBloodTime / 2 ? RS_TRANS2 : 0);
    rotatesprite(160 << 16, 100 << 16, 65536, 0, pState->nScreenBloodTile, 0, 0, nFlags, 0, 0, xdim - 1, ydim - 1);
}

void goryKillReset(void)
{
    for (auto &s : gState)
        s = GKSTATE();
}
