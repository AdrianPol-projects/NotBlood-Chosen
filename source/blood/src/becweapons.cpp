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
#include "gorykill.h"
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
    kBecCodex,         // the Mourning Codex: one spell, then the book is gone
    kBecAxe,           // a zombie's axe: swung, or thrown (and picked up again where it lands)
    kBecOrb,           // the Phantasm Orb: thrown, it finds a head
    kBecAnathema,      // the Vile Anathema (a Necromancer's): the dead rise for Caleb, or skulls fly from his shoulder
    kBecChronometer,   // the Bastion Chronometer (a Necromancer's): reflective shots, or the Doom Bell
    kBecRevolvers,     // Caleb's revolvers, in place of the pitchfork (the mod's weapon_pitchfork.txt)
    kBecWeaponMax
};

enum { kFireworksPicnum = 7590, kAAGunPicnum = 6394 };  // the AA guns are 6394-6396

enum
{
    kMowerPicnum = 5840, kMaximPicnum = 5866, kBlunderbussPicnum = 5119,
    kCannonPicnum = 6276, kCannonEmptyPicnum = 6282, kCannonBallPicnum = 6281,
};
enum
{
    kAxePicnum = 5481, kAxeFlyingPicnum = 5496, kOrbPicnum = 5558, kAnathemaPicnum = 7305, kChronometerPicnum = 7353,
    kBellPicnum = 7827, kSkullPicnum = 5798, kPortalPicnum = 4743,
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
    int32_t nFrost;     // frames left of the Mourning Codex's blizzard around Caleb
    int32_t nSkulls;    // of the Vile Anathema's skulls flying from his shoulder
    int32_t nPurify;    // the revolvers: frames left of Purify, and of their waits (Purify, the Purge)
    int32_t nPurifyWait, nPurgeWait;
    int8_t bPitchfork;  // the pitchfork chosen with key 1 (otherwise the revolvers come up in its place)
};

enum { kPhaseNone, kPhaseRaise, kPhaseIdle, kPhaseFire, kPhaseLower };

enum { kPickupSound = 22071 };  // the mod's weapon pickups' pickup.pickupSound

WSTATE gWState[kMaxPlayers];

// the weather and the HUD's flashes (further down)
extern int gLightning[kMaxPlayers];
extern int gSkullShot[kMaxPlayers];
uint8_t gWeather[kMaxSprites];       // a weather marker's kind (kWeather...)
int32_t gWeatherTimer[kMaxSprites];  // the artillery's next shell (frames)
int32_t gThunderNext;                // the storm's next thunder (gFrameClock)
void weatherLevelStart(void);

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
    bool bGone;                             // used up, it isn't lowered: it's gone from Caleb's hands
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
        int const nDown = pWeapon->bGone && ammo() <= 0 ? -1 : becDefine(pWeapon->pzDown, -1);
        play(nDown, kPhaseLower);
        if (pState->nPhase != kPhaseLower || !getQav(nDown))  // none: down at once
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

enum { kBoomNone, kBoomSmall, kBoomStandard, kBoomLarge, kBoomNuke, kBoomOrb, kBoomAxe, kBoomPortal };

uint8_t gMissileBoom[kMaxSprites];

// the spells' things in the level: the Death Portal and the Doom Bell (decorations), the orb and the
// portal on their way (missiles)
enum { kSpellNone, kSpellPortal, kSpellBell };
uint8_t gSpellKind[kMaxSprites];
int32_t gSpellTime[kMaxSprites];    // frames left (the bell's: to its next ring)
int16_t gSpellTarget[kMaxSprites];  // the orb's head, the bell's rings left

int nFiringBoom;  // the one being fired: it can hit something at once, before actFireMissile returns it

spritetype *rocket(CTX &c, int nPicnum, int nRepeat, int nBoom, int nSpeed, int nSpread, int nOffset = 0)
{
    nFiringBoom = nBoom;
    spritetype *pMissile = c.missile(kMissileFireballNapalm, nSpread, nOffset);
    nFiringBoom = kBoomNone;
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

// an enemy (not one of Caleb's allies), alive
bool isEnemy(spritetype *pSprite)
{
    return pSprite->statnum == kStatDude && !IsPlayerSprite(pSprite) && !becIsAlly(pSprite->index) && pSprite->extra > 0
        && xsprite[pSprite->extra].health > 0;
}

// the enemy nearest where Caleb looks that he can see (within nCone of it), -1 for none
int enemyInSight(PLAYER *pPlayer, int nCone, int nRange)
{
    spritetype *pSprite = pPlayer->pSprite;
    int nBest = -1, nBestScore = INT_MAX;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pOther = &sprite[nSprite];
        if (pOther == pSprite || !isEnemy(pOther))
            continue;
        int const dx = pOther->x - pSprite->x, dy = pOther->y - pSprite->y;
        int const nDist = approxDist(dx, dy);
        int const nAngle = klabs(((getangle(dx, dy) + 1024 - pSprite->ang) & 2047) - 1024);
        if (nDist > nRange || nAngle > nCone)
            continue;
        int top, bottom;
        GetSpriteExtents(pOther, &top, &bottom);
        if (!cansee(pSprite->x, pSprite->y, pPlayer->zView, pSprite->sectnum, pOther->x, pOther->y, (top + bottom) / 2, pOther->sectnum))
            continue;
        int const nScore = nDist + nAngle * 16;
        if (nScore < nBestScore)
            nBest = nSprite, nBestScore = nScore;
    }
    return nBest;
}

// what's within nRange of a spot, alive, not Caleb or his allies (taken first: hurting them changes the lists)
int enemiesNear(spritetype *pAt, int nRange, bool bSeen, int16_t *pList, int nMax)
{
    int nCount = 0;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0 && nCount < nMax; nSprite = nextspritestat[nSprite])
    {
        spritetype *pOther = &sprite[nSprite];
        if (!isEnemy(pOther) || approxDist(pOther->x - pAt->x, pOther->y - pAt->y) > nRange)
            continue;
        if (bSeen && !cansee(pAt->x, pAt->y, pAt->z - (16 << 8), pAt->sectnum, pOther->x, pOther->y, pOther->z - (24 << 8), pOther->sectnum))
            continue;
        pList[nCount++] = nSprite;
    }
    return nCount;
}

// a picture of the mod's on the floor in front of Caleb (a decoration)
spritetype *putInFront(spritetype *pSprite, int nDist, int nPicnum, int nRepeat, bool bXSprite)
{
    int x = pSprite->x + mulscale30(nDist, Cos(pSprite->ang)), y = pSprite->y + mulscale30(nDist, Sin(pSprite->ang));
    int16_t nSector = pSprite->sectnum;
    updatesector(x, y, &nSector);
    if (nSector < 0)
        x = pSprite->x, y = pSprite->y, nSector = pSprite->sectnum;
    int ceilZ, floorZ;
    getzsofslope(nSector, x, y, &ceilZ, &floorZ);
    spritetype *pItem = actSpawnSprite(nSector, x, y, floorZ, kStatDecoration, bXSprite);
    if (!pItem)
        return NULL;
    pItem->picnum = nPicnum;
    pItem->xrepeat = pItem->yrepeat = nRepeat;
    pItem->ang = pSprite->ang;
    pItem->cstat = 0;
    pItem->type = 0;
    pItem->owner = pSprite->index;
    gBecFlags[pItem->index] = 0;
    gSpellKind[pItem->index] = kSpellNone;
    return pItem;
}

//
// the Mourning Codex (weapon_grimoire.txt): one spell, and the book is spent. Fire: thirteen flesh gargoyles
// fight for Caleb; alt fire: a blizzard around him; crouched, fire: lightning (the mod's key for it is map
// zoom out); crouched, alt fire: his heart for a Death Portal (map zoom in)
//

void summonGargoyles(CTX &c)
{
    spritetype *pSprite = c.sprite();
    for (int i = 0; i < 13; i++)
    {
        int const nAngle = (pSprite->ang + Random2(384)) & 2047;
        int const nDist = 512 + Random(1024);
        int const x = pSprite->x + mulscale30(nDist, Cos(nAngle)), y = pSprite->y + mulscale30(nDist, Sin(nAngle));
        int16_t nSector = pSprite->sectnum;
        updatesector(x, y, &nSector);
        int const z = pSprite->z - (24 << 8) - (int)Random(24 << 8);
        if (nSector < 0 || !cansee(pSprite->x, pSprite->y, pSprite->z - (24 << 8), pSprite->sectnum, x, y, z, nSector))
            continue;
        int ceilZ, floorZ;
        getzsofslope(nSector, x, y, &ceilZ, &floorZ);
        spritetype *pGarg = becSpawnDude(pSprite, kDudeGargoyleFlesh, x, y, ClipRange(z, ceilZ + (48 << 8), floorZ), pSprite->ang);
        if (!pGarg)
            continue;
        becMakeAlly(pGarg, pSprite, 15 * kTicRate);  // the mod's 15 seconds
        gFX.fxSpawn(FX_29, pGarg->sectnum, pGarg->x, pGarg->y, pGarg->z, 0);
    }
}

