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

// Blood Extra Crispy's weapons (see becweapons.h). The mod writes each one as a script class for
// Nightdive's engine (scripts/weapons/ in its BloodEX.kpf): what happens when it's raised, lowered, idle,
// fired and alt-fired, every tick, and at the trigger frames of its QAVs. The same things are here,
// written the way Blood's own weapons are (weapon.cpp), with the mod's QAVs, sounds and numbers.
//
// Ammo: the guns that take Blood's ammo (the Trench Gun and the double shotguns take shells, the
// double tommy guns bullets, the double flare guns flares) share it with Blood's; the others have
// their own.

#include <unordered_map>

#include "build.h"
#include "compat.h"
#include "common_game.h"
#include "actor.h"
#include "becdefs.h"
#include "becdudes.h"
#include "becweapons.h"
#include "blood.h"
#include "db.h"
#include "dude.h"
#include "eventq.h"
#include "fx.h"
#include "globals.h"
#include "levels.h"
#include "loadsave.h"
#include "mmulti.h"
#include "osd.h"
#include "player.h"
#include "qav.h"
#include "resource.h"
#include "seq.h"
#include "sfx.h"
#include "trig.h"
#include "view.h"

void WeaponLower(PLAYER *pPlayer);
char WeaponIsEquipable(PLAYER *pPlayer, int nWeapon, char checkUnderwater);

namespace
{

enum
{
    kBecGatling,
    kBecTrench,
    kBecDevastator,
    kBecRocket,
    kBecRailgun,
    kBecDualTommy,
    kBecDualShotgun,
    kBecDualFlare,
    kBecMower,         // the Death Mower: grabbed with USE in the maps (on no key)
    kBecTurret,        // the Maxim gun on its stand: USE gets behind it, RUN rips it off
    kBecMaxim,         // the Maxim carried
    kBecBlunderbuss,   // one shot, then it's thrown away
    kBecCannon,        // a loaded cannon: USE gets behind it, one shot
    kBecCannonBall,    // a cannon ball carried to an empty cannon
    kBecFireworks,     // the fireworks cannon: a show, then it's thrown away
    kBecWeaponMax
};

enum { kFireworksPicnum = 7590, kAAGunPicnum = 6394 };  // the AA guns are 6394-6396

enum
{
    kMowerPicnum = 5840, kMaximPicnum = 5866, kBlunderbussPicnum = 5119,
    kCannonPicnum = 6276, kCannonEmptyPicnum = 6282, kCannonBallPicnum = 6281,
};
enum { kMowerTime = 37 };  // seconds: the mod's 2240 (its timer goes 2 a frame)

// the mod's names, looked up once
#define QAVID(name) ([]() { static int n = becDefine(name, -1); return n; }())
#define SFX(name) ([]() { static int n = becDefine(name, -1); return n; }())

struct WSTATE
{
    int8_t nActive;     // the mod's weapon in hand, -1 for none
    int8_t nPending;    // the one to raise once Blood's is put away, -1 for none
    int8_t nPhase;      // what the QAV playing is for
    int8_t nNext;       // after the one in hand is lowered: Blood's weapon (positive) or the mod's (-2 - n)
    int16_t nQav;       // the QAV playing, -1 for none
    int16_t nIdleQav;   // the one to loop between shots
    int32_t nTimer;     // ticks left of it (120 a second), as Blood's weaponTimer
    uint32_t nHave;     // the mod's weapons Caleb has (a bit each)
    int16_t nAmmo[kBecWeaponMax];  // their own ammo
    int32_t nVar[6];    // the weapon in hand's own counters (rounds in the magazine...)
    int16_t nTurret;    // the Maxim stand Caleb is behind
};

enum { kPhaseNone, kPhaseRaise, kPhaseIdle, kPhaseFire, kPhaseLower };

enum { kPickupSound = 22071 };  // the mod's weapon pickups' pickup.pickupSound

WSTATE gWState[kMaxPlayers];

struct CTX;

struct WEAPON
{
    char const *pzName;
    int nSlot;              // the number key it's on (with Blood's weapon of that number)
    int nBloodAmmo;         // the Blood ammo it takes, -1 for its own
    int nMaxAmmo;           // its own ammo's most
    int nGive;              // ammo a pickup gives
    char const *pzIcon;     // its pickup picture (a #define of the mod's) or a tile number
    int nIcon;
    char const *pzUp, *pzIdle, *pzDown;
    void (*pFire)(CTX &c, bool bAlt);       // fire pressed while it's idle
    void (*pTick)(CTX &c);                  // every frame while it's in hand
    void (*pTrigger)(CTX &c, int nTrigger); // a trigger frame of its QAV
};

extern WEAPON const gWeapons[kBecWeaponMax];

std::unordered_map<int, QAV *> *pQavs;
bool bActive;
int nQavClient;

QAV *getQav(int nId)
{
    if (nId < 0 || !pQavs)
        return NULL;
    auto it = pQavs->find(nId);
    if (it != pQavs->end())
        return it->second;
    QAV *pQav = NULL;
    if (DICTNODE *hQav = gSysRes.Lookup(nId, "QAV"))
    {
        pQav = (QAV *)gSysRes.Lock(hQav);
        pQav->nSprite = -1;
        pQav->Preload();
    }
    (*pQavs)[nId] = pQav;
    return pQav;
}

// the weapon in hand and its player, as the mod's script sees them (self.Owner(), self.ObjectOwner())
struct CTX
{
    PLAYER *pPlayer;
    WSTATE *pState;
    WEAPON const *pWeapon;
    int nWeapon;

