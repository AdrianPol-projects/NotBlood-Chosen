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

#include "compat.h"
#include "build.h"
#include "mmulti.h"
#include "common_game.h"
#include "blood.h"
#include "config.h"
#include "coopsave.h"
#include "demo.h"
#include "gamemenu.h"
#include "globals.h"
#include "levels.h"
#include "menu.h"
#include "network.h"
#include "player.h"
#include "view.h"

void playerRefreshItemBackup(PLAYER *pPlayer);

namespace
{

const int kCoopSaveVersion = 1;

const char *const kCoopSaveFiles[kCoopSaveMax] = { "coopsave.txt", "coopsave_levelstart.txt" };

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

// every start game setting, by name, so saves survive changes to the packet layout
#define COOP_SETTINGS(X) \
    X(gameType) X(difficulty) X(monsterSettings) X(weaponSettings) X(itemSettings) X(respawnSettings) \
    X(keySettings) X(itemWeaponSettings) X(randomizerMode) X(episodeId) X(levelId) X(uNetGameFlags) \
    X(userMap) X(monsterQuantity) X(monsterHealth) X(monsterSpeed) X(bQuadDamagePowerup) X(nBeastVision) \
    X(nDamageInvul) X(nProjectileBehavior) X(bNapalmFalloff) X(nEnemyBehavior) X(bEnemyRandomTNT) \
    X(nWeaponsVer) X(bWeaponImpulseLimit) X(nAmmoScale) X(bSectorBehavior) X(nHitscanProjectiles) \
    X(nGoreBehavior) X(nPlayerSpeed) X(nPlayerWater) X(bFriendlyFire) X(bAutoTeams) X(nSpawnProtection) \
    X(nSpawnWeapon) X(uSpriteBannedFlags)

bool coopGetPath(int nSlot, char *pzPath)
{
    pzPath[0] = '\0';
    G_ModDirSnprintfLite(pzPath, BMAX_PATH, kCoopSaveFiles[nSlot]);
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

bool writeSaveFile(int nSlot, COOPSAVE const *pSave)
{
    char path[BMAX_PATH];
    if (!coopGetPath(nSlot, path))
        return false;

    FILE *fp = fopen(path, "w");
    if (!fp)
    {
        LOG_F(ERROR, "Could not write co-op save \"%s\"", path);
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

bool readSaveFile(int nSlot, COOPSAVE *pSave)
{
    *pSave = {};

    char path[BMAX_PATH];
    if (!coopGetPath(nSlot, path))
        return false;

    FILE *fp = fopen(path, "r");
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
        LOG_F(WARNING, "Ignoring co-op save \"%s\" (unsupported version or not a co-op game)", path);
        return false;
    }
    return true;
}

// pick the saved loadout for every connected player: same name first, then same slot, then
// whatever saved loadouts are left over, in order
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

    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        for (int i = 0; i < pSave->nPlayers && !bAssigned[p]; i++)
            if (!bUsed[i] && pSave->players[i].nSlot == p)
            {
                gCoopLoadout[p] = pSave->players[i];
                bUsed[i] = bAssigned[p] = true;
            }

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
            LOG_F(INFO, "Co-op continue: player %d (%s) has no saved loadout, starts fresh", p, gProfile[p].name);
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

} // namespace

bool coopWriteSave(int nSlot, const char *pzSavedBy)
{
    if (gGameOptions.nGameType != kGameTypeCoop || !gGameStarted || gDemo.bPlaying)
        return false;

    static COOPSAVE save;
    save = {};
    save.settings = gPacketStartGame;
    save.settings.gameType = kGameTypeCoop;
    save.settings.episodeId = gGameOptions.nEpisode;
    save.settings.levelId = gGameOptions.nLevel;

    time_t const now = time(NULL);
    strftime(save.szSaved, sizeof(save.szSaved), "%Y-%m-%d %H:%M", localtime(&now));
    Bstrncpyz(save.szSavedBy, pzSavedBy ? pzSavedBy : "", sizeof(save.szSavedBy));

    char const *pzTitle = gGameOptions.zLevelName;
    if (!gPacketStartGame.userMap && gGameOptions.nEpisode >= 0 && gGameOptions.nEpisode < gEpisodeCount
        && gEpisodeInfo[gGameOptions.nEpisode].levelsInfo[gGameOptions.nLevel].Title[0])
        pzTitle = gEpisodeInfo[gGameOptions.nEpisode].levelsInfo[gGameOptions.nLevel].Title;
    Bstrncpyz(save.szTitle, pzTitle, sizeof(save.szTitle));

    for (int p = connecthead; p >= 0; p = connectpoint2[p])
        if (save.nPlayers < kMaxPlayers)
            fillLoadout(&save.players[save.nPlayers++], p);

    bool const bOk = writeSaveFile(nSlot, &save);
    if (bOk)
        LOG_F(INFO, "Wrote co-op save \"%s\" (E%dM%d, %d players)", kCoopSaveFiles[nSlot], save.settings.episodeId+1, save.settings.levelId+1, save.nPlayers);
    return bOk;
}

void coopRequestSave(void)
{
    if (gGameOptions.nGameType != kGameTypeCoop || !gGameStarted)
    {
        viewSetMessage("Co-op progress can only be saved in co-op games");
        return;
    }

    if (numplayers > 1)
    {
        char *pPacket = packet;
        PutPacketByte(pPacket, 245);
        netSendPacketAll(packet, pPacket - packet);
    }

    if (coopWriteSave(kCoopSaveManual, gProfile[myconnectindex].name))
        viewSetMessage("Co-op progress saved for all players");
    else
        viewSetMessage("Could not save co-op progress");
}

void coopReceiveSaveRequest(int nPlayer)
{
    if (nPlayer < 0 || nPlayer >= kMaxPlayers)
        return;

    char buffer[128];
    if (coopWriteSave(kCoopSaveManual, gProfile[nPlayer].name))
        Bsnprintf(buffer, sizeof(buffer), "%s saved the co-op progress", gProfile[nPlayer].name);
    else
        Bsnprintf(buffer, sizeof(buffer), "%s saved the co-op progress (failed here)", gProfile[nPlayer].name);
    viewSetMessage(buffer);
}

bool coopDescribeSave(int nSlot, char *pzLevel, int nLevelSize, char *pzInfo, int nInfoSize)
{
    static COOPSAVE save;
    if (!readSaveFile(nSlot, &save))
    {
        Bsnprintf(pzLevel, nLevelSize, "NO SAVE YET");
        pzInfo[0] = '\0';
        return false;
    }

    if (save.settings.userMap)
        Bsnprintf(pzLevel, nLevelSize, "%s", save.szTitle);
    else
        Bsnprintf(pzLevel, nLevelSize, "E%dM%d %s", save.settings.episodeId+1, save.settings.levelId+1, save.szTitle);
    Bsnprintf(pzInfo, nInfoSize, "%s - %d PLAYER%s", save.szSaved, save.nPlayers, save.nPlayers == 1 ? "" : "S");
    Bstrupr(pzLevel);
    return true;
}

bool coopContinueGame(int nSlot)
{
    static COOPSAVE save;
    if (!readSaveFile(nSlot, &save))
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

    LOG_F(INFO, "Continuing co-op game from \"%s\" (E%dM%d, saved %s)", kCoopSaveFiles[nSlot],
          gPacketStartGame.episodeId+1, gPacketStartGame.levelId+1, save.szSaved);

    SetGameVanillaMode(0); // as StartNetGame()
    netBroadcastNewGame();
    gStartNewGame = 1;
    gGameMenuMgr.Deactivate();
    return true;
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
