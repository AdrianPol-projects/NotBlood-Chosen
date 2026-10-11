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

// Menus added by NotBlood Chosen (online lobbies, ...). menu.cpp links to them and calls
// SetupChosenMenus() from SetupMenus().

#include "compat.h"
#include "build.h"
#include "mmulti.h"
#include "common_game.h"
#include "blood.h"
#include "coopsave.h"
#include "fx_man.h"
#include "gamemenu.h"
#include "globals.h"
#include "menu.h"
#include "menuchosen.h"
#include "network.h"
#include "online.h"
#include "screen.h"
#include "sound.h"
#include "view.h"
#include "player.h"

extern CGameMenuItemQAV itemBloodQAV;

// ---- ONLINE ---------------------------------------------------------------------------------

CGameMenu menuOnline;
CGameMenu menuOnlineLobbies;

static void OnlineHost(CGameMenuItemChain *);
static void OnlineJoinByName(CGameMenuItemChain *);
static void OnlineJoinListed(CGameMenuItemChain *);
static void SetupOnlineLobbies(CGameMenuItemChain *);
static void UpdateOnlineStatus(CGameMenuItem *);

static char zOnlineStatus[64];
static char zOnlineDashboard[80];

CGameMenuItemTitle itemOnlineTitle("ONLINE", 1, 160, 20, 2038);
CGameMenuItemZEdit itemOnlineServer("SERVER:", 3, 46, 42, 228, gOnlineServer, sizeof(gOnlineServer), 0, NULL, 0);
CGameMenuItemZEdit itemOnlineKey("SERVER KEY:", 3, 46, 52, 228, gOnlineKey, sizeof(gOnlineKey), 0, NULL, 0);
CGameMenuItemZEdit itemOnlineLobby("LOBBY NAME:", 3, 46, 62, 228, gOnlineLobby, sizeof(gOnlineLobby), 0, NULL, 0);
CGameMenuItemSlider itemOnlinePlayers("PLAYERS:", 3, 46, 72, 228, 2, 2, kMaxPlayers, 1, NULL, -1, -1, kMenuSliderValue);
CGameMenuItemChain itemOnlineHost("HOST A LOBBY", 3, 0, 90, 320, 1, NULL, -1, OnlineHost, 0);
CGameMenuItemChain itemOnlineFind("FIND LOBBIES", 3, 0, 102, 320, 1, &menuOnlineLobbies, -1, SetupOnlineLobbies, 0);
CGameMenuItemChain itemOnlineJoin("JOIN LOBBY BY NAME", 3, 0, 114, 320, 1, NULL, -1, OnlineJoinByName, 0);
CGameMenuItemZBool itemOnlineLanStats("SHOW MY LAN GAMES ON THE DASHBOARD:", 3, 46, 132, 228, false, NULL, NULL, NULL);
CGameMenuItemText itemOnlineStatus(zOnlineStatus, 3, 160, 150, 1);
CGameMenuItemText itemOnlineDashboard(zOnlineDashboard, 3, 160, 165, 1);

#define kMaxListedLobbies 8
static char zLobbyNames[kMaxListedLobbies][25];
static char zLobbyInfo[kMaxListedLobbies][64];
static char zLobbyText[kMaxListedLobbies][25];