    spritetype *sprite() { return pPlayer->pSprite; }
    int ammo()
    {
        return pWeapon->nBloodAmmo >= 0 ? pPlayer->ammoCount[pWeapon->nBloodAmmo] : pState->nAmmo[nWeapon];
    }
    void useAmmo(int n)
    {
        if (gInfiniteAmmo)
            return;
        if (pWeapon->nBloodAmmo >= 0)
            pPlayer->ammoCount[pWeapon->nBloodAmmo] = ClipLow(pPlayer->ammoCount[pWeapon->nBloodAmmo] - n, 0);
        else
            pState->nAmmo[nWeapon] = ClipLow(pState->nAmmo[nWeapon] - n, 0);
    }
    bool buttonFire() { return pPlayer->input.buttonFlags.shoot; }
    bool buttonAlt() { return pPlayer->input.buttonFlags.shoot2; }
    int playing() { return pState->nQav; }
    void play(int nQav, int nPhase)
    {
        QAV *pQav = getQav(nQav);
        if (!pQav)
            return;
        pState->nQav = nQav;
        pState->nPhase = nPhase;
        pState->nTimer = pQav->at10;
        pQav->nSprite = pPlayer->pSprite->index;
        // its first frame now (its trigger, often): as Blood's StartQAV
        pQav->Play(-kTicsPerFrame, 0, nQavClient, pPlayer);
    }
    void idle(int nIdle)
    {
        pState->nIdleQav = nIdle;
        play(nIdle, kPhaseIdle);
        if (!getQav(nIdle))  // nothing in its hands to show (a cannon ball carried)
            pState->nPhase = kPhaseIdle, pState->nQav = -1, pState->nTimer = 0;
    }
    void lower()
    {
        play(becDefine(pWeapon->pzDown, -1), kPhaseLower);
        if (pState->nPhase != kPhaseLower || !getQav(becDefine(pWeapon->pzDown, -1)))  // none: down at once
            pState->nPhase = kPhaseLower, pState->nQav = -1, pState->nTimer = 0;
    }
    void sound(int nSfx)
    {
        if (nSfx >= 0)
            sfxPlay3DSound(pPlayer->pSprite, nSfx, -1, 0);
    }
    // a vector where Caleb aims, with the mod's spread (its BiRandomA14 spread: Random2 here)
    void vector(char const *pzVector, int nSpread, int nOffset = 0)
    {
        Aim *aim = &pPlayer->aim;
        becFireVector(pPlayer->pSprite, nOffset, pPlayer->zWeapon - pPlayer->pSprite->z, aim->dx + Random2(nSpread),
                      aim->dy + Random2(nSpread), aim->dz + Random2(nSpread), pzVector);
    }
    spritetype *missile(int nType, int nSpread, int nOffset = 0)
    {
        Aim *aim = &pPlayer->aim;
        return actFireMissile(pPlayer->pSprite, nOffset, pPlayer->zWeapon - pPlayer->pSprite->z, aim->dx + Random2(nSpread),
                              aim->dy + Random2(nSpread), aim->dz + Random2(nSpread), nType);
    }
    void flash(int nVisibility) { pPlayer->visibility = nVisibility; }
    void kick(int nTilt) { pPlayer->tiltEffect = nTilt; }
};

//
// the mod's missiles: Blood's, with the mod's picture, and the mod's explosion where they hit
//

enum { kBoomNone, kBoomSmall, kBoomStandard, kBoomLarge, kBoomNuke };

uint8_t gMissileBoom[kMaxSprites];

spritetype *rocket(CTX &c, int nPicnum, int nRepeat, int nBoom, int nSpeed, int nSpread, int nOffset = 0)
{
    spritetype *pMissile = c.missile(kMissileFireballNapalm, nSpread, nOffset);
    if (!pMissile)
        return NULL;
    pMissile->picnum = nPicnum;
    pMissile->xrepeat = pMissile->yrepeat = nRepeat;
    gMissileBoom[pMissile->index] = nBoom;
    if (pMissile->extra > 0)
        seqKill(3, pMissile->extra);  // its own picture, not the napalm ball's
    xvel[pMissile->index] = mulscale16(xvel[pMissile->index], nSpeed);
    yvel[pMissile->index] = mulscale16(yvel[pMissile->index], nSpeed);
    zvel[pMissile->index] = mulscale16(zvel[pMissile->index], nSpeed);
    return pMissile;
}

//
// the Gatling Gun (weapon_gatlinggun.txt): a bullet a tick while the trigger is held
//

void gatlingFire(CTX &c, bool bAlt)
{
    if (c.ammo() <= 0)
        return;
    c.play(bAlt ? QAVID("QAV_GATFIRE3") : QAVID("QAV_GATFIRE"), kPhaseFire);
}

void gatlingTick(CTX &c)
{
    int const nFire = QAVID("QAV_GATFIRE"), nFire3 = QAVID("QAV_GATFIRE3");
    bool const bOverkill = c.playing() == nFire3;
    if (c.playing() != nFire && !bOverkill)
        return;
    if (!(c.buttonFire() || c.buttonAlt()) || c.ammo() <= 0)
    {
        sfxKill3DSound(c.sprite(), -1, SFX("SFX_GATCRAN"));
        c.idle(QAVID("QAV_GATIDLE"));
        return;
    }
    // overkill: wider and two at a time
    int const nShots = bOverkill ? 2 : 1;
    for (int i = 0; i < nShots && c.ammo() > 0; i++)
    {
        c.vector(Random(100) <= 10 ? "BV_GATLINGBULLETFLESH3" : "BV_GATLINGBULLETFLESH", bOverkill ? 2250 : 1250);
        c.useAmmo(1);
    }
    if ((gFrame & 1) == 0)
        c.sound(SFX("SFX_GATF"));
    c.flash(40);
    c.kick(15);
    // the spin goes on while it's held
    if (c.pState->nTimer <= kTicsPerFrame)
        c.play(c.playing(), kPhaseFire);
}

//
// the Devastator (weapon_devastator.txt): two small rockets a shot
//

void devastatorFire(CTX &c, bool bAlt)
{
    UNREFERENCED_PARAMETER(bAlt);
    if (c.ammo() <= 0)
        return;
    c.play(QAVID("QAV_DUKEF1"), kPhaseFire);
}

void devastatorTrigger(CTX &c, int nTrigger)
{
    UNREFERENCED_PARAMETER(nTrigger);
    if (c.ammo() <= 0)
        return;
    c.useAmmo(2);
    c.sound(SFX("SFX_DEVFIRE"));
    c.sound(SFX("SFX_GORGEF1"));
    rocket(c, 7152, 48, kBoomSmall, 0x18000, 300, -30);
    rocket(c, 7152, 48, kBoomSmall, 0x18000, 300, 30);
    c.flash(30);
    c.kick(20);
}

//
// the rocket launcher (weapon_swrl.txt, Shadow Warrior's): a rocket, or the nuke on alt fire
//

void rocketFire(CTX &c, bool bAlt)
{
    if (c.ammo() <= 0)
        return;
    c.pState->nVar[0] = bAlt && c.ammo() >= 10;  // the nuke takes ten
    c.play(c.pState->nVar[0] ? QAVID("QAV_SWFIREN") : QAVID("QAV_SWFIRER"), kPhaseFire);
}

void rocketTrigger(CTX &c, int nTrigger)
{
    UNREFERENCED_PARAMETER(nTrigger);
    if (c.ammo() <= 0)
        return;
    c.sound(SFX("SFX_SWFIRE"));
    if (c.pState->nVar[0])
    {
        c.useAmmo(10);
        c.sound(SFX("SFX_NUKWARN"));
        rocket(c, 7157, 64, kBoomNuke, 0x8000, 0);
    }
    else
    {
        c.useAmmo(1);
        rocket(c, 7157, 64, kBoomStandard, 0x10000, 0);
    }
    c.flash(40);
    c.kick(30);
}

//
// the Devil's Bore (weapon_railgun.txt): a rail that goes through everything in its way
//

void railFire(CTX &c, bool bAlt)
{
    UNREFERENCED_PARAMETER(bAlt);
    if (c.ammo() <= 0)
        return;
    c.play(QAVID("QAV_RAILGF"), kPhaseFire);
}

void railTrigger(CTX &c, int nTrigger)
{
    if (nTrigger != 1 || c.playing() != QAVID("QAV_RAILGF") || c.ammo() <= 0)  // 2 is its last frame
        return;
    c.useAmmo(1);
    c.sound(SFX("SFX_RGFIRE1"));
    // the mod's MISSILE_RailGun hits with BV_RAILGUN all along its way: here three hits of it
    for (int i = 0; i < 3; i++)
        c.vector("BV_RAILGUN", 0);
    c.flash(60);
    c.kick(40);
}

//
// the Trench Gun (weapon_trenchgun.txt): eight pellets a shot, its alt fire four dragon's breath flares
//

void trenchFire(CTX &c, bool bAlt)
{
    if (c.ammo() <= 0)
        return;
    if (bAlt && c.ammo() >= 3)
        c.play(QAVID("QAV_PUMPF"), kPhaseFire);
    else
        c.play(QAVID("QAV_PUMPSF"), kPhaseFire);
}

void trenchTrigger(CTX &c, int nTrigger)
{
    int const nQav = c.playing();
    if (nTrigger == 2 || nQav == QAVID("QAV_PUMPUL") || nQav == QAVID("QAV_PUMPE"))
    {
        c.sound(SFX("SFX_TPUMP"));
        return;
    }
    if (nTrigger != 1 || c.ammo() <= 0)
        return;
    c.sound(SFX("SFX_TFIRE"));
    c.sound(SFX("SFX_SHOTFIR2"));
    c.flash(20);
    c.kick(30);
    if (nQav == QAVID("QAV_PUMPF"))
    {
        c.useAmmo(3);
        for (int i = 0; i < 4; i++)
        {
            Aim *aim = &c.pPlayer->aim;
            actFireMissile(c.sprite(), 0, c.pPlayer->zWeapon - c.sprite()->z, aim->dx + Random2(750), aim->dy + Random2(750),
                           aim->dz + Random2(750), kMissileFlareRegular);
        }
    }
    else
    {
        c.useAmmo(1);
        for (int i = 0; i < 8; i++)
            c.vector("BV_SHELLADSFLESH", 1300);
    }
}

//
// the double guns (weapon_dualtommygun.txt, dualsawoff, dualflaregun): Blood's akimbo QAVs, with the
// mod's own changes
//

void dualTommyFire(CTX &c, bool bAlt)
{
    if (c.ammo() < 2)
        return;
    c.play(bAlt ? QAVID("QAV_2TOMALT") : QAVID("QAV_2TOMFIRE"), kPhaseFire);
}

void dualTommyTick(CTX &c)
{
    // the trigger held: it goes on firing
    if (c.playing() == QAVID("QAV_2TOMFIRE") && c.pState->nTimer <= kTicsPerFrame && c.buttonFire() && c.ammo() >= 2)
        c.play(QAVID("QAV_2TOMFIRE"), kPhaseFire);
}

void dualTommyTrigger(CTX &c, int nTrigger)
{
    UNREFERENCED_PARAMETER(nTrigger);
    if (c.ammo() < 2)
        return;
    bool const bWide = c.playing() == QAVID("QAV_2TOMALT");
    c.sound(SFX("SFX__TOM"));
    c.vector(bWide ? "BV_BULLET_WIDEFLESH" : "BV_BULLETFLESH", bWide ? 3000 : 1200, -120);
    c.vector(bWide ? "BV_BULLET_WIDEFLESH" : "BV_BULLETFLESH", bWide ? 3000 : 1200, 120);
    c.useAmmo(2);
    c.flash(30);
    c.kick(20);
}

void dualShotgunFire(CTX &c, bool bAlt)
{
    if (c.ammo() < 2)
        return;
    if (bAlt && c.ammo() >= 4)
        c.play(QAVID("QAV_2SHOTF3"), kPhaseFire);  // all four barrels
    else
        c.play(QAVID("QAV_2SHOTFIR"), kPhaseFire);
}

void dualShotgunTrigger(CTX &c, int nTrigger)
{
    UNREFERENCED_PARAMETER(nTrigger);
    bool const bAll = c.playing() == QAVID("QAV_2SHOTF3");
    int const nShells = bAll ? 4 : 2;
    if (c.ammo() < nShells)
        return;
    c.useAmmo(nShells);
    c.sound(SFX("SFX_SHOTFIRE"));
    c.sound(SFX("SFX_SHOTFIR2"));
    for (int i = 0; i < nShells * 8; i++)
        c.vector(bAll ? "BV_SHELL_WIDEFLESH" : "BV_SHELLFLESH", bAll ? 3000 : 2000, (i & 1) ? 120 : -120);
    c.flash(bAll ? 50 : 30);
    c.kick(bAll ? 60 : 40);
}

void dualFlareFire(CTX &c, bool bAlt)
{
    if (c.ammo() < 1)
        return;
    c.pState->nVar[0] = bAlt && c.ammo() >= 8;  // the alt fire: a burst of flares from both
    c.play(QAVID("QAV_FLAR2FIR"), kPhaseFire);
}

void dualFlareTrigger(CTX &c, int nTrigger)
{
    UNREFERENCED_PARAMETER(nTrigger);
    if (c.ammo() < 1)
        return;
    c.sound(SFX("SFX_FLAREF"));
    if (c.pState->nVar[0])
    {
        c.useAmmo(8);
        for (int i = 0; i < 8; i++)
            c.missile(kMissileFlareRegular, 1500, (i & 1) ? 120 : -120);
    }
    else
    {
        c.useAmmo(1);
        c.missile(kMissileFlareRegular, 200, (nTrigger & 1) ? 120 : -120);
    }
    c.flash(30);
}

//
// the Death Mower (BEC_Players.txt, "DEATH MOWER"): it runs on its own and shreds what's in front of it;
// fire throws it away. Its ammo is the seconds it has left.
//

void mowerFire(CTX &c, bool bAlt)
{
    UNREFERENCED_PARAMETER(bAlt);
    c.pState->nAmmo[kBecMower] = 0;  // thrown away (the idle code puts it down)
}

void mowerTick(CTX &c)
{
    if (c.pState->nPhase == kPhaseLower || c.pState->nPhase == kPhaseRaise)
        return;
    c.vector("BV_MOWER", 3000);
    if (++c.pState->nVar[0] >= kTicsPerSec)  // a second gone
    {
        c.pState->nVar[0] = 0;
        c.useAmmo(1);
        c.sound(SFX("SFX_MOWERA"));
    }
    // bloodier as it goes (the mod's QAV_MOWERA, B, C, D)
    int const nLeft = c.ammo();
    int const nIdle = nLeft > kMowerTime * 3 / 4 ? QAVID("QAV_MOWERA") : nLeft > kMowerTime / 2 ? QAVID("QAV_MOWERB")
                    : nLeft > kMowerTime / 4 ? QAVID("QAV_MOWERC") : QAVID("QAV_MOWERD");
    if (nIdle >= 0 && nIdle != c.pState->nIdleQav)
        c.idle(nIdle);
    c.kick(5);
}

//
// the Maxim gun: on its stand (BEC_Players.txt, "MAXIM TURRET") and carried (weapon_maxim.txt)
//

void turretFire(CTX &c, bool bAlt)
{
    UNREFERENCED_PARAMETER(bAlt);
    int const r = Random(150);
    c.play(r <= 25 ? QAVID("QAV_MAXIM2") : r <= 100 ? QAVID("QAV_MAXIM3") + (int)Random(2) : QAVID("QAV_MAXIM6"), kPhaseFire);
    for (int i = 0; i < 2; i++)
        c.vector(Random(100) <= 10 ? "BV_GATLINGBULLETFLESH3" : "BV_GATLINGBULLETFLESH", 625);
    c.sound(SFX("SFX_MAXFIRE"));
    c.flash(40);
    c.kick(10);
}

void maximFire(CTX &c, bool bAlt)
{
    UNREFERENCED_PARAMETER(bAlt);
    c.play(QAVID("QAV_MAXFI"), kPhaseFire);
}

void maximTrigger(CTX &c, int nTrigger)
{
    UNREFERENCED_PARAMETER(nTrigger);
    if (c.ammo() <= 0)
        return;
    c.useAmmo(1);
    c.sound(SFX("SFX_MAXFIRE"));
    c.vector(Random(100) <= 10 ? "BV_GATLINGBULLETFLESH3" : "BV_GATLINGBULLETFLESH", 900);
    c.flash(40);
    c.kick(15);
}

void maximTick(CTX &c)
{
    // the trigger held: it goes on
    if (c.playing() == QAVID("QAV_MAXFI") && c.pState->nTimer <= kTicsPerFrame && (c.buttonFire() || c.buttonAlt()) && c.ammo() > 0)
        c.play(QAVID("QAV_MAXFI"), kPhaseFire);
}

//
// the Blunderbuss (weapon_blunderbuss.txt): one big blast
//

void blunderbussFire(CTX &c, bool bAlt)
{
    UNREFERENCED_PARAMETER(bAlt);
    c.play(QAVID("QAV_BLUNF1"), kPhaseFire);
}

void blunderbussTrigger(CTX &c, int nTrigger)
{
    UNREFERENCED_PARAMETER(nTrigger);
    if (c.ammo() <= 0)
        return;
    c.useAmmo(1);
    c.sound(SFX("SFX_BLUNDFI"));
    c.sound(SFX("SFX_SHOTFIRE"));
    for (int i = 0; i < 24; i++)
        c.vector(Random(100) <= 30 ? "BV_GATLINGBULLETFLESH3" : "BV_GATLINGBULLETFLESH", 3500);
    c.flash(60);
    c.kick(60);
}

//
// the cannons (BEC_Players.txt, "CANNON"): one shot, then the cannon is empty until a cannon ball is
// carried to it
//

void cannonFire(CTX &c, bool bAlt)
{
    UNREFERENCED_PARAMETER(bAlt);
    if (c.ammo() <= 0)
        return;
    c.useAmmo(1);
    c.play(QAVID("QAV_CANNONF"), kPhaseFire);
    c.sound(SFX("SFX_CANNONF"));
    c.sound(SFX("SFX_BLUNDFI"));
    rocket(c, kCannonBallPicnum, 32, kBoomLarge, 0x14000, 0);
    c.flash(60);
    c.kick(80);
    int const nCannon = c.pState->nTurret;
    if (nCannon >= 0 && nCannon < kMaxSprites && sprite[nCannon].statnum == kStatDecoration)
        sprite[nCannon].picnum = kCannonEmptyPicnum;
}

//
// the fireworks cannon (BEC_Players.txt, HoldingFWCannon): it goes off by itself, the mod's three kinds
// of rockets at its times, and is thrown away when it's done
//

void fireworksTick(CTX &c)
{
    if (c.pState->nPhase == kPhaseLower || c.pState->nPhase == kPhaseRaise)
        return;
    // the mod's timer (2 a frame) and the times it fires at
    static short const gTimes[] = { 32, 42, 63, 70, 84, 85, 95, 96, 106, 107, 117, 118, 128, 129, 139, 140, 150, 151,
                                    161, 162, 172, 173, 182, 192, 203, 213 };
    int const nBefore = c.pState->nVar[0];
    c.pState->nVar[0] += 2;
    for (int i = 0; i < (int)ARRAY_SIZE(gTimes); i++)
    {
        if (gTimes[i] <= nBefore || gTimes[i] > c.pState->nVar[0])
            continue;
        static short const gPicnums[3] = { 7390, 7393, 7396 };
        int const nKind = i % 3;
        if (nKind == 1)
            rocket(c, gPicnums[nKind], 32, kBoomSmall, 0xC000, 600);
        else if (spritetype *pFlare = c.missile(kMissileFlareRegular, 600))
            pFlare->picnum = gPicnums[nKind];
        c.sound(SFX("SFX_FCFIRE1") + nKind);
        c.flash(40);
        c.kick(15);
    }
    if (c.pState->nVar[0] >= 268)  // done: thrown away
        c.useAmmo(1);
}

WEAPON const gWeapons[kBecWeaponMax] = {
    { "GATLING GUN", 7, -1, 540, 540, "", 4689, "QAV_GATUP", "QAV_GATIDLE", "QAV_GATDOWN", gatlingFire, gatlingTick, NULL },
    { "TRENCH GUN", 3, 2, 0, 8, "", 4725, "QAV_PUMPU", "QAV_PUMPI", "QAV_PUMPD", trenchFire, NULL, trenchTrigger },
    { "DEVASTATOR", 7, -1, 400, 200, "", 5553, "QAV_DUKEUP", "QAV_DUKEID", "QAV_DUKEDO", devastatorFire, NULL, devastatorTrigger },
    { "ROCKET LAUNCHER", 7, -1, 100, 100, "", 5093, "QAV_SWUP", "QAV_SWIDLER", "QAV_SWDOWN", rocketFire, NULL, rocketTrigger },
    { "DEVIL'S BORE", 8, -1, 60, 30, "", 4730, "QAV_RAILGU", "QAV_RAILGI", "QAV_RAILGD", railFire, NULL, railTrigger },
    { "DUAL TOMMY GUNS", 4, 3, 0, 100, "", 4766, "QAV_2TOMUP", "QAV_2TOMIDLE", "QAV_2TOMDOWN", dualTommyFire, dualTommyTick, dualTommyTrigger },
    { "DUAL SAWED-OFFS", 3, 2, 0, 8, "", 4756, "QAV_2SHOTUP", "QAV_2SHOTI", "QAV_2SHOTDOWN", dualShotgunFire, NULL, dualShotgunTrigger },
    { "DUAL FLARE GUNS", 2, 1, 0, 9, "", 4763, "QAV_FLAR2UP", "QAV_FLAR2I", "QAV_FLAR2DWN", dualFlareFire, NULL, dualFlareTrigger },
    { "DEATH MOWER", 0, -1, kMowerTime, kMowerTime, "", kMowerPicnum, "QAV_MOWERR", "QAV_MOWERA", "QAV_MOWERT", mowerFire, mowerTick, NULL },
    { "MAXIM GUN", 0, -1, 1, 1, "", kMaximPicnum, "QAV_MAXIM1", "QAV_MAXIM1", "QAV_MAXIM1", turretFire, NULL, NULL },
    { "MOBILE MAXIM", 0, -1, 250, 250, "", 5077, "QAV_MAXUP", "QAV_MAXID", "QAV_MOWERT", maximFire, maximTick, maximTrigger },
    { "BLUNDERBUSS", 0, -1, 1, 1, "", kBlunderbussPicnum, "QAV_BLUNUP", "QAV_BLUNID", "QAV_MOWERT", blunderbussFire, NULL, blunderbussTrigger },
    { "CANNON", 0, -1, 1, 1, "", kCannonPicnum, "QAV_CANNONI", "QAV_CANNONI", NULL, cannonFire, NULL, NULL },
    { "CANNON BALL", 0, -1, 1, 1, "", kCannonBallPicnum, NULL, NULL, NULL, NULL, NULL, NULL },
    { "FIREWORKS CANNON", 0, -1, 1, 1, "", kFireworksPicnum, "QAV_FCFIRE", "QAV_FCFIRE", "QAV_MOWERT", NULL, fireworksTick, NULL },
};

CTX ctxFor(PLAYER *pPlayer)
{
    CTX c;
    c.pPlayer = pPlayer;
    c.pState = &gWState[pPlayer->nPlayer];
    c.nWeapon = c.pState->nActive;
    c.pWeapon = c.nWeapon >= 0 ? &gWeapons[c.nWeapon] : NULL;
    return c;
}

// a trigger frame of the mod's weapon QAV playing
void qavTrigger(int nTrigger, void *pData)
{
    PLAYER *pPlayer = (PLAYER *)pData;
    if (!pPlayer)
        return;
    CTX c = ctxFor(pPlayer);
    if (c.pWeapon && c.pWeapon->pTrigger)
        c.pWeapon->pTrigger(c, nTrigger);
}

bool has(WSTATE *pState, int nWeapon)
{
    return (pState->nHave >> nWeapon) & 1;
}

int ammoOf(PLAYER *pPlayer, int nWeapon)
{
    WEAPON const &w = gWeapons[nWeapon];
    return w.nBloodAmmo >= 0 ? pPlayer->ammoCount[w.nBloodAmmo] : gWState[pPlayer->nPlayer].nAmmo[nWeapon];
}

bool usable(PLAYER *pPlayer, int nWeapon)
{
    return has(&gWState[pPlayer->nPlayer], nWeapon) && ammoOf(pPlayer, nWeapon) > 0;
}

void raise(PLAYER *pPlayer, int nWeapon)
{
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    pState->nActive = nWeapon;
    pState->nPending = -1;
    pState->nNext = 0;
    memset(pState->nVar, 0, sizeof(pState->nVar));
    CTX c = ctxFor(pPlayer);
    c.pState->nIdleQav = becDefine(c.pWeapon->pzIdle, -1);
    c.pState->nQav = -1;
    c.play(becDefine(c.pWeapon->pzUp, -1), kPhaseRaise);
    if (c.pState->nQav < 0)
        c.idle(c.pState->nIdleQav);
}

// the slot's weapons in the order a key press goes through them: Blood's, then the mod's
int slotNext(PLAYER *pPlayer, int nSlot, int nFromBec, bool bFromBlood)
{
    // the list: 0 = Blood's weapon of that number, 1.. = the mod's (index + 1)
    int list[kBecWeaponMax + 1], nCount = 0;
    if (nSlot < kWeaponMax && WeaponIsEquipable(pPlayer, nSlot, true))
        list[nCount++] = 0;
    for (int i = 0; i < kBecWeaponMax; i++)
        if (gWeapons[i].nSlot == nSlot && usable(pPlayer, i))
            list[nCount++] = i + 1;
    if (!nCount)
        return -1;
    int nAt = -1;
    for (int i = 0; i < nCount; i++)
        if ((bFromBlood && list[i] == 0) || (nFromBec >= 0 && list[i] == nFromBec + 1))
            nAt = i;
    return list[(nAt + 1) % nCount];
}

void giveWeapon(PLAYER *pPlayer, int nWeapon, int nAmmo)
{
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    WEAPON const &w = gWeapons[nWeapon];
    bool const bNew = !has(pState, nWeapon);
    pState->nHave |= 1u << nWeapon;
    if (w.nBloodAmmo >= 0)
        pPlayer->ammoCount[w.nBloodAmmo] = ClipHigh(pPlayer->ammoCount[w.nBloodAmmo] + nAmmo, gAmmoInfo[w.nBloodAmmo].max);
    else
        pState->nAmmo[nWeapon] = ClipHigh(pState->nAmmo[nWeapon] + nAmmo, w.nMaxAmmo);
    // a new gun goes in hand, as Blood's do
    if (bNew && pState->nActive != nWeapon && pState->nPending < 0)
    {
        pState->nPending = nWeapon;
        if (pState->nActive >= 0)
        {
            CTX c = ctxFor(pPlayer);
            c.lower();
            pState->nNext = -2 - nWeapon;
            pState->nPending = -1;
        }
    }
}

// becgive [all|number]: the mod's weapons (for trying them; the mod's own "loaded" cheat)
int osdcmd_becgive(osdcmdptr_t parm)
{
    if (!gGameStarted || !gMe || !gMe->pSprite || !bActive)
        return OSDCMD_OK;
    if (parm->numparms < 1 || !Bstrcasecmp(parm->parms[0], "all"))
    {
        for (int i = 0; i < kBecWeaponMax; i++)
            giveWeapon(gMe, i, gWeapons[i].nMaxAmmo ? gWeapons[i].nMaxAmmo : gWeapons[i].nGive * 4);
        for (int i : { kWeaponFlare, kWeaponShotgun, kWeaponTommy })
            gMe->hasWeapon[i] = 1;
        OSD_Printf("All of Blood Extra Crispy's weapons\n");
        return OSDCMD_OK;
    }
    int const n = atoi(parm->parms[0]);
    if (n < 0 || n >= kBecWeaponMax)
    {
        for (int i = 0; i < kBecWeaponMax; i++)
            OSD_Printf("%d %s (slot %d)\n", i, gWeapons[i].pzName, gWeapons[i].nSlot);
        return OSDCMD_SHOWHELP;
    }
    giveWeapon(gMe, n, gWeapons[n].nMaxAmmo ? gWeapons[n].nMaxAmmo : gWeapons[n].nGive * 4);
    OSD_Printf("%s\n", gWeapons[n].pzName);
    return OSDCMD_OK;
}

// a sprite of the mod's world weapons in front of Caleb, within reach (the mod's grabDist, grabFOV)
int inReach(PLAYER *pPlayer, int nPicnum)
{
    spritetype *pSprite = pPlayer->pSprite;
    int nBest = -1, nBestDist = 160;
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pItem = &sprite[nSprite];
        if (pItem->picnum != nPicnum || (gBecFlags[nSprite] & kBecFlagUsed))
            continue;
        int const dx = pItem->x - pSprite->x, dy = pItem->y - pSprite->y;
        int const nDist = approxDist(dx, dy);
        int const nAngle = klabs(((getangle(dx, dy) + 1024 - pSprite->ang) & 2047) - 1024);
        if (nDist < nBestDist && nAngle < 256 && klabs(pItem->z - pSprite->z) < (64 << 8))
            nBest = nSprite, nBestDist = nDist;
    }
    return nBest;
}

