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

#include <time.h>
#include <vector>

#include "compat.h"
#include "build.h"
#include "crc32.h"
#include "mmulti.h"
#include "common_game.h"
#include "actor.h"
#include "blood.h"
#include "config.h"
#include "bot.h"
#include "coopsave.h"
#include "demo.h"
#include "gamemenu.h"
#include "globals.h"
#include "levels.h"
#include "loadsave.h"
#include "menu.h"
#include "network.h"
#include "player.h"
#include "screen.h"
#include "seq.h"
#include "view.h"
#ifdef NOONE_EXTENSIONS
#include "nnexts.h"
#endif

void playerRefreshItemBackup(PLAYER *pPlayer);

int gCoopMatchOrder = 0;
int gCoopAutosaveLeave = 1;

namespace
{

const int kCoopSaveVersion = 1;

bool fileExists(const char *pzPath)
{
    FILE *fp = fopen(pzPath, "rb");
    if (fp)
        fclose(fp);
    return fp != NULL;
}

const char *const kTypeDirs[kCoopSaveTypes] = { "coopsaves/carryover", "coopsaves/midlevel" };
const char *const kAutoNames[kCoopSaveTypes] = { "levelstart", "playerleft" };

enum
{
    kMidLevelMatchOrder = 1,  // packet 243 flags
};

struct COOPLOADOUT
{
    bool bValid;
    int  nSlot;                     // connect index when it was saved
    char szName[MAXPLAYERNAME];
    int  nHealth;                   // xsprite health (<<4)
    int  nWeapon;
    int  nPackItem;
    bool hasWeapon[kWeaponMax];
    int  ammoCount[12];
    int  armor[3];
    int  packAmount[kPackMax];
    int  packTime[4];               // diving suit, crystal ball, beast vision, jump boots
};

const int kPackPowerUps[4] = { kPwUpDivingSuit, kPwUpCrystalBall, kPwUpBeastVision, kPwUpJumpBoots };

struct COOPSAVE
{
    PKT_STARTGAME settings;
    char szSaved[32];
    char szSavedBy[MAXPLAYERNAME];
    char szTitle[32];
    COOPLOADOUT players[kMaxPlayers];
    int  nPlayers;
};

// loadouts for the next StartLevel(), indexed by connect index (same on every machine)
COOPLOADOUT gCoopLoadout[kMaxPlayers];
bool gCoopPending;

// mid-level load in progress
std::vector<char> gMidFile;      // the host's save, as it arrives
bool gMidLoadPending;
char gMidLoadPath[BMAX_PATH];
PKT_STARTGAME gMidSettings;
int gMidFlags;
char gMidNames[kMaxPlayers][MAXPLAYERNAME];  // saved players by connect index ("" for none)

// every start game setting, by name, so saves survive changes to the packet layout
#define COOP_SETTINGS(X) \
    X(gameType) X(difficulty) X(monsterSettings) X(weaponSettings) X(itemSettings) X(respawnSettings) \
    X(keySettings) X(itemWeaponSettings) X(randomizerMode) X(episodeId) X(levelId) X(uNetGameFlags) \
    X(userMap) X(monsterQuantity) X(monsterHealth) X(monsterSpeed) X(bQuadDamagePowerup) X(nBeastVision) \
    X(nDamageInvul) X(nProjectileBehavior) X(bNapalmFalloff) X(nEnemyBehavior) X(bEnemyRandomTNT) \
    X(nWeaponsVer) X(bWeaponImpulseLimit) X(nAmmoScale) X(bSectorBehavior) X(nHitscanProjectiles) \
    X(nGoreBehavior) X(nPlayerSpeed) X(nPlayerWater) X(bFriendlyFire) X(bAutoTeams) X(nSpawnProtection) \
    X(nSpawnWeapon) X(uSpriteBannedFlags)

void makeDirs(void)
{
    char path[BMAX_PATH];
    G_ModDirSnprintfLite(path, sizeof(path), "coopsaves");
    Bmkdir(path, S_IRWXU);
    for (int i = 0; i < kCoopSaveTypes; i++)
    {
        G_ModDirSnprintfLite(path, sizeof(path), kTypeDirs[i]);
        Bmkdir(path, S_IRWXU);
    }
}

// coopsaves/<type>/slot<N>.<ext>, or the type's automatic save for kCoopSlotAuto
bool coopPath(int nType, int nSlot, const char *pzExt, char *pzPath)
{
    char name[64];
    if (nSlot == kCoopSlotAuto)
        Bsnprintf(name, sizeof(name), "%s/%s.%s", kTypeDirs[nType], kAutoNames[nType], pzExt);
    else
        Bsnprintf(name, sizeof(name), "%s/slot%d.%s", kTypeDirs[nType], nSlot+1, pzExt);
    pzPath[0] = '\0';
    G_ModDirSnprintfLite(pzPath, BMAX_PATH, name);
    return pzPath[0] != '\0';
}

void writeInts(FILE *fp, const char *pzKey, const int *pValues, int nCount)
{
    fprintf(fp, "%s=", pzKey);
    for (int i = 0; i < nCount; i++)
        fprintf(fp, i ? " %d" : "%d", pValues[i]);
    fprintf(fp, "\n");
}

void readInts(const char *pzValue, int *pValues, int nCount)
{
    for (int i = 0; i < nCount; i++)
    {
        char *pEnd;
        pValues[i] = strtol(pzValue, &pEnd, 10);
        if (pEnd == pzValue)
            break;
        pzValue = pEnd;
    }
}

void fillLoadout(COOPLOADOUT *pLoadout, int nPlayer)
{
    PLAYER *pPlayer = &gPlayer[nPlayer];
    *pLoadout = {};
    pLoadout->bValid = true;
    pLoadout->nSlot = nPlayer;
    Bstrncpyz(pLoadout->szName, gProfile[nPlayer].name, sizeof(pLoadout->szName));

    // a dead player comes back with full health, like when respawning
    int nHealth = pPlayer->pXSprite ? pPlayer->pXSprite->health : 0;
    if (nHealth <= 0 && pPlayer->pDudeInfo)
        nHealth = pPlayer->pDudeInfo->startHealth << 4;
    pLoadout->nHealth = nHealth;

    // at a level start the weapon isn't raised yet, only requested
    pLoadout->nWeapon = pPlayer->curWeapon;
    if (pLoadout->nWeapon == kWeaponNone)
        pLoadout->nWeapon = pPlayer->input.newWeapon ? pPlayer->input.newWeapon : pPlayer->nextWeapon;
    pLoadout->nPackItem = pPlayer->packItemId;
    for (int i = 0; i < kWeaponMax; i++)
        pLoadout->hasWeapon[i] = pPlayer->hasWeapon[i];
    for (int i = 0; i < 12; i++)
        pLoadout->ammoCount[i] = pPlayer->ammoCount[i];
    for (int i = 0; i < 3; i++)
        pLoadout->armor[i] = pPlayer->armor[i];
    for (int i = 0; i < kPackMax; i++)
        pLoadout->packAmount[i] = pPlayer->packSlots[i].curAmount;
    for (int i = 0; i < 4; i++)
        pLoadout->packTime[i] = pPlayer->pwUpTime[kPackPowerUps[i]];
}

bool writeTextSave(const char *pzPath, COOPSAVE const *pSave)
{
    FILE *fp = fopen(pzPath, "w");
    if (!fp)
    {
        LOG_F(ERROR, "Could not write co-op save \"%s\"", pzPath);
        return false;
    }

    PKT_STARTGAME const &s = pSave->settings;
    fprintf(fp, "; NotBlood co-op save: settings and loadouts for continuing a co-op game.\n");
    fprintf(fp, "; Continue it from the multiplayer menu: NEW GAME > LOAD CO-OP.\n");
    fprintf(fp, "version=%d\n", kCoopSaveVersion);
    fprintf(fp, "saved=%s\n", pSave->szSaved);
    fprintf(fp, "savedby=%s\n", pSave->szSavedBy);
    fprintf(fp, "title=%s\n", pSave->szTitle);
#define WRITE_SETTING(f) fprintf(fp, #f "=%lld\n", (long long)s.f);
    COOP_SETTINGS(WRITE_SETTING)
#undef WRITE_SETTING
    fprintf(fp, "userMapName=%s\n", s.userMapName);
    fprintf(fp, "szRandomizerSeed=%s\n", s.szRandomizerSeed);

    for (int i = 0; i < pSave->nPlayers; i++)
    {
        COOPLOADOUT const &l = pSave->players[i];
        int hasWeapon[kWeaponMax];
        for (int w = 0; w < kWeaponMax; w++)
            hasWeapon[w] = l.hasWeapon[w];

        fprintf(fp, "\n[player]\n");
        fprintf(fp, "slot=%d\n", l.nSlot);
        fprintf(fp, "name=%s\n", l.szName);
        fprintf(fp, "health=%d\n", l.nHealth);
        fprintf(fp, "weapon=%d\n", l.nWeapon);
        fprintf(fp, "packitem=%d\n", l.nPackItem);
        writeInts(fp, "weapons", hasWeapon, kWeaponMax);
        writeInts(fp, "ammo", l.ammoCount, 12);
        writeInts(fp, "armor", l.armor, 3);
        writeInts(fp, "pack", l.packAmount, kPackMax);
        writeInts(fp, "packtime", l.packTime, 4);
    }

    fclose(fp);
    return true;
}

bool readTextSave(const char *pzPath, COOPSAVE *pSave)
{
    *pSave = {};

    FILE *fp = fopen(pzPath, "r");
    if (!fp)
        return false;

    PKT_STARTGAME &s = pSave->settings;
    COOPLOADOUT *pLoadout = NULL;
    int nVersion = 0;
    char line[512];

    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == ';' || line[0] == '\0')
            continue;