void codexFire(CTX &c, bool bAlt)
{
    if (c.ammo() <= 0)
        return;
    bool const bCrouch = c.pPlayer->input.buttonFlags.crouch;
    c.useAmmo(1);
    if (bCrouch && !bAlt)
    {
        c.pState->nVar[1] = 0;
        c.pState->nVar[2] = 400;  // the mod's Volt: the lightning's strength
        c.play(QAVID("QAV_BOOKE1"), kPhaseFire);
        c.sound(SFX("SFX_THUNDER1"));
        c.sound(SFX("SFX_ZAPVOC"));
    }
    else if (bCrouch)
    {
        c.play(QAVID("QAV_BOOKHM"), kPhaseFire);
        c.sound(SFX("SFX_HARTVOC"));
    }
    else if (bAlt)
    {
        c.play(QAVID("QAV_BOOKDM"), kPhaseFire);
        c.sound(SFX("SFX_ICAST"));
    }
    else
    {
        c.play(QAVID("QAV_BOOKSM"), kPhaseFire);
        c.sound(SFX("SFX_DSPAWN"));
    }
}

void codexTick(CTX &c)
{
    int nPlaying = c.playing();
    bool const bEnd = c.pState->nTimer <= kTicsPerFrame;
    if (nPlaying == QAVID("QAV_BOOKE1") && ++c.pState->nVar[1] >= 25)  // the mod's 50 ticks of its
        c.play(QAVID("QAV_BOOKE2"), kPhaseFire);
    else if (nPlaying == QAVID("QAV_BOOKE2"))
    {
        c.pState->nVar[2] -= 4;
        if (bEnd)
            c.play(c.pState->nVar[2] > 0 ? QAVID("QAV_BOOKE2") : QAVID("QAV_BOOKE3"), kPhaseFire);
    }
    else if (nPlaying == QAVID("QAV_BOOKHM") && bEnd)
        c.play(QAVID("QAV_BOOKHM2"), kPhaseFire);
    nPlaying = c.playing();
    if (nPlaying == QAVID("QAV_BOOKE2") || nPlaying == QAVID("QAV_BOOKE3"))
    {
        if (!Random(8))
            c.sound(SFX("SFX_ARC1"));
        if (!Random(8))
            c.sound(SFX("SFX_ARC2"));
        if (!Random(8))
            c.sound(SFX("SFX_ARC3"));
        c.flash(40);
    }
}

void codexTrigger(CTX &c, int nTrigger)
{
    switch (nTrigger)
    {
    case 1:  // the blizzard
        c.pState->nFrost = 8 * kTicsPerSec;
        c.sound(SFX("SFX_DEMOTR"));
        c.sound(SFX("SFX_IWIND"));
        break;
    case 7:  // the gargoyles
        summonGargoyles(c);
        break;
    case 11:  // the lightning
        if (++c.pState->nVar[3] & 1)
        {
            c.missile(kMissileTeslaRegular, 900);
            c.kick(5);
        }
        break;
    case 12:  // and its last, all about
        for (int i = 0; i < 25; i++)
            c.missile(kMissileTeslaRegular, 8000);
        c.sound(SFX("SFX_ZAPFIRE"));
        c.flash(80);
        c.kick(40);
        break;
    case 13:
        c.sound(SFX("SFX_ZAPFIRE"));
        break;
    case 14:  // the heart torn out: a Death Portal on its way
    {
        c.sound(SFX("SFX_RGFIRE2"));
        c.sound(SFX("SFX_GIBSQ"));
        spritetype *pPortal = rocket(c, kPortalPicnum, 48, kBoomPortal, 0x8000, 0);
        if (pPortal)
        {
            gSpellTime[pPortal->index] = kTicsPerSec;  // it opens a second away (the mod's 60 ticks)
            pPortal->cstat |= CSTAT_SPRITE_TRANSLUCENT;
        }
        c.flash(60);
        break;
    }
    case 15:
        c.sound(SFX("SFX_HARTSPE"));
        break;
    case 16:
        c.sound(SFX("SFX_PFLESH"));
        break;
    case 18:
        c.sound(SFX("SFX_HEARTBE"));
        break;
    }
}

//
// a zombie's axe (weapon_axe.txt): swung, or thrown
//

void axeFire(CTX &c, bool bAlt)
{
    c.play(bAlt ? QAVID("QAV_THROAXE") : QAVID("QAV_AXESWIN"), kPhaseFire);
}

void axeTrigger(CTX &c, int nTrigger)
{
    if (nTrigger == 1)
        c.sound(SFX("SFX_AZOMSWIP"));
    else if (nTrigger == 2)  // the blow
    {
        c.vector("BV_AXE_PLAYER", 0);
        c.vector("BV_SWORDSLASH", 400);
        c.kick(10);
    }
    else if (nTrigger == 3)
        c.sound(SFX("SFX_GARGTHRO"));
    else if (nTrigger == 4 && c.ammo() > 0)  // thrown
    {
        c.useAmmo(1);
        rocket(c, kAxePicnum, 32, kBoomAxe, 0xC000, 0);
    }
}

//
// the Phantasm Orb (weapon_orb.txt): thrown, it goes for the enemy Caleb looks at and drills through its head
// (alt fire too: the mod's alt fire flies it by hand, Caleb's eyes in it)
//

void orbFire(CTX &c, bool bAlt)
{
    UNREFERENCED_PARAMETER(bAlt);
    c.play(QAVID("QAV_FIREORB"), kPhaseFire);
}

void orbTrigger(CTX &c, int nTrigger)
{
    if (nTrigger != 1 || c.ammo() <= 0)
        return;
    c.useAmmo(1);
    sfxKill3DSound(c.sprite(), -1, SFX("SFX_ORBIDLE"));
    c.sound(SFX("SFX_ORBFIRE"));
    spritetype *pOrb = rocket(c, kOrbPicnum, 16, kBoomOrb, 0x8000, 0);
    if (pOrb)
    {
        gSpellTarget[pOrb->index] = enemyInSight(c.pPlayer, kAng30, 40 * 512);
        gSpellTime[pOrb->index] = 10 * kTicsPerSec;  // it gives up after that
        sfxPlay3DSound(pOrb, SFX("SFX_ORBFLY"), -1, 0);
    }
}

//
// the Necromancer's: the Vile Anathema (weapon_necro.txt) and the Bastion Chronometer (weapon_watch.txt), one use each
//

void necroFire(CTX &c, bool bAlt)
{
    c.pState->nVar[0] = bAlt;
    c.play(c.nWeapon == kBecAnathema ? QAVID("QAV_NECROFIRE") : QAVID("QAV_WATCHFIRE"), kPhaseFire);
}

void anathemaTrigger(CTX &c, int nTrigger)
{
    if (nTrigger != 1 || c.ammo() <= 0)
        return;
    c.useAmmo(1);
    c.sound(SFX("SFX_NECREVI"));
    c.flash(60);
    char buffer[80];
    if (c.pState->nVar[0])  // alt: fifteen seconds of skulls flying from Caleb's shoulder at what he looks at
    {
        c.pState->nSkulls = 15 * kTicsPerSec;
        Bsnprintf(buffer, sizeof(buffer), "Skulls fly at what you look at");
    }
    else  // the dead he can see rise and fight for him
    {
        int const n = becRaiseCorpses(c.sprite(), 40 * 512);
        if (n)
            Bsnprintf(buffer, sizeof(buffer), "%d of the dead rise for you", n);
        else
            Bsnprintf(buffer, sizeof(buffer), "No dead here to rise");
    }
    if (c.pPlayer == gMe)
        viewSetMessage(buffer, 0, MESSAGE_PRIORITY_PICKUP);
}

void chronometerTrigger(CTX &c, int nTrigger)
{
    if (nTrigger != 1 || c.ammo() <= 0)
        return;
    c.useAmmo(1);
    c.sound(SFX("SFX_GLASHIT2"));
    c.flash(60);
    if (c.pState->nVar[0])  // alt: the Doom Bell, in front of Caleb, ringing death about it
    {
        c.sound(SFX("SFX_GORGEF2"));
        spritetype *pBell = putInFront(c.sprite(), 768, kBellPicnum, 40, false);
        if (pBell)
        {
            pBell->cstat |= CSTAT_SPRITE_BLOCK;
            gSpellKind[pBell->index] = kSpellBell;
            gSpellTime[pBell->index] = kTicsPerSec / 2;
            gSpellTarget[pBell->index] = 12;  // rings
            gFX.fxSpawn(FX_29, pBell->sectnum, pBell->x, pBell->y, pBell->z, 0);
        }
    }
    else  // thirty seconds of Blood's reflective shots
    {
        c.pPlayer->pwUpTime[kPwUpReflectShots] = ClipLow(c.pPlayer->pwUpTime[kPwUpReflectShots], 30 * kTicRate);
        powerupActivate(c.pPlayer, kPwUpReflectShots);
        if (c.pPlayer == gMe)
            viewSetMessage("Time stands still for their bullets: they go back where they came from", 0, MESSAGE_PRIORITY_PICKUP);
    }
}