// the one in hand is up at once (Blood's weapon goes down first)
void takeUp(PLAYER *pPlayer, int nWeapon, int nAmmo)
{
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    pState->nHave |= 1u << nWeapon;
    pState->nAmmo[nWeapon] = nAmmo;
    if (pState->nActive >= 0)
    {
        CTX c = ctxFor(pPlayer);
        c.lower();
        pState->nNext = -2 - nWeapon;
    }
    else
    {
        pState->nPending = nWeapon;
        if (pPlayer->curWeapon)
            WeaponLower(pPlayer);
    }
}

// the ones in no slot leave Caleb's hands for good
void putDown(PLAYER *pPlayer, int nWeapon)
{
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    pState->nHave &= ~(1u << nWeapon);
    pState->nAmmo[nWeapon] = 0;
    spritetype *pSprite = pPlayer->pSprite;
    int const nTurret = pState->nTurret;
    bool const bObject = nTurret >= 0 && nTurret < kMaxSprites && sprite[nTurret].statnum == kStatDecoration;
    pState->nTurret = -1;
    if (nWeapon == kBecTurret || nWeapon == kBecCannon)  // the gun stays on its stand
    {
        if (bObject)
        {
            sprite[nTurret].cstat &= ~CSTAT_SPRITE_INVISIBLE;
            sprite[nTurret].cstat |= CSTAT_SPRITE_BLOCK;
        }
        // Caleb steps back from it
        vec3_t pos = { pSprite->x - mulscale30(pSprite->clipdist << 2, Cos(pSprite->ang)), pSprite->y - mulscale30(pSprite->clipdist << 2, Sin(pSprite->ang)), pSprite->z };
        int16_t nSector = pSprite->sectnum;
        updatesector(pos.x, pos.y, &nSector);
        if (nSector >= 0)
            setsprite(pSprite->index, &pos);
        return;
    }
    if (nWeapon == kBecCannonBall)  // put down in front of Caleb
    {
        if (!bObject)
            return;
        int x = pSprite->x + mulscale30(pSprite->clipdist << 3, Cos(pSprite->ang)), y = pSprite->y + mulscale30(pSprite->clipdist << 3, Sin(pSprite->ang));
        int16_t nSector = pSprite->sectnum;
        updatesector(x, y, &nSector);
        if (nSector < 0)
            x = pSprite->x, y = pSprite->y, nSector = pSprite->sectnum;
        int ceilZ, floorZ;
        getzsofslope(nSector, x, y, &ceilZ, &floorZ);
        vec3_t pos = { x, y, floorZ };
        setsprite(nTurret, &pos);
        sprite[nTurret].cstat &= ~CSTAT_SPRITE_INVISIBLE;
        return;
    }
    if (nWeapon != kBecMower)
        return;
    // the mower lands in front of Caleb, spent (the mod's ACTOR_MowerDone)
    int const x = pSprite->x + mulscale30(pSprite->clipdist << 3, Cos(pSprite->ang));
    int const y = pSprite->y + mulscale30(pSprite->clipdist << 3, Sin(pSprite->ang));
    int16_t nSector = pSprite->sectnum;
    updatesector(x, y, &nSector);
    if (nSector < 0)
        return;
    int ceilZ, floorZ;
    getzsofslope(nSector, x, y, &ceilZ, &floorZ);
    spritetype *pMower = actSpawnSprite(nSector, x, y, floorZ, kStatDecoration, 0);
    if (!pMower)
        return;
    pMower->picnum = kMowerPicnum;
    pMower->xrepeat = pMower->yrepeat = 48;
    pMower->ang = pSprite->ang;
    pMower->cstat = 0;
    pMower->type = 0;
    gBecFlags[pMower->index] |= kBecFlagUsed;
    sfxPlay3DSound(pMower, becDefine("SFX_MOWDROP", -1), -1, 0);
}

