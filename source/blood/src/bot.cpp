//-------------------------------------------------------------------------
/*
This file is part of NotBlood.

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

// Bots for offline bloodbath. Each bot only knows what a player in its place could know:
// what is in front of it and not behind a wall, what it hears (shots, footsteps, jumps)
// and what it remembers. It finds its way with the map's sectors (rooms joined by
// openings), decides every few frames what to do (fight, chase, ambush, flank, run away,
// camp, pick up items) and then "presses keys" through a normal GINPUT, with a human
// reaction time, limited turning speed and an aim that wobbles and settles.
// The bots never call the game's random number generators, so they don't change the game.

#include <math.h>

#include "build.h"
#include "compat.h"
#include "mmulti.h"
#include "osd.h"
#include "common_game.h"
#include "actor.h"
#include "blood.h"
#include "db.h"
#include "endgame.h"
#include "gamemenu.h"
#include "eventq.h"
#include "gameutil.h"
#include "globals.h"
#include "levels.h"
#include "network.h"
#include "player.h"
#include "sfx.h"
#include "trig.h"
#include "view.h"
#include "weapon.h"
#include "menuchosen.h"
#include "bot.h"

int gBotCount = 3;
int gBotSkill = kBotSkillNormal;
int gBotPersona = kBotPersonaMixed;
int gBotEachSkill[kBotMaxBots] = { -1, -1, -1, -1, -1, -1, -1 };
int gBotEachPersona[kBotMaxBots] = { -1, -1, -1, -1, -1, -1, -1 };
int gBotAccuracy = 100;
int gBotReaction = 100;
int gBotAggression = 100;
int gBotJumping = 100;
int gBotAmbush = 100;
int gBotFlank = 100;
int gBotCamping = 100;
int gBotTaunts = 1;
int gBotDebug = 0;
int gBotCultists = 1;
int gBotHandicap = 2;
static int gBotWatch = 0;

bool gBotMatch = false;

const char *gBotSkillNames[kBotSkillMax] = { "EASY", "NORMAL", "HARD", "NIGHTMARE" };
const char *gBotPersonaNames[kBotPersonaMax] = { "MIXED", "BALANCED", "RUSHER", "CAMPER", "TRICKSTER", "COWARD", "JUMPER" };

static const char *pzBotNames[] = {
    "Malachi", "Ezekiel", "Obadiah", "Mordecai", "Jedidiah", "Zebulon", "Thaddeus",
    "Barnabas", "Lazarus", "Silas", "Gideon", "Josiah", "Ishmael", "Jericho",
};

static const char *pzBotKillLines[] = {
    "Rest in pieces.", "Next!", "Too slow.", "Stay down.", "That's gotta hurt.", "Back to the grave with you.",
    "Is that all?", "You should see the other guy... oh wait.", "Burn.", "Another one for the pile.",
};

static const char *pzBotDeathLines[] = {
    "Lucky shot.", "I'll be back.", "Cheap!", "Not again...", "Hmph.", "Enjoy it while it lasts.",
};

enum {
    kBotStateRoam = 0,
    kBotStateCollect,
    kBotStateFight,
    kBotStateHunt,
    kBotStateAmbush,
    kBotStateFlank,
    kBotStateFlee,
    kBotStateCamp,
    kBotStateDead,
    kBotStateMax
};

static const char *pzBotStateNames[kBotStateMax] = { "roam", "collect", "fight", "hunt", "ambush", "flank", "flee", "camp", "dead" };

struct BOTSKILL
{
    int nAimError;  // build angle units (2048 = full turn)
    int nReaction;  // frames (30 per second) before reacting to a new enemy
    int nTurnSpeed; // most it can turn in a frame
    int nFov;       // half of its field of view
    int nHearing;   // how far it hears shots
    int nLead;      // percent of correct lead on projectiles
    int nTactics;   // chance of using its head (ambush, flank, retreat to health)
    int nTrigger;   // percent of the time it keeps shooting while on target (lower: pauses like a nervous player)
    int nAutoAim;   // the profile's autoaim (0 off, 2 hitscan weapons only, 1 on)
};

static const BOTSKILL gBotSkillInfo[kBotSkillMax] = {
    { 240, 26, 30, 240, 4500, 10, 15, 50, 0 },
    { 160, 17, 48, 290, 6500, 30, 35, 70, 0 },
    { 100, 12, 75, 350, 9500, 55, 60, 85, 2 },
    { 60, 8, 110, 410, 13000, 80, 85, 100, 1 },
};

struct BOTPERSONA
{
    int nAggression; // chase and push in instead of keeping distance
    int nCamping;    // stay in a good spot and wait
    int nAmbush;     // wait beside the doorway an enemy is about to come through
    int nFlank;      // take another way round to come from behind/the side
    int nJump;       // jump around in fights
    int nFlee;       // health below which it backs off
    int nStrafe;     // dodge sideways in fights
};

static const BOTPERSONA gBotPersonaInfo[kBotPersonaMax] = {
    { 55, 20, 45, 40, 35, 30, 60 }, // mixed (not used directly)
    { 55, 20, 45, 40, 35, 30, 60 }, // balanced
    { 95, 0, 10, 25, 50, 10, 50 },  // rusher
    { 30, 90, 75, 15, 10, 40, 30 }, // camper
    { 55, 25, 85, 90, 35, 35, 65 }, // trickster
    { 20, 50, 50, 30, 45, 65, 70 }, // coward
    { 70, 5, 20, 30, 95, 25, 90 },  // jumper
};

#define kBotMaxPath 96
#define kBotMaxBlocked 4
#define kBotMaxCampSpots 48
#define kBotMaxItemSpots 128

struct BOTMEMORY
{
    bool bValid;
    int x, y, z, nSector;
    int vx, vy;
    int nTime;   // when it was last known (gLevelTime)
    bool bSeen;  // seen (not just heard)
};

struct BOT
{
    bool bActive;
    int nPlayer;
    int nSkill, nPersona;
    BOTSKILL skill;
    BOTPERSONA persona;
    uint32_t nRand;

    int nState, nStateTime, nNextThink;
    int nGoalX, nGoalY, nGoalZ, nGoalSect, nGoalSprite;
    short path[kBotMaxPath];
    int nPathLen, nPathPos, nPathGoalSect, nRepathTime;
    int nWaitUntil, nWatchAng;

    int nTarget;          // sprite index, -1 none
    int nTargetSince;     // when it first saw the current target
    int nReactAt;         // can shoot/turn to it from then on
    int nAimErr, nAimErrGoal, nAimErrNext, nTrackFrames, nAimErrZ, nFirePauseUntil;
    int nStrafeDir, nStrafeUntil, nJumpNext, nCrouchUntil;
    int nWeaponNext;
    int nFireHold, nFireRelease;

    int nLastHealth, nHurtTime;
    BOTMEMORY mem[kMaxPlayers];

    int nStuckX, nStuckY, nStuckCheck, nStuckCount, nUnstickUntil, nUnstickStrafe;
    int blockedWall[kBotMaxBlocked], blockedUntil[kBotMaxBlocked];
    int nUseTime;
    int nRespawnDelay;
    int nLastFrags, nLastDeaths, nTalkAt;
    const char *pzTalk;
    int nSearchUntil;
    bool bWasDead;
    int ignoreSprite[4], ignoreUntil[4]; // items it couldn't pick up
    int nAtGoalSince;
    int nProgressDist, nProgressTime, nProgressX, nProgressY; // getting closer to where it's heading?
    int nAvoidSide, nAvoidUntil;
    int nCampSpot, nLastCamp;
    int nGlance, nGlanceNext;
};

static BOT gBot[kMaxPlayers];

static int nCampSpots;
static vec3_t campSpot[kBotMaxCampSpots];
static short campSect[kBotMaxCampSpots];
static short campAng[kBotMaxCampSpots];
static uint8_t nCampFails[kBotMaxCampSpots];
static int nItemSpots;
static vec2_t itemSpot[kBotMaxItemSpots];
static short itemSpotSect[kBotMaxItemSpots];

static vec2_t sectCenter[MAXSECTORS];
static int nSectPenalty[MAXSECTORS]; // extra path cost in percent, for flanking and running away

static int nNow; // gLevelTime of the frame being thought about

static void BotDescribe(int i, char *pzBuffer, int nSize);

//
// small helpers
//

static inline int BotRand(BOT *pBot, int nRange)
{
    // xorshift; deliberately not the game's own random numbers
    uint32_t x = pBot->nRand;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pBot->nRand = x;
    return nRange > 0 ? int((x >> 8) % (uint32_t)nRange) : 0;
}

static inline bool BotChance(BOT *pBot, int nPercent)
{
    return BotRand(pBot, 100) < nPercent;
}

static inline int AngDiff(int a, int b) // b-a in -1024..1023
{
    return ((b - a + 1024) & 2047) - 1024;
}

static inline int BotDist(int x1, int y1, int x2, int y2)
{
    return approxDist(x2 - x1, y2 - y1);
}

static int SpriteCenterZ(spritetype *pSprite)
{
    int top, bottom;
    GetSpriteExtents(pSprite, &top, &bottom);
    return (top + bottom) >> 1;
}

static bool IsEnemyPlayer(PLAYER *pMe, PLAYER *pOther)
{
    if (pMe == pOther || !pOther->pSprite || !pOther->pXSprite)
        return false;
    if (gGameOptions.nGameType == kGameTypeCoop)
        return false;
    if (gGameOptions.nGameType == kGameTypeTeams && pMe->teamId == pOther->teamId)
        return false;
    return true;
}

static PLAYER *SpritePlayer(int nSprite)
{
    if (nSprite < 0)
        return NULL;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (gPlayer[p].nSprite == nSprite)
            return &gPlayer[p];
    return NULL;
}

static bool SpriteAlive(int nSprite)
{
    if (nSprite < 0 || sprite[nSprite].statnum >= kMaxStatus || sprite[nSprite].extra <= 0)
        return false;
    if (sprite[nSprite].statnum != kStatDude)
        return false;
    return xsprite[sprite[nSprite].extra].health > 0;
}

//
// map knowledge: sectors as rooms joined by openings
//

static int PortalGap(int nWall, int x, int y)
{
    const int s1 = sectorofwall(nWall), s2 = wall[nWall].nextsector;
    int c1, f1, c2, f2;
    getzsofslope(s1, x, y, &c1, &f1);
    getzsofslope(s2, x, y, &c2, &f2);
    return min(f1, f2) - max(c1, c2);
}

static uint8_t bDoorSect[MAXSECTORS]; // doors a player can open by pressing "use" (on them or a switch beside them)

static bool IsMovingSectorType(int nType)
{
    return nType == kSectorZMotion || nType == kSectorSlide || nType == kSectorRotate || nType == kSectorSlideMarked || nType == kSectorRotateMarked
        || nType == kSectorZMotionSprite || nType == kSectorRotateStep;
}

// where the buttons are: for every channel the sectors that have something you can push sending to it
#define kMaxOpeners 8
static short nOpenerSect[1024][kMaxOpeners];
static uint8_t nOpeners[1024];

static void AddOpener(int nChannel, int nSector)
{
    nChannel &= 1023;
    for (int i = 0; i < nOpeners[nChannel]; i++)
        if (nOpenerSect[nChannel][i] == nSector)
            return;
    if (nOpeners[nChannel] < kMaxOpeners)
        nOpenerSect[nChannel][nOpeners[nChannel]++] = nSector;
}

// can someone standing in nFrom open door sector nDoor?
static bool CanOpenFrom(int nDoor, int nFrom)
{
    XSECTOR *pXSector = &xsector[sector[nDoor].extra];
    if (pXSector->Push || pXSector->Wallpush)
        return true;
    const int nChannel = pXSector->rxID & 1023;
    for (int i = 0; i < nOpeners[nChannel]; i++)
        if (nOpenerSect[nChannel][i] == nFrom || nOpenerSect[nChannel][i] == nDoor)
            return true;
    return false;
}

static void FindDoors(void)
{
    Bmemset(nOpeners, 0, sizeof(nOpeners));
    uint8_t bPushChannel[1024];
    Bmemset(bPushChannel, 0, sizeof(bPushChannel));
    for (int w = 0; w < numwalls; w++)
    {
        if (wall[w].extra > 0 && xwall[wall[w].extra].triggerPush && xwall[wall[w].extra].txID)
        {
            bPushChannel[xwall[wall[w].extra].txID & 1023] = 1;
            AddOpener(xwall[wall[w].extra].txID, sectorofwall(w));
        }
    }
    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum < kMaxStatus && sprite[i].extra > 0 && xsprite[sprite[i].extra].Push && xsprite[sprite[i].extra].txID)
        {
            bPushChannel[xsprite[sprite[i].extra].txID & 1023] = 1;
            AddOpener(xsprite[sprite[i].extra].txID, sprite[i].sectnum);
        }
    }
    for (int s = 0; s < numsectors; s++)
    {
        bDoorSect[s] = 0;
        if (sector[s].extra <= 0 || !IsMovingSectorType(sector[s].type))
            continue;
        XSECTOR *pXSector = &xsector[sector[s].extra];
        if (pXSector->locked || pXSector->Key)
            continue;
        bDoorSect[s] = pXSector->Push || pXSector->Wallpush || (pXSector->rxID && bPushChannel[pXSector->rxID & 1023]);
    }
}

static bool IsDoorSector(int nSector)
{
    return nSector >= 0 && nSector < numsectors && bDoorSect[nSector];
}

static bool IsLift(int nSector) // a moving sector whose floor goes up and down
{
    if (nSector < 0 || sector[nSector].extra <= 0 || !IsMovingSectorType(sector[nSector].type))
        return false;
    XSECTOR *pXSector = &xsector[sector[nSector].extra];
    return pXSector->onFloorZ != pXSector->offFloorZ;
}

static bool DoorClosed(int nSector) // rotating and sliding doors don't change height: closed is their "off" state
{
    if (!IsDoorSector(nSector))
        return false;
    const int nType = sector[nSector].type;
    if (nType != kSectorSlide && nType != kSectorRotate && nType != kSectorSlideMarked && nType != kSectorRotateMarked && nType != kSectorRotateStep)
        return false;
    XSECTOR *pXSector = &xsector[sector[nSector].extra];
    return !pXSector->state && !pXSector->busy;
}

static bool IsHurtSector(int nSector)
{
    if (sector[nSector].extra <= 0)
        return false;
    XSECTOR *pXSector = &xsector[sector[nSector].extra];
    return pXSector->damageType > 0 && sector[nSector].type == kSectorDamage;
}

enum {
    kEdgeWalk = 0,
    kEdgeJump = 1,
    kEdgeDoor = 2,
    kEdgeBlocked = 3,
};

static int EdgeType(int nWall)
{
    const int nNext = wall[nWall].nextsector;
    if (nNext < 0)
        return kEdgeBlocked;
    const int nFrom = sectorofwall(nWall);
    // a door this side can open (some only open with a button on the other side)
    const bool bDoor = (IsDoorSector(nNext) && CanOpenFrom(nNext, nFrom)) || (IsDoorSector(nFrom) && CanOpenFrom(nFrom, nFrom));
    if (wall[nWall].cstat & 1) // blocking wall
        return bDoor ? kEdgeDoor : kEdgeBlocked;
    if (DoorClosed(nNext) || DoorClosed(nFrom))
        return bDoor ? kEdgeDoor : kEdgeBlocked;
    const int x = (wall[nWall].x + wall[wall[nWall].point2].x) >> 1;
    const int y = (wall[nWall].y + wall[wall[nWall].point2].y) >> 1;
    if (approxDist(wall[nWall].x - wall[wall[nWall].point2].x, wall[nWall].y - wall[wall[nWall].point2].y) < 48)
        return kEdgeBlocked; // too narrow to walk through
    const int nGap = PortalGap(nWall, x, y);
    const int nRiseNow = getflorzofslope(nFrom, x, y) - getflorzofslope(nNext, x, y);
    if ((IsLift(nNext) || IsLift(nFrom)) && nRiseNow > (54<<8))
        return kEdgeBlocked; // a lift that is up (or down): bots don't ride lifts yet
    if (nGap < (28<<8))
        return bDoor ? kEdgeDoor : kEdgeBlocked;
    const int nRise = getflorzofslope(nFrom, x, y) - getflorzofslope(nNext, x, y);
    if (nRise > (54<<8) && !(sector[nNext].extra > 0 && xsector[sector[nNext].extra].Underwater))
        return kEdgeBlocked;
    if (nRise > (18<<8))
        return kEdgeJump;
    return kEdgeWalk;
}

static void PortalPoint(int nWall, int tx, int ty, int *px, int *py)
{
    // the point of the opening closest to (tx,ty), kept away from its edges
    const int x1 = wall[nWall].x, y1 = wall[nWall].y;
    const int x2 = wall[wall[nWall].point2].x, y2 = wall[wall[nWall].point2].y;
    const float dx = float(x2 - x1), dy = float(y2 - y1);
    const float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.f)
    {
        *px = x1, *py = y1;
        return;
    }
    const float margin = min(len * 0.35f, 220.f) / len;
    float t = ((tx - x1) * dx + (ty - y1) * dy) / (len * len);
    t = clamp(t, margin, 1.f - margin);
    *px = x1 + int(dx * t);
    *py = y1 + int(dy * t);
}

static uint8_t nWallFails[kMaxWalls]; // openings bots got stuck at, this level
static uint8_t nItemFails[kMaxSprites]; // items bots couldn't get to (secret lifts and the like)

// An opening it couldn't get through: avoid it for a while; when several times, for good.
static void BlockWall(BOT *pBot, int nWall)
{
    int nSlot = 0;
    for (int i = 1; i < kBotMaxBlocked; i++)
        if (pBot->blockedUntil[i] < pBot->blockedUntil[nSlot])
            nSlot = i;
    pBot->blockedWall[nSlot] = nWall;
    pBot->blockedUntil[nSlot] = nNow + 30*60;
    if (nWallFails[nWall] < 255)
        nWallFails[nWall]++;
    if (gBotDebug == 2)
    {
        const int s1 = sectorofwall(nWall), s2 = wall[nWall].nextsector;
        LOG_F(INFO, "Bot %s stuck at opening %d (%d times): sector %d floor %d ceiling %d -> sector %d floor %d ceiling %d, cstat %d/%d, width %d",
            gProfile[pBot->nPlayer].name, nWall, nWallFails[nWall], s1, sector[s1].floorz, sector[s1].ceilingz, s2, s2 >= 0 ? sector[s2].floorz : 0, s2 >= 0 ? sector[s2].ceilingz : 0,
            wall[nWall].cstat, wall[nWall].nextwall >= 0 ? wall[wall[nWall].nextwall].cstat : -1,
            approxDist(wall[nWall].x - wall[wall[nWall].point2].x, wall[nWall].y - wall[wall[nWall].point2].y));
    }
}

enum { kDoorOpen, kDoorClosed, kDoorMoving };

static int DoorState(int nWall)
{
    const int nSectors[2] = { wall[nWall].nextsector, sectorofwall(nWall) };
    bool bAnyDoor = false;
    for (int s : nSectors)
    {
        if (!IsDoorSector(s))
            continue;
        bAnyDoor = true;
        XSECTOR *pXSector = &xsector[sector[s].extra];
        if (pXSector->busy)
            return kDoorMoving;
        if (!pXSector->state)
            return kDoorClosed;
    }
    if (!bAnyDoor)
        return kDoorClosed;
    return PortalGap(nWall, wall[nWall].x, wall[nWall].y) < (28<<8) ? kDoorClosed : kDoorOpen;
}

// What "use" would do right now: 1 push something, 0 nothing, -1 it's a level exit (never press).
static int UseCheck(PLAYER *pPlayer)
{
    spritetype *pSprite = pPlayer->pSprite;
    const int hit = HitScan(pSprite, pPlayer->zView, Cos(pSprite->ang)>>16, Sin(pSprite->ang)>>16, pPlayer->slope, 0x10000040, 128);
    if ((approxDist(pSprite->x - gHitInfo.hitx, pSprite->y - gHitInfo.hity) >> 4) >= 64)
        return 0;
    int nTx = 0, nResult = 0;
    switch (hit)
    {
    case 3:
        if (sprite[gHitInfo.hitsprite].extra > 0 && sprite[gHitInfo.hitsprite].statnum != kStatDude)
        {
            XSPRITE *pXSprite = &xsprite[sprite[gHitInfo.hitsprite].extra];
            nResult = pXSprite->Push, nTx = pXSprite->txID;
        }
        break;
    case 0:
    case 4:
        if (wall[gHitInfo.hitwall].extra > 0 && xwall[wall[gHitInfo.hitwall].extra].triggerPush)
            nResult = 1, nTx = xwall[wall[gHitInfo.hitwall].extra].txID;
        else if (wall[gHitInfo.hitwall].nextsector >= 0 && sector[wall[gHitInfo.hitwall].nextsector].extra > 0)
        {
            XSECTOR *pXSector = &xsector[sector[wall[gHitInfo.hitwall].nextsector].extra];
            nResult = pXSector->Wallpush, nTx = pXSector->txID;
        }
        break;
    case 1:
    case 2:
        if (gHitInfo.hitsect >= 0 && sector[gHitInfo.hitsect].extra > 0)
        {
            XSECTOR *pXSector = &xsector[sector[gHitInfo.hitsect].extra];
            nResult = pXSector->Push, nTx = pXSector->txID;
        }
        break;
    }
    if (nResult && (nTx == kChannelLevelExitNormal || nTx == kChannelLevelExitSecret))
        return -1;
    return nResult ? 1 : 0;
}

static bool WallBlocked(BOT *pBot, int nWall)
{
    if (nWallFails[nWall] >= 3)
        return true;
    if (!pBot)
        return false;
    for (int i = 0; i < kBotMaxBlocked; i++)
        if (pBot->blockedWall[i] == nWall && pBot->blockedUntil[i] > nNow)
            return true;
    return false;
}
// A* over sectors
static int pathCost[MAXSECTORS];
static short pathFrom[MAXSECTORS];  // wall used to enter the sector
static vec2_t pathEntry[MAXSECTORS];
static uint8_t pathState[MAXSECTORS]; // 0 new, 1 open, 2 closed
struct PATHNODE { int nKey; short nSector; };
#define kPathHeapMax (MAXSECTORS*4)
static PATHNODE pathHeap[kPathHeapMax];
static int pathHeapSize;
static short pathChain[MAXSECTORS];

static void HeapPush(int nSector, int nKey)
{
    if (pathHeapSize >= kPathHeapMax)
        return;
    int i = pathHeapSize++;
    while (i > 0)
    {
        const int nParent = (i - 1) >> 1;
        if (pathHeap[nParent].nKey <= nKey)
            break;
        pathHeap[i] = pathHeap[nParent];
        i = nParent;
    }
    pathHeap[i].nKey = nKey;
    pathHeap[i].nSector = nSector;
}

static int HeapPop(void)
{
    const int nTop = pathHeap[0].nSector;
    const PATHNODE last = pathHeap[--pathHeapSize];
    int i = 0;
    for (;;)
    {
        int nChild = i*2 + 1;
        if (nChild >= pathHeapSize)
            break;
        if (nChild + 1 < pathHeapSize && pathHeap[nChild + 1].nKey < pathHeap[nChild].nKey)
            nChild++;
        if (pathHeap[nChild].nKey >= last.nKey)
            break;
        pathHeap[i] = pathHeap[nChild];
        i = nChild;
    }
    if (pathHeapSize > 0)
        pathHeap[i] = last;
    return nTop;
}

// Finds a way from (sx,sy) in nStart to (gx,gy) in nGoal. Fills pOut with the walls (openings)
// to go through and returns how many, or -1 if there is no way. pBot may be NULL.
static int PathSearch(BOT *pBot, int nStart, int sx, int sy, int nGoal, int gx, int gy, short *pOut, int nMaxOut, int *pCost)
{
    if (nStart < 0 || nGoal < 0 || nStart >= numsectors || nGoal >= numsectors)
        return -1;
    if (pCost)
        *pCost = 0;
    if (nStart == nGoal)
        return 0;
    Bmemset(pathState, 0, numsectors);
    pathHeapSize = 0;
    pathCost[nStart] = 0;
    pathFrom[nStart] = -1;
    pathEntry[nStart].x = sx;
    pathEntry[nStart].y = sy;
    pathState[nStart] = 1;
    HeapPush(nStart, BotDist(sx, sy, gx, gy));
    int nExpanded = 0;
    bool bFound = false;
    while (pathHeapSize > 0 && nExpanded < 4096)
    {
        const int s = HeapPop();
        if (pathState[s] == 2)
            continue;
        pathState[s] = 2;
        nExpanded++;
        if (s == nGoal)
        {
            bFound = true;
            break;
        }
        const int nStartWall = sector[s].wallptr, nEndWall = nStartWall + sector[s].wallnum;
        for (int w = nStartWall; w < nEndWall; w++)
        {
            const int n = wall[w].nextsector;
            if (n < 0 || pathState[n] == 2)
                continue;
            const int nEdge = EdgeType(w);
            if (nEdge == kEdgeBlocked || WallBlocked(pBot, w))
                continue;
            int px, py;
            PortalPoint(w, pathEntry[s].x, pathEntry[s].y, &px, &py);
            int nCost = BotDist(pathEntry[s].x, pathEntry[s].y, px, py) + 64;
            if (nEdge == kEdgeJump)
                nCost += 400;
            else if (nEdge == kEdgeDoor)
                nCost += 600;
            if (IsHurtSector(n))
                nCost += 6000;
            if (nSectPenalty[n])
                nCost += nCost * nSectPenalty[n] / 100 + nSectPenalty[n] * 8;
            nCost += pathCost[s];
            if (pathState[n] == 0 || nCost < pathCost[n])
            {
                pathCost[n] = nCost;
                pathFrom[n] = w;
                pathEntry[n].x = px;
                pathEntry[n].y = py;
                pathState[n] = 1;
                HeapPush(n, nCost + BotDist(px, py, gx, gy)); // stale entries are skipped when popped
            }
        }
    }
    if (!bFound)
        return -1;
    if (pCost)
        *pCost = pathCost[nGoal] + BotDist(pathEntry[nGoal].x, pathEntry[nGoal].y, gx, gy);
    int n = 0;
    for (int s = nGoal; s != nStart && n < MAXSECTORS; )
    {
        const int w = pathFrom[s];
        if (w < 0)
            break;
        pathChain[n++] = w;
        s = sectorofwall(w);
    }
    const int nOut = min(n, nMaxOut); // a very long way: the first part of it, it plans again later
    for (int i = 0; i < nOut; i++)
        pOut[i] = pathChain[n - 1 - i];
    return nOut;
}

static bool BotFindPath(BOT *pBot, int nGoalSect, int gx, int gy)
{
    spritetype *pSprite = gPlayer[pBot->nPlayer].pSprite;
    pBot->nPathLen = pBot->nPathPos = 0;
    pBot->nPathGoalSect = nGoalSect;
    pBot->nRepathTime = nNow;
    const int nLen = PathSearch(pBot, pSprite->sectnum, pSprite->x, pSprite->y, nGoalSect, gx, gy, pBot->path, kBotMaxPath, NULL);
    if (nLen < 0)
        return false;
    pBot->nPathLen = nLen;
    return true;
}

static void BotClearPenalties(void)
{
    Bmemset(nSectPenalty, 0, sizeof(nSectPenalty[0]) * numsectors);
}

//
// level setup
//

static void BotFindSpots(void)
{
    nCampSpots = 0;
    nItemSpots = 0;
    Bmemset(nWallFails, 0, sizeof(nWallFails));
    Bmemset(nItemFails, 0, sizeof(nItemFails));
    Bmemset(nCampFails, 0, sizeof(nCampFails));
    FindDoors();
    for (int s = 0; s < numsectors; s++)
    {
        int x = 0, y = 0;
        const int nWalls = sector[s].wallnum;
        for (int w = sector[s].wallptr; w < sector[s].wallptr + nWalls; w++)
            x += wall[w].x, y += wall[w].y;
        sectCenter[s].x = nWalls ? x / nWalls : 0;
        sectCenter[s].y = nWalls ? y / nWalls : 0;
    }
    for (int i = headspritestat[kStatItem]; i >= 0 && nItemSpots < kBotMaxItemSpots; i = nextspritestat[i])
    {
        itemSpot[nItemSpots].x = sprite[i].x;
        itemSpot[nItemSpots].y = sprite[i].y;
        itemSpotSect[nItemSpots] = sprite[i].sectnum;
        nItemSpots++;
    }
    // camping spots: inner corners of rooms with at least two ways in, watching the openings
    uint32_t nRand = 0x9e3779b9u ^ (uint32_t)numsectors;
    for (int nTry = 0; nTry < numsectors * 2 && nCampSpots < kBotMaxCampSpots; nTry++)
    {
        nRand ^= nRand << 13, nRand ^= nRand >> 17, nRand ^= nRand << 5;
        const int s = nRand % (uint32_t)numsectors;
        if (sector[s].wallnum < 4 || IsHurtSector(s))
            continue;
        if (sector[s].extra > 0 && xsector[sector[s].extra].Underwater)
            continue;
        int nPortals = 0;
        for (int w = sector[s].wallptr; w < sector[s].wallptr + sector[s].wallnum; w++)
            if (EdgeType(w) != kEdgeBlocked)
                nPortals++;
        if (nPortals < 2)
            continue;
        const int w = sector[s].wallptr + (nRand >> 8) % (uint32_t)sector[s].wallnum;
        // step from the corner towards the room's middle
        const int cx = wall[w].x, cy = wall[w].y;
        const int nAng = getangle(sectCenter[s].x - cx, sectCenter[s].y - cy);
        const int x = cx + mulscale30(Cos(nAng), 220), y = cy + mulscale30(Sin(nAng), 220);
        if (!inside(x, y, s))
            continue;
        const int z = getflorzofslope(s, x, y);
        if (getceilzofslope(s, x, y) > z - (56<<8))
            continue;
        bool bTooClose = false;
        for (int i = 0; i < nCampSpots && !bTooClose; i++)
            bTooClose = BotDist(campSpot[i].x, campSpot[i].y, x, y) < 1500;
        if (bTooClose)
            continue;
        campSpot[nCampSpots].x = x;
        campSpot[nCampSpots].y = y;
        campSpot[nCampSpots].z = z;
        campSect[nCampSpots] = s;
        campAng[nCampSpots] = nAng;
        nCampSpots++;
    }
}

// "mixed": the personalities in a random order, so the bots of a match all differ
static int nMixedOrder[kBotMaxBots] = { kBotPersonaBalanced, kBotPersonaRusher, kBotPersonaCamper, kBotPersonaTrickster, kBotPersonaCoward, kBotPersonaJumper, kBotPersonaBalanced };

static void ShuffleMixed(uint32_t nSeed)
{
    static const int nAll[kBotPersonaMax - 1] = { kBotPersonaBalanced, kBotPersonaRusher, kBotPersonaCamper, kBotPersonaTrickster, kBotPersonaCoward, kBotPersonaJumper };
    int nOrder[ARRAY_SSIZE(nAll)];
    for (int i = 0; i < ARRAY_SSIZE(nAll); i++)
        nOrder[i] = nAll[i];
    for (int i = ARRAY_SSIZE(nAll) - 1; i > 0; i--)
    {
        nSeed = nSeed * 1103515245u + 12345u;
        const int j = (nSeed >> 16) % (i + 1);
        swap(&nOrder[i], &nOrder[j]);
    }
    for (int i = 0; i < kBotMaxBots; i++)
        nMixedOrder[i] = nOrder[i % ARRAY_SSIZE(nAll)];
}

static void BotResolveSettings(BOT *pBot, int nBot)
{
    int nSkill = gBotEachSkill[nBot] >= 0 ? gBotEachSkill[nBot] : gBotSkill;
    int nPersona = gBotEachPersona[nBot] >= 0 ? gBotEachPersona[nBot] : gBotPersona;
    nSkill = ClipRange(nSkill, 0, kBotSkillMax - 1);
    nPersona = ClipRange(nPersona, 0, kBotPersonaMax - 1);
    if (nPersona == kBotPersonaMixed)
        nPersona = nMixedOrder[nBot % ARRAY_SSIZE(nMixedOrder)];
    pBot->nSkill = nSkill;
    pBot->nPersona = nPersona;
    pBot->skill = gBotSkillInfo[nSkill];
    pBot->persona = gBotPersonaInfo[nPersona];
    BOTSKILL *pSkill = &pBot->skill;
    pSkill->nAimError = pSkill->nAimError * 100 / ClipRange(gBotAccuracy, 10, 300);
    pSkill->nReaction = pSkill->nReaction * ClipRange(gBotReaction, 10, 300) / 100;
    BOTPERSONA *pPersona = &pBot->persona;
    pPersona->nAggression = ClipRange(pPersona->nAggression * gBotAggression / 100, 0, 100);
    pPersona->nJump = ClipRange(pPersona->nJump * gBotJumping / 100, 0, 100);
    pPersona->nAmbush = ClipRange(pPersona->nAmbush * gBotAmbush / 100, 0, 100);
    pPersona->nFlank = ClipRange(pPersona->nFlank * gBotFlank / 100, 0, 100);
    pPersona->nCamping = ClipRange(pPersona->nCamping * gBotCamping / 100, 0, 100);
}

bool botIsBot(int nPlayer)
{
    return gBotMatch && nPlayer >= 0 && nPlayer < kMaxPlayers && gBot[nPlayer].bActive;
}

const char *botStateName(int nPlayer)
{
    if (!botIsBot(nPlayer))
        return "";
    return pzBotStateNames[gBot[nPlayer].nState];
}

void botSetupMatch(void)
{
    const int nBots = ClipRange(gBotCount, 1, kMaxPlayers - 1);
    gBotMatch = true;
    myconnectindex = connecthead = 0;
    numplayers = 1; // still a local game as far as the network code is concerned
    gNetPlayers = gInitialNetPlayers = nBots + 1;
    for (int i = 0; i < kMaxPlayers; i++)
        connectpoint2[i] = (i + 1 < gNetPlayers) ? i + 1 : -1;
    netBroadcastPlayerInfo(myconnectindex);
    gProfileNet[myconnectindex].skill = gProfile[myconnectindex].skill = gSkill;
    uint32_t nSeed = ((uint32_t)totalclock + (uint32_t)time(NULL)) * 2654435761u + 12345u;
    int nNameStart = (nSeed >> 8) % ARRAY_SSIZE(pzBotNames);
    ShuffleMixed(nSeed);
    for (int i = 0; i < kMaxPlayers; i++)
    {
        BOT *pBot = &gBot[i];
        Bmemset(pBot, 0, sizeof(BOT));
        if (i == myconnectindex || i >= gNetPlayers)
            continue;
        const int nBot = i - 1;
        pBot->bActive = true;
        pBot->nPlayer = i;
        pBot->nRand = nSeed ^ (0x51ed270bu * (i + 1));
        if (!pBot->nRand)
            pBot->nRand = 1;
        BotResolveSettings(pBot, nBot);
        PROFILE *pProfile = &gProfileNet[i];
        Bmemset(pProfile, 0, sizeof(PROFILE));
        Bstrncpyz(pProfile->name, pzBotNames[(nNameStart + nBot) % ARRAY_SSIZE(pzBotNames)], sizeof(pProfile->name));
        pProfile->nAutoAim = pBot->skill.nAutoAim;
        pProfile->nWeaponSwitch = 1;
        pProfile->bWeaponFastSwitch = 0;
        pProfile->skill = ClipRange(gBotHandicap, 0, 4);
        pProfile->nTeamPreference = 0;
        pProfile->nColorPreference = 0;
        pProfile->nModel = gBotCultists ? (nBot & 1) : 0;
        pProfile->nWeaponHBobbing = 1;
        gProfile[i] = *pProfile;
        LOG_F(INFO, "Bot %d: %s, %s, %s", nBot + 1, pProfile->name, gBotSkillNames[pBot->nSkill], gBotPersonaNames[pBot->nPersona]);
    }
    gGameOptions.nGameType = gPacketStartGame.gameType;
    gGameOptions.uGameFlags &= ~(kGameFlagContinuing|kGameFlagEnding);
    gPlayerRoundEnding = 0;
}

void botEndMatch(void)
{
    if (!gBotMatch)
        return;
    gBotMatch = false;
    for (int i = 0; i < kMaxPlayers; i++)
        gBot[i].bActive = false;
    netResetToSinglePlayer();
}

static int osdcmd_bot_sector(osdcmdptr_t parm);

void botLevelStart(void)
{
    if (!gBotMatch)
        return;
    BotFindSpots();
    if (const char *pzTx = getenv("NOTBLOOD_BOT_TX")) // testing: who sends to this channel
    {
        const int nTx = atoi(pzTx);
        for (int w = 0; w < numwalls; w++)
            if (wall[w].extra > 0 && xwall[wall[w].extra].txID == nTx)
                LOG_F(INFO, "tx %d: wall %d of sector %d push %d", nTx, w, sectorofwall(w), xwall[wall[w].extra].triggerPush);
        for (int s = 0; s < numsectors; s++)
            if (sector[s].extra > 0 && xsector[sector[s].extra].txID == nTx)
                LOG_F(INFO, "tx %d: sector %d type %d enter %d push %d", nTx, s, sector[s].type, xsector[sector[s].extra].Enter, xsector[sector[s].extra].Push);
        for (int i = 0; i < kMaxSprites; i++)
            if (sprite[i].statnum < kMaxStatus && sprite[i].extra > 0 && xsprite[sprite[i].extra].txID == nTx)
                LOG_F(INFO, "tx %d: sprite %d type %d sector %d push %d", nTx, i, sprite[i].type, sprite[i].sectnum, xsprite[sprite[i].extra].Push);
    }
    if (const char *pzDump = getenv("NOTBLOOD_BOT_SECTORS")) // testing: dump these sectors (comma separated)
    {
        char buffer[256];
        Bstrncpyz(buffer, pzDump, sizeof(buffer));
        for (char *pzTok = strtok(buffer, ","); pzTok; pzTok = strtok(NULL, ","))
        {
            const char *parms[1] = { pzTok };
            osdfuncparm_t parm = { 1, "bot_sector", parms, "" };
            osdcmd_bot_sector(&parm);
        }
    }
    BotClearPenalties();
    for (int i = 0; i < kMaxPlayers; i++)
    {
        BOT *pBot = &gBot[i];
        if (!pBot->bActive)
            continue;
        pBot->nState = kBotStateRoam;
        pBot->nStateTime = 0;
        pBot->nNextThink = 0;
        pBot->nTarget = -1;
        pBot->nPathLen = pBot->nPathPos = 0;
        pBot->nGoalSect = -1;
        pBot->nGoalSprite = -1;
        pBot->nStuckCheck = 0;
        pBot->nStuckCount = 0;
        pBot->nLastHealth = gPlayer[i].pXSprite ? gPlayer[i].pXSprite->health : 0;
        pBot->nLastFrags = gPlayer[i].fragCount;
        pBot->nLastDeaths = 0;
        pBot->nTalkAt = 0;
        pBot->pzTalk = NULL;
        pBot->nWeaponNext = 0;
        pBot->nCampSpot = -1;
        pBot->nLastCamp = 0;
        pBot->nFirePauseUntil = 0;
        pBot->nAimErrZ = 0;
        pBot->nFireHold = pBot->nFireRelease = 0;
        pBot->bWasDead = false;
        for (int j = 0; j < kBotMaxBlocked; j++)
            pBot->blockedUntil[j] = 0;
        for (int j = 0; j < 4; j++)
            pBot->ignoreUntil[j] = 0;
        pBot->nAtGoalSince = 0;
        Bmemset(pBot->mem, 0, sizeof(pBot->mem));
    }
}

//
// weapons
//

struct BOTWEAPON
{
    int nPower;
    int nMinDist, nBestDist, nMaxDist;
    int nCone;      // fire when the aim is this close (angle units)
    int nLeadSpeed; // like gWeaponTrack's predict value, 0 for hitscan
};

static const BOTWEAPON gBotWeapon[kWeaponMax] = {
    { 0, 0, 0, 0, 0, 0 },                     // none
    { 12, 0, 0, 900, 140, 0 },                // pitchfork
    { 45, 300, 3000, 12000, 70, 0x2aaaaa },   // flare gun
    { 75, 0, 1100, 4000, 60, 0 },             // shotgun
    { 65, 0, 2500, 10000, 50, 0 },            // tommy gun
    { 85, 1400, 4500, 14000, 70, 0x2aaaaa },  // napalm
    { 70, 1400, 3200, 6000, 140, 0 },         // TNT
    { 55, 0, 700, 1700, 140, 0 },             // spray can
    { 85, 0, 3000, 10000, 55, 0x255555 },     // tesla
    { 80, 600, 3500, 12000, 70, 0x2aaaaa },   // life leech
    { 55, 0, 3000, 25000, 400, 0 },           // voodoo doll
    { 0, 0, 0, 0, 0, 0 },                     // proximity TNT
    { 0, 0, 0, 0, 0, 0 },                     // remote TNT
    { 90, 0, 0, 900, 140, 0 },                // beast hands
};

static int WeaponScore(PLAYER *pPlayer, int nWeapon, int nDist)
{
    const BOTWEAPON *pInfo = &gBotWeapon[nWeapon];
    if (!pInfo->nPower)
        return 0;
    if (nWeapon == kWeaponBeast)
        return pPlayer->curWeapon == kWeaponBeast ? 1000 : 0;
    if (!WeaponIsEquipable(pPlayer, nWeapon))
        return 0;
    int nScore = pInfo->nPower * 10;
    if (nDist < pInfo->nMinDist)
        nScore /= 8; // it would hurt itself
    else if (nDist > pInfo->nMaxDist)
        nScore /= 4;
    else
    {
        const int nOff = klabs(nDist - pInfo->nBestDist);
        nScore -= nScore * ClipHigh(nOff, 6000) / 20000;
    }
    if (powerupCheck(pPlayer, kPwUpTwoGuns) && (nWeapon == kWeaponFlare || nWeapon == kWeaponShotgun || nWeapon == kWeaponTommy || nWeapon == kWeaponNapalm || nWeapon == kWeaponTesla))
        nScore += nScore / 3;
    return nScore;
}

static int BestWeapon(PLAYER *pPlayer, int nDist, int *pScore)
{
    int nBest = 0, nBestScore = 0;
    for (int w = kWeaponPitchfork; w < kWeaponMax; w++)
    {
        const int nScore = WeaponScore(pPlayer, w, nDist);
        if (nScore > nBestScore)
            nBest = w, nBestScore = nScore;
    }
    if (pScore)
        *pScore = nBestScore;
    return nBest;
}

static int PlayerPower(PLAYER *pPlayer) // a rough idea of how dangerous someone is, as a player could judge it
{
    int nPower = gBotWeapon[ClipRange(pPlayer->curWeapon, 0, kWeaponMax-1)].nPower;
    if (powerupCheck(pPlayer, kPwUpTwoGuns))
        nPower += nPower / 2;
    if (powerupCheck(pPlayer, kPwUpDeathMask))
        nPower += 100;
    return ClipLow(nPower, 10);
}

//
// items worth walking to
//

static int ItemWeapon(int nType)
{
    switch (nType)
    {
    case kItemWeaponSawedoff: case kItemAmmoSawedoffFew: case kItemAmmoSawedoffBox: return kWeaponShotgun;
    case kItemWeaponTommygun: case kItemAmmoTommygunFew: case kItemAmmoTommygunDrum: return kWeaponTommy;
    case kItemWeaponFlarePistol: case kItemAmmoFlares: return kWeaponFlare;
    case kItemWeaponVoodooDoll: case kItemAmmoVoodooDoll: return kWeaponVoodoo;
    case kItemWeaponTeslaCannon: case kItemAmmoTeslaCharge: return kWeaponTesla;
    case kItemWeaponNapalmLauncher: case kItemAmmoGasolineCan: return kWeaponNapalm;
    case kItemWeaponSprayCan: case kItemAmmoSprayCan: return kWeaponSprayCan;
    case kItemWeaponTNT: case kItemAmmoTNTBundle: case kItemAmmoTNTBox: return kWeaponTNT;
    case kItemWeaponLifeLeech: case kItemAmmoTrappedSoul: return kWeaponLifeLeech;
    }
    return 0;
}

static int ItemValue(PLAYER *pPlayer, spritetype *pItem)
{
    const int nType = pItem->type;
    const int nHealth = pPlayer->pXSprite->health >> 4;
    if (nType >= kItemWeaponBase && nType < kItemWeaponMax)
    {
        const int w = ItemWeapon(nType);
        if (!w)
            return 0;
        if (!pPlayer->hasWeapon[w])
            return 40 + gBotWeapon[w].nPower;
        return WeaponIsEquipable(pPlayer, w) ? 8 : 35;
    }
    if (nType >= kItemAmmoBase && nType < kItemAmmoMax)
    {
        const int w = ItemWeapon(nType);
        if (!w || !pPlayer->hasWeapon[w])
            return 4;
        return WeaponIsEquipable(pPlayer, w) ? 12 : 40;
    }
    switch (nType)
    {
    case kItemHealthMedPouch:
    case kItemHealthLifeEssense:
    case kItemHealthRedPotion:
    case kItemHealthDoctorBag:
        return nHealth >= 100 ? 0 : (100 - nHealth) * 3 / 2;
    case kItemHealthLifeSeed:
        return 60 + ClipLow(100 - nHealth, 0);
    case kItemTwoGuns:
        return 110;
    case kItemDeathMask:
        return 100;
    case kItemShadowCloak:
        return 70;
    case kItemReflectShots:
        return 60;
    }
    if (nType >= kItemArmorAsbest && nType <= kItemArmorSuper)
    {
        const int nArmor = (pPlayer->armor[0] + pPlayer->armor[1] + pPlayer->armor[2]) >> 4;
        return nArmor < 100 ? 40 - nArmor / 4 : 5;
    }
    if (nType >= kItemFeatherFall && nType <= kItemBeastVision)
        return 15;
    return 0;
}

static bool ItemIgnored(BOT *pBot, int nSprite)
{
    if (nItemFails[nSprite] >= 2)
        return true;
    for (int i = 0; i < 4; i++)
        if (pBot->ignoreSprite[i] == nSprite && pBot->ignoreUntil[i] > nNow)
            return true;
    return false;
}

static void IgnoreItem(BOT *pBot, int nSprite, bool bUnreachable = false)
{
    if (bUnreachable && nItemFails[nSprite] < 255)
        nItemFails[nSprite]++;
    int nSlot = 0;
    for (int i = 1; i < 4; i++)
        if (pBot->ignoreUntil[i] < pBot->ignoreUntil[nSlot])
            nSlot = i;
    pBot->ignoreSprite[nSlot] = nSprite;
    pBot->ignoreUntil[nSlot] = nNow + 30*20;
    pBot->nAtGoalSince = 0;
}

static int FindBestItem(BOT *pBot, PLAYER *pPlayer, int *pValue, int nAvoidX, int nAvoidY)
{
    spritetype *pSprite = pPlayer->pSprite;
    int nBest = -1, nBestScore = 0;
    for (int i = headspritestat[kStatItem]; i >= 0; i = nextspritestat[i])
    {
        spritetype *pItem = &sprite[i];
        if ((pItem->cstat & CSTAT_SPRITE_INVISIBLE) || ItemIgnored(pBot, i))
            continue;
        const int nValue = ItemValue(pPlayer, pItem);
        if (nValue <= 0)
            continue;
        const int nDist = BotDist(pSprite->x, pSprite->y, pItem->x, pItem->y);
        if (nDist > 24000)
            continue;
        int nScore = nValue * 4000 / (2500 + nDist);
        if (nAvoidX != INT32_MIN && BotDist(nAvoidX, nAvoidY, pItem->x, pItem->y) < nDist) // closer to the enemy than to it
            nScore /= 4;
        if (nScore > nBestScore)
            nBest = i, nBestScore = nScore;
    }
    if (pValue)
        *pValue = nBestScore;
    return nBest;
}

//
// senses
//

static bool BotCanSee(BOT *pBot, PLAYER *pPlayer, spritetype *pTarget, bool bIgnoreFov)
{
    spritetype *pSprite = pPlayer->pSprite;
    const int dx = pTarget->x - pSprite->x, dy = pTarget->y - pSprite->y;
    const int nDist = approxDist(dx, dy);
    if (nDist > 30000)
        return false;
    PLAYER *pTargetPlayer = SpritePlayer(pTarget->index);
    const bool bFiring = pTargetPlayer && (pTargetPlayer->input.buttonFlags.shoot || pTargetPlayer->input.buttonFlags.shoot2);
    if (!bIgnoreFov && nDist > 500)
    {
        int nFov = pBot->skill.nFov;
        if (bFiring)
            nFov += nFov / 3; // muzzle flash in the corner of the eye
        if (klabs(AngDiff(pSprite->ang, getangle(dx, dy))) > nFov)
            return false;
    }
    if (pTargetPlayer)
    {
        if (powerupCheck(pTargetPlayer, kPwUpShadowCloak) && !bFiring && nDist > 900)
            return false;
        const int nShade = sector[pTarget->sectnum].floorshade;
        if (nShade > 40 && !bFiring && nDist > 3500 && !pTargetPlayer->flashEffect)
            return false;
    }
    const int z1 = pPlayer->zView;
    const int z2 = SpriteCenterZ(pTarget);
    if (cansee(pSprite->x, pSprite->y, z1, pSprite->sectnum, pTarget->x, pTarget->y, z2, pTarget->sectnum))
        return true;
    int top, bottom;
    GetSpriteExtents(pTarget, &top, &bottom);
    return cansee(pSprite->x, pSprite->y, z1, pSprite->sectnum, pTarget->x, pTarget->y, top + (4<<8), pTarget->sectnum);
}

static void Remember(BOT *pBot, int nPlayer, spritetype *pTarget, bool bSeen, int nFuzz)
{
    BOTMEMORY *pMem = &pBot->mem[nPlayer];
    pMem->bValid = true;
    pMem->x = pTarget->x;
    pMem->y = pTarget->y;
    pMem->z = pTarget->z;
    pMem->nSector = pTarget->sectnum;
    if (nFuzz > 0)
    {
        short nSector = pTarget->sectnum;
        const int x = pTarget->x + BotRand(pBot, nFuzz*2) - nFuzz;
        const int y = pTarget->y + BotRand(pBot, nFuzz*2) - nFuzz;
        updatesector(x, y, &nSector);
        if (nSector >= 0)
            pMem->x = x, pMem->y = y, pMem->nSector = nSector;
    }
    pMem->vx = xvel[pTarget->index];
    pMem->vy = yvel[pTarget->index];
    pMem->nTime = nNow;
    pMem->bSeen = bSeen;
}

// Looks and listens. Returns the sprite it should fight (-1 none).
static int BotPerceive(BOT *pBot, PLAYER *pPlayer)
{
    spritetype *pSprite = pPlayer->pSprite;
    int nBest = -1, nBestScore = 0x7fffffff;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pOther = &gPlayer[p];
        if (!IsEnemyPlayer(pPlayer, pOther))
            continue;
        if (pOther->pXSprite->health <= 0)
        {
            pBot->mem[p].bValid = false;
            continue;
        }
        spritetype *pTarget = pOther->pSprite;
        const int nDist = BotDist(pSprite->x, pSprite->y, pTarget->x, pTarget->y);
        const bool bTracking = pBot->nTarget == pTarget->index && nNow - pBot->mem[p].nTime < 6;
        if (BotCanSee(pBot, pPlayer, pTarget, bTracking))
        {
            Remember(pBot, p, pTarget, true, 0);
            int nScore = nDist;
            if (pTarget->index == pBot->nTarget)
                nScore -= 2000; // stick with who it is fighting
            if (nScore < nBestScore)
                nBest = pTarget->index, nBestScore = nScore;
            continue;
        }
        // hearing: shots carry far, jumps and footsteps less so
        const int nHear = pBot->skill.nHearing;
        const bool bShooting = pOther->input.buttonFlags.shoot || pOther->input.buttonFlags.shoot2;
        const int nSpeed = approxDist(xvel[pTarget->index], yvel[pTarget->index]);
        if (bShooting && nDist < nHear && pOther->curWeapon != kWeaponPitchfork && pOther->curWeapon != kWeaponVoodoo)
            Remember(pBot, p, pTarget, false, nDist / 10);
        else if (pOther->input.buttonFlags.jump && nDist < nHear / 3)
            Remember(pBot, p, pTarget, false, nDist / 8);
        else if (nSpeed > 0x30000 && nDist < nHear / 6 && !pOther->input.buttonFlags.crouch)
            Remember(pBot, p, pTarget, false, nDist / 6);
    }
    // monsters, in co-op or when the match has them
    if (gGameOptions.nGameType == kGameTypeCoop || gGameOptions.nMonsterSettings)
    {
        for (int i = headspritestat[kStatDude]; i >= 0; i = nextspritestat[i])
        {
            spritetype *pDude = &sprite[i];
            if (IsPlayerSprite(pDude) || !SpriteAlive(i) || (pDude->flags & 32))
                continue;
            const int nDist = BotDist(pSprite->x, pSprite->y, pDude->x, pDude->y);
            if (nDist > 9000)
                continue;
            if (!BotCanSee(pBot, pPlayer, pDude, pBot->nTarget == i))
                continue;
            int nScore = nDist + 1500; // players first
            if (i == pBot->nTarget)
                nScore -= 2000;
            if (nScore < nBestScore)
                nBest = i, nBestScore = nScore;
        }
    }
    return nBest;
}

static int RecentMemory(BOT *pBot, int nMaxAge) // the enemy player it knows the most recent thing about
{
    int nBest = -1, nBestTime = nNow - nMaxAge;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        BOTMEMORY *pMem = &pBot->mem[p];
        if (pMem->bValid && pMem->nTime > nBestTime)
            nBest = p, nBestTime = pMem->nTime;
    }
    return nBest;
}

//
// decisions
//

static void SetState(BOT *pBot, int nState)
{
    if (pBot->nState == nState)
        return;
    pBot->nState = nState;
    pBot->nStateTime = nNow;
    pBot->nPathLen = pBot->nPathPos = 0;
    pBot->nGoalSect = -1;
    pBot->nGoalSprite = -1;
    pBot->nWaitUntil = 0;
}

static bool SetGoal(BOT *pBot, int x, int y, int z, int nSector)
{
    if (nSector < 0)
        return false;
    pBot->nGoalX = x;
    pBot->nGoalY = y;
    pBot->nGoalZ = z;
    pBot->nGoalSect = nSector;
    if (!BotFindPath(pBot, nSector, x, y))
    {
        pBot->nGoalSect = -1;
        return false;
    }
    return true;
}

static bool GoalReached(BOT *pBot, spritetype *pSprite, int nRange)
{
    return pBot->nGoalSect >= 0 && BotDist(pSprite->x, pSprite->y, pBot->nGoalX, pBot->nGoalY) < nRange;
}

// Somewhere to go when there is nothing better to do: places where items lie (where
// players go too), now and then a camping spot. Aggressive bots go looking for players,
// roughly where a player who knows the map and hears distant fighting would guess.
static bool PickRoamGoal(BOT *pBot, PLAYER *pPlayer)
{
    spritetype *pSprite = pPlayer->pSprite;
    if (BotChance(pBot, 15 + pBot->persona.nAggression / 2))
    {
        int nCount = 0, nPick = -1;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            if (IsEnemyPlayer(pPlayer, &gPlayer[p]) && gPlayer[p].pXSprite->health > 0 && BotRand(pBot, ++nCount) == 0)
                nPick = p;
        if (nPick >= 0)
        {
            spritetype *pEnemy = gPlayer[nPick].pSprite;
            for (int nTry = 0; nTry < 4; nTry++)
            {
                const int nFuzz = 2500;
                const int x = pEnemy->x + BotRand(pBot, nFuzz*2) - nFuzz, y = pEnemy->y + BotRand(pBot, nFuzz*2) - nFuzz;
                short nSector = pEnemy->sectnum;
                updatesector(x, y, &nSector);
                if (nSector < 0)
                    continue;
                if (SetGoal(pBot, x, y, getflorzofslope(nSector, x, y), nSector))
                    return true;
            }
        }
    }
    for (int nTry = 0; nTry < 6; nTry++)
    {
        int x, y, nSector;
        if (nCampSpots > 0 && (nItemSpots == 0 || BotChance(pBot, 25)))
        {
            const int i = BotRand(pBot, nCampSpots);
            x = campSpot[i].x, y = campSpot[i].y, nSector = campSect[i];
        }
        else if (nItemSpots > 0)
        {
            const int i = BotRand(pBot, nItemSpots);
            x = itemSpot[i].x, y = itemSpot[i].y, nSector = itemSpotSect[i];
        }
        else
        {
            nSector = BotRand(pBot, numsectors);
            x = sectCenter[nSector].x, y = sectCenter[nSector].y;
            if (!inside(x, y, nSector))
                continue;
        }
        if (BotDist(pSprite->x, pSprite->y, x, y) < 1200)
            continue;
        if (SetGoal(pBot, x, y, getflorzofslope(nSector, x, y), nSector))
            return true;
    }
    return false;
}

static bool PickCampSpot(BOT *pBot, spritetype *pSprite)
{
    if (nCampSpots <= 0)
        return false;
    int nBest = -1, nBestScore = 0x7fffffff;
    for (int nTry = 0; nTry < 6; nTry++)
    {
        const int i = BotRand(pBot, nCampSpots);
        if (nCampFails[i] >= 2)
            continue;
        bool bTaken = false; // two bots in the same corner looks silly
        for (int j = 0; j < kMaxPlayers && !bTaken; j++)
            bTaken = gBot[j].bActive && &gBot[j] != pBot && gBot[j].nCampSpot == i && (gBot[j].nState == kBotStateCamp || gBot[j].nState == kBotStateAmbush);
        if (bTaken)
            continue;
        const int nScore = BotDist(pSprite->x, pSprite->y, campSpot[i].x, campSpot[i].y) + BotRand(pBot, 3000);
        if (nScore < nBestScore)
            nBest = i, nBestScore = nScore;
    }
    if (nBest < 0 || !SetGoal(pBot, campSpot[nBest].x, campSpot[nBest].y, campSpot[nBest].z, campSect[nBest]))
        return false;
    pBot->nWatchAng = campAng[nBest];
    pBot->nCampSpot = nBest;
    pBot->nLastCamp = nNow;
    return true;
}

// Wait beside the opening the enemy is expected to come through.
static bool PlanAmbush(BOT *pBot, PLAYER *pPlayer, BOTMEMORY *pMem)
{
    spritetype *pSprite = pPlayer->pSprite;
    short enemyPath[kBotMaxPath];
    int nEnemyCost;
    const int nLen = PathSearch(NULL, pMem->nSector, pMem->x, pMem->y, pSprite->sectnum, pSprite->x, pSprite->y, enemyPath, kBotMaxPath, &nEnemyCost);
    if (nLen <= 0)
        return false;
    // the last opening on its way that the bot can reach well before it
    for (int i = nLen - 1; i >= 0; i--)
    {
        const int w = enemyPath[i];
        const int nWaitSect = wall[w].nextsector;
        int dx, dy;
        PortalPoint(w, pMem->x, pMem->y, &dx, &dy);
        const int nMyDist = BotDist(pSprite->x, pSprite->y, dx, dy);
        const int nEnemyDist = BotDist(pMem->x, pMem->y, dx, dy);
        if (nMyDist > 5000)
            break;
        if (nEnemyDist < 900 || nMyDist > nEnemyDist * 2 / 3)
            continue;
        // beside the opening, on the side the enemy comes out
        const int wx = wall[wall[w].point2].x - wall[w].x, wy = wall[wall[w].point2].y - wall[w].y;
        const int nWallAng = getangle(wx, wy);
        int nNormal = (nWallAng + 512) & 2047;
        if (!inside(dx + mulscale30(Cos(nNormal), 64), dy + mulscale30(Sin(nNormal), 64), nWaitSect))
            nNormal = (nWallAng - 512) & 2047;
        const int nOff = approxDist(wx, wy) / 2 + 260;
        const int nFirstSide = BotChance(pBot, 50) ? 1 : -1;
        for (int nTry = 0; nTry < 3; nTry++)
        {
            int x, y;
            if (nTry < 2)
            {
                // to one side of the opening, out of sight of anyone still coming
                const int nSide = nTry ? -nFirstSide : nFirstSide;
                x = dx + mulscale30(Cos(nNormal), 320) + nSide * mulscale30(Cos(nWallAng), nOff);
                y = dy + mulscale30(Sin(nNormal), 320) + nSide * mulscale30(Sin(nWallAng), nOff);
            }
            else
            {
                // or further back, facing it
                x = dx + mulscale30(Cos(nNormal), 800);
                y = dy + mulscale30(Sin(nNormal), 800);
            }
            short nSector = nWaitSect;
            updatesector(x, y, &nSector);
            if (nSector < 0)
                continue;
            const int z = getflorzofslope(nSector, x, y);
            if (getceilzofslope(nSector, x, y) > z - (56<<8))
                continue;
            if (!cansee(x, y, z - (32<<8), nSector, dx, dy, getflorzofslope(nWaitSect, dx, dy) - (32<<8), nWaitSect))
                continue;
            if (!SetGoal(pBot, x, y, z, nSector))
                continue;
            pBot->nWatchAng = getangle(dx - x, dy - y);
            return true;
        }
    }
    return false;
}

// Go the other way round: the direct way between them costs a lot more.
static bool PlanFlank(BOT *pBot, PLAYER *pPlayer, BOTMEMORY *pMem)
{
    spritetype *pSprite = pPlayer->pSprite;
    short directPath[kBotMaxPath];
    int nDirectCost;
    const int nLen = PathSearch(pBot, pSprite->sectnum, pSprite->x, pSprite->y, pMem->nSector, pMem->x, pMem->y, directPath, kBotMaxPath, &nDirectCost);
    if (nLen < 2)
        return false;
    for (int i = 0; i < nLen; i++)
    {
        const int s = wall[directPath[i]].nextsector;
        if (s != pMem->nSector)
            nSectPenalty[s] = 600;
    }
    // where it could be seen coming from
    for (int s = 0; s < numsectors; s++)
    {
        if (BotDist(sectCenter[s].x, sectCenter[s].y, pMem->x, pMem->y) > 7000 || s == pMem->nSector)
            continue;
        if (cansee(pMem->x, pMem->y, pMem->z - (40<<8), pMem->nSector, sectCenter[s].x, sectCenter[s].y, getflorzofslope(s, sectCenter[s].x, sectCenter[s].y) - (32<<8), s))
            nSectPenalty[s] = max(nSectPenalty[s], 250);
    }
    nSectPenalty[pMem->nSector] = 0;
    nSectPenalty[pSprite->sectnum] = 0;
    int nFlankCost;
    const int nFlankLen = PathSearch(pBot, pSprite->sectnum, pSprite->x, pSprite->y, pMem->nSector, pMem->x, pMem->y, pBot->path, kBotMaxPath, &nFlankCost);
    BotClearPenalties();
    if (nFlankLen <= 0)
        return false;
    // the same way after all, or far too long a way round
    bool bSame = nFlankLen == nLen;
    for (int i = 0; bSame && i < nLen; i++)
        bSame = pBot->path[i] == directPath[i];
    if (bSame || nFlankCost > nDirectCost * 6)
    {
        pBot->nPathLen = 0;
        return false;
    }
    pBot->nGoalX = pMem->x;
    pBot->nGoalY = pMem->y;
    pBot->nGoalZ = pMem->z;
    pBot->nGoalSect = pMem->nSector;
    pBot->nPathGoalSect = pMem->nSector;
    pBot->nPathLen = nFlankLen;
    pBot->nPathPos = 0;
    pBot->nRepathTime = nNow + 30*20; // keep this way, don't fall back to the direct one
    return true;
}

// Run somewhere out of sight, preferably to health.
static bool PlanFlee(BOT *pBot, PLAYER *pPlayer, spritetype *pEnemy)
{
    spritetype *pSprite = pPlayer->pSprite;
    for (int s = 0; s < numsectors; s++)
    {
        const int nDist = BotDist(sectCenter[s].x, sectCenter[s].y, pEnemy->x, pEnemy->y);
        if (nDist < 2500)
            nSectPenalty[s] = 500;
        else if (nDist < 6000 && cansee(pEnemy->x, pEnemy->y, pEnemy->z - (40<<8), pEnemy->sectnum, sectCenter[s].x, sectCenter[s].y, getflorzofslope(s, sectCenter[s].x, sectCenter[s].y) - (32<<8), s))
            nSectPenalty[s] = 200;
    }
    nSectPenalty[pSprite->sectnum] = 0;
    bool bOk = false;
    int nValue;
    const int nItem = FindBestItem(pBot, pPlayer, &nValue, pEnemy->x, pEnemy->y);
    if (nItem >= 0 && ItemValue(pPlayer, &sprite[nItem]) >= 30)
    {
        bOk = SetGoal(pBot, sprite[nItem].x, sprite[nItem].y, sprite[nItem].z, sprite[nItem].sectnum);
        if (bOk)
            pBot->nGoalSprite = nItem;
    }
    for (int nTry = 0; !bOk && nTry < 8; nTry++)
    {
        int x, y, nSector;
        if (nItemSpots > 0 && (nTry & 1))
        {
            const int i = BotRand(pBot, nItemSpots);
            x = itemSpot[i].x, y = itemSpot[i].y, nSector = itemSpotSect[i];
        }
        else if (nCampSpots > 0)
        {
            const int i = BotRand(pBot, nCampSpots);
            x = campSpot[i].x, y = campSpot[i].y, nSector = campSect[i];
        }
        else
            break;
        if (BotDist(x, y, pEnemy->x, pEnemy->y) < BotDist(pSprite->x, pSprite->y, pEnemy->x, pEnemy->y) + 1500)
            continue;
        bOk = SetGoal(pBot, x, y, getflorzofslope(nSector, x, y), nSector);
    }
    BotClearPenalties();
    return bOk;
}

static void BotSay(BOT *pBot, const char *pzLine, int nDelay)
{
    if (!gBotTaunts || pBot->pzTalk)
        return;
    pBot->pzTalk = pzLine;
    pBot->nTalkAt = nNow + nDelay;
}

static void BotThink(BOT *pBot, PLAYER *pPlayer)
{
    spritetype *pSprite = pPlayer->pSprite;
    const int nHealth = pPlayer->pXSprite->health >> 4;
    const BOTPERSONA *pPersona = &pBot->persona;
    const int nTactics = pBot->skill.nTactics;

    // somebody it can see: fight or get away
    if (pBot->nTarget >= 0)
    {
        spritetype *pEnemy = &sprite[pBot->nTarget];
        PLAYER *pEnemyPlayer = SpritePlayer(pBot->nTarget);
        const int nDist = BotDist(pSprite->x, pSprite->y, pEnemy->x, pEnemy->y);
        if (pEnemyPlayer && pBot->nState != kBotStateFlee)
        {
            int nMyScore;
            BestWeapon(pPlayer, nDist, &nMyScore);
            const int nMine = nMyScore / 10 + 10;
            const int nTheirs = PlayerPower(pEnemyPlayer);
            const bool bOutgunned = nTheirs * 10 > nMine * 13;
            const bool bHurt = nHealth < pPersona->nFlee || (nHealth < pPersona->nFlee + 25 && bOutgunned);
            if (bHurt && nHealth < 60 && BotChance(pBot, 40 + nTactics / 2) && !powerupCheck(pPlayer, kPwUpDeathMask))
            {
                if (PlanFlee(pBot, pPlayer, pEnemy))
                {
                    const int nGoalSprite = pBot->nGoalSprite;
                    pBot->nState = kBotStateFlee; // keep the path SetState would clear
                    pBot->nStateTime = nNow;
                    pBot->nGoalSprite = nGoalSprite;
                    return;
                }
            }
            // far away and not looking this way: sneak round instead of running at it
            const bool bUnaware = klabs(AngDiff(pEnemy->ang, getangle(pSprite->x - pEnemy->x, pSprite->y - pEnemy->y))) > 300;
            if (nDist > 4500 && bUnaware && pBot->nState != kBotStateFight && BotChance(pBot, pPersona->nFlank * nTactics / 150))
            {
                const int p = pEnemyPlayer->nPlayer;
                if (PlanFlank(pBot, pPlayer, &pBot->mem[p]))
                {
                    pBot->nState = kBotStateFlank;
                    pBot->nStateTime = nNow;
                    return;
                }
            }
        }
        if (pBot->nState == kBotStateFlee && nNow - pBot->nStateTime < 30*6 && pBot->nGoalSect >= 0)
            return; // keep running for a while
        if (pBot->nState != kBotStateFight)
        {
            SetState(pBot, kBotStateFight);
            pBot->nStrafeUntil = 0;
        }
        return;
    }

    // keep doing what it does while it still makes sense
    switch (pBot->nState)
    {
    case kBotStateAmbush:
    case kBotStateCamp:
        if (pBot->nWaitUntil && nNow > pBot->nWaitUntil)
            break;
        if (pBot->nGoalSect >= 0 || pBot->nWaitUntil)
            return;
        break;
    case kBotStateFlank:
    case kBotStateHunt:
        if (pBot->nGoalSect >= 0 && !GoalReached(pBot, pSprite, 400) && nNow - pBot->nStateTime < 30*25)
            return;
        if (pBot->nState == kBotStateHunt && GoalReached(pBot, pSprite, 400) && !pBot->nSearchUntil)
        {
            pBot->nSearchUntil = nNow + 45 + BotRand(pBot, 45); // look around a bit
            return;
        }
        if (pBot->nSearchUntil && nNow < pBot->nSearchUntil)
            return;
        break;
    case kBotStateFlee:
        if (pBot->nGoalSect >= 0 && !GoalReached(pBot, pSprite, 200) && nNow - pBot->nStateTime < 30*8)
            return;
        break;
    case kBotStateCollect:
        if (pBot->nGoalSprite >= 0 && sprite[pBot->nGoalSprite].statnum == kStatItem && pBot->nGoalSect >= 0)
        {
            // standing on it and it's still there: it can't take it (full up), forget it for a while
            if (GoalReached(pBot, pSprite, 80))
            {
                if (!pBot->nAtGoalSince)
                    pBot->nAtGoalSince = nNow;
                else if (nNow - pBot->nAtGoalSince > 20)
                {
                    IgnoreItem(pBot, pBot->nGoalSprite);
                    break;
                }
            }
            else
                pBot->nAtGoalSince = 0;
            if (nNow - pBot->nStateTime < 30*15)
                return;
            IgnoreItem(pBot, pBot->nGoalSprite, true); // couldn't get there
        }
        break;
    case kBotStateRoam:
        if (pBot->nGoalSect >= 0 && !GoalReached(pBot, pSprite, 300) && nNow - pBot->nStateTime < 30*20)
        {
            int nValue;
            FindBestItem(pBot, pPlayer, &nValue, INT32_MIN, 0);
            if (nValue < 60)
                return;
        }
        break;
    }
    pBot->nSearchUntil = 0;

    // badly needs something (health when hurt, a real weapon): that first
    int nValue;
    const int nItem = FindBestItem(pBot, pPlayer, &nValue, INT32_MIN, 0);
    if (nItem >= 0 && nValue >= 80 - pPersona->nAggression / 4)
    {
        SetState(pBot, kBotStateCollect);
        if (SetGoal(pBot, sprite[nItem].x, sprite[nItem].y, sprite[nItem].z, sprite[nItem].sectnum))
        {
            pBot->nGoalSprite = nItem;
            return;
        }
        IgnoreItem(pBot, nItem, true);
    }

    // heard or remembers somebody: ambush, flank or chase
    const int p = RecentMemory(pBot, 30*5);
    if (p >= 0)
    {
        BOTMEMORY *pMem = &pBot->mem[p];
        const int nDist = BotDist(pSprite->x, pSprite->y, pMem->x, pMem->y);
        const bool bComing = (pMem->vx || pMem->vy) && dmulscale16(pMem->vx, pSprite->x - pMem->x, pMem->vy, pSprite->y - pMem->y) > 0;
        if (bComing && nDist < 9000 && nHealth >= pPersona->nFlee && BotChance(pBot, pPersona->nAmbush * nTactics / 100 + 10))
        {
            if (PlanAmbush(pBot, pPlayer, pMem))
            {
                const int nWatch = pBot->nWatchAng;
                pBot->nState = kBotStateAmbush;
                pBot->nStateTime = nNow;
                pBot->nWatchAng = nWatch;
                pBot->nWaitUntil = 0;
                return;
            }
        }
        if (nDist > 2500 && BotChance(pBot, pPersona->nFlank * nTactics / 100))
        {
            if (PlanFlank(pBot, pPlayer, pMem))
            {
                pBot->nState = kBotStateFlank;
                pBot->nStateTime = nNow;
                return;
            }
        }
        if (nHealth >= pPersona->nFlee && BotChance(pBot, 25 + pPersona->nAggression * 3 / 4))
        {
            SetState(pBot, kBotStateHunt);
            // where it was going
            int x = pMem->x + ((pMem->vx * 30) >> 12), y = pMem->y + ((pMem->vy * 30) >> 12);
            short nSector = pMem->nSector;
            updatesector(x, y, &nSector);
            if (nSector < 0)
                x = pMem->x, y = pMem->y, nSector = pMem->nSector;
            if (SetGoal(pBot, x, y, pMem->z, nSector) || SetGoal(pBot, pMem->x, pMem->y, pMem->z, pMem->nSector))
                return;
        }
    }

    // items it could use
    if (nItem >= 0 && nValue >= 25 - pPersona->nCamping / 10)
    {
        SetState(pBot, kBotStateCollect);
        if (SetGoal(pBot, sprite[nItem].x, sprite[nItem].y, sprite[nItem].z, sprite[nItem].sectnum))
        {
            pBot->nGoalSprite = nItem;
            return;
        }
    }

    // camp somewhere or walk around
    if (BotChance(pBot, pPersona->nCamping * 2 / 3) && pBot->nState != kBotStateCamp && (!pBot->nLastCamp || nNow - pBot->nLastCamp > 30*25))
    {
        SetState(pBot, kBotStateCamp);
        if (PickCampSpot(pBot, pSprite))
            return;
    }
    SetState(pBot, kBotStateRoam);
    if (PickRoamGoal(pBot, pPlayer))
        return;
    // no way it knows anywhere (a lift that's away, stacked rooms, a door opened elsewhere):
    // wander towards a neighbouring area like a lost player would; jumping and pressing use
    // when it gets stuck, the game may well take it somewhere
    const int nSector = pSprite->sectnum;
    for (int nTry = 0; nTry < 8; nTry++)
    {
        const int w = sector[nSector].wallptr + BotRand(pBot, sector[nSector].wallnum);
        const int nNext = wall[w].nextsector;
        int x = (wall[w].x + wall[wall[w].point2].x) >> 1, y = (wall[w].y + wall[wall[w].point2].y) >> 1;
        if (nNext < 0 && nTry < 6)
            continue;
        if (nNext >= 0 && inside(sectCenter[nNext].x, sectCenter[nNext].y, nNext))
            x = sectCenter[nNext].x, y = sectCenter[nNext].y;
        pBot->nGoalX = x;
        pBot->nGoalY = y;
        pBot->nGoalZ = pSprite->z;
        pBot->nGoalSect = nNext >= 0 ? nNext : nSector;
        pBot->nPathGoalSect = pBot->nGoalSect;
        pBot->nPathLen = pBot->nPathPos = 0;
        pBot->nRepathTime = nNow;
        return;
    }
}

//
// moving, turning and shooting
//

static void FollowPath(BOT *pBot, spritetype *pSprite, int *tx, int *ty, int *pEdge, int *pPortal)
{
    *pEdge = kEdgeWalk;
    *pPortal = -1;
    for (int i = pBot->nPathPos; i < pBot->nPathLen; i++)
    {
        if (wall[pBot->path[i]].nextsector == pSprite->sectnum)
        {
            pBot->nPathPos = i + 1;
            break;
        }
    }
    if (pBot->nPathPos >= pBot->nPathLen)
    {
        if (pBot->nPathLen > 0 && pSprite->sectnum != pBot->nPathGoalSect && pBot->nGoalSect >= 0 && nNow - pBot->nRepathTime > 10)
            BotFindPath(pBot, pBot->nGoalSect, pBot->nGoalX, pBot->nGoalY); // a long way, the next part of it
        *tx = pBot->nGoalX;
        *ty = pBot->nGoalY;
        return;
    }
    const int w = pBot->path[pBot->nPathPos];
    if (sectorofwall(w) != pSprite->sectnum)
    {
        // pushed off its way
        if (nNow - pBot->nRepathTime > 8 && pBot->nGoalSect >= 0)
        {
            BotFindPath(pBot, pBot->nGoalSect, pBot->nGoalX, pBot->nGoalY);
            if (pBot->nPathLen == 0 && pSprite->sectnum != pBot->nGoalSect)
                pBot->nGoalSect = -1;
        }
        *tx = pBot->nGoalX;
        *ty = pBot->nGoalY;
        if (pBot->nPathPos < pBot->nPathLen)
        {
            const int w2 = pBot->path[pBot->nPathPos];
            PortalPoint(w2, pSprite->x, pSprite->y, tx, ty);
        }
        return;
    }
    int nx, ny;
    if (pBot->nPathPos + 1 < pBot->nPathLen)
    {
        const int w2 = pBot->path[pBot->nPathPos + 1];
        nx = (wall[w2].x + wall[wall[w2].point2].x) >> 1;
        ny = (wall[w2].y + wall[wall[w2].point2].y) >> 1;
    }
    else
        nx = pBot->nGoalX, ny = pBot->nGoalY;
    PortalPoint(w, (pSprite->x + nx) >> 1, (pSprite->y + ny) >> 1, tx, ty);
    *pEdge = EdgeType(w);
    *pPortal = w;
    // close to the opening: aim past it so it doesn't slow down in the doorway
    if (*pEdge == kEdgeWalk && BotDist(pSprite->x, pSprite->y, *tx, *ty) < 256)
    {
        int px = *tx, py = *ty;
        *tx = px + (nx - px) / 4;
        *ty = py + (ny - py) / 4;
    }
}

// how far it can walk that way before bumping into a wall (or someone)
static int ProbeDist(spritetype *pSprite, int nAng, int z)
{
    hitdata_t hit;
    vec3_t pos = { pSprite->x, pSprite->y, z };
    const int nCstat = pSprite->cstat;
    pSprite->cstat &= ~CSTAT_SPRITE_BLOCK_HITSCAN;
    hitscangoal.x = hitscangoal.y = 0x1fffffff;
    hitscan(&pos, pSprite->sectnum, Cos(nAng) >> 16, Sin(nAng) >> 16, 0, &hit, CLIPMASK0);
    pSprite->cstat = nCstat;
    if (hit.sect < 0)
        return 0x7fffffff;
    return approxDist(hit.xyz.x - pos.x, hit.xyz.y - pos.y);
}

// Feelers: walking straight at a corner, turn a little to one side until the way is clear.
static int AvoidWalls(BOT *pBot, spritetype *pSprite, int nAng, int nWant)
{
    const int z = pSprite->z - (30<<8);
    const int nNeed = ClipRange(nWant, 96, 320);
    if (ProbeDist(pSprite, nAng, z) >= nNeed)
        return nAng;
    int nSide = nNow < pBot->nAvoidUntil ? pBot->nAvoidSide : 0;
    if (!nSide)
    {
        // the side with more room
        nSide = ProbeDist(pSprite, (nAng + 256) & 2047, z) >= ProbeDist(pSprite, (nAng - 256) & 2047, z) ? 1 : -1;
    }
    static const int nOffsets[] = { 96, 192, 320, 448, 576 };
    for (int nOff : nOffsets)
    {
        for (int s = 0; s < 2; s++)
        {
            const int nTry = (nAng + (s ? -nSide : nSide) * nOff) & 2047;
            if (ProbeDist(pSprite, nTry, z) >= nNeed)
            {
                pBot->nAvoidSide = s ? -nSide : nSide;
                pBot->nAvoidUntil = nNow + 15;
                return nTry;
            }
        }
    }
    return nAng;
}

static int GaussRand(BOT *pBot, int nRange) // roughly normal, -nRange..nRange
{
    return (BotRand(pBot, nRange + 1) + BotRand(pBot, nRange + 1) + BotRand(pBot, nRange + 1)) * 2 / 3 - nRange;
}

static void BotTalk(BOT *pBot, PLAYER *pPlayer)
{
    if (!pBot->pzTalk || nNow < pBot->nTalkAt)
        return;
    char buffer[128];
    Bsnprintf(buffer, sizeof(buffer), "%s: %s", gProfile[pPlayer->nPlayer].name, pBot->pzTalk);
    viewSetMessage(buffer, 0, MESSAGE_PRIORITY_NORMAL);
    pBot->pzTalk = NULL;
}

void botGetInput(int nPlayer, GINPUT *pInput)
{
    Bmemset(pInput, 0, sizeof(GINPUT));
    if (!botIsBot(nPlayer))
        return;
    BOT *pBot = &gBot[nPlayer];
    PLAYER *pPlayer = &gPlayer[nPlayer];
    spritetype *pSprite = pPlayer->pSprite;
    if (!pSprite || !pPlayer->pXSprite || gPaused || gGameMenuMgr.m_bActive || gEndGameMgr.at0 || gPlayerRoundEnding)
        return;
    nNow = gLevelTime;
    pInput->syncFlags.run = 1;
    BotTalk(pBot, pPlayer);

    // dead: wait a moment, then respawn
    if (pPlayer->pXSprite->health <= 0)
    {
        if (!pBot->bWasDead)
        {
            pBot->bWasDead = true;
            pBot->nRespawnDelay = 25 + BotRand(pBot, 50);
            if (pPlayer->fraggerId >= 0 && pPlayer->fraggerId != pPlayer->nSprite && BotChance(pBot, 30))
                BotSay(pBot, pzBotDeathLines[BotRand(pBot, ARRAY_SSIZE(pzBotDeathLines))], 20 + BotRand(pBot, 30));
        }
        pBot->nState = kBotStateDead;
        if (pPlayer->deathTime > pBot->nRespawnDelay * kTicsPerFrame && !(nNow & 7))
            pInput->keyFlags.action = 1;
        return;
    }
    if (pBot->bWasDead)
    {
        pBot->bWasDead = false;
        pBot->nState = -1;
        SetState(pBot, kBotStateRoam);
        pBot->nTarget = -1;
        pBot->nLastHealth = pPlayer->pXSprite->health;
        pBot->nStuckCount = 0;
        Bmemset(pBot->mem, 0, sizeof(pBot->mem));
    }
    if (pPlayer->fragCount > pBot->nLastFrags)
    {
        if (BotChance(pBot, 35))
        {
            BotSay(pBot, pzBotKillLines[BotRand(pBot, ARRAY_SSIZE(pzBotKillLines))], 15 + BotRand(pBot, 40));
            if (gBotTaunts)
                sfxPlay3DSound(pSprite, 4400 + BotRand(pBot, 10), -1, 0);
        }
        pBot->nLastFrags = pPlayer->fragCount;
    }

    // look and listen (half the bots one frame, the others the next)
    const int nOldTarget = pBot->nTarget;
    if (((nNow + nPlayer) & 1) == 0 || (pBot->nTarget >= 0 && !SpriteAlive(pBot->nTarget)))
    {
        pBot->nTarget = BotPerceive(pBot, pPlayer);
        if (pBot->nTarget >= 0 && pBot->nTarget != nOldTarget)
        {
            // a new enemy: it takes a moment to react, longer at the edge of its view
            spritetype *pEnemy = &sprite[pBot->nTarget];
            const int nOff = klabs(AngDiff(pSprite->ang, getangle(pEnemy->x - pSprite->x, pEnemy->y - pSprite->y)));
            int nReact = pBot->skill.nReaction;
            nReact += nReact * nOff / 700;
            if (pBot->nState == kBotStateAmbush || pBot->nState == kBotStateCamp)
                nReact /= 2; // was expecting it
            nReact += GaussRand(pBot, nReact / 3);
            pBot->nReactAt = nNow + ClipLow(nReact, 1);
            pBot->nTargetSince = nNow;
            pBot->nTrackFrames = 0;
            pBot->nAimErrGoal = GaussRand(pBot, pBot->skill.nAimError * 2);
            pBot->nAimErr = pBot->nAimErrGoal;
            pBot->nNextThink = nNow; // rethink now
        }
    }
    else if (pBot->nTarget >= 0 && !SpriteAlive(pBot->nTarget))
        pBot->nTarget = -1;

    // hurt by something it doesn't see: turn round towards whoever it'd guess
    const int nHealth = pPlayer->pXSprite->health;
    if (nHealth < pBot->nLastHealth && pBot->nTarget < 0)
    {
        pBot->nHurtTime = nNow;
        int nBest = -1, nBestDist = 0x7fffffff;
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
        {
            PLAYER *pOther = &gPlayer[p];
            if (!IsEnemyPlayer(pPlayer, pOther) || pOther->pXSprite->health <= 0)
                continue;
            const int nDist = BotDist(pSprite->x, pSprite->y, pOther->pSprite->x, pOther->pSprite->y);
            if (nDist < nBestDist && (pOther->input.buttonFlags.shoot || pOther->input.buttonFlags.shoot2 || nDist < 3000))
                nBest = p, nBestDist = nDist;
        }
        if (nBest >= 0)
        {
            Remember(pBot, nBest, gPlayer[nBest].pSprite, false, nBestDist / 12);
            pBot->nWatchAng = getangle(pBot->mem[nBest].x - pSprite->x, pBot->mem[nBest].y - pSprite->y);
            if (pBot->nState == kBotStateRoam || pBot->nState == kBotStateCollect || pBot->nState == kBotStateCamp)
                pBot->nNextThink = nNow;
        }
    }
    pBot->nLastHealth = nHealth;

    if (nNow >= pBot->nNextThink)
    {
        BotThink(pBot, pPlayer);
        pBot->nNextThink = nNow + 6 + BotRand(pBot, 6);
    }

    // where to go and where to look
    int nMoveAng = pSprite->ang, nMoveSpeed = 0;
    int nLookAng = pSprite->ang;
    int nLookZ = INT32_MIN;
    bool bWantJump = false, bWantCrouch = false, bWantUse = false, bPathMove = false;
    spritetype *pEnemy = pBot->nTarget >= 0 ? &sprite[pBot->nTarget] : NULL;
    int nEnemyDist = pEnemy ? BotDist(pSprite->x, pSprite->y, pEnemy->x, pEnemy->y) : 0;
    const bool bOnGround = pPlayer->pXSprite->height < 16 && pPlayer->posture != kPostureSwim;

    int nEdge = kEdgeWalk, nPortal = -1;
    int tx = pSprite->x, ty = pSprite->y;
    const bool bHasGoal = pBot->nGoalSect >= 0;
    if (bHasGoal)
    {
        FollowPath(pBot, pSprite, &tx, &ty, &nEdge, &nPortal);
        const int nDist = BotDist(pSprite->x, pSprite->y, tx, ty);
        if (pBot->nGoalSect >= 0 && (nDist > 48 || pBot->nPathPos < pBot->nPathLen))
        {
            nMoveAng = getangle(tx - pSprite->x, ty - pSprite->y), bPathMove = true;
            nMoveSpeed = 2048;
            nLookAng = nMoveAng;
        }
        if (nPortal >= 0 && BotDist(pSprite->x, pSprite->y, tx, ty) < 500)
        {
            if (nEdge == kEdgeJump && bOnGround)
                bWantJump = true;
            else if (nEdge == kEdgeDoor)
            {
                const int nDoor = DoorState(nPortal);
                if (nDoor != kDoorOpen)
                {
                    // walk up to it, face it and open it; wait while it moves
                    nLookAng = nMoveAng;
                    if (nDoor == kDoorClosed && !((nNow + nPlayer) % 12) && klabs(AngDiff(pSprite->ang, nMoveAng)) < 200)
                        bWantUse = UseCheck(pPlayer) >= 0;
                    if (BotDist(pSprite->x, pSprite->y, tx, ty) < 160)
                        nMoveSpeed = 0;
                }
            }
        }
    }

    switch (pBot->nState)
    {
    case kBotStateAmbush:
    case kBotStateCamp:
        if (pBot->nGoalSect >= 0 && GoalReached(pBot, pSprite, 120))
        {
            if (!pBot->nWaitUntil)
            {
                int nPatience = pBot->nState == kBotStateAmbush ? 30*3 + pBot->persona.nAmbush * 2 : 30*4 + pBot->persona.nCamping * 3;
                pBot->nWaitUntil = nNow + nPatience + BotRand(pBot, 30*3);
            }
            nMoveSpeed = 0;
            // watch the way in, glancing about now and then
            nLookAng = pBot->nWatchAng;
            if (pBot->nState == kBotStateCamp && ((nNow / 60) & 3) == 3)
                nLookAng = (pBot->nWatchAng + ((nNow / 60) & 4 ? 300 : -300)) & 2047;
            bWantCrouch = pBot->persona.nCamping > 50 && pBot->nState == kBotStateCamp;
        }
        break;
    case kBotStateHunt:
        if (pBot->nSearchUntil && nNow < pBot->nSearchUntil)
        {
            nMoveSpeed = 0;
            nLookAng = (pSprite->ang + 70) & 2047; // turn round looking for it
        }
        break;
    case kBotStateFlee:
        if (pEnemy && nEnemyDist < 3000 && BotChance(pBot, 60))
            nLookAng = getangle(pEnemy->x - pSprite->x, pEnemy->y - pSprite->y); // backpedal, shooting
        if (bOnGround && BotChance(pBot, pBot->persona.nJump / 12))
            bWantJump = true;
        break;
    }
    // walking about, it glances to the sides now and then instead of staring down the corridor
    if (!pEnemy && nMoveSpeed && pBot->nState != kBotStateFight && nEdge != kEdgeDoor && nNow >= pBot->nUnstickUntil)
    {
        if (nNow >= pBot->nGlanceNext)
        {
            pBot->nGlance = BotChance(pBot, 40) ? GaussRand(pBot, 260) : 0;
            pBot->nGlanceNext = nNow + 20 + BotRand(pBot, 60);
        }
        nLookAng = (nLookAng + pBot->nGlance) & 2047;
    }
    if (pBot->nState != kBotStateFight && pBot->nHurtTime && nNow - pBot->nHurtTime < 20 && !pEnemy)
        nLookAng = pBot->nWatchAng;

    // fighting
    int nWeapon = pPlayer->curWeapon;
    bool bFire = false, bAltFire = false;
    if (pEnemy)
    {
        PLAYER *pEnemyPlayer = SpritePlayer(pEnemy->index);
        const int nAngTo = getangle(pEnemy->x - pSprite->x, pEnemy->y - pSprite->y);
        // weapon for the distance
        if (nNow >= pBot->nWeaponNext && !pBot->nFireHold)
        {
            int nBestScore;
            const int nBest = BestWeapon(pPlayer, nEnemyDist, &nBestScore);
            const int nCurScore = WeaponScore(pPlayer, nWeapon, nEnemyDist);
            if (nBest && nBest != nWeapon && (nBestScore > nCurScore * 5 / 4 || !nCurScore))
            {
                pInput->newWeapon = nBest;
                pBot->nWeaponNext = nNow + 45 + BotRand(pBot, 60);
            }
        }
        if (pBot->nState == kBotStateFight)
        {
            bPathMove = false;
            const BOTWEAPON *pWeapon = &gBotWeapon[ClipRange(nWeapon, 0, kWeaponMax-1)];
            int nWant = pWeapon->nBestDist * (130 - pBot->persona.nAggression * 6 / 10) / 100;
            if (pEnemyPlayer && powerupCheck(pEnemyPlayer, kPwUpDeathMask))
                nWant = max(nWant, 4000); // can't hurt it: keep away
            const bool bSeeNow = nNow - pBot->mem[pEnemyPlayer ? pEnemyPlayer->nPlayer : 0].nTime < 3 || !pEnemyPlayer;
            if (nEnemyDist > nWant + 700 || !bSeeNow)
            {
                // close in; the path finds the way round things
                if (pBot->nGoalSect != pEnemy->sectnum || nNow - pBot->nRepathTime > 20)
                    SetGoal(pBot, pEnemy->x, pEnemy->y, pEnemy->z, pEnemy->sectnum);
                if (pBot->nGoalSect >= 0)
                {
                    FollowPath(pBot, pSprite, &tx, &ty, &nEdge, &nPortal);
                    nMoveAng = getangle(tx - pSprite->x, ty - pSprite->y), bPathMove = true;
                }
                else
                    nMoveAng = nAngTo;
                nMoveSpeed = 2048;
            }
            else if (nEnemyDist < nWant - 500)
            {
                nMoveAng = (nAngTo + 1024) & 2047; // back off
                nMoveSpeed = 2048;
            }
            else
                nMoveSpeed = 0;
            // dodge sideways; standing still at the right distance looks like a turret, so at
            // that distance it always moves one way or the other
            if (nNow >= pBot->nStrafeUntil)
            {
                const int nStrafe = pBot->persona.nStrafe;
                pBot->nStrafeDir = (BotChance(pBot, nStrafe) || !nMoveSpeed) ? (BotChance(pBot, 50) ? 1 : -1) : 0;
                pBot->nStrafeUntil = nNow + 10 + BotRand(pBot, 40 - nStrafe / 4);
            }
            if (pBot->nStrafeDir)
            {
                const int nSideAng = (nAngTo + pBot->nStrafeDir * 512) & 2047;
                if (nMoveSpeed)
                {
                    // blend the dodge into the movement
                    const int nDiff = AngDiff(nMoveAng, nSideAng);
                    nMoveAng = (nMoveAng + nDiff / 2) & 2047;
                }
                else
                {
                    nMoveAng = nSideAng;
                    nMoveSpeed = 2048;
                }
            }
            if (bOnGround && nNow >= pBot->nJumpNext)
            {
                if (BotChance(pBot, pBot->persona.nJump))
                    bWantJump = true;
                pBot->nJumpNext = nNow + 20 + BotRand(pBot, 50);
            }
            if (nNow < pBot->nCrouchUntil)
                bWantCrouch = true;
            else if (BotChance(pBot, 1) && nEnemyDist > 2000 && pBot->persona.nCamping > 40)
                pBot->nCrouchUntil = nNow + 20 + BotRand(pBot, 30);
        }

        if (nNow >= pBot->nReactAt)
        {
            // aim: where it is, plus a lead for slow shots, plus a wobble that settles while tracking
            const BOTWEAPON *pWeapon = &gBotWeapon[ClipRange(nWeapon, 0, kWeaponMax-1)];
            int ax = pEnemy->x, ay = pEnemy->y;
            if (pWeapon->nLeadSpeed)
            {
                const int t = divscale12(nEnemyDist, pWeapon->nLeadSpeed) * pBot->skill.nLead / 100;
                ax += (xvel[pEnemy->index] * t) >> 12;
                ay += (yvel[pEnemy->index] * t) >> 12;
            }
            pBot->nTrackFrames++;
            if (nNow >= pBot->nAimErrNext)
            {
                int nErr = pBot->skill.nAimError;
                nErr = nErr * ClipLow(100 - pBot->nTrackFrames, 35) / 100;
                const int nSideSpeed = klabs(dmulscale30(xvel[pEnemy->index], Sin(nAngTo), -yvel[pEnemy->index], Cos(nAngTo)));
                nErr += nErr * ClipHigh(nSideSpeed >> 14, 8) / 8;
                if (!bOnGround)
                    nErr += nErr / 3;
                pBot->nAimErrGoal = GaussRand(pBot, nErr);
                pBot->nAimErrZ = GaussRand(pBot, nErr / 3);
                pBot->nAimErrNext = nNow + 6 + BotRand(pBot, 14);
            }
            pBot->nAimErr += (pBot->nAimErrGoal - pBot->nAimErr) / 4;
            nLookAng = (getangle(ax - pSprite->x, ay - pSprite->y) + pBot->nAimErr) & 2047;
            nLookZ = SpriteCenterZ(pEnemy);
            // shoot when it thinks it's on target
            const int nOff = klabs(AngDiff(pSprite->ang, nLookAng));
            const bool bSeeNow = !pEnemyPlayer || nNow - pBot->mem[pEnemyPlayer->nPlayer].nTime < 3;
            int nCone = pWeapon->nCone;
            if (nEnemyDist < 1500)
                nCone += nCone / 2;
            bFire = bSeeNow && nOff < nCone && nEnemyDist <= pWeapon->nMaxDist + 2000 && nEnemyDist >= pWeapon->nMinDist * 3 / 4;
            if (nWeapon == kWeaponPitchfork && nEnemyDist > 1000)
                bFire = false;
            // not a machine: now and then it stops shooting for a moment (re-aiming, nerves)
            if (nNow < pBot->nFirePauseUntil)
                bFire = false;
            else if (bFire && nWeapon != kWeaponTNT && BotRand(pBot, 1000) < (100 - pBot->skill.nTrigger) * 2)
                pBot->nFirePauseUntil = nNow + 8 + BotRand(pBot, 16);
            if (bFire)
            {
                switch (nWeapon)
                {
                case kWeaponShotgun:
                    bAltFire = nEnemyDist < 1200 && BotChance(pBot, 40);
                    break;
                case kWeaponTommy:
                    bAltFire = nEnemyDist < 1800 && BotChance(pBot, 8);
                    break;
                case kWeaponTesla:
                    bAltFire = BotChance(pBot, 6);
                    break;
                case kWeaponFlare:
                    bAltFire = nEnemyDist > 2500 && BotChance(pBot, 10);
                    break;
                }
            }
            // friendly fire in teams: don't shoot through a team mate
            if (bFire && gGameOptions.nGameType == kGameTypeTeams)
            {
                for (int p = connecthead; p >= 0 && bFire; p = connectpoint2[p])
                {
                    PLAYER *pMate = &gPlayer[p];
                    if (pMate == pPlayer || IsEnemyPlayer(pPlayer, pMate) || pMate->pXSprite->health <= 0)
                        continue;
                    const int nMateDist = BotDist(pSprite->x, pSprite->y, pMate->pSprite->x, pMate->pSprite->y);
                    if (nMateDist < nEnemyDist && klabs(AngDiff(pSprite->ang, getangle(pMate->pSprite->x - pSprite->x, pMate->pSprite->y - pSprite->y))) < 60)
                        bFire = false;
                }
            }
        }
        else
            nLookAng = pSprite->ang; // hasn't reacted yet
    }
    else
    {
        pBot->nTrackFrames = 0;
        if (nNow >= pBot->nWeaponNext && !pBot->nFireHold)
        {
            // something good in hand for whatever comes next
            const int nBest = BestWeapon(pPlayer, 2500, NULL);
            if (nBest && nBest != nWeapon && WeaponScore(pPlayer, nBest, 2500) > WeaponScore(pPlayer, nWeapon, 2500) * 5 / 4)
                pInput->newWeapon = nBest;
            pBot->nWeaponNext = nNow + 60 + BotRand(pBot, 60);
        }
    }

    // TNT: hold to throw further, then let go
    if (nWeapon == kWeaponTNT)
    {
        if (pBot->nFireHold > 0)
        {
            bFire = true;
            if (--pBot->nFireHold == 0)
            {
                bFire = false;
                pBot->nFireRelease = nNow + 25;
            }
        }
        else if (bFire && nNow >= pBot->nFireRelease)
        {
            pBot->nFireHold = ClipRange((nEnemyDist - 800) / 140, 2, 26);
            bAltFire = false;
        }
        else
            bFire = false;
    }
    else
        pBot->nFireHold = 0;

    // stuck against something, or getting nowhere: jump, open, sidestep, then another way
    if (nMoveSpeed && bPathMove)
    {
        bool bStuck = false;
        // press "use" on whatever is in front (doors, turning walls, switches), but never on a level exit
        const bool bDoorNear = UseCheck(pPlayer) >= 0;
        const int nTargetDist = BotDist(pSprite->x, pSprite->y, tx, ty);
        if (BotDist(tx, ty, pBot->nProgressX, pBot->nProgressY) > 256 || !pBot->nProgressTime)
        {
            pBot->nProgressX = tx, pBot->nProgressY = ty;
            pBot->nProgressDist = nTargetDist;
            pBot->nProgressTime = nNow;
        }
        else if (nTargetDist < pBot->nProgressDist - 48)
        {
            pBot->nProgressDist = nTargetDist;
            pBot->nProgressTime = nNow;
        }
        else if (nNow - pBot->nProgressTime > 30)
        {
            bStuck = true;
            pBot->nProgressTime = nNow;
        }
        if (nNow >= pBot->nStuckCheck)
        {
            const int nMoved = BotDist(pSprite->x, pSprite->y, pBot->nStuckX, pBot->nStuckY);
            if (nMoved < 96 && pBot->nStuckCheck)
                bStuck = true;
            else if (nMoved >= 96 && nNow - pBot->nProgressTime < 20)
                pBot->nStuckCount = 0;
            pBot->nStuckX = pSprite->x;
            pBot->nStuckY = pSprite->y;
            pBot->nStuckCheck = nNow + 15;
        }
        {
            if (bStuck)
            {
                pBot->nStuckCount++;
                switch (pBot->nStuckCount)
                {
                case 1:
                    bWantJump = true;
                    bWantUse = bDoorNear;
                    break;
                case 2:
                    pBot->nUnstickUntil = nNow + 12; // step back
                    pBot->nUnstickStrafe = 1024;
                    bWantUse = bDoorNear;
                    break;
                case 3:
                    pBot->nUnstickUntil = nNow + 15; // and to the side
                    pBot->nUnstickStrafe = BotChance(pBot, 50) ? 512 : -512;
                    bWantUse = bDoorNear;
                    break;
                case 4:
                    if (nPortal >= 0)
                    {
                        BlockWall(pBot, nPortal);
                        if (pBot->nGoalSect >= 0)
                            BotFindPath(pBot, pBot->nGoalSect, pBot->nGoalX, pBot->nGoalY);
                    }
                    break;
                default:
                    pBot->nStuckCount = 0;
                    if (pBot->nGoalSprite >= 0)
                        IgnoreItem(pBot, pBot->nGoalSprite, true);
                    if (pBot->nState == kBotStateCamp && pBot->nCampSpot >= 0 && nCampFails[pBot->nCampSpot] < 255)
                        nCampFails[pBot->nCampSpot]++;
                    pBot->nState = -1;
                    SetState(pBot, kBotStateRoam);
                    pBot->nNextThink = nNow;
                    break;
                }
            }
        }
    }
    else
    {
        pBot->nStuckCheck = 0;
        pBot->nProgressTime = 0;
        if (!nMoveSpeed)
            pBot->nStuckCount = 0;
    }
    if (nMoveSpeed && nNow >= pBot->nUnstickUntil)
        nMoveAng = AvoidWalls(pBot, pSprite, nMoveAng, bPathMove ? BotDist(pSprite->x, pSprite->y, tx, ty) : 400);
    if (nNow < pBot->nUnstickUntil)
    {
        nMoveAng = (nMoveAng + pBot->nUnstickStrafe) & 2047;
        nMoveSpeed = 2048;
    }

    // swimming: up or down towards where it is going
    if (pPlayer->posture == kPostureSwim)
    {
        const int nWantZ = nLookZ != INT32_MIN ? nLookZ : (pBot->nGoalSect >= 0 ? pBot->nGoalZ - (32<<8) : pSprite->z);
        if (nWantZ < pSprite->z - (16<<8))
            bWantJump = true;
        else if (nWantZ > pSprite->z + (16<<8))
            bWantCrouch = true;
    }

    // turn like a hand on a mouse: fast for big turns, settling at the end, never instantly
    const int nTurnDiff = AngDiff(pSprite->ang, nLookAng);
    int nTurn = nTurnDiff * 45 / 100;
    if (klabs(nTurnDiff) < 6)
        nTurn = nTurnDiff;
    nTurn = ClipRange(nTurn, -pBot->skill.nTurnSpeed, pBot->skill.nTurnSpeed);
    pInput->q16turn = fix16_from_int(nTurn);

    // look up or down
    int nWantLook = 0;
    if (nLookZ != INT32_MIN && nEnemyDist > 0)
    {
        // the look angle whose aim slope (horiz) points at the target: horiz = 100*tan(look), slope dz = -horiz*dist/8
        const float fHoriz = float(pPlayer->zWeapon - nLookZ) * 8.f / float(nEnemyDist);
        nWantLook = int(atanf(fHoriz / 100.f) * (1024.f / fPI)) + pBot->nAimErrZ;
        nWantLook = ClipRange(nWantLook, -340, 280);
    }
    const int nLook = fix16_to_int(pPlayer->q16look);
    const int nLookStep = ClipRange((nWantLook - nLook) / 3, -30, 30);
    pInput->q16mlook = fix16_from_int(nLookStep) >> 3;

    // keys
    if (nMoveSpeed)
    {
        const int nRel = (nMoveAng - (pSprite->ang + nTurn)) & 2047;
        pInput->forward = mulscale30(nMoveSpeed, Cos(nRel));
        pInput->strafe = -mulscale30(nMoveSpeed, Sin(nRel));
    }
    pInput->buttonFlags.jump = bWantJump;
    pInput->buttonFlags.crouch = bWantCrouch && !bWantJump;
    pInput->buttonFlags.shoot = bFire && !bAltFire;
    pInput->buttonFlags.shoot2 = bFire && bAltFire;
    if (bWantUse)
        pInput->keyFlags.action = 1;
    // a medkit when it's hurt and safe
    if (pPlayer->packSlots[kPackMedKit].curAmount > 0 && (nHealth >> 4) < 45 && (!pEnemy || nEnemyDist > 5000))
        pInput->useFlags.useMedKit = 1;

    if (gBotDebug == 2 && (nNow % 60) == nPlayer)
    {
        char buffer[160];
        BotDescribe(nPlayer, buffer, sizeof(buffer));
        const int nNext = pBot->nPathPos < pBot->nPathLen ? pBot->path[pBot->nPathPos] : -1;
        LOG_F(INFO, "Bot %s; at %d,%d,%d sector %d, opening %d (%d) to %d, goal sector %d, move %d look %d", buffer, pSprite->x, pSprite->y, pSprite->z, pSprite->sectnum,
            nNext, nNext >= 0 ? EdgeType(nNext) : -1, nNext >= 0 ? wall[nNext].nextsector : -1, pBot->nGoalSect, nMoveSpeed, pSprite->ang);
    }
    if (gBotWatch == nPlayer) // see through its eyes
    {
        gViewIndex = nPlayer;
        gView = pPlayer;
    }
}

//
// debugging aid: what every bot is up to
//

static void BotDescribe(int i, char *pzBuffer, int nSize)
{
    BOT *pBot = &gBot[i];
    PLAYER *pPlayer = &gPlayer[i];
    const char *pzTarget = "-";
    if (pBot->nTarget >= 0)
    {
        PLAYER *pTarget = SpritePlayer(pBot->nTarget);
        pzTarget = pTarget ? gProfile[pTarget->nPlayer].name : "monster";
    }
    const int nGoalDist = pBot->nGoalSect >= 0 && pPlayer->pSprite ? BotDist(pPlayer->pSprite->x, pPlayer->pSprite->y, pBot->nGoalX, pBot->nGoalY) : -1;
    Bsnprintf(pzBuffer, nSize, "%s (%s %s): %s, hp %d, weapon %d, target %s, path %d/%d, goal %d away, frags %d, stuck %d", gProfile[i].name,
        gBotSkillNames[pBot->nSkill], gBotPersonaNames[pBot->nPersona], pBot->nState >= 0 && pBot->nState < kBotStateMax ? pzBotStateNames[pBot->nState] : "?",
        pPlayer->pXSprite ? pPlayer->pXSprite->health >> 4 : 0, pPlayer->curWeapon, pzTarget, pBot->nPathPos, pBot->nPathLen, nGoalDist,
        pPlayer->fragCount, pBot->nStuckCount);
}

void botDrawDebug(void)
{
    if (!gBotMatch || !gBotDebug)
        return;
    int y = windowxy1.y + 30;
    for (int i = connecthead; i >= 0; i = connectpoint2[i])
    {
        if (!botIsBot(i))
            continue;
        char buffer[160];
        BotDescribe(i, buffer, sizeof(buffer));
        Bstrupr(buffer);
        printext256(windowxy1.x + 4 + 1, y + 1, 0, -1, buffer, xdim <= 640);
        printext256(windowxy1.x + 4, y, 31, -1, buffer, xdim <= 640);
        y += xdim <= 640 ? 8 : 10;
    }
}

//
// settings kept in the cfg
//

static int osdcmd_bot_match(osdcmdptr_t parm)
{
    if (gNetMode != NETWORK_NONE)
    {
        OSD_Printf("bot_match: not in a network game\n");
        return OSDCMD_OK;
    }
    if (parm->numparms >= 1)
        BotMatchSetLimits(Batol(parm->parms[0]), parm->numparms >= 2 ? Batol(parm->parms[1]) : -1);
    StartBotMatch(NULL);
    return OSDCMD_OK;
}

static int osdcmd_bot_sector(osdcmdptr_t parm) // map debugging: how bots see a sector's openings
{
    if (parm->numparms < 1)
        return OSDCMD_SHOWHELP;
    const int s = Batol(parm->parms[0]);
    if (s < 0 || s >= numsectors)
        return OSDCMD_OK;
    XSECTOR *pX = sector[s].extra > 0 ? &xsector[sector[s].extra] : NULL;
    LOG_F(INFO, "sector %d: type %d floor %d ceiling %d door %d lift %d x %d rx %d tx %d state %d busy %d push %d wallpush %d locked %d key %d",
        s, sector[s].type, sector[s].floorz, sector[s].ceilingz, bDoorSect[s], IsLift(s), sector[s].extra, pX ? pX->rxID : -1, pX ? pX->txID : -1,
        pX ? pX->state : -1, pX ? pX->busy : -1, pX ? pX->Push : -1, pX ? pX->Wallpush : -1, pX ? pX->locked : -1, pX ? pX->Key : -1);
    for (int w = sector[s].wallptr; w < sector[s].wallptr + sector[s].wallnum; w++)
    {
        XWALL *pXW = wall[w].extra > 0 ? &xwall[wall[w].extra] : NULL;
        LOG_F(INFO, "  wall %d -> sector %d edge %d cstat %d fails %d xwall %d push %d tx %d rx %d", w, wall[w].nextsector,
            wall[w].nextsector >= 0 ? EdgeType(w) : -1, wall[w].cstat, nWallFails[w], wall[w].extra, pXW ? pXW->triggerPush : -1, pXW ? pXW->txID : -1, pXW ? pXW->rxID : -1);
    }
    for (int i = headspritesect[s]; i >= 0; i = nextspritesect[i])
        LOG_F(INFO, "  sprite %d type %d stat %d cstat %d xsprite %d push %d tx %d", i, sprite[i].type, sprite[i].statnum, sprite[i].cstat, sprite[i].extra,
            sprite[i].extra > 0 ? xsprite[sprite[i].extra].Push : -1, sprite[i].extra > 0 ? xsprite[sprite[i].extra].txID : -1);
    return OSDCMD_OK;
}

void botRegisterCvars(void)
{
    OSD_RegisterFunction("bot_sector", "bot_sector <sector>: writes how bots see that sector's openings to the log", osdcmd_bot_sector);
    OSD_RegisterFunction("bot_match", "bot_match [frag limit] [time limit]: starts a bot match with the BOTS menu's settings", osdcmd_bot_match);
    static osdcvardata_t cvars[] = {
        { "bot_count", "number of bots in a bot match", (void *)&gBotCount, CVAR_INT, 1, kMaxPlayers - 1 },
        { "bot_skill", "bot difficulty (0: easy, 1: normal, 2: hard, 3: nightmare)", (void *)&gBotSkill, CVAR_INT, 0, kBotSkillMax - 1 },
        { "bot_personality", "bot personality (0: mixed, 1: balanced, 2: rusher, 3: camper, 4: trickster, 5: coward, 6: jumper)", (void *)&gBotPersona, CVAR_INT, 0, kBotPersonaMax - 1 },
        { "bot_accuracy", "bot aim, percent of the difficulty's", (void *)&gBotAccuracy, CVAR_INT, 10, 300 },
        { "bot_reaction", "bot reaction time, percent of the difficulty's", (void *)&gBotReaction, CVAR_INT, 10, 300 },
        { "bot_aggression", "bot aggression, percent of the personality's", (void *)&gBotAggression, CVAR_INT, 0, 200 },
        { "bot_jumping", "how much bots jump around, percent of the personality's", (void *)&gBotJumping, CVAR_INT, 0, 200 },
        { "bot_ambush", "how often bots ambush, percent of the personality's", (void *)&gBotAmbush, CVAR_INT, 0, 200 },
        { "bot_flank", "how often bots go round to flank, percent of the personality's", (void *)&gBotFlank, CVAR_INT, 0, 200 },
        { "bot_camping", "how much bots camp, percent of the personality's", (void *)&gBotCamping, CVAR_INT, 0, 200 },
        { "bot_taunts", "bots talk and taunt", (void *)&gBotTaunts, CVAR_BOOL, 0, 1 },
        { "bot_handicap", "how much damage bots take (0: much less, 2: normal, 4: much more)", (void *)&gBotHandicap, CVAR_INT, 0, 4 },
        { "bot_cultists", "half the bots look like cultists", (void *)&gBotCultists, CVAR_BOOL, 0, 1 },
        { "bot_debug", "show what every bot is doing (1: on screen, 2: also in the log every 2 seconds)", (void *)&gBotDebug, CVAR_INT, 0, 2 },
        { "bot_watch", "watch the game through a bot's eyes (0: off, 1-7: that bot)", (void *)&gBotWatch, CVAR_INT|CVAR_NOSAVE, 0, kBotMaxBots },
    };
    for (auto &cv : cvars)
        OSD_RegisterCvar(&cv, osdcmd_cvar_set);
#define BOTEACHCVARS(n) \
        { "bot_skill_" #n, "this bot's difficulty (-1: same as bot_skill)", (void *)&gBotEachSkill[n-1], CVAR_INT, -1, kBotSkillMax - 1 }, \
        { "bot_personality_" #n, "this bot's personality (-1: same as bot_personality)", (void *)&gBotEachPersona[n-1], CVAR_INT, -1, kBotPersonaMax - 1 },
    static osdcvardata_t eachCvars[] = {
        BOTEACHCVARS(1) BOTEACHCVARS(2) BOTEACHCVARS(3) BOTEACHCVARS(4) BOTEACHCVARS(5) BOTEACHCVARS(6) BOTEACHCVARS(7)
    };
#undef BOTEACHCVARS
    for (auto &cv : eachCvars)
        OSD_RegisterCvar(&cv, osdcmd_cvar_set);
}