//
// Caleb's revolvers (weapon_pitchfork.txt: the mod's pitchfork): twelve shots, then a reload (never out of
// bullets). Alt fire: Purify, five seconds of them shooting every enemy in sight by themselves, then ten
// to wait. Crouched, fire: the Purge, both barrels' blast (ten seconds to wait); crouched, alt fire: the
// knife (the mod's keys for these two are map zoom in and out)
//

enum { kRevRounds = 12 };

void revolverReload(CTX &c)
{
    c.play(QAVID("QAV_REVRL"), kPhaseFire);
    c.sound(SFX("SFX_REVLOAD"));
    c.pState->nAmmo[kBecRevolvers] = kRevRounds;
}

void revolverFire(CTX &c, bool bAlt)
{
    WSTATE *pState = c.pState;
    bool const bCrouch = c.pPlayer->input.buttonFlags.crouch;
    if (bCrouch && !bAlt)
    {
        if (pState->nPurgeWait <= 0)
        {
            c.play(QAVID("QAV_2REVFIRB"), kPhaseFire);
            pState->nPurgeWait = 10 * kTicsPerSec;
        }
    }
    else if (bCrouch)
        c.play(QAVID("QAV_KNIFE"), kPhaseFire);
    else if (bAlt)
    {
        if (pState->nPurify <= 0 && pState->nPurifyWait <= 0)
        {
            pState->nPurify = 5 * kTicsPerSec;
            c.sound(SFX("SFX_HEARTBE"));
        }
    }
    else if (c.ammo() <= 0)
        revolverReload(c);
    else
        c.play(QAVID("QAV_REVFIRE2"), kPhaseFire);
}

void revolverTick(CTX &c)
{
    WSTATE *pState = c.pState;
    if (pState->nPurifyWait > 0 && --pState->nPurifyWait == 0)
        c.sound(SFX("SFX_READY"));
    if (pState->nPurgeWait > 0)
        pState->nPurgeWait--;
    if (pState->nPhase == kPhaseRaise || pState->nPhase == kPhaseLower)
        return;
    if (pState->nPurify > 0)
    {
        if (--pState->nPurify == 0)  // done: reload, and a while before the next
        {
            pState->nPurifyWait = 10 * kTicsPerSec;
            revolverReload(c);
            return;
        }
        if (pState->nPurify % 4 == 0)
        {
            // a shot at each enemy it sees
            spritetype *pSprite = c.sprite();
            int16_t list[16];
            int nCount = enemiesNear(pSprite, 1000 * 16, true, list, ARRAY_SSIZE(list));
            // and the other players in Bloodbath and team games (not team mates)
            for (int p = connecthead; p >= 0 && nCount < (int)ARRAY_SSIZE(list); p = connectpoint2[p])
            {
                PLAYER *pOther = &gPlayer[p];
                if (pOther == c.pPlayer || !pOther->pSprite || !pOther->pXSprite || pOther->pXSprite->health <= 0
                    || gGameOptions.nGameType == kGameTypeSinglePlayer || gGameOptions.nGameType == kGameTypeCoop
                    || IsTargetTeammate(c.pPlayer, pOther->pSprite))
                    continue;
                spritetype *pTarget = pOther->pSprite;
                if (approxDist(pTarget->x - pSprite->x, pTarget->y - pSprite->y) <= 1000 * 16
                    && cansee(pSprite->x, pSprite->y, c.pPlayer->zView, pSprite->sectnum, pTarget->x, pTarget->y, pTarget->z - (24 << 8), pTarget->sectnum))
                    list[nCount++] = pTarget->index;
            }
            for (int i = 0; i < nCount; i++)
            {
                spritetype *pEnemy = &sprite[list[i]];
                int top, bottom;
                GetSpriteExtents(pEnemy, &top, &bottom);
                int const dx = pEnemy->x - pSprite->x, dy = pEnemy->y - pSprite->y;
                int const nDist = ClipLow(approxDist(dx, dy), 1);
                int const nAngle = getangle(dx, dy);
                becFireVector(pSprite, 0, c.pPlayer->zWeapon - pSprite->z, Cos(nAngle) >> 16, Sin(nAngle) >> 16,
                              divscale10((top + bottom) / 2 - c.pPlayer->zWeapon, nDist), "BV_REVOLVERBULLETFLESH");
            }
            if (nCount)
            {
                if (c.playing() != QAVID("QAV_REVFIRE3") || pState->nTimer <= kTicsPerFrame)
                    c.play(QAVID("QAV_REVFIRE3"), kPhaseFire);
                c.flash(30);
                c.kick(10);
            }
        }
        return;
    }
    if (c.ammo() <= 0 && pState->nPhase == kPhaseIdle)
        revolverReload(c);
}

void revolverTrigger(CTX &c, int nTrigger)
{
    switch (nTrigger)
    {
    case 1:  // a shot
        if (c.playing() != QAVID("QAV_REVFIRE2") || c.ammo() <= 0)
            break;
        c.useAmmo(1);
        c.vector("BV_REVOLVERBULLETFLESH", 300);
        c.sound(SFX("SFX_REVFIRE"));
        c.flash(20);
        c.kick(10);
        break;
    case 2:
        c.sound(SFX("SFX_REVCOCK"));
        break;
    case 3:
        c.sound(SFX("SFX_REVCHAR"));
        break;
    case 4:  // the Purge: both barrels' blast
        c.sound(SFX("SFX_REVBOOM"));
        rocket(c, 0, 16, kBoomSmall, 0x10000, 0, 8 * 16);
        rocket(c, 0, 16, kBoomSmall, 0x10000, 0, -8 * 16);
        c.pState->nAmmo[kBecRevolvers] = kRevRounds;
        c.flash(60);
        c.kick(40);
        break;
    case 5:  // the knife
        c.sound(SFX("SFX_AZOMSWIP"));
        c.vector("BV_KNIFESWIPE", 200);
        break;
    case 6:
    case 9:
        c.sound(SFX("SFX_REVFIRE"));
        break;
    }
}

WEAPON const gWeapons[kBecWeaponMax] = {
    { "GATLING GUN", 7, -1, 540, 540, "", 4689, "QAV_GATUP", "QAV_GATIDLE", "QAV_GATDOWN", gatlingFire, gatlingTick, NULL, false },
    { "TRENCH GUN", 3, 2, 0, 8, "", 4725, "QAV_PUMPU", "QAV_PUMPI", "QAV_PUMPD", trenchFire, NULL, trenchTrigger, false },
    { "DEVASTATOR", 7, -1, 400, 200, "", 5553, "QAV_DUKEUP", "QAV_DUKEID", "QAV_DUKEDO", devastatorFire, NULL, devastatorTrigger, false },
    { "ROCKET LAUNCHER", 7, -1, 100, 100, "", 5093, "QAV_SWUP", "QAV_SWIDLER", "QAV_SWDOWN", rocketFire, NULL, rocketTrigger, false },
    { "DEVIL'S BORE", 8, -1, 60, 30, "", 4730, "QAV_RAILGU", "QAV_RAILGI", "QAV_RAILGD", railFire, NULL, railTrigger, false },
    { "DUAL TOMMY GUNS", 4, 3, 0, 100, "", 4766, "QAV_2TOMUP", "QAV_2TOMIDLE", "QAV_2TOMDOWN", dualTommyFire, dualTommyTick, dualTommyTrigger, false },
    { "DUAL SAWED-OFFS", 3, 2, 0, 8, "", 4756, "QAV_2SHOTUP", "QAV_2SHOTI", "QAV_2SHOTDOWN", dualShotgunFire, NULL, dualShotgunTrigger, false },
    { "DUAL FLARE GUNS", 2, 1, 0, 9, "", 4763, "QAV_FLAR2UP", "QAV_FLAR2I", "QAV_FLAR2DWN", dualFlareFire, NULL, dualFlareTrigger, false },
    { "DEATH MOWER", 0, -1, kMowerTime, kMowerTime, "", kMowerPicnum, "QAV_MOWERR", "QAV_MOWERA", "QAV_MOWERT", mowerFire, mowerTick, NULL, false },
    { "MAXIM GUN", 0, -1, 1, 1, "", kMaximPicnum, "QAV_MAXIM1", "QAV_MAXIM1", "QAV_MAXIM1", turretFire, NULL, NULL, false },
    { "MOBILE MAXIM", 0, -1, 250, 250, "", 5077, "QAV_MAXUP", "QAV_MAXID", "QAV_MOWERT", maximFire, maximTick, maximTrigger, false },
    { "BLUNDERBUSS", 0, -1, 1, 1, "", kBlunderbussPicnum, "QAV_BLUNUP", "QAV_BLUNID", "QAV_MOWERT", blunderbussFire, NULL, blunderbussTrigger, false },
    { "CANNON", 0, -1, 1, 1, "", kCannonPicnum, "QAV_CANNONI", "QAV_CANNONI", NULL, cannonFire, NULL, NULL, false },
    { "CANNON BALL", 0, -1, 1, 1, "", kCannonBallPicnum, NULL, NULL, NULL, NULL, NULL, NULL, false },
    { "FIREWORKS CANNON", 0, -1, 1, 1, "", kFireworksPicnum, "QAV_FCFIRE", "QAV_FCFIRE", "QAV_MOWERT", NULL, fireworksTick, NULL, false },
    { "MOURNING CODEX", 10, -1, 1, 1, "", 4705, "QAV_BOOKU", "QAV_BOOKI", "QAV_BOOKD", codexFire, codexTick, codexTrigger, true },
    { "ZOMBIE AXE", 1, -1, 1, 1, "", kAxePicnum, "QAV_RAISAXE", "QAV_HOLDAXE", "QAV_LOWAXE", axeFire, NULL, axeTrigger, true },
    { "PHANTASM ORB", 1, -1, 1, 1, "", kOrbPicnum, "QAV_RAISORB", "QAV_HOLDORB", "QAV_LOWORB", orbFire, NULL, orbTrigger, true },
    { "VILE ANATHEMA", 1, -1, 1, 1, "", kAnathemaPicnum, "QAV_NECROUP", "QAV_NECROIDLE", "QAV_NECRODOWN", necroFire, NULL, anathemaTrigger, true },
    { "BASTION CHRONOMETER", 1, -1, 1, 1, "", kChronometerPicnum, "QAV_WATCHUP", "QAV_WATCHIDLE", "QAV_WATCHDOWN", necroFire, NULL, chronometerTrigger, true },
    { "REVOLVERS", 1, -1, kRevRounds, kRevRounds, "", 4752, "QAV_REVUP", "QAV_REVIDLE", "QAV_REVDOWN", revolverFire, revolverTick, revolverTrigger, false },
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
    // the revolvers are never empty: they reload
    return has(&gWState[pPlayer->nPlayer], nWeapon) && (ammoOf(pPlayer, nWeapon) > 0 || nWeapon == kBecRevolvers);
}