// becput <mower|maxim|blunderbuss>: one of the mod's world weapons in front of you, as its maps have them
int osdcmd_becput(osdcmdptr_t parm)
{
    if (parm->numparms < 1 || !gGameStarted || !gMe || !gMe->pSprite)
        return OSDCMD_SHOWHELP;
    int const nPicnum = !Bstrcasecmp(parm->parms[0], "mower") ? kMowerPicnum : !Bstrcasecmp(parm->parms[0], "maxim") ? kMaximPicnum
                      : !Bstrcasecmp(parm->parms[0], "blunderbuss") ? kBlunderbussPicnum : !Bstrcasecmp(parm->parms[0], "cannon") ? kCannonEmptyPicnum
                      : !Bstrcasecmp(parm->parms[0], "ball") ? kCannonBallPicnum : !Bstrcasecmp(parm->parms[0], "fireworks") ? kFireworksPicnum
                      : !Bstrcasecmp(parm->parms[0], "aagun") ? kAAGunPicnum : atoi(parm->parms[0]);
    spritetype *pSprite = gMe->pSprite;
    int const nDist = parm->numparms > 1 ? atoi(parm->parms[1]) : 120;
    int const x = pSprite->x + mulscale30(nDist, Cos(pSprite->ang)), y = pSprite->y + mulscale30(nDist, Sin(pSprite->ang));
    int16_t nSector = pSprite->sectnum;
    updatesector(x, y, &nSector);
    if (nSector < 0)
        return OSDCMD_OK;
    int ceilZ, floorZ;
    getzsofslope(nSector, x, y, &ceilZ, &floorZ);
    spritetype *pItem = actSpawnSprite(nSector, x, y, floorZ, kStatDecoration, 0);
    if (pItem)
    {
        pItem->picnum = nPicnum;
        pItem->xrepeat = pItem->yrepeat = nPicnum == kMaximPicnum ? 24 : 48;
        pItem->ang = pSprite->ang;
        pItem->cstat = 0;
        pItem->type = 0;
    }
    return OSDCMD_OK;
}

