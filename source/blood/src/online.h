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

// Online lobbies on a relay server and live game stats for its dashboard (see netrelay.h and
// server/relay.py).

#pragma once

enum
{
    kOnlineNone = 0,
    kOnlineHost,    // hosting a lobby: remote players come in through the relay
    kOnlineClient,  // joined a lobby
    kOnlineReport,  // LAN game whose stats are shown on the server's dashboard
};

extern char gOnlineServer[64];
extern char gOnlineKey[48];
extern char gOnlineLobby[25];
extern int gOnlineReportLan;  // show hosted LAN games on the dashboard
extern int gOnlineMode;

// the version string players have to match ("NotBlood Chosen 1.0")
const char *onlineVersion(void);

// menu and command line actions (bConsole: before the screen is set up); false (with onlineStatusText() saying why) when it didn't work
bool onlineHostLobby(int nPlayers, bool bConsole);
bool onlineJoinLobby(const char *pzLobby, bool bConsole);
// connects and asks for the lobby list; fills up to nMax entries ("name", "host", "players/max")
int onlineListLobbies(char (*pzNames)[25], char (*pzInfo)[64], int nMax);
const char *onlineStatusText(void);

// called by the game loop: relay messages, stats for the dashboard
void onlineFrame(void);
// a network game ended (back to single player)
void onlineGameEnded(void);
// network game is set up: start reporting LAN games, mark online lobbies as started
void onlineGameStarted(void);
// netInitialize() wait loops: true when the online connection is gone and waiting is pointless
bool onlineConnectionLost(void);
// text for the host's "waiting for players" screen
const char *onlineWaitText(void);