        if (!strcmp(line, "[player]"))
        {
            pLoadout = (pSave->nPlayers < kMaxPlayers) ? &pSave->players[pSave->nPlayers++] : NULL;
            if (pLoadout)
            {
                *pLoadout = {};
                pLoadout->bValid = true;
                pLoadout->nSlot = -1;
                pLoadout->nPackItem = -1;
                pLoadout->hasWeapon[kWeaponPitchfork] = true;
            }
            continue;
        }

        char *pValue = strchr(line, '=');
        if (!pValue)
            continue;
        *pValue++ = '\0';
        const char *pKey = line;

        if (pLoadout)
        {
            int hasWeapon[kWeaponMax] = {};
            if (!strcmp(pKey, "slot")) pLoadout->nSlot = atoi(pValue);
            else if (!strcmp(pKey, "name")) Bstrncpyz(pLoadout->szName, pValue, sizeof(pLoadout->szName));
            else if (!strcmp(pKey, "health")) pLoadout->nHealth = atoi(pValue);
            else if (!strcmp(pKey, "weapon")) pLoadout->nWeapon = atoi(pValue);
            else if (!strcmp(pKey, "packitem")) pLoadout->nPackItem = atoi(pValue);
            else if (!strcmp(pKey, "weapons"))
            {
                readInts(pValue, hasWeapon, kWeaponMax);
                for (int w = 0; w < kWeaponMax; w++)
                    pLoadout->hasWeapon[w] = hasWeapon[w] != 0;
            }
            else if (!strcmp(pKey, "ammo")) readInts(pValue, pLoadout->ammoCount, 12);
            else if (!strcmp(pKey, "armor")) readInts(pValue, pLoadout->armor, 3);
            else if (!strcmp(pKey, "pack")) readInts(pValue, pLoadout->packAmount, kPackMax);
            else if (!strcmp(pKey, "packtime")) readInts(pValue, pLoadout->packTime, 4);
            continue;
        }