CGameMenuItemTitle itemOnlineLobbiesTitle("LOBBIES", 1, 160, 20, 2038);
CGameMenuItemChain itemOnlineLobby0(zLobbyText[0], 3, 30, 45, 260, 0, NULL, -1, OnlineJoinListed, 0, 0, zLobbyInfo[0]);
CGameMenuItemChain itemOnlineLobby1(zLobbyText[1], 3, 30, 57, 260, 0, NULL, -1, OnlineJoinListed, 1, 0, zLobbyInfo[1]);
CGameMenuItemChain itemOnlineLobby2(zLobbyText[2], 3, 30, 69, 260, 0, NULL, -1, OnlineJoinListed, 2, 0, zLobbyInfo[2]);
CGameMenuItemChain itemOnlineLobby3(zLobbyText[3], 3, 30, 81, 260, 0, NULL, -1, OnlineJoinListed, 3, 0, zLobbyInfo[3]);
CGameMenuItemChain itemOnlineLobby4(zLobbyText[4], 3, 30, 93, 260, 0, NULL, -1, OnlineJoinListed, 4, 0, zLobbyInfo[4]);
CGameMenuItemChain itemOnlineLobby5(zLobbyText[5], 3, 30, 105, 260, 0, NULL, -1, OnlineJoinListed, 5, 0, zLobbyInfo[5]);
CGameMenuItemChain itemOnlineLobby6(zLobbyText[6], 3, 30, 117, 260, 0, NULL, -1, OnlineJoinListed, 6, 0, zLobbyInfo[6]);
CGameMenuItemChain itemOnlineLobby7(zLobbyText[7], 3, 30, 129, 260, 0, NULL, -1, OnlineJoinListed, 7, 0, zLobbyInfo[7]);
CGameMenuItemChain itemOnlineRefresh("REFRESH", 3, 0, 148, 320, 1, NULL, -1, SetupOnlineLobbies, 0);
CGameMenuItemText itemOnlineLobbiesStatus(zOnlineStatus, 3, 160, 165, 1);

static CGameMenuItemChain *const pLobbyItems[kMaxListedLobbies] = {
    &itemOnlineLobby0, &itemOnlineLobby1, &itemOnlineLobby2, &itemOnlineLobby3,
    &itemOnlineLobby4, &itemOnlineLobby5, &itemOnlineLobby6, &itemOnlineLobby7,
};

static void SetOnlineStatus(const char *pzText)
{
    Bstrncpyz(zOnlineStatus, pzText, sizeof(zOnlineStatus));
    Bstrupr(zOnlineStatus);
}

static void UpdateOnlineStatus(CGameMenuItem *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    gOnlineReportLan = itemOnlineLanStats.at20;
    if (gOnlineServer[0])
        Bsnprintf(zOnlineDashboard, sizeof(zOnlineDashboard), "LIVE STATS: %s IN A WEB BROWSER", gOnlineServer);
    else
        zOnlineDashboard[0] = 0;
    Bstrupr(zOnlineDashboard);
}

static void StartOnlineNetGame(NETWORKMODE nMode)
{
    sndStopSong();
    FX_StopAllSounds();
    UpdateDacs(0, true);
    gNetMode = nMode;
    netInitialize(false);
    gGameMenuMgr.Deactivate();
    gQuitGame = gRestartGame = true;
}

static void OnlineHost(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    gNetPort = strtoul(zNetPortBuffer, NULL, 10);
    if (!gNetPort)
        gNetPort = kNetDefaultPort;
    if (!onlineHostLobby(itemOnlinePlayers.nValue, false))
    {
        SetOnlineStatus(onlineStatusText());
        return;
    }
    gNetPlayers = itemOnlinePlayers.nValue;
    StartOnlineNetGame(NETWORK_SERVER);
}

static void OnlineJoin(const char *pzLobby)
{
    if (!onlineJoinLobby(pzLobby, false))
    {
        SetOnlineStatus(onlineStatusText());
        return;
    }
    StartOnlineNetGame(NETWORK_CLIENT);
}

static void OnlineJoinByName(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    OnlineJoin(gOnlineLobby);
}

static void OnlineJoinListed(CGameMenuItemChain *pItem)
{
    int i = ClipRange(pItem->at30, 0, kMaxListedLobbies-1);
    OnlineJoin(zLobbyNames[i]);
}

static void SetupOnlineLobbies(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    int nCount = onlineListLobbies(zLobbyNames, zLobbyInfo, kMaxListedLobbies);
    SetOnlineStatus(onlineStatusText());
    for (int i = 0; i < kMaxListedLobbies; i++)
    {
        bool bShow = i < nCount;
        Bstrncpyz(zLobbyText[i], bShow ? zLobbyNames[i] : "", sizeof(zLobbyText[i]));
        Bstrupr(zLobbyText[i]);
        Bstrupr(zLobbyInfo[i]);
        if (!bShow)
            zLobbyInfo[i][0] = 0;
        pLobbyItems[i]->bEnable = bShow;
        pLobbyItems[i]->bNoDraw = !bShow;
    }
    menuOnlineLobbies.SetFocusItem(nCount > 0 ? (CGameMenuItem *)pLobbyItems[0] : (CGameMenuItem *)&itemOnlineRefresh);
}

void SetupOnlineMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    itemOnlineLanStats.at20 = gOnlineReportLan != 0;
    SetOnlineStatus(onlineStatusText());
}

// ---- CO-OP SAVES ---------------------------------------------------------------------------

CGameMenu menuCoopLoad;
CGameMenu menuCoopSlots;
CGameMenu menuCoopSave;

static void SetupCoopSlots(CGameMenuItemChain *);
static void CoopSlotChosen(CGameMenuItemChain *);
static void CoopSaveChosen(CGameMenuItemChain *);
static void UpdateCoopOptions(CGameMenuItemZBool *);

static int nCoopSlotsType;
static char zCoopSlotsTitle[24];
static char zCoopSlotText[kCoopSlots+1][40];
static char zCoopSlotInfo[kCoopSlots+1][24];
static char zCoopSaveText[kCoopSlots][40];
static char zCoopSaveInfo[kCoopSlots][24];

CGameMenuItemTitle itemCoopLoadTitle("LOAD CO-OP", 1, 160, 20, 2038);
CGameMenuItemChain itemCoopLoadMid("CONTINUE WHERE YOU SAVED", 1, 0, 45, 320, 1, &menuCoopSlots, -1, SetupCoopSlots, kCoopSaveMid);
CGameMenuItemText itemCoopLoadMidHelp("EVERYTHING AS IT WAS: POSITIONS, KEYS, ENEMIES", 3, 160, 63, 1);
CGameMenuItemChain itemCoopLoadCarry("RESTART LEVEL WITH YOUR GEAR", 1, 0, 82, 320, 1, &menuCoopSlots, -1, SetupCoopSlots, kCoopSaveCarry);
CGameMenuItemText itemCoopLoadCarryHelp("THE LEVEL STARTS OVER, EVERYBODY KEEPS WEAPONS AND ITEMS", 3, 160, 100, 1);
CGameMenuItemZBool itemCoopLoadOrder("NEW PLAYERS GET:", 3, 46, 125, 228, false, UpdateCoopOptions, "LEFTOVER GEAR", "PITCHFORK ONLY");
CGameMenuItemZBool itemCoopLoadLeave("AUTOSAVE WHEN A PLAYER LEAVES:", 3, 46, 137, 228, true, UpdateCoopOptions, NULL, NULL);
CGameMenuItemText itemCoopLoadHelp1("PLAYERS GET THEIR GEAR BY NAME. NEW PLAYERS", 3, 160, 157, 1);
CGameMenuItemText itemCoopLoadHelp2("START AT THE LEVEL START.", 3, 160, 167, 1);

CGameMenuItemTitle itemCoopSlotsTitle(zCoopSlotsTitle, 1, 160, 20, 2038);
#define COOP_SLOT_ITEM(n) CGameMenuItemChain itemCoopSlot##n(zCoopSlotText[n], 3, 20, 42 + n*13, 280, 0, NULL, -1, CoopSlotChosen, n, 0, zCoopSlotInfo[n]);
COOP_SLOT_ITEM(0) COOP_SLOT_ITEM(1) COOP_SLOT_ITEM(2) COOP_SLOT_ITEM(3) COOP_SLOT_ITEM(4)
COOP_SLOT_ITEM(5) COOP_SLOT_ITEM(6) COOP_SLOT_ITEM(7) COOP_SLOT_ITEM(8)
static CGameMenuItemChain *const pCoopSlotItems[kCoopSlots+1] = { &itemCoopSlot0, &itemCoopSlot1, &itemCoopSlot2, &itemCoopSlot3,
    &itemCoopSlot4, &itemCoopSlot5, &itemCoopSlot6, &itemCoopSlot7, &itemCoopSlot8 };