class BecWeaponsLoadSave : public LoadSave
{
    virtual void Load(void)
    {
        Read(gWState, sizeof(gWState));
        Read(gMissileBoom, sizeof(gMissileBoom));
    }
    virtual void Save(void)
    {
        Write(gWState, sizeof(gWState));
        Write(gMissileBoom, sizeof(gMissileBoom));
    }
};

} // namespace

void BecWeaponsLoadSaveConstruct(void)
{
    new BecWeaponsLoadSave();
}

void becWeaponsInit(void)
{
    bActive = false;
    if (!becDudesActive())  // the mod's definitions (its QAV names) weren't read
        return;
    pQavs = new std::unordered_map<int, QAV *>;
    nQavClient = qavRegisterClient(qavTrigger);
    OSD_RegisterFunction("becgive", "becgive [all|number]: Blood Extra Crispy's weapons", osdcmd_becgive);
    OSD_RegisterFunction("becput", "becput <mower|maxim|blunderbuss>: one of Blood Extra Crispy's world weapons in front of you", osdcmd_becput);
    int nOk = 0;
    for (auto const &w : gWeapons)
        nOk += getQav(becDefine(w.pzIdle, -1)) != NULL;
    bActive = nOk > 0;
    for (auto &s : gWState)
        s.nActive = s.nPending = -1, s.nQav = -1;
    LOG_F(INFO, "Blood Extra Crispy: %d of %d weapons", nOk, kBecWeaponMax);
}

