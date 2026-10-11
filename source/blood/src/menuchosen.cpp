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

void SetupChosenMenus(void)
{
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