CGameMenuItemTitle itemCoopSaveTitle("SAVE CO-OP", 1, 160, 20, 2038);
#define COOP_SAVE_ITEM(n) CGameMenuItemChain itemCoopSave##n(zCoopSaveText[n], 3, 20, 45 + n*13, 280, 0, NULL, -1, CoopSaveChosen, n, 0, zCoopSaveInfo[n]);
COOP_SAVE_ITEM(0) COOP_SAVE_ITEM(1) COOP_SAVE_ITEM(2) COOP_SAVE_ITEM(3)
COOP_SAVE_ITEM(4) COOP_SAVE_ITEM(5) COOP_SAVE_ITEM(6) COOP_SAVE_ITEM(7)
static CGameMenuItemChain *const pCoopSaveItems[kCoopSlots] = { &itemCoopSave0, &itemCoopSave1, &itemCoopSave2, &itemCoopSave3,
    &itemCoopSave4, &itemCoopSave5, &itemCoopSave6, &itemCoopSave7 };
CGameMenuItemText itemCoopSaveHelp("SAVED ON EVERY PLAYER'S MACHINE", 3, 160, 160, 1);

static void UpdateCoopOptions(CGameMenuItemZBool *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    gCoopMatchOrder = itemCoopLoadOrder.at20;
    gCoopAutosaveLeave = itemCoopLoadLeave.at20;
}

static void describeSlot(int nType, int nSlot, char *pzText, int nTextSize, char *pzInfo, int nInfoSize)
{
    char level[64];
    bool bSave = coopDescribeSave(nType, nSlot, level, sizeof(level), pzInfo, nInfoSize);
    const char *pzName = nSlot < kCoopSlots ? NULL : (nType == kCoopSaveCarry ? "LEVEL START" : "PLAYER LEFT");
    char slot[16];
    if (pzName)
        Bsnprintf(slot, sizeof(slot), "%s:", pzName);
    else
        Bsnprintf(slot, sizeof(slot), "%d.", nSlot+1);
    Bsnprintf(pzText, nTextSize, "%s %s", slot, bSave ? level : "EMPTY");
    if (!bSave)
        pzInfo[0] = 0;
    Bstrupr(pzInfo);
}

void SetupCoopLoadMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    itemCoopLoadOrder.at20 = gCoopMatchOrder != 0;
    itemCoopLoadLeave.at20 = gCoopAutosaveLeave != 0;
}

static void SetupCoopSlots(CGameMenuItemChain *pItem)
{
    nCoopSlotsType = pItem->at30;
    Bstrncpyz(zCoopSlotsTitle, nCoopSlotsType == kCoopSaveMid ? "CONTINUE" : "RESTART LEVEL", sizeof(zCoopSlotsTitle));
    int nFocus = -1;
    for (int i = 0; i <= kCoopSlots; i++)
    {
        describeSlot(nCoopSlotsType, i, zCoopSlotText[i], sizeof(zCoopSlotText[i]), zCoopSlotInfo[i], sizeof(zCoopSlotInfo[i]));
        pCoopSlotItems[i]->bEnable = zCoopSlotInfo[i][0] != 0;
        if (nFocus < 0 && pCoopSlotItems[i]->bEnable)
            nFocus = i;
    }
    menuCoopSlots.SetFocusItem(pCoopSlotItems[nFocus < 0 ? 0 : nFocus]);
}

void OpenCoopSlotsMenu(int nType)
{
    CGameMenuItemChain *pItem = nType == kCoopSaveMid ? &itemCoopLoadMid : &itemCoopLoadCarry;
    SetupCoopSlots(pItem);
    gGameMenuMgr.Push(&menuCoopSlots, -1);
}