void raise(PLAYER *pPlayer, int nWeapon)
{
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    pState->nActive = nWeapon;
    pState->nPending = -1;
    pState->bPitchfork = 0;
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
    bool const bNew = !has(pState, nWeapon) || (w.bGone && pState->nAmmo[nWeapon] <= 0);  // a one-use one again
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
        else if (pPlayer->curWeapon)  // Blood's goes down first
            WeaponLower(pPlayer);
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
    // the mod's grabDist is 90 of its units (16 of ours each), its grabFOV 180 degrees; the maps' ones are
    // solid, so Caleb stops a good way off
    spritetype *pSprite = pPlayer->pSprite;
    int nBest = -1, nBestDist = 90 * 16;
    int16_t nEyeSect = pSprite->sectnum;  // the sector of Caleb's eyes (his sprite's can be the one below a step)
    updatesectorz(pSprite->x, pSprite->y, pPlayer->zView, &nEyeSect);
    if (nEyeSect < 0)
        nEyeSect = pSprite->sectnum;
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pItem = &sprite[nSprite];
        if (pItem->picnum != nPicnum || (gBecFlags[nSprite] & kBecFlagUsed) || (pItem->cstat & CSTAT_SPRITE_INVISIBLE))
            continue;
        int const dx = pItem->x - pSprite->x, dy = pItem->y - pSprite->y;
        int const nDist = approxDist(dx, dy);
        int const nAngle = klabs(((getangle(dx, dy) + 1024 - pSprite->ang) & 2047) - 1024);
        if (nDist >= nBestDist || nAngle >= 512 || klabs(pItem->z - pSprite->z) >= (64 << 8))
            continue;
        int top, bottom;
        GetSpriteExtents(pItem, &top, &bottom);
        if (!cansee(pSprite->x, pSprite->y, pPlayer->zView, nEyeSect, pItem->x, pItem->y, (top + bottom) / 2, pItem->sectnum))
            continue;
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
                      : !Bstrcasecmp(parm->parms[0], "aagun") ? kAAGunPicnum : !Bstrcasecmp(parm->parms[0], "axe") ? kAxePicnum
                      : !Bstrcasecmp(parm->parms[0], "orb") ? kOrbPicnum : !Bstrcasecmp(parm->parms[0], "anathema") ? kAnathemaPicnum
                      : !Bstrcasecmp(parm->parms[0], "chronometer") ? kChronometerPicnum : atoi(parm->parms[0]);
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
        pItem->cstat = CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN;  // solid, as the maps have them
        pItem->clipdist = 32;
        pItem->type = 0;
    }
    return OSDCMD_OK;
}

// bectest <player> [dist] | puke | blood: that player (a bot) in front of you, or puke on your face (for trying them)
int osdcmd_bectestbot(osdcmdptr_t parm)
{
    if (parm->numparms < 1 || !gGameStarted || !gMe || !gMe->pSprite)
        return OSDCMD_SHOWHELP;
    if (!Bstrcasecmp(parm->parms[0], "weather"))  // the level's weather markers; you go to the nearest
    {
        spritetype *pSprite = gMe->pSprite, *pNearest = NULL;
        int nNearest = INT_MAX;
        for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
        {
            if (!gWeather[nSprite])
                continue;
            int const nDist = approxDist(sprite[nSprite].x - pSprite->x, sprite[nSprite].y - pSprite->y);
            OSD_Printf("weather %d at sprite %d, %d away, sector %d %s\n", gWeather[nSprite], nSprite, nDist, sprite[nSprite].sectnum,
                       (sector[sprite[nSprite].sectnum].ceilingstat & 1) ? "(sky)" : "");
            if (nDist < nNearest)
                nNearest = nDist, pNearest = &sprite[nSprite];
        }
        if (pNearest && parm->numparms > 1)
        {
            int ceilZ, floorZ;
            getzsofslope(pNearest->sectnum, pNearest->x, pNearest->y, &ceilZ, &floorZ);
            vec3_t pos = { pNearest->x, pNearest->y, floorZ };
            setsprite(pSprite->index, &pos);
        }
        return OSDCMD_OK;
    }
    if (!Bstrcasecmp(parm->parms[0], "puke") || !Bstrcasecmp(parm->parms[0], "blood"))
    {
        goryKillScreenPuke(gMe, !Bstrcasecmp(parm->parms[0], "blood"));
        if (!Bstrcasecmp(parm->parms[0], "puke"))
        {
            gMe->pwUpTime[kPwUpDeliriumShroom] = ClipLow(gMe->pwUpTime[kPwUpDeliriumShroom], 3 * kTicRate);
            powerupActivate(gMe, kPwUpDeliriumShroom);
        }
        return OSDCMD_OK;
    }
    int const n = atoi(parm->parms[0]);
    if (n < 0 || n >= kMaxPlayers || !gPlayer[n].pSprite)
        return OSDCMD_OK;
    spritetype *pSprite = gMe->pSprite;
    int const nDist = parm->numparms > 1 ? atoi(parm->parms[1]) : 400;
    vec3_t pos = { pSprite->x + mulscale30(nDist, Cos(pSprite->ang)), pSprite->y + mulscale30(nDist, Sin(pSprite->ang)), pSprite->z };
    setsprite(gPlayer[n].pSprite->index, &pos);
    return OSDCMD_OK;
}