        if (!strcmp(pKey, "version")) nVersion = atoi(pValue);
        else if (!strcmp(pKey, "saved")) Bstrncpyz(pSave->szSaved, pValue, sizeof(pSave->szSaved));
        else if (!strcmp(pKey, "savedby")) Bstrncpyz(pSave->szSavedBy, pValue, sizeof(pSave->szSavedBy));
        else if (!strcmp(pKey, "title")) Bstrncpyz(pSave->szTitle, pValue, sizeof(pSave->szTitle));
        else if (!strcmp(pKey, "userMapName")) Bstrncpyz(s.userMapName, pValue, sizeof(s.userMapName));
        else if (!strcmp(pKey, "szRandomizerSeed")) Bstrncpyz(s.szRandomizerSeed, pValue, sizeof(s.szRandomizerSeed));
#define READ_SETTING(f) else if (!strcmp(pKey, #f)) s.f = (decltype(s.f))strtoll(pValue, NULL, 10);
        COOP_SETTINGS(READ_SETTING)
#undef READ_SETTING
    }

    fclose(fp);

    if (nVersion < 1 || nVersion > kCoopSaveVersion || s.gameType != kGameTypeCoop)
    {
        LOG_F(WARNING, "Ignoring co-op save \"%s\" (unsupported version or not a co-op game)", pzPath);
        return false;
    }
    return true;
}

bool readSave(int nType, int nSlot, COOPSAVE *pSave)
{
    char path[BMAX_PATH];
    return coopPath(nType, nSlot, "txt", path) && readTextSave(path, pSave);
}

// the current game as a carry-over save
void buildSave(COOPSAVE *pSave, const char *pzSavedBy)
{
    *pSave = {};
    pSave->settings = gPacketStartGame;
    pSave->settings.gameType = kGameTypeCoop;
    pSave->settings.episodeId = gGameOptions.nEpisode;
    pSave->settings.levelId = gGameOptions.nLevel;

    time_t const now = time(NULL);
    strftime(pSave->szSaved, sizeof(pSave->szSaved), "%Y-%m-%d %H:%M", localtime(&now));
    Bstrncpyz(pSave->szSavedBy, pzSavedBy ? pzSavedBy : "", sizeof(pSave->szSavedBy));

    char const *pzTitle = gGameOptions.zLevelName;
    if (!gPacketStartGame.userMap && gGameOptions.nEpisode >= 0 && gGameOptions.nEpisode < gEpisodeCount
        && gEpisodeInfo[gGameOptions.nEpisode].levelsInfo[gGameOptions.nLevel].Title[0])
        pzTitle = gEpisodeInfo[gGameOptions.nEpisode].levelsInfo[gGameOptions.nLevel].Title;
    Bstrncpyz(pSave->szTitle, pzTitle, sizeof(pSave->szTitle));

    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (pSave->nPlayers < kMaxPlayers)
            fillLoadout(&pSave->players[pSave->nPlayers++], p);
}

// positions and health of everything in the level, to compare saves and loads in the log
uint32_t worldHash(void)
{
    uint32_t crc = 0;
    for (int i = 0; i < kMaxSprites; i++)
    {
        spritetype const *pSprite = &sprite[i];
        if (pSprite->statnum >= kMaxStatus)
            continue;
        int const data[6] = { i, pSprite->x, pSprite->y, pSprite->z, pSprite->type,
                              pSprite->extra > 0 ? xsprite[pSprite->extra].health : 0 };
        crc = Bcrc32(data, sizeof(data), crc);
    }
    return crc;
}

bool canSave(void)
{
    // not with bots: the save would need them back in the same player slots
    return gGameOptions.nGameType == kGameTypeCoop && gGameStarted && !gDemo.bPlaying && !botAny();
}