static void CoopSlotChosen(CGameMenuItemChain *pItem)
{
    if (numplayers > 1 && myconnectindex != connecthead)
    {
        viewSetMessage("Only the host can load a co-op game");
        return;
    }
    int const nSlot = ClipRange(pItem->at30, 0, kCoopSlots);
    bool const bOk = nCoopSlotsType == kCoopSaveMid ? coopLoadMidLevel(nSlot) : coopContinueGame(nSlot);
    if (!bOk)
        viewSetMessage("Could not load the co-op save");
}

void SetupCoopSaveMenu(CGameMenuItemChain *pItem)
{
    UNREFERENCED_PARAMETER(pItem);
    for (int i = 0; i < kCoopSlots; i++)
        describeSlot(kCoopSaveMid, i, zCoopSaveText[i], sizeof(zCoopSaveText[i]), zCoopSaveInfo[i], sizeof(zCoopSaveInfo[i]));
}

static void CoopSaveChosen(CGameMenuItemChain *pItem)
{
    coopRequestSave(ClipRange(pItem->at30, 0, kCoopSlots-1));
    gGameMenuMgr.Deactivate();
}

void SetupChosenMenus(void)
{
    menuCoopLoad.Add(&itemCoopLoadTitle, false);
    menuCoopLoad.Add(&itemCoopLoadMid, true);
    menuCoopLoad.Add(&itemCoopLoadMidHelp, false);
    menuCoopLoad.Add(&itemCoopLoadCarry, false);
    menuCoopLoad.Add(&itemCoopLoadCarryHelp, false);
    menuCoopLoad.Add(&itemCoopLoadOrder, false);
    menuCoopLoad.Add(&itemCoopLoadLeave, false);
    menuCoopLoad.Add(&itemCoopLoadHelp1, false);
    menuCoopLoad.Add(&itemCoopLoadHelp2, false);
    menuCoopLoad.Add(&itemBloodQAV, false);
    itemCoopLoadOrder.tooltip_pzTextUpper = "What players who aren't in the save start with";
    itemCoopLoadLeave.tooltip_pzTextUpper = "Saves the game when somebody leaves, to continue later";

    menuCoopSlots.Add(&itemCoopSlotsTitle, false);
    for (int i = 0; i <= kCoopSlots; i++)
        menuCoopSlots.Add(pCoopSlotItems[i], i == 0);
    menuCoopSlots.Add(&itemBloodQAV, false);

    menuCoopSave.Add(&itemCoopSaveTitle, false);
    for (int i = 0; i < kCoopSlots; i++)
        menuCoopSave.Add(pCoopSaveItems[i], i == 0);
    menuCoopSave.Add(&itemCoopSaveHelp, false);
    menuCoopSave.Add(&itemBloodQAV, false);

    menuOnline.Add(&itemOnlineTitle, false);
    menuOnline.Add(&itemOnlineServer, false);
    menuOnline.Add(&itemOnlineKey, false);
    menuOnline.Add(&itemOnlineLobby, false);
    menuOnline.Add(&itemOnlinePlayers, false);
    menuOnline.Add(&itemOnlineHost, true);
    menuOnline.Add(&itemOnlineFind, false);
    menuOnline.Add(&itemOnlineJoin, false);
    menuOnline.Add(&itemOnlineLanStats, false);
    menuOnline.Add(&itemOnlineStatus, false);
    menuOnline.Add(&itemOnlineDashboard, false);
    menuOnline.Add(&itemBloodQAV, false);
    itemOnlineStatus.pPreDrawCallback = UpdateOnlineStatus;
    itemOnlineServer.tooltip_pzTextUpper = "Address of the online server";
    itemOnlineKey.tooltip_pzTextUpper = "The secret key of the server";
    itemOnlineLobby.tooltip_pzTextUpper = "Name of the lobby to host or join";

    menuOnlineLobbies.Add(&itemOnlineLobbiesTitle, false);
    for (int i = 0; i < kMaxListedLobbies; i++)
        menuOnlineLobbies.Add(pLobbyItems[i], i == 0);
    menuOnlineLobbies.Add(&itemOnlineRefresh, false);
    menuOnlineLobbies.Add(&itemOnlineLobbiesStatus, false);
    menuOnlineLobbies.Add(&itemBloodQAV, false);
}