class BecWeaponsLoadSave : public LoadSave
{
    virtual void Load(void)
    {
        Read(gWState, sizeof(gWState));
        Read(gMissileBoom, sizeof(gMissileBoom));
        Read(gSpellKind, sizeof(gSpellKind));
        Read(gSpellTime, sizeof(gSpellTime));
        Read(gSpellTarget, sizeof(gSpellTarget));
        Read(gWeather, sizeof(gWeather));
        Read(gWeatherTimer, sizeof(gWeatherTimer));
        Read(&gThunderNext, sizeof(gThunderNext));
    }
    virtual void Save(void)
    {
        Write(gWState, sizeof(gWState));
        Write(gMissileBoom, sizeof(gMissileBoom));
        Write(gSpellKind, sizeof(gSpellKind));
        Write(gSpellTime, sizeof(gSpellTime));
        Write(gSpellTarget, sizeof(gSpellTarget));
        Write(gWeather, sizeof(gWeather));
        Write(gWeatherTimer, sizeof(gWeatherTimer));
        Write(&gThunderNext, sizeof(gThunderNext));
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
    OSD_RegisterFunction("bectest", "bectest <player> [distance] | puke | blood: a player in front of you, or puke on your face (testing)", osdcmd_bectestbot);
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
    if (bActive && getQav(QAVID("QAV_REVIDLE")))  // Caleb's revolvers, the mod's pitchfork
    {
        pState->nHave |= 1u << kBecRevolvers;
        pState->nAmmo[kBecRevolvers] = kRevRounds;
    }
}

void becWeaponsPlayerStart(PLAYER *pPlayer)
{
    // a new level or life: Blood's weapon comes up first, the mod's stay in the pack
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    pState->nActive = pState->nPending = -1;
    pState->nQav = -1;
    pState->nNext = 0;
    pState->nFrost = pState->nSkulls = 0;
    pState->nPurify = 0;
    pState->bPitchfork = 0;  // the revolvers come up first
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

    // Blood's pitchfork coming up or in hand: Caleb's revolvers instead (they're the mod's pitchfork)
    if (pPlayer->curWeapon > kWeaponPitchfork)  // (raise() clears it too)
        pState->bPitchfork = 0;
    if (has(pState, kBecRevolvers) && pState->nActive < 0 && pState->nPending < 0 && pState->nNext == 0 && !pState->bPitchfork
        && (pPlayer->input.newWeapon == kWeaponPitchfork || (pPlayer->curWeapon == kWeaponPitchfork && pPlayer->input.newWeapon == 0)))
    {
        pPlayer->input.newWeapon = kWeaponNone;
        pState->nPending = kBecRevolvers;
        if (pPlayer->curWeapon)
            WeaponLower(pPlayer);
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
            pState->bPitchfork = nNew == kWeaponPitchfork;  // key 1 again: Blood's pitchfork, wanted
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
        if (c.ammo() <= 0 && c.nWeapon == kBecRevolvers)  // the revolvers reload instead
            revolverReload(c);
        else if (c.ammo() <= 0)  // empty: Blood's next weapon
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

// the mod's HUD pictures (BEC_Players.txt, weapon_pitchfork.txt; its HUD is 320x240, ours 320x200 with the
// status bar at the bottom): the revolvers' Purify (left) and Purge (right) with their countdowns, the
// Necromancer's things carried, the skull turret on the shoulder, the lightning's flash
static void hudDigits(int x, int y, int nValue, int nFlags)
{
    char buffer[8];
    Bsnprintf(buffer, sizeof(buffer), "%02d", ClipRange(nValue, 0, 99));
    for (int i = 0; buffer[i]; i++)
        rotatesprite((x + i * 4) << 16, y << 16, 65536, 0, 5050 + buffer[i] - '0', 0, 0, nFlags, 0, 0, xdim - 1, ydim - 1);
}

void becWeaponsHudDraw(PLAYER *pPlayer)
{
    if (!bActive || !pPlayer)
        return;
    WSTATE const *pState = &gWState[pPlayer->nPlayer];
    int const nPlayer = pPlayer->nPlayer;
    if (gLightning[nPlayer] > 0 && tilesiz[5924].x > 0)
        rotatesprite(160 << 16, 100 << 16, 65536, 0, 5924, -128, 0, RS_AUTO | RS_STRETCH | RS_TRANS1, 0, 0, xdim - 1, ydim - 1);
    if (!pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
        return;
    int const nBottom = 163;  // just above the status bar
    if (pState->nActive == kBecRevolvers && pState->nPhase != kPhaseLower)
    {
        int const nLeft = RS_AUTO | RS_ALIGN_L, nRight = RS_AUTO | RS_ALIGN_R;
        bool const bPurifyReady = pState->nPurify <= 0 && pState->nPurifyWait <= 0;
        rotatesprite(12 << 16, nBottom << 16, 65536, 0, bPurifyReady ? 5118 : 5117, 0, 0, nLeft, 0, 0, xdim - 1, ydim - 1);
        if (pState->nPurifyWait > 0)
            hudDigits(27, nBottom, pState->nPurifyWait / kTicsPerSec + 1, nLeft);
        rotatesprite(307 << 16, nBottom << 16, 65536, 0, pState->nPurgeWait > 0 ? 5061 : 5062, 0, 0, nRight, 0, 0, xdim - 1, ydim - 1);
        if (pState->nPurgeWait > 0)
            hudDigits(288, nBottom, pState->nPurgeWait / kTicsPerSec + 1, nRight);
    }
    // the things the dead left, carried (a small picture each, right)
    static struct { int8_t nWeapon; short nTile; short x; } const gCarried[] = {
        { kBecAnathema, 5219, 292 }, { kBecAxe, 5596, 300 }, { kBecOrb, 5597, 308 }, { kBecChronometer, 7364, 316 },
    };
    for (auto const &c : gCarried)
        if (usable(pPlayer, c.nWeapon) && tilesiz[c.nTile].x > 0)
            rotatesprite(c.x << 16, (nBottom - 14) << 16, 65536 / 8, 0, c.nTile, 0, 0, RS_AUTO | RS_ALIGN_R, 0, 0, xdim - 1, ydim - 1);
    // the Vile Anathema's skull on his shoulder while its skulls fly, and their time
    if (pState->nSkulls > 0)
    {
        int const nTile = gSkullShot[nPlayer] > 0 ? ((gSkullShot[nPlayer] & 1) ? 7849 : 7854) : 7841;
        if (gSkullShot[nPlayer] > 0)
            gSkullShot[nPlayer]--;
        if (tilesiz[nTile].x > 0)
            rotatesprite(30 << 16, 120 << 16, 65536, 0, nTile, 0, 0, RS_AUTO | RS_ALIGN_L, 0, 0, xdim - 1, ydim - 1);
        hudDigits(24, 45, pState->nSkulls / kTicsPerSec + 1, RS_AUTO | RS_ALIGN_L);
    }
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
    // what the dead leave (BEC_Players.txt: grabbed when Caleb hasn't one already): an axe, an orb, the Necromancer's
    static struct { short nPicnum; int8_t nWeapon; } const gGrab[] = {
        { kAxePicnum, kBecAxe }, { kOrbPicnum, kBecOrb }, { kAnathemaPicnum, kBecAnathema }, { kChronometerPicnum, kBecChronometer },
    };
    for (auto const &g : gGrab)
    {
        if (usable(pPlayer, g.nWeapon))
            continue;
        int const nItem = inReach(pPlayer, g.nPicnum);
        if (nItem < 0)
            continue;
        actPostSprite(nItem, kStatFree);
        sfxPlay3DSound(pPlayer->pSprite, kPickupSound, -1, 0);
        pPlayer->pickupEffect = 30;
        takeUp(pPlayer, g.nWeapon, 1);
        if (pPlayer == gMe)
        {
            char buffer[64];
            Bsnprintf(buffer, sizeof(buffer), "Picked up %s -SLOT %d-", gWeapons[g.nWeapon].pzName, gWeapons[g.nWeapon].nSlot);
            viewSetMessage(buffer, 0, MESSAGE_PRIORITY_PICKUP);
        }
        return true;
    }
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

// the bots' view of the mod's weapons (bot.cpp gBotWeapon has Blood's: power 12 pitchfork .. 85 tesla)
static BECBOTWEAPON const gBotWeapons[kBecWeaponMax] = {
    { 95, 0, 2500, 9000, 60, 0, 1800, 0 },           // Gatling Gun (alt: its wider overkill up close)
    { 80, 0, 1100, 4000, 60, 0, 1500, 0 },           // Trench Gun (alt: dragon's breath)
    { 90, 900, 3500, 12000, 70, 0x2aaaaa, 0, 0 },    // Devastator
    { 95, 1500, 5000, 16000, 60, 0x2aaaaa, 0, 0 },   // rocket launcher
    { 100, 0, 6000, 30000, 30, 0, 0, 0 },            // Devil's Bore
    { 80, 0, 2500, 10000, 55, 0, 0, 0 },             // dual tommy guns
    { 90, 0, 1000, 3500, 60, 0, 0, 0 },              // dual sawed-offs
    { 60, 300, 3000, 12000, 70, 0x2aaaaa, 0, 0 },    // dual flare guns
    { 0 },                                           // Death Mower (it runs by itself)
    { 0 },                                           // Maxim on its stand
    { 90, 0, 2500, 9000, 60, 0, 0, 0 },              // Mobile Maxim
    { 85, 0, 900, 2500, 80, 0, 0, 0 },               // Blunderbuss
    { 0 }, { 0 }, { 0 },                             // cannon, cannon ball, fireworks
    { 100, 0, 3000, 20000, 400, 0, 6000, 0 },        // Mourning Codex (alt: the blizzard when they're near)
    { 50, 0, 600, 3000, 140, 0x2aaaaa, 0, 1000 },    // Zombie Axe (alt: thrown, further off)
    { 85, 0, 2000, 10000, 250, 0, 0, 0 },            // Phantasm Orb (it finds its way)
    { 70, 0, 3000, 15000, 300, 0, 30000, 0 },        // Vile Anathema (alt: the skulls, no corpses to raise)
    { 60, 0, 2000, 10000, 400, 0, 4000, 0 },         // Bastion Chronometer (alt: the Doom Bell when near)
    { 40, 0, 2000, 9000, 60, 0, 0, 0 },              // revolvers
};

int becBotWeaponCount(void)
{
    return bActive ? kBecWeaponMax : 0;
}

bool becBotWeaponInfo(int nWeapon, BECBOTWEAPON *pInfo)
{
    if (!bActive || nWeapon < 0 || nWeapon >= kBecWeaponMax || !gBotWeapons[nWeapon].nPower)
        return false;
    *pInfo = gBotWeapons[nWeapon];
    return true;
}

bool becBotUsable(PLAYER *pPlayer, int nWeapon)
{
    return bActive && nWeapon >= 0 && nWeapon < kBecWeaponMax && usable(pPlayer, nWeapon);
}

int becBotActive(PLAYER *pPlayer)
{
    if (!bActive)
        return -1;
    WSTATE const *pState = &gWState[pPlayer->nPlayer];
    if (pState->nNext <= -2)
        return -2 - pState->nNext;
    if (pState->nPending >= 0)
        return pState->nPending;
    return pState->nPhase == kPhaseLower ? -1 : pState->nActive;
}

int becBotSlot(int nWeapon)
{
    return nWeapon >= 0 && nWeapon < kBecWeaponMax ? gWeapons[nWeapon].nSlot : 0;
}

bool becBotItemGivesWeapon(PLAYER *pPlayer, int nItemType)
{
    if (!bActive)
        return false;
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    switch (nItemType)
    {
    case kItemTwoGuns:
        return !usable(pPlayer, kBecGatling);
    case kItemBeastVision:
        return !usable(pPlayer, kBecRailgun);
    case kItemReflectShots:
        return !usable(pPlayer, kBecCodex);
    case kItemWeaponTommygun:
        return pPlayer->hasWeapon[kWeaponTommy] && !has(pState, kBecDualTommy);
    case kItemWeaponSawedoff:
        return pPlayer->hasWeapon[kWeaponShotgun] && !has(pState, kBecDualShotgun);
    case kItemWeaponFlarePistol:
        return pPlayer->hasWeapon[kWeaponFlare] && !has(pState, kBecDualFlare);
    }
    return false;
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
    case kItemReflectShots:  // its Reflective Shots the Mourning Codex
        nWeapon = kBecCodex;
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

// in Blood's level preload: the animations of the mod's weapons the players carry, loaded with the level
void becWeaponsPrecache(void)
{
    if (!bActive)
        return;
    uint32_t nHave = 0;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        nHave |= gWState[p].nHave;
    for (int i = 0; i < kBecWeaponMax; i++)
    {
        if (!((nHave >> i) & 1))
            continue;
        WEAPON const &w = gWeapons[i];
        for (char const *pzQav : { w.pzUp, w.pzIdle, w.pzDown })
            if (QAV *pQav = getQav(becDefine(pzQav, -1)))
                pQav->Precache();
    }
}

// the mod's weapons its maps place: pictures (decorations) its scripts turn into them
void becWeaponsLevelStart(bool bSaveLoad)
{
    if (!bActive || bSaveLoad)  // a saved game brings its own
        return;
    memset(gMissileBoom, 0, sizeof(gMissileBoom));
    memset(gSpellKind, 0, sizeof(gSpellKind));
    memset(gSpellTime, 0, sizeof(gSpellTime));
    memset(gSpellTarget, 0, sizeof(gSpellTarget));
    weatherLevelStart();
}

namespace
{
//
// the mod's weather (WeatherMaker.txt): its maps mark where with pictures (6421-6427, 6444, 6445). Near
// one, rain, a storm (thunder and lightning), blood rain or snow falls around each player under the sky;
// the artillery marker drops shells about itself now and then. (The mod sends invisible missiles that
// drop the drops; here they're Blood's falling effects, made where the players are.)
//

enum { kWeatherNone, kWeatherRain, kWeatherStorm, kWeatherBloodRain, kWeatherSnow, kWeatherArtillery };
enum { kWeatherRange = 25000 };  // how near a marker the players get its weather

int gLightning[kMaxPlayers];         // frames left of the lightning's flash (drawn only)
int gSkullShot[kMaxPlayers];         // frames left of the skull turret's fire picture (drawn only)

void weatherLevelStart(void)
{
    memset(gWeather, 0, sizeof(gWeather));
    memset(gWeatherTimer, 0, sizeof(gWeatherTimer));
    gThunderNext = 0;
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pMarker = &sprite[nSprite];
        int nKind = kWeatherNone;
        switch (pMarker->picnum)
        {
        case 6421: nKind = kWeatherArtillery; break;
        case 6422: nKind = kWeatherBloodRain; break;
        case 6424: case 6425: nKind = kWeatherRain; break;
        case 6426: case 6445: nKind = kWeatherSnow; break;
        case 6427: case 6444: nKind = kWeatherStorm; break;
        }
        if (!nKind)
            continue;
        gWeather[nSprite] = nKind;
        gWeatherTimer[nSprite] = (3 + Random(15)) * kTicsPerSec;
        pMarker->cstat |= CSTAT_SPRITE_INVISIBLE;
        pMarker->cstat &= ~(CSTAT_SPRITE_BLOCK | CSTAT_SPRITE_BLOCK_HITSCAN);
    }
}

// a drop where a player is: somewhere under the sky within reach of him
void weatherDrop(spritetype *pPlayer, int nKind)
{
    int const x = pPlayer->x + Random2(3000), y = pPlayer->y + Random2(3000);
    int16_t nSector = pPlayer->sectnum;
    updatesector(x, y, &nSector);
    if (nSector < 0 || !(sector[nSector].ceilingstat & 1))
        return;
    int ceilZ, floorZ;
    getzsofslope(nSector, x, y, &ceilZ, &floorZ);
    // snow lives a second (Blood's slow drift): it starts nearer the eyes
    int const z = max(ceilZ + (8 << 8), pPlayer->z - ((nKind == kWeatherSnow ? 110 : 192) << 8));
    if (z >= floorZ - (16 << 8))
        return;
    FX_ID const nFx = nKind == kWeatherBloodRain ? FX_13 : nKind == kWeatherSnow ? FX_28 : FX_16;
    spritetype *pDrop = gFX.fxSpawn(nFx, nSector, x, y, z, 0);
    if (!pDrop)
        return;
    switch (nKind)
    {
    case kWeatherSnow:  // MISSILE_SnowFall: slow, drifting
        pDrop->picnum = 5467;
        pDrop->xrepeat = pDrop->yrepeat = 16;
        zvel[pDrop->index] = 0x10000;
        xvel[pDrop->index] = Random2(0x8000);
        yvel[pDrop->index] = Random2(0x8000);
        break;
    case kWeatherBloodRain:  // MISSILE_BloodRain: it splats where it lands
        pDrop->picnum = 4741;
        pDrop->xrepeat = pDrop->yrepeat = 32;
        zvel[pDrop->index] = 0x80000;
        break;
    default:  // MISSILE_StormRain / RainFall: a streak of water
        pDrop->picnum = 1147;
        pDrop->xrepeat = 24;
        pDrop->yrepeat = 128;
        zvel[pDrop->index] = 0xc0000;
        break;
    }
}

// a shell from the sky (MISSILE_Artillery): it whistles down near its marker and blows up
void weatherShell(spritetype *pMarker)
{
    int const x = pMarker->x + Random2(4000), y = pMarker->y + Random2(4000);
    int16_t nSector = pMarker->sectnum;
    updatesector(x, y, &nSector);
    if (nSector < 0 || !(sector[nSector].ceilingstat & 1))
        return;
    int ceilZ, floorZ;
    getzsofslope(nSector, x, y, &ceilZ, &floorZ);
    vec3_t pos = { x, y, ceilZ + (16 << 8) };
    setsprite(pMarker->index, &pos);  // the marker is where it comes from
    nFiringBoom = kBoomStandard;
    spritetype *pShell = actFireMissile(pMarker, 0, 0, 0, 0, 1 << 14, kMissileFireballNapalm);
    nFiringBoom = kBoomNone;
    if (!pShell)
        return;
    pShell->picnum = 5845;
    pShell->xrepeat = pShell->yrepeat = 48;
    gMissileBoom[pShell->index] = kBoomStandard;
    if (pShell->extra > 0)
        seqKill(3, pShell->extra);
    sfxPlay3DSound(pShell, becDefine("SFX_SHELL1", -1) + Random(2), -1, 0);
}

void weather(void)
{
    bool bAny = false;
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
        bAny |= gWeather[nSprite] != kWeatherNone;
    if (!bAny)
        return;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pPlayer = &gPlayer[p];
        if (gLightning[p] > 0)
            gLightning[p]--;
        if (!pPlayer->pSprite || !pPlayer->pXSprite || pPlayer->pXSprite->health <= 0)
            continue;
        spritetype *pSprite = pPlayer->pSprite;
        uint32_t nKinds = 0;  // the weathers near him
        for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
            if (gWeather[nSprite] && gWeather[nSprite] != kWeatherArtillery
                && approxDist(sprite[nSprite].x - pSprite->x, sprite[nSprite].y - pSprite->y) < kWeatherRange)
                nKinds |= 1u << gWeather[nSprite];
        for (int nKind = kWeatherRain; nKind <= kWeatherSnow; nKind++)
        {
            if (!((nKinds >> nKind) & 1))
                continue;
            int const nDrops = nKind == kWeatherStorm ? 6 : nKind == kWeatherRain ? 4 : nKind == kWeatherSnow ? 8 : 3;
            for (int i = 0; i < nDrops; i++)
                weatherDrop(pSprite, nKind == kWeatherStorm ? kWeatherRain : nKind);
        }
        // the storm: thunder now and then, and the flash of the lightning
        if ((nKinds >> kWeatherStorm) & 1)
        {
            int const nNow = (int)gFrameClock;
            if (!gThunderNext)
                gThunderNext = nNow + (10 + Random(20)) * kTicRate;
            else if (nNow >= gThunderNext)
            {
                gThunderNext = nNow + (10 + Random(20)) * kTicRate;
                for (int q = connecthead; q >= 0; q = connectpoint2[q])
                {
                    if (!gPlayer[q].pSprite)
                        continue;
                    gLightning[q] = 6;
                    sfxPlay3DSound(gPlayer[q].pSprite, becDefine("SFX_THUNDER1", -1), -1, 0);
                }
            }
        }
    }
    // the artillery: a shell every few seconds while somebody is about
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        if (gWeather[nSprite] != kWeatherArtillery)
            continue;
        bool bNear = false;
        for (int p = connecthead; p >= 0 && !bNear; p = connectpoint2[p])
            if (spritetype *pPlayer = gPlayer[p].pSprite)
                bNear = approxDist(pPlayer->x - sprite[nSprite].x, pPlayer->y - sprite[nSprite].y) < kWeatherRange;
        if (!bNear || --gWeatherTimer[nSprite] > 0)
            continue;
        gWeatherTimer[nSprite] = (3 + Random(17)) * kTicsPerSec;
        weatherShell(&sprite[nSprite]);
    }
}

// the AA guns (BEC_Players.txt): volleys of eleven flak shells into the sky while you're about
int16_t gAADelay[kMaxSprites];
int8_t gAAVolley[kMaxSprites];

// a player within that distance (any of them: the game runs the same on every player's machine)
bool playerNear(spritetype *pSprite, int nRange)
{
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (spritetype *pPlayer = gPlayer[p].pSprite)
            if (approxDist(pPlayer->x - pSprite->x, pPlayer->y - pSprite->y) <= nRange)
                return true;
    return false;
}

void aaGuns(void)
{
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pGun = &sprite[nSprite];
        if (pGun->picnum < kAAGunPicnum || pGun->picnum > kAAGunPicnum + 2)
            continue;
        if (!playerNear(pGun, 4000 * 16))
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

// the mod's deaths by the orb (Orb.txt): the body with the orb drilling through its head
char const *orbDeathSeq(int nType)
{
    switch (nType)
    {
    case kDudeCultistTommy:
    case kDudeCultistTommyProne:
        return "SEQ_CUL1ORB";
    case kDudeCultistShotgun:
    case kDudeCultistShotgunProne:
        return "SEQ_CUL2ORB";
    case kDudeCultistTesla:
        return "SEQ_CUL3ORB";
    case kDudeCultistTNT:
        return "SEQ_CUL4ORB";
    case kDudeZombieAxeNormal:
    case kDudeZombieAxeBuried:
    case kDudeZombieAxeLaying:
        return "SEQ_AXEZORB";
    case kDudeBecZombieSkeletal:
    case kDudeBecZombieSkeletalEarth:
    case kDudeBecZombieSkeletalSleep:
        return "SEQ_SKEZORB";
    case kDudeZombieButcher:
        return "SEQ_FATZORB";
    case kDudeGillBeast:
        return "SEQ_GILLORB";
    case kDudeInnocent:
        return "SEQ_INNOORB";
    case kDudeBecCrimson:
    case kDudeBecCrimsonEarth:
        return "SEQ_CRIMORB";
    }
    return NULL;
}

// the nearest enemy to a spot, within nRange, -1 for none
int nearestTo(spritetype *pAt, int nRange)
{
    int16_t list[64];
    int const nCount = enemiesNear(pAt, nRange, false, list, 64);
    int nBest = -1, nBestDist = INT_MAX;
    for (int i = 0; i < nCount; i++)
    {
        int const nDist = approxDist(sprite[list[i]].x - pAt->x, sprite[list[i]].y - pAt->y);
        if (nDist < nBestDist)
            nBest = list[i], nBestDist = nDist;
    }
    return nBest;
}

void orbKill(spritetype *pOrb, spritetype *pTarget)
{
    int const nOwner = actSpriteOwnerToSpriteId(pOrb);
    int const nType = pTarget->type;
    sfxPlay3DSound(pTarget, becDefine("SFX_ORBDRIL", -1), -1, 0);
    sfxPlay3DSound(pTarget, becDefine("SFX_GORHEAD", -1), -1, 0);
    actDamageSprite(nOwner, pTarget, kDamageBullet, (xsprite[pTarget->extra].health << 4) + (1 << 12));
    if (pTarget->extra > 0 && xsprite[pTarget->extra].health > 0)  // not hurt by bullets: by the spirit
        actDamageSprite(nOwner, pTarget, kDamageSpirit, (xsprite[pTarget->extra].health << 4) + (1 << 12));
    if (pTarget->extra > 0 && xsprite[pTarget->extra].health <= 0 && orbDeathSeq(nType))
        becCorpseSeq(pTarget, orbDeathSeq(nType));
}

// the axe thrown lands: in whoever it hit, then on the floor to be picked up again
void axeLands(spritetype *pAxe)
{
    int const nOwner = actSpriteOwnerToSpriteId(pAxe);
    int const nHit = nearestTo(pAxe, 384);
    if (nHit >= 0)
    {
        actDamageSprite(nOwner, &sprite[nHit], kDamageBullet, 1152);
        sfxPlay3DSound(&sprite[nHit], becDefine("SFX_AXEHIT1", -1), -1, 0);
    }
    int x = pAxe->x - (xvel[pAxe->index] >> 12), y = pAxe->y - (yvel[pAxe->index] >> 12);  // back from the wall
    int16_t nSector = pAxe->sectnum;
    updatesector(x, y, &nSector);
    if (nSector < 0)
        x = pAxe->x, y = pAxe->y, nSector = pAxe->sectnum;
    int ceilZ, floorZ;
    getzsofslope(nSector, x, y, &ceilZ, &floorZ);
    spritetype *pItem = actSpawnSprite(nSector, x, y, floorZ, kStatDecoration, 0);
    if (pItem)
    {
        pItem->picnum = kAxePicnum;
        pItem->xrepeat = pItem->yrepeat = 32;
        pItem->ang = pAxe->ang;
        pItem->cstat = 0;
        pItem->type = 0;
        gBecFlags[pItem->index] = 0;
        gSpellKind[pItem->index] = kSpellNone;
    }
}

// the Death Portal opens (DeathPortal.txt): it pulls in what it sees and swallows what comes near
void openPortal(spritetype *pAt)
{
    spritetype *pPortal = actSpawnSprite(pAt->sectnum, pAt->x, pAt->y, pAt->z, kStatDecoration, 1);
    if (!pPortal)
        return;
    pPortal->picnum = kPortalPicnum;
    pPortal->xrepeat = pPortal->yrepeat = 64;
    pPortal->cstat = CSTAT_SPRITE_TRANSLUCENT;
    pPortal->shade = -128;
    pPortal->type = 0;
    pPortal->owner = actSpriteOwnerToSpriteId(pAt);
    gBecFlags[pPortal->index] = 0;
    int const nSeq = becDefine("SEQ_DEATHPOR", -1);
    if (nSeq >= 0 && pPortal->extra > 0 && gSysRes.Lookup(nSeq, "SEQ"))
        seqSpawn(nSeq, 3, pPortal->extra, -1);
    gSpellKind[pPortal->index] = kSpellPortal;
    gSpellTime[pPortal->index] = 4 * kTicsPerSec;
    sfxPlay3DSound(pPortal, becDefine("SFX_RGTELEP", -1), -1, 0);
}

void portalTick(spritetype *pPortal)
{
    int16_t list[64];
    int const nCount = enemiesNear(pPortal, 32 * 512, true, list, 64);
    for (int i = 0; i < nCount; i++)
    {
        spritetype *pVictim = &sprite[list[i]];
        int const dx = pPortal->x - pVictim->x, dy = pPortal->y - pVictim->y;
        int const nDist = ClipLow(approxDist(dx, dy), 1);
        if (nDist < 1024)
        {
            sfxPlay3DSound(pPortal, becDefine("SFX_RGTELEP", -1), -1, 0);
            actDamageSprite(pPortal->owner, pVictim, kDamageExplode, 8000);
            continue;
        }
        xvel[pVictim->index] += (int)(((int64_t)dx << 15) / nDist);
        yvel[pVictim->index] += (int)(((int64_t)dy << 15) / nDist);
    }
}

// the Doom Bell rings (sharedCallbacks.txt ACTOR_DoomBell): everything about it is hurt, every ring
void bellTick(spritetype *pBell)
{
    int const nSprite = pBell->index;
    if (--gSpellTime[nSprite] > 0)
        return;
    gSpellTime[nSprite] = 20;  // the mod's 40 ticks of its
    int const nDoom = becDefine("SFX_DOOMBEL", -1);
    sfxPlay3DSound(pBell, nDoom >= 0 ? nDoom : 270, -1, 0);
    int16_t list[64];
    int const nCount = enemiesNear(pBell, 16 * 512, false, list, 64);
    for (int i = 0; i < nCount; i++)
    {
        actDamageSprite(pBell->owner, &sprite[list[i]], kDamageExplode, 200);
        if (isEnemy(&sprite[list[i]]))
            actDamageSprite(pBell->owner, &sprite[list[i]], kDamageSpirit, 200);
    }
    if (--gSpellTarget[nSprite] > 0)
        return;
    // its last: it blows apart
    gSpellKind[nSprite] = kSpellNone;
    spritetype *pBoom = actSpawnThing(pBell->sectnum, pBell->x, pBell->y, pBell->z - (16 << 8), kThingArmedTNTBundle);
    if (pBoom)
    {
        if (pBell->owner >= 0)
            actPropagateSpriteOwner(pBoom, &sprite[pBell->owner]);
        actExplodeSprite(pBoom);
    }
    actPostSprite(nSprite, kStatFree);
}

// the orb finds its head; the portal on its way opens
void spellMissiles(void)
{
    for (int nSprite = headspritestat[kStatProjectile]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        int const nBoom = gMissileBoom[nSprite];
        if (nBoom != kBoomOrb && nBoom != kBoomPortal)
            continue;
        spritetype *pMissile = &sprite[nSprite];
        if (--gSpellTime[nSprite] <= 0)  // the portal opens where it is; the orb gives up
        {
            gMissileBoom[nSprite] = 0;
            if (nBoom == kBoomPortal)
                openPortal(pMissile);
            else
                sfxPlay3DSound(pMissile->x, pMissile->y, pMissile->z, becDefine("SFX_ORBEXPL", -1), pMissile->sectnum);
            actPostSprite(nSprite, kStatFree);
            continue;
        }
        if (nBoom != kBoomOrb)
            continue;
        int nTarget = gSpellTarget[nSprite];
        if (nTarget < 0 || !isEnemy(&sprite[nTarget]))
        {
            nTarget = gSpellTarget[nSprite] = nearestTo(pMissile, 16 * 512);
            if (nTarget < 0)
                continue;
        }
        spritetype *pTarget = &sprite[nTarget];
        int top, bottom;
        GetSpriteExtents(pTarget, &top, &bottom);
        int const dx = pTarget->x - pMissile->x, dy = pTarget->y - pMissile->y, dz = top + (bottom - top) / 6 - pMissile->z;
        if (approxDist(dx, dy) < 256 && klabs(dz) < (32 << 8))
        {
            orbKill(pMissile, pTarget);
            gMissileBoom[nSprite] = 0;
            actPostSprite(nSprite, kStatFree);
            continue;
        }
        // on toward its head, as fast as it goes (z is 16 times finer than x and y)
        double const vx = xvel[nSprite], vy = yvel[nSprite], vz = zvel[nSprite];
        double const fSpeed = sqrt(vx * vx + vy * vy + vz * vz);
        double const fx = dx, fy = dy, fz = dz / 16.0;
        double const fLen = sqrt(fx * fx + fy * fy + fz * fz);
        if (fLen < 1)
            continue;
        xvel[nSprite] = (int)(fx / fLen * fSpeed);
        yvel[nSprite] = (int)(fy / fLen * fSpeed);
        zvel[nSprite] = (int)(fz / fLen * fSpeed);
    }
    for (int nSprite = headspritestat[kStatDecoration]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        if (gSpellKind[nSprite] == kSpellBell)
            bellTick(&sprite[nSprite]);
        else if (gSpellKind[nSprite] == kSpellPortal)
        {
            portalTick(&sprite[nSprite]);
            if (--gSpellTime[nSprite] <= 0)
            {
                gSpellKind[nSprite] = kSpellNone;
                actPostSprite(nSprite, kStatFree);
            }
        }
    }
}

// the spells on Caleb: the Codex's blizzard, the Anathema's skulls
void playerSpells(PLAYER *pPlayer)
{
    WSTATE *pState = &gWState[pPlayer->nPlayer];
    spritetype *pSprite = pPlayer->pSprite;
    if (pState->nFrost > 0 && --pState->nFrost % 4 == 0)  // an ice spike into one of them about
    {
        int16_t list[64];
        int const nCount = enemiesNear(pSprite, 16 * 512, true, list, 64);
        if (nCount)
        {
            spritetype *pVictim = &sprite[list[Random(nCount)]];
            sfxPlay3DSound(pVictim, becDefine("SFX_ISPIKE", -1), -1, 0);
            actDamageSprite(pSprite->index, pVictim, kDamageBullet, 160);
        }
    }
    if (pState->nSkulls > 0 && --pState->nSkulls % 4 == 0 && enemyInSight(pPlayer, kAng60, 40 * 512) >= 0)
    {
        Aim *aim = &pPlayer->aim;
        spritetype *pSkull = actFireMissile(pSprite, -50, pPlayer->zWeapon - pSprite->z, aim->dx + Random2(200), aim->dy + Random2(200),
                                            aim->dz + Random2(200), kMissileEctoSkull);
        if (pSkull)
            pSkull->picnum = kSkullPicnum;
        sfxPlay3DSound(pSprite, becDefine("SFX_GORGEF2", -1), -1, 0);
        gSkullShot[pPlayer->nPlayer] = 3;
    }
}
} // namespace

// touching one of the mod's weapon pictures in a map picks it up
void becWeaponsFrame(void)
{
    if (!bActive)
        return;
    aaGuns();
    spellMissiles();
    weather();
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
        {
            gWState[p].nFrost = gWState[p].nSkulls = 0;
            continue;
        }
        playerSpells(pPlayer);
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
bool becImpactMissile(spritetype *pMissile, int nHitCode)
{
    // puke on a player's face (a Crimson Innocent's or Slime Zombie's red, a fat zombie's green): it stays
    // on the screen until he wipes it off (USE), as the mod's ScreenPuke
    if (pMissile->type == kMissilePukeGreen && nHitCode == 3 && gHitInfo.hitsprite >= 0 && gHitInfo.hitsprite < kMaxSprites
        && IsPlayerSprite(&sprite[gHitInfo.hitsprite]))
    {
        PLAYER *pVictim = &gPlayer[sprite[gHitInfo.hitsprite].type - kDudePlayer1];
        goryKillScreenPuke(pVictim, pMissile->pal == 2);
        if (pMissile->pal != 2)  // the green makes his head swim (the mod's PU_DELIRIUM), as the shroom does
        {
            pVictim->pwUpTime[kPwUpDeliriumShroom] = ClipLow(pVictim->pwUpTime[kPwUpDeliriumShroom], 3 * kTicRate);
            powerupActivate(pVictim, kPwUpDeliriumShroom);
        }
    }
    int const nBoom = gMissileBoom[pMissile->index] ? gMissileBoom[pMissile->index] : nFiringBoom;
    if (!nBoom)
        return false;
    gMissileBoom[pMissile->index] = 0;
    if (nBoom == kBoomOrb || nBoom == kBoomAxe || nBoom == kBoomPortal)
    {
        if (nBoom == kBoomOrb)  // into whoever it hit, or gone against the wall
        {
            int const nHit = nearestTo(pMissile, 384);
            if (nHit >= 0)
                orbKill(pMissile, &sprite[nHit]);
            else
                sfxPlay3DSound(pMissile->x, pMissile->y, pMissile->z, becDefine("SFX_ORBEXPL", -1), pMissile->sectnum);
        }
        else if (nBoom == kBoomAxe)
            axeLands(pMissile);
        else
            openPortal(pMissile);
        actPostSprite(pMissile->index, kStatFree);
        return true;
    }
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