// writes slot nSlot: the carry-over save, and the mid-level save with a copy of it for the menu
bool writeSlot(int nSlot, const char *pzSavedBy, bool bCarry, bool bMid)
{
    if (!canSave())
        return false;
    makeDirs();

    static COOPSAVE save;
    buildSave(&save, pzSavedBy);
    bool bOk = true;
    char path[BMAX_PATH];
    if (bCarry)
        bOk &= coopPath(kCoopSaveCarry, nSlot, "txt", path) && writeTextSave(path, &save);
    if (bMid && coopPath(kCoopSaveMid, nSlot, "sav", path))
    {
        LoadSave::SaveGame(path);
        bOk &= coopPath(kCoopSaveMid, nSlot, "txt", path) && writeTextSave(path, &save);
    }
    if (bOk)
        LOG_F(INFO, "Wrote co-op save %s %d (E%dM%d, %d players, level time %d, world %08x)", nSlot == kCoopSlotAuto ? "auto" : "slot", nSlot+1,
              save.settings.episodeId+1, save.settings.levelId+1, save.nPlayers, gLevelTime, worldHash());
    return bOk;
}

// pick the saved loadout for every connected player: same name; with gCoopMatchOrder, players
// without one get the loadouts nobody took, in order
void matchLoadouts(COOPSAVE const *pSave)
{
    bool bUsed[kMaxPlayers] = {};
    bool bAssigned[kMaxPlayers] = {};

    for (int p = 0; p < kMaxPlayers; p++)
        gCoopLoadout[p] = {};

    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        for (int i = 0; i < pSave->nPlayers; i++)
            if (!bUsed[i] && !Bstrcasecmp(pSave->players[i].szName, gProfile[p].name))
            {
                gCoopLoadout[p] = pSave->players[i];
                bUsed[i] = bAssigned[p] = true;
                break;
            }

    if (gCoopMatchOrder)
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            for (int i = 0; i < pSave->nPlayers && !bAssigned[p]; i++)
                if (!bUsed[i])
                {
                    gCoopLoadout[p] = pSave->players[i];
                    bUsed[i] = bAssigned[p] = true;
                }

    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (bAssigned[p])
            LOG_F(INFO, "Co-op continue: player %d (%s) gets the loadout saved for \"%s\"", p, gProfile[p].name, gCoopLoadout[p].szName);
        else
            LOG_F(INFO, "Co-op continue: player %d (%s) is not in the save, starts with the pitchfork", p, gProfile[p].name);
}

void broadcastLoadout(int nPlayer)
{
    COOPLOADOUT const &l = gCoopLoadout[nPlayer];
    char *pPacket = packet;
    PutPacketByte(pPacket, 246);
    PutPacketByte(pPacket, nPlayer);
    PutPacketDWord(pPacket, l.nHealth);
    PutPacketByte(pPacket, l.nWeapon);
    PutPacketDWord(pPacket, l.nPackItem);
    for (int i = 0; i < kWeaponMax; i++)
        PutPacketByte(pPacket, l.hasWeapon[i]);
    for (int i = 0; i < 12; i++)
        PutPacketDWord(pPacket, l.ammoCount[i]);
    for (int i = 0; i < 3; i++)
        PutPacketDWord(pPacket, l.armor[i]);
    for (int i = 0; i < kPackMax; i++)
        PutPacketDWord(pPacket, l.packAmount[i]);
    for (int i = 0; i < 4; i++)
        PutPacketDWord(pPacket, l.packTime[i]);
    netSendPacketAll(packet, pPacket - packet);
}

// ---- mid-level saves -----------------------------------------------------------------------

// header written by MyLoadSave::Save(): 'VSBN', BYTEVERSION, CRC of the INI name
bool checkSaveHeader(const char *pData, int nSize, char *pzError, int nErrorSize)
{
    if (nSize < 10)
    {
        Bsnprintf(pzError, nErrorSize, "The save is damaged");
        return false;
    }
    int id;
    short version;
    uint32_t uINICRC;
    memcpy(&id, pData, 4);
    memcpy(&version, pData + 4, 2);
    memcpy(&uINICRC, pData + 6, 4);
    if (id != 0x5653424e || version != BYTEVERSION)
    {
        Bsnprintf(pzError, nErrorSize, "The save is from another NotBlood version");
        return false;
    }
    if (uINICRC != Bcrc32(pINISelected->zName, (unsigned int)BMAX_PATH, 0))
    {
        Bsnprintf(pzError, nErrorSize, "The save is from another episode or add-on");
        return false;
    }
    return true;
}

void killSpriteNow(int nSprite)
{
    if (nSprite < 0 || nSprite >= kMaxSprites || sprite[nSprite].statnum >= kMaxStatus)
        return;
    if (sprite[nSprite].extra > 0)
        seqKill(3, sprite[nSprite].extra);
    sprite[nSprite].type = 0;  // no longer a player, whatever happens before it is gone
    actPostSprite(nSprite, kStatFree);
    actPostProcess();
}