bool becWeaponsActive(void)
{
    return bActive;
}

void becWeaponsPlayerReset(PLAYER *pPlayer)
{
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    memset(pState, 0, sizeof(*pState));
    pState->nActive = pState->nPending = -1;
    pState->nQav = -1;
}

void becWeaponsPlayerStart(PLAYER *pPlayer)
{
    // a new level or life: Blood's weapon comes up first, the mod's stay in the pack
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    pState->nActive = pState->nPending = -1;
    pState->nQav = -1;
    pState->nNext = 0;
}

bool becWeaponProcess(PLAYER *pPlayer)
{
    if (!bActive)
        return false;
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    if (!pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
    {
        pState->nActive = pState->nPending = -1;
        pState->nQav = -1;
        return false;
    }

    // a number key: the next weapon of that slot, which can be one of the mod's
    int const nNew = pPlayer->input.newWeapon;
    if (nNew > 0 && nNew <= 10 && pState->nNext == 0)
    {
        int const nNext = slotNext(pPlayer, nNew, pState->nActive, pState->nActive < 0 && pPlayer->curWeapon == nNew);
        if (nNext > 0 && nNext - 1 != pState->nActive)
        {
            pPlayer->input.newWeapon = kWeaponNone;
            if (pState->nActive >= 0)  // the one in hand goes down, the other comes up
            {
                CTX c = ctxFor(pPlayer);
                c.lower();
                pState->nNext = -2 - (nNext - 1);
            }
            else
            {
                pState->nPending = nNext - 1;
                if (pPlayer->curWeapon)
                    WeaponLower(pPlayer);
            }
        }
        else if (nNext == 0 && pState->nActive >= 0)  // Blood's weapon of that number
        {
            CTX c = ctxFor(pPlayer);
            c.lower();
            pState->nNext = nNew;
            pPlayer->input.newWeapon = kWeaponNone;
        }
        else if (pState->nActive >= 0)
            pPlayer->input.newWeapon = kWeaponNone;
    }
    // the mouse wheel / next and previous weapon keys while one of the mod's is in hand: Blood's again
    if (pState->nActive >= 0 && pState->nNext == 0 && (pPlayer->input.keyFlags.nextWeapon || pPlayer->input.keyFlags.prevWeapon || pPlayer->input.keyFlags.lastWeapon))
    {
        CTX c = ctxFor(pPlayer);
        c.lower();
        pState->nNext = pPlayer->lastWeapon > 0 ? (int)pPlayer->lastWeapon : (int)kWeaponPitchfork;
        pPlayer->input.keyFlags.nextWeapon = pPlayer->input.keyFlags.prevWeapon = pPlayer->input.keyFlags.lastWeapon = 0;
    }

    // waiting for Blood's weapon to be put away
    if (pState->nPending >= 0)
    {
        if (pPlayer->curWeapon == kWeaponNone && pPlayer->weaponTimer <= 0)
        {
            pPlayer->weaponQav = -1;
            raise(pPlayer, pState->nPending);
        }
        else
            return false;
    }
    if (pState->nActive < 0)
        return false;

    CTX c = ctxFor(pPlayer);
    pPlayer->curWeapon = kWeaponNone;
    // the QAV playing: its sounds and trigger frames
    if (QAV *pQav = getQav(pState->nQav))
    {
        pQav->nSprite = pPlayer->pSprite->index;
        pState->nTimer -= kTicsPerFrame;
        int const nTicks = pQav->at10 - pState->nTimer;
        pQav->Play(nTicks - kTicsPerFrame, nTicks, nQavClient, pPlayer);
    }
    if (c.pWeapon->pTick)
        c.pWeapon->pTick(c);
    if (pState->nTimer > 0 && pState->nPhase != kPhaseIdle)  // idle, it can be fired at any moment
        return true;
    switch (pState->nPhase)
    {
    case kPhaseLower:
    {
        int const nNext = pState->nNext;
        if (gWeapons[pState->nActive].nSlot == 0)  // the ones in no slot are dropped (put down, thrown away)
            putDown(pPlayer, pState->nActive);
        pState->nActive = -1;
        pState->nQav = -1;
        pState->nNext = 0;
        if (nNext <= -2)  // another of the mod's
            raise(pPlayer, -2 - nNext);
        else if (nNext > 0)  // Blood's: it raises it, its curWeapon being none
            pPlayer->input.newWeapon = nNext;
        else
            pPlayer->input.newWeapon = kWeaponPitchfork;
        return pState->nActive >= 0;
    }
    case kPhaseRaise:
    case kPhaseFire:
        c.idle(pState->nIdleQav);
        break;
    default:  // idle: it loops, until it's fired
        break;
    }
    if (pState->nPhase == kPhaseIdle)
    {
        if (c.ammo() <= 0)  // empty: Blood's next weapon
        {
            c.lower();
            pState->nNext = pPlayer->lastWeapon > 0 ? (int)pPlayer->lastWeapon : (int)kWeaponPitchfork;
        }
        else if ((c.buttonFire() || c.buttonAlt()) && c.pWeapon->pFire)
            c.pWeapon->pFire(c, !c.buttonFire());
        else if (pState->nTimer <= 0)
            c.idle(pState->nIdleQav);
    }
    return true;
}

bool becWeaponDraw(PLAYER *pPlayer, int nShade, int x, int y, int nPal)
{
    if (!bActive)
        return false;
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    if (pState->nActive < 0)
        return false;
    QAV *pQav = getQav(pState->nQav);
    if (!pQav)
        return true;
    int const nTicks = ClipRange(pQav->at10 - pState->nTimer, 0, pQav->at10 - 1);
    pQav->x = x;
    pQav->y = y;
    int nFlags = 2 | kQavOrientationQ16;
    int const nInv = powerupCheck(pPlayer, kPwUpShadowCloak);
    if (nInv >= 120 * 8 || (nInv != 0 && ((int)totalclock & 32)))
    {
        nShade = -128;
        nFlags |= 1;
    }
    pQav->Draw(nTicks, nFlags, nShade, nPal, W_WEAPON_START);
    return true;
}

int becWeaponAmmo(PLAYER *pPlayer)
{
    int const nWeapon = gWState[pPlayer->nPlayer].nActive;
    if (!bActive || nWeapon < 0 || nWeapon == kBecTurret)  // the gun on its stand never runs out
        return -1;
    return ammoOf(pPlayer, nWeapon);
}

bool becWeaponUse(PLAYER *pPlayer)
{
    if (!bActive || !pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
        return false;
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    if ((pState->nActive == kBecTurret || pState->nActive == kBecCannon) && pState->nPhase != kPhaseLower)  // off the gun again
    {
        CTX c = ctxFor(pPlayer);
        c.lower();
        pState->nNext = pPlayer->lastWeapon > 0 ? (int)pPlayer->lastWeapon : (int)kWeaponPitchfork;
        return true;
    }
    if (pState->nActive == kBecCannonBall && pState->nPhase != kPhaseLower)
    {
        // into an empty cannon, or put down
        int const nCannon = inReach(pPlayer, kCannonEmptyPicnum);
        if (nCannon >= 0)
        {
            sprite[nCannon].picnum = kCannonPicnum;
            sfxPlay3DSound(pPlayer->pSprite, becDefine("SFX_CANNONL", -1), -1, 0);
            if (pState->nTurret >= 0 && pState->nTurret < kMaxSprites)
                actPostSprite(pState->nTurret, kStatFree);
            pState->nTurret = -1;
        }
        CTX c = ctxFor(pPlayer);
        c.lower();
        pState->nNext = pPlayer->lastWeapon > 0 ? (int)pPlayer->lastWeapon : (int)kWeaponPitchfork;
        return true;
    }
    if (pState->nActive >= 0 && gWeapons[pState->nActive].nSlot == 0)
        return false;
    if (pState->nPending >= 0 || pState->nNext)
        return false;
    int nSprite = inReach(pPlayer, kMowerPicnum);
    if (nSprite >= 0)
    {
        sfxPlay3DSound(pPlayer->pSprite, becDefine("SFX_PARTYO", -1), -1, 0);
        actPostSprite(nSprite, kStatFree);
        takeUp(pPlayer, kBecMower, kMowerTime);
        return true;
    }
    nSprite = inReach(pPlayer, kFireworksPicnum);
    if (nSprite >= 0)
    {
        sfxPlay3DSound(pPlayer->pSprite, becDefine("SFX_BTNTPU", -1), -1, 0);
        actPostSprite(nSprite, kStatFree);
        takeUp(pPlayer, kBecFireworks, 1);
        return true;
    }
    nSprite = inReach(pPlayer, kCannonBallPicnum);
    if (nSprite >= 0)  // a cannon ball, carried (out of sight: Caleb has it)
    {
        sfxPlay3DSound(pPlayer->pSprite, becDefine("SFX_BTNTPU", -1), -1, 0);
        sprite[nSprite].cstat |= CSTAT_SPRITE_INVISIBLE;
        sprite[nSprite].cstat &= ~(CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);
        takeUp(pPlayer, kBecCannonBall, 1);
        pState->nTurret = nSprite;
        if (pPlayer == gMe)
            viewSetMessage("Carrying a cannon ball: USE on an empty cannon loads it", 0, MESSAGE_PRIORITY_PICKUP);
        return true;
    }
    int const nCannon = inReach(pPlayer, kCannonPicnum);
    nSprite = nCannon >= 0 ? nCannon : inReach(pPlayer, kMaximPicnum);
    if (nSprite >= 0)
    {
        // behind the gun: where it stands, facing where it faces; it's in Caleb's hands, not on its stand
        spritetype *pTurret = &sprite[nSprite];
        spritetype *pSprite = pPlayer->pSprite;
        sfxPlay3DSound(pSprite, becDefine("SFX_GATPU", -1), -1, 0);
        if (Random(100) <= 25)
            sfxPlay3DSound(pSprite, becDefine("SFX_VOMGFUN", -1), -1, 0);
        vec3_t pos = { pTurret->x - mulscale30(pSprite->clipdist << 2, Cos(pTurret->ang)), pTurret->y - mulscale30(pSprite->clipdist << 2, Sin(pTurret->ang)), pSprite->z };
        setsprite(pSprite->index, &pos);
        pSprite->ang = pTurret->ang;
        pPlayer->q16ang = fix16_from_int(pTurret->ang);
        pTurret->cstat |= CSTAT_SPRITE_INVISIBLE;
        pTurret->cstat &= ~CSTAT_SPRITE_BLOCK;
        takeUp(pPlayer, nCannon >= 0 ? kBecCannon : kBecTurret, 1);
        pState->nTurret = nSprite;
        return true;
    }
    return false;
}

bool becWeaponMounted(PLAYER *pPlayer)
{
    if (!bActive)
        return false;
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    return pState->nActive == kBecTurret || pState->nPending == kBecTurret || pState->nActive == kBecCannon || pState->nPending == kBecCannon;
}

void becWeaponMountedInput(PLAYER *pPlayer, bool bRun)
{
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    spritetype *pSprite = pPlayer->pSprite;
    // standing behind it
    xvel[pSprite->index] = yvel[pSprite->index] = 0;
    // JUMP: ripped off its stand, it goes with Caleb (the mod's mobile Maxim)
    if (bRun && pState->nActive == kBecTurret && pState->nPhase != kPhaseLower)
    {
        int const nTurret = pState->nTurret;
        if (nTurret >= 0 && nTurret < kMaxSprites && sprite[nTurret].statnum == kStatDecoration)
            actPostSprite(nTurret, kStatFree);
        pState->nTurret = -1;
        pState->nHave &= ~(1u << kBecTurret);
        pState->nActive = -1;
        pState->nQav = -1;
        sfxPlay3DSound(pSprite, becDefine("SFX_GATPU", -1), -1, 0);
        pState->nHave |= 1u << kBecMaxim;
        pState->nAmmo[kBecMaxim] = gWeapons[kBecMaxim].nMaxAmmo;
        raise(pPlayer, kBecMaxim);
    }
}

char const *becWeaponName(PLAYER *pPlayer, int *pnTile)
{
    if (!bActive || gWState[pPlayer->nPlayer].nActive < 0)
        return NULL;
    WEAPON const &w = gWeapons[gWState[pPlayer->nPlayer].nActive];
    if (pnTile)
        *pnTile = w.nIcon;
    return w.pzName;
}

bool becWeaponPickUp(PLAYER *pPlayer, spritetype *pItem, char *pzMessage, int nMessageSize)
{
    if (!bActive)
        return false;
    int nWeapon = -1, nAmmo = 0;
    switch (pItem->type)
    {
    case kItemTwoGuns:  // the mod's Guns Akimbo is its Gatling Gun
        nWeapon = kBecGatling;
        break;
    case kItemBeastVision:  // its Beast Vision the Devil's Bore
        nWeapon = kBecRailgun;
        break;
    // a second gun of one Caleb has: two of them
    case kItemWeaponTommygun:
        if (pPlayer->hasWeapon[kWeaponTommy] && !has(&gWState[pPlayer->nPlayer], kBecDualTommy))
            nWeapon = kBecDualTommy;
        break;
    case kItemWeaponSawedoff:
        if (pPlayer->hasWeapon[kWeaponShotgun] && !has(&gWState[pPlayer->nPlayer], kBecDualShotgun))
            nWeapon = kBecDualShotgun;
        break;
    case kItemWeaponFlarePistol:
        if (pPlayer->hasWeapon[kWeaponFlare] && !has(&gWState[pPlayer->nPlayer], kBecDualFlare))
            nWeapon = kBecDualFlare;
        break;
    }
    if (nWeapon < 0)
        return false;
    nAmmo = gWeapons[nWeapon].nGive;
    giveWeapon(pPlayer, nWeapon, nAmmo);
    Bsnprintf(pzMessage, nMessageSize, "Picked up %s -SLOT %d-", gWeapons[nWeapon].pzName, gWeapons[nWeapon].nSlot);
    sfxPlay3DSound(pPlayer->pSprite, kPickupSound, -1, 0);
    return true;
}

// the mod's weapons its maps place: pictures (decorations) its scripts turn into them
void becWeaponsLevelStart(bool bSaveLoad)
{
    if (!bActive)
        return;
    memset(gMissileBoom, 0, sizeof(gMissileBoom));
    UNREFERENCED_PARAMETER(bSaveLoad);
}

namespace
{
// the AA guns (BEC_Players.txt): volleys of eleven flak shells into the sky while you're about
int16_t gAADelay[kMaxSprites];
int8_t gAAVolley[kMaxSprites];

void aaGuns(void)
{
    if (!gMe || !gMe->pSprite)
        return;
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pGun = &sprite[nSprite];
        if (pGun->picnum < kAAGunPicnum || pGun->picnum > kAAGunPicnum + 2)
            continue;
        if (approxDist(gMe->pSprite->x - pGun->x, gMe->pSprite->y - pGun->y) > 4000 * 16)
            continue;
        if (gAADelay[nSprite] > 0)
        {
            gAADelay[nSprite] -= 2;
            continue;
        }
        if (gAAVolley[nSprite] > 10)  // a pause between volleys
        {
            gAAVolley[nSprite] = 0;
            gAADelay[nSprite] = 50 + Random(100);
            continue;
        }
        gAAVolley[nSprite]++;
        gAADelay[nSprite] = 10;
        int const nTilt = (pGun->picnum == kAAGunPicnum ? 1 : -1) * (1400 + Random(300));  // up, a little to the side
        spritetype *pShell = actFireMissile(pGun, 0, -(16 << 8), mulscale30(nTilt, Cos(pGun->ang + 512)), mulscale30(nTilt, Sin(pGun->ang + 512)), -(1 << 14), kMissileBullet);
        if (pShell)
        {
            pShell->xrepeat = pShell->yrepeat = 64;
            actPropagateSpriteOwner(pShell, pGun);
        }
        sfxPlay3DSound(pGun, becDefine("SFX_GATF", -1), -1, 0);
    }
}
} // namespace

// touching one of the mod's weapon pictures in a map picks it up
void becWeaponsFrame(void)
{
    if (!bActive)
        return;
    aaGuns();
    struct PLACED { short nPicnum; int8_t nWeapon; };
    static PLACED const gPlaced[] = {
        { 4725, kBecTrench },
        { 5553, kBecDevastator },
        { 5093, kBecRocket },
        { kBlunderbussPicnum, kBecBlunderbuss },
    };
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pPlayer = &gPlayer[p];
        if (!pPlayer->pSprite || !pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
            continue;
        spritetype *pSprite = pPlayer->pSprite;
        for (int nSprite = headspritesect[pSprite->sectnum]; nSprite >= 0; nSprite = nextspritesect[nSprite])
        {
            spritetype *pItem = &sprite[nSprite];
            if (pItem->statnum != kStatDecoration)
                continue;
            for (auto const &w : gPlaced)
            {
                if (pItem->picnum != w.nPicnum)
                    continue;
                if (approxDist(pItem->x - pSprite->x, pItem->y - pSprite->y) > 96 || klabs(pItem->z - pSprite->z) > (64 << 8))
                    break;
                giveWeapon(pPlayer, w.nWeapon, gWeapons[w.nWeapon].nGive);
                if (pPlayer == gMe)
                {
                    char buffer[64];
                    Bsnprintf(buffer, sizeof(buffer), "Picked up %s -SLOT %d-", gWeapons[w.nWeapon].pzName, gWeapons[w.nWeapon].nSlot);
                    viewSetMessage(buffer, 0, MESSAGE_PRIORITY_PICKUP);
                }
                sfxPlay3DSound(pSprite, kPickupSound, -1, 0);
                pPlayer->pickupEffect = 30;
                actPostSprite(nSprite, kStatFree);
                break;
            }
        }
    }
}

// the mod's rockets hit: its explosions (Blood's dynamite, bundle or barrel blowing up there)
bool becImpactMissile(spritetype *pMissile)
{
    int const nBoom = gMissileBoom[pMissile->index];
    if (!nBoom)
        return false;
    gMissileBoom[pMissile->index] = 0;
    int const nThing = nBoom == kBoomSmall ? kThingArmedTNTStick : nBoom == kBoomStandard ? kThingArmedTNTBundle : kThingTNTBarrel;
    spritetype *pBoom = actSpawnThing(pMissile->sectnum, pMissile->x, pMissile->y, pMissile->z, nThing);
    if (pBoom)
    {
        actPropagateSpriteOwner(pBoom, pMissile);
        actExplodeSprite(pBoom);
        if (nBoom == kBoomNuke)  // the nuke: more around it
            for (int i = 0; i < 4; i++)
            {
                spritetype *pMore = actSpawnThing(pMissile->sectnum, pMissile->x + Random2(1024), pMissile->y + Random2(1024), pMissile->z, kThingTNTBarrel);
                if (pMore)
                {
                    actPropagateSpriteOwner(pMore, pMissile);
                    actExplodeSprite(pMore);
                }
            }
    }
    actPostSprite(pMissile->index, kStatFree);
    return true;
}