// After loading the host's save every machine runs this with the same input: saved players
// are moved to the connect index of the player with the same name; saved players nobody is
// playing leave, new players start at the level start.
void remapPlayers(PROFILE const *pSession, char const (*pNames)[MAXPLAYERNAME], bool bMatchOrder)
{
    static PLAYER saved[kMaxPlayers];
    memcpy(saved, gPlayer, sizeof(saved));
    int scores[kMaxPlayers];
    memcpy(scores, gPlayerScores, sizeof(scores));
#ifdef NOONE_EXTENSIONS
    static TRPLAYERCTRL ctrl[kMaxPlayers];
    memcpy(ctrl, gPlayerCtrl, sizeof(ctrl));
#endif
    // the names come from the save's description: LoadGame() has already put this machine's own
    // name into its gProfile entry
    char names[kMaxPlayers][MAXPLAYERNAME];
    bool bSaved[kMaxPlayers];
    for (int i = 0; i < kMaxPlayers; i++)
    {
        Bstrncpyz(names[i], pNames[i], MAXPLAYERNAME);
        int const nSprite = saved[i].nSprite;  // the save has the saving machine's pointers, use the index
        bSaved[i] = names[i][0] && nSprite >= 0 && nSprite < kMaxSprites && sprite[nSprite].statnum < kMaxStatus
            && sprite[nSprite].type == kDudePlayer1 + i;
    }

    int map[kMaxPlayers], inv[kMaxPlayers];
    for (int i = 0; i < kMaxPlayers; i++)
        map[i] = inv[i] = -1;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        for (int i = 0; i < kMaxPlayers; i++)
            if (bSaved[i] && inv[i] < 0 && !Bstrcasecmp(names[i], pSession[p].name))
            {
                map[p] = i;
                inv[i] = p;
                break;
            }
    if (bMatchOrder)
        for (int p = connecthead; p >= 0; p = connectpoint2[p])
            for (int i = 0; i < kMaxPlayers && map[p] < 0; i++)
                if (bSaved[i] && inv[i] < 0)
                {
                    map[p] = i;
                    inv[i] = p;
                }

    // saved players nobody plays now leave the level
    for (int i = 0; i < kMaxPlayers; i++)
    {
        if (!bSaved[i] || inv[i] >= 0)
            continue;
        int const nSprite = saved[i].nSprite;
        LOG_F(INFO, "Co-op load: \"%s\" is not here, removed from the level", names[i]);
        for (int s = headspritestat[kStatDude]; s >= 0; s = nextspritestat[s])
            if (sprite[s].extra > 0 && xsprite[sprite[s].extra].target == nSprite)
                xsprite[sprite[s].extra].target = -1;
        killSpriteNow(nSprite);
    }

    for (int p = 0; p < kMaxPlayers; p++)
    {
        if (map[p] >= 0)
        {
            gPlayer[p] = saved[map[p]];
            gPlayerScores[p] = scores[map[p]];
#ifdef NOONE_EXTENSIONS
            gPlayerCtrl[p] = ctrl[map[p]];
#endif
            for (int q = 0; q < kMaxPlayers; q++)
                gPlayer[p].fragInfo[q] = map[q] >= 0 ? saved[map[p]].fragInfo[map[q]] : 0;
            gPlayer[p].nPlayer = p;
            spritetype *pSprite = &sprite[gPlayer[p].nSprite];
            pSprite->type = kDudePlayer1 + p;
            gPlayer[p].pSprite = pSprite;
            gPlayer[p].pXSprite = &xsprite[pSprite->extra];
            gPlayer[p].pDudeInfo = &dudeInfo[pSprite->type - kDudeBase];
        }
        else
        {
            memset((void *)&gPlayer[p], 0, sizeof(PLAYER));
            gPlayerScores[p] = 0;
        }
    }

    // things owned by a player store its connect index
    for (int s = 0; s < kMaxSprites; s++)
    {
        spritetype *pSprite = &sprite[s];
        if (pSprite->statnum >= kMaxStatus || pSprite->owner < 0 || !(pSprite->owner & kMaxSprites))
            continue;
        int const nOld = pSprite->owner & (kMaxSprites-1);
        pSprite->owner = (nOld < kMaxPlayers && inv[nOld] >= 0) ? (inv[nOld] | kMaxSprites) : -1;
    }

    gNetPlayers = numplayers;
    memcpy(gProfile, pSession, sizeof(gProfile));

    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        if (map[p] >= 0)
        {
            LOG_F(INFO, "Co-op load: player %d (%s) continues as \"%s\"", p, gProfile[p].name, names[map[p]]);
            continue;
        }
        LOG_F(INFO, "Co-op load: player %d (%s) is new, starts at the level start", p, gProfile[p].name);
        playerInit(p, 0);
        playerStart(p, 1);
    }

    gViewIndex = myconnectindex;
    gMe = gView = &gPlayer[myconnectindex];
    viewInitializePrediction();
}

void midLevelExecute(void)
{
    gMidLoadPending = false;

    // nobody sends game input from before the load any more once everybody is here
    netWaitForEveryone(0);
    if (gRestartGame || gQuitGame)
        return;

    static PROFILE session[kMaxPlayers];
    memcpy(session, gProfile, sizeof(session));
    bool const bWasStarted = gGameStarted;
    memset(xvel, 0, sizeof(xvel[0])*kMaxSprites);
    memset(yvel, 0, sizeof(yvel[0])*kMaxSprites);
    memset(zvel, 0, sizeof(zvel[0])*kMaxSprites);

    viewLoadingScreen(gMenuPicnum, "Loading", "Loading Co-op Game", NULL);
    videoNextPage();

    // LoadGame() works on the player of this machine (view, prediction) and announces its
    // profile; a player who isn't in the save has no player there yet, and the profiles belong
    // to this session. So it runs as one of the saved players, without network.
    int const nMe = myconnectindex;
    NETWORKMODE const nNetMode = gNetMode;
    for (int i = 0; i < kMaxPlayers; i++)
        if (gMidNames[i][0])
        {
            myconnectindex = i;
            break;
        }
    gMe = gView = &gPlayer[myconnectindex];
    gNetMode = NETWORK_NONE;
    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;
    gGameStarted = 1;  // LoadGame() would wait for the other players itself otherwise
    LoadSave::LoadGame(gMidLoadPath);
    pathsearchmode = bakpathsearchmode;
    gNetMode = nNetMode;
    myconnectindex = nMe;
    gMe = gView = &gPlayer[myconnectindex];
    if (!bWasStarted)
        scrLoadPLUs();
    LOG_F(INFO, "Co-op load: level time %d, world %08x", gLevelTime, worldHash());

    remapPlayers(session, gMidNames, (gMidFlags & kMidLevelMatchOrder) != 0);
    gPacketStartGame.gameType = kGameTypeCoop;
    PKT_STARTGAME const prev = gPacketStartGame;
    gPacketStartGame = gMidSettings;
    gPacketStartGame.version = prev.version;
    gPacketStartGame.bCoopContinue = 0;
    gAutosaveInCurLevel = false;

    netWaitForEveryone(0);
    totalclock = 0;
    gNetFifoClock = 0;
    gPaused = 0;
    ready2send = 1;
    gStartNewGame = 0;
    gGameMenuMgr.Deactivate();
    viewSetMessage("Co-op game loaded");
}

} // namespace

void coopWriteLevelStartSave(void)
{
    writeSlot(kCoopSlotAuto, NULL, true, false);
}

void coopRequestSave(int nSlot)
{
    if (!canSave())
    {
        viewSetMessage("Co-op games can only be saved during a co-op game");
        return;
    }
    nSlot = ClipRange(nSlot, 0, kCoopSlots-1);

    if (numplayers > 1)
    {
        char *pPacket = packet;
        PutPacketByte(pPacket, 245);
        PutPacketByte(pPacket, nSlot);
        netSendPacketAll(packet, pPacket - packet);
    }

    char buffer[128];
    if (writeSlot(nSlot, gProfile[myconnectindex].name, true, true))
        Bsnprintf(buffer, sizeof(buffer), "Co-op game saved to slot %d for all players", nSlot+1);
    else
        Bsnprintf(buffer, sizeof(buffer), "Could not save the co-op game");
    viewSetMessage(buffer);
}

void coopReceiveSaveRequest(int nPlayer, char *pPacket)
{
    if (nPlayer < 0 || nPlayer >= kMaxPlayers)
        return;
    int const nSlot = ClipRange(GetPacketByte(pPacket), 0, kCoopSlots-1);

    char buffer[128];
    if (writeSlot(nSlot, gProfile[nPlayer].name, true, true))
        Bsnprintf(buffer, sizeof(buffer), "%s saved the co-op game (slot %d)", gProfile[nPlayer].name, nSlot+1);
    else
        Bsnprintf(buffer, sizeof(buffer), "%s saved the co-op game (failed here)", gProfile[nPlayer].name);
    viewSetMessage(buffer);
}

void coopPlayerLeft(int nPlayer)
{
    if (!gCoopAutosaveLeave || !canSave() || numplayers < 2)
        return;
    if (writeSlot(kCoopSlotAuto, gProfile[nPlayer].name, false, true))
        LOG_F(INFO, "%s left: co-op game saved (mid-level, \"player left\")", gProfile[nPlayer].name);
}

bool coopDescribeSave(int nType, int nSlot, char *pzLevel, int nLevelSize, char *pzInfo, int nInfoSize)
{
    static COOPSAVE save;
    char path[BMAX_PATH];
    bool bOk = readSave(nType, nSlot, &save);
    if (bOk && nType == kCoopSaveMid)
        bOk = coopPath(kCoopSaveMid, nSlot, "sav", path) && fileExists(path);
    if (!bOk)
    {
        Bsnprintf(pzLevel, nLevelSize, "EMPTY");
        pzInfo[0] = '\0';
        return false;
    }

    if (save.settings.userMap)
        Bsnprintf(pzLevel, nLevelSize, "%s", save.szTitle);
    else
        Bsnprintf(pzLevel, nLevelSize, "E%dM%d %s", save.settings.episodeId+1, save.settings.levelId+1, save.szTitle);
    // "10-11 18:42 2P": skip the year
    Bsnprintf(pzInfo, nInfoSize, "%s %dP", save.szSaved[0] ? save.szSaved + 5 : "", save.nPlayers);
    Bstrupr(pzLevel);
    return true;
}

bool coopContinueGame(int nSlot)
{
    static COOPSAVE save;
    if (!readSave(kCoopSaveCarry, nSlot, &save))
        return false;

    PKT_STARTGAME const prev = gPacketStartGame;
    gPacketStartGame = save.settings;
    gPacketStartGame.version = prev.version;
    gPacketStartGame.bCoopContinue = 1;

    matchLoadouts(&save);
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (gCoopLoadout[p].bValid && numplayers > 1)
            broadcastLoadout(p);
    gCoopPending = true;

    LOG_F(INFO, "Continuing co-op game from carry-over slot %d (E%dM%d, saved %s)", nSlot+1,
          gPacketStartGame.episodeId+1, gPacketStartGame.levelId+1, save.szSaved);

    SetGameVanillaMode(0); // as StartNetGame()
    botNetNoBots(); // a continued co-op game has no bots: they aren't in the save
    netBroadcastNewGame();
    gStartNewGame = 1;
    gGameMenuMgr.Deactivate();
    return true;
}

bool coopLoadMidLevel(int nSlot)
{
    static COOPSAVE save;
    char path[BMAX_PATH];
    if (!readSave(kCoopSaveMid, nSlot, &save) || !coopPath(kCoopSaveMid, nSlot, "sav", path))
        return false;
    if (botAny()) // the save has the players only; start a game without bots first
    {
        viewSetMessage("Start a game without bots to load this save");
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    std::vector<char> data;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        data.insert(data.end(), buf, buf + n);
    fclose(fp);

    char error[96];
    if (!checkSaveHeader(data.data(), (int)data.size(), error, sizeof(error)))
    {
        viewSetMessage(error);
        LOG_F(ERROR, "Co-op load: %s", error);
        return false;
    }

    int const nFlags = gCoopMatchOrder ? kMidLevelMatchOrder : 0;
    memset(gMidNames, 0, sizeof(gMidNames));
    for (int i = 0; i < save.nPlayers; i++)
        if (save.players[i].nSlot >= 0 && save.players[i].nSlot < kMaxPlayers)
            Bstrncpyz(gMidNames[save.players[i].nSlot], save.players[i].szName[0] ? save.players[i].szName : "?", MAXPLAYERNAME);
    uint32_t const uCRC = Bcrc32(data.data(), (int)data.size(), 0);
    if (numplayers > 1)
    {
        // 244: offset, then up to 512 bytes of the file
        int const nChunk = 512;
        for (int nOffset = 0; nOffset < (int)data.size(); nOffset += nChunk)
        {
            int const nLen = ClipHigh(nChunk, (int)data.size() - nOffset);
            char *pPacket = packet;
            PutPacketByte(pPacket, 244);
            PutPacketDWord(pPacket, nOffset);
            PutPacketWord(pPacket, nLen);
            PutPacketBuffer(pPacket, data.data() + nOffset, nLen);
            netSendPacketAll(packet, pPacket - packet);
        }
        // 243: size, CRC, flags, the game settings of the save and its players' names
        static_assert(10 + sizeof(PKT_STARTGAME) + sizeof(gMidNames) <= sizeof(packet), "co-op load packet too big");
        char *pPacket = packet;
        PutPacketByte(pPacket, 243);
        PutPacketDWord(pPacket, (int)data.size());
        PutPacketDWord(pPacket, (int)uCRC);
        PutPacketByte(pPacket, nFlags);
        PutPacketBuffer(pPacket, &save.settings, sizeof(PKT_STARTGAME));
        PutPacketBuffer(pPacket, gMidNames, sizeof(gMidNames));
        netSendPacketAll(packet, pPacket - packet);
    }

    LOG_F(INFO, "Loading co-op game from mid-level slot %d for everybody (%d bytes)", nSlot+1, (int)data.size());
    Bstrncpyz(gMidLoadPath, path, sizeof(gMidLoadPath));
    gMidSettings = save.settings;
    gMidFlags = nFlags;
    gMidLoadPending = true;
    gGameMenuMgr.Deactivate();
    return true;
}

void coopReceiveFilePart(char *pPacket)
{
    int const nOffset = GetPacketDWord(pPacket);
    int const nLen = (uint16_t)GetPacketWord(pPacket);
    if (nOffset < 0 || nOffset > (64 << 20) || nLen > 1024)
        return;
    if (nOffset == 0)
        gMidFile.clear();
    if ((int)gMidFile.size() < nOffset + nLen)
        gMidFile.resize(nOffset + nLen);
    memcpy(gMidFile.data() + nOffset, pPacket, nLen);
}

void coopReceiveLoadNow(char *pPacket)
{
    int const nSize = GetPacketDWord(pPacket);
    uint32_t const uCRC = (uint32_t)GetPacketDWord(pPacket);
    int const nFlags = GetPacketByte(pPacket);
    PKT_STARTGAME settings;
    GetPacketBuffer(pPacket, &settings, sizeof(PKT_STARTGAME));
    char names[kMaxPlayers][MAXPLAYERNAME];
    GetPacketBuffer(pPacket, names, sizeof(names));
    for (int i = 0; i < kMaxPlayers; i++)
        names[i][MAXPLAYERNAME-1] = '\0';

    char error[96] = "The co-op save didn't arrive completely";
    bool bOk = nSize == (int)gMidFile.size() && Bcrc32(gMidFile.data(), nSize, 0) == uCRC &&
        checkSaveHeader(gMidFile.data(), nSize, error, sizeof(error));
    char path[BMAX_PATH];
    makeDirs();
    if (bOk)
    {
        G_ModDirSnprintfLite(path, sizeof(path), "coopsaves/midlevel/received.sav");
        FILE *fp = fopen(path, "wb");
        bOk = fp && fwrite(gMidFile.data(), 1, nSize, fp) == (size_t)nSize;
        if (fp)
            fclose(fp);
        Bsnprintf(error, sizeof(error), "Could not store the co-op save");
    }
    gMidFile.clear();
    gMidFile.shrink_to_fit();
    if (!bOk)
    {
        // the others are loading: this machine can't continue with them
        LOG_F(ERROR, "Co-op load: %s", error);
        ThrowError("%s.\nEverybody else is loading the host's co-op save, this game can't follow.", error);
        return;
    }
    Bstrncpyz(gMidLoadPath, path, sizeof(gMidLoadPath));
    gMidSettings = settings;
    gMidFlags = nFlags;
    memcpy(gMidNames, names, sizeof(gMidNames));
    gMidLoadPending = true;
}

void coopFrame(void)
{
    if (gMidLoadPending)
        midLevelExecute();
}

void coopReceiveLoadout(char *pPacket)
{
    int const nPlayer = (uint8_t)GetPacketByte(pPacket);
    if (nPlayer >= kMaxPlayers)
        return;

    COOPLOADOUT &l = gCoopLoadout[nPlayer];
    l = {};
    l.bValid = true;
    l.nSlot = nPlayer;
    l.nHealth = GetPacketDWord(pPacket);
    l.nWeapon = (uint8_t)GetPacketByte(pPacket);
    l.nPackItem = GetPacketDWord(pPacket);
    for (int i = 0; i < kWeaponMax; i++)
        l.hasWeapon[i] = GetPacketByte(pPacket) != 0;
    for (int i = 0; i < 12; i++)
        l.ammoCount[i] = GetPacketDWord(pPacket);
    for (int i = 0; i < 3; i++)
        l.armor[i] = GetPacketDWord(pPacket);
    for (int i = 0; i < kPackMax; i++)
        l.packAmount[i] = GetPacketDWord(pPacket);
    for (int i = 0; i < 4; i++)
        l.packTime[i] = GetPacketDWord(pPacket);
}

void coopStartPacketReceived(bool bContinue)
{
    gCoopPending = bContinue;
    if (!bContinue)
        for (int p = 0; p < kMaxPlayers; p++)
            gCoopLoadout[p] = {};
}

void coopApplyLoadouts(void)
{
    if (!gCoopPending)
        return;
    gCoopPending = false;
    gPacketStartGame.bCoopContinue = 0;

    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        COOPLOADOUT const &l = gCoopLoadout[p];
        PLAYER *pPlayer = &gPlayer[p];
        if (!l.bValid || !pPlayer->pXSprite)
            continue;

        for (int i = 0; i < kWeaponMax; i++)
            pPlayer->hasWeapon[i] = l.hasWeapon[i];
        pPlayer->hasWeapon[kWeaponPitchfork] = 1;
        for (int i = 0; i < 12; i++)
            pPlayer->ammoCount[i] = ClipRange(l.ammoCount[i], 0, gAmmoInfo[i].max);
        for (int i = 0; i < 3; i++)
            pPlayer->armor[i] = ClipRange(l.armor[i], 0, 200<<4);
        for (int i = 0; i < kPackMax; i++)
        {
            pPlayer->packSlots[i].curAmount = ClipRange(l.packAmount[i], 0, 100);
            pPlayer->packSlots[i].isActive = 0;
        }
        for (int i = 0; i < 4; i++)
            pPlayer->pwUpTime[kPackPowerUps[i]] = l.packTime[i];
        pPlayer->packItemId = (l.nPackItem >= 0 && l.nPackItem < kPackMax) ? l.nPackItem : -1;
        pPlayer->pXSprite->health = ClipRange(l.nHealth, 1<<4, 200<<4);
        if (l.nWeapon > kWeaponNone && l.nWeapon < kWeaponMax && pPlayer->hasWeapon[l.nWeapon])
            pPlayer->input.newWeapon = l.nWeapon;

        playerRefreshItemBackup(pPlayer); // respawns in co-op restore from this
    }

    for (int p = 0; p < kMaxPlayers; p++)
        gCoopLoadout[p] = {};

    viewSetMessage("Co-op game continued");
}

void coopMigrateOldSaves(void)
{
    static const struct { const char *pzOld; int nSlot; } old[] = {
        { "coopsave.txt", 0 }, { "coopsave_levelstart.txt", kCoopSlotAuto } };
    for (auto const &o : old)
    {
        char from[BMAX_PATH], to[BMAX_PATH];
        G_ModDirSnprintfLite(from, sizeof(from), o.pzOld);
        if (!fileExists(from) || !coopPath(kCoopSaveCarry, o.nSlot, "txt", to) || fileExists(to))
            continue;
        makeDirs();
        if (!rename(from, to))
            LOG_F(INFO, "Moved co-op save \"%s\" to \"%s\"", from, to);
    }
}
