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

#include <string>
#include <vector>

#include "compat.h"
#include "baselayer.h"
#include "build.h"
#include "mmulti.h"
#include "timer.h"
#include "common_game.h"
#include "blood.h"
#include "bot.h"
#include "chosen.h"
#include "config.h"
#include "demo.h"
#include "endgame.h"
#include "globals.h"
#include "keyboard.h"
#include "levels.h"
#include "mods.h"
#include "netrelay.h"
#include "network.h"
#include "online.h"
#include "player.h"
#include "resource.h"
#include "view.h"

char gOnlineServer[64];
char gOnlineKey[48];
char gOnlineLobby[25];
char gOnlineLobbyPassword[25];
int gOnlineReportLan;
int gOnlineMode;

extern const char *zDiffStrings[];

static char zStatus[128];
static char zLobbyName[25];
static bool bReportSent;
static uint32_t nNextStats;
static bool bConsoleWait;
static std::string mapSentFor;
static int nLastLevelTime;
static int nBotsSent = -1;

// lobby chat: the last lines, and what this player is typing on the waiting screens
#define kChatLines 6
static std::string chatLines[kChatLines];
static int nChatLines;
static std::string chatInput;

const char *onlineVersion(void)
{
    return CHOSEN_NAME " " CHOSEN_VERSION;
}

const char *onlineStatusText(void)
{
    return zStatus;
}

static void setStatus(const char *pzText)
{
    Bstrncpyz(zStatus, pzText, sizeof(zStatus));
    LOG_F(INFO, "Online: %s", pzText);
}

// fields of a tab separated message
static std::string msgField(const char *pzMsg, int nField)
{
    std::string s(pzMsg);
    size_t a = 0;
    while (nField-- > 0)
    {
        a = s.find('\t', a);
        if (a == std::string::npos)
            return "";
        a++;
    }
    size_t b = s.find_first_of("\t\n", a);
    return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

static std::string cleanField(const char *pzText)
{
    std::string s;
    for (const char *p = pzText; *p; p++)
        if ((unsigned char)*p >= 32 && *p != '\t')
            s += *p;
    return s;
}

static const char *playerName(void)
{
    return szPlayerName[0] ? szPlayerName : "Player";
}

// shows a waiting screen until bDone() is true; false on timeout, Escape or a dead connection
template<typename F> static bool onlineWait(const char *pzText, int nTimeoutMs, F bDone)
{
    uint32_t nStart = timerGetTicks();
    while (!bDone())
    {
        if (relayStatus() == kRelayClosed)
        {
            setStatus(relayError()[0] ? relayError() : "Connection closed");
            return false;
        }
        if (timerGetTicks() - nStart > (uint32_t)nTimeoutMs)
        {
            setStatus("The server didn't answer in time");
            return false;
        }
        handleevents();
        if (quitevent || (!bConsoleWait && KB_KeyPressed(sc_Escape)))
        {
            KB_ClearKeyDown(sc_Escape);
            setStatus("Cancelled");
            return false;
        }
        if (!bConsoleWait)
        {
            viewLoadingScreen(gMenuPicnum, "Online", onlineServer(), pzText);
            videoNextPage();
        }
    }
    return true;
}

const char *onlineServer(void)
{
#ifndef NOTBLOOD_WEB // in a browser no address means the server the page came from
    if (!gOnlineServer[0])
        return CHOSEN_PUBLIC_SERVER;
#endif
    return gOnlineServer;
}

static bool onlineConnect(void)
{
#ifndef NOTBLOOD_WEB
    if (!onlineServer()[0])
    {
        setStatus("Enter the server address first");
        return false;
    }
#endif
    if (relayStatus() == kRelayOnline)
        return true;
    relayConnect(onlineServer(), gOnlineKey, onlineVersion());
    char buffer[128];
    Bsnprintf(buffer, sizeof(buffer), "Connecting to %s", onlineServer()[0] ? onlineServer() : "the server");
    if (!onlineWait(buffer, 15000, []() { return relayStatus() == kRelayOnline; }))
    {
        relayClose();
        return false;
    }
    return true;
}

// next reply that starts with one of the given commands
static bool onlineReply(const char *pzText, char *pReply, int nSize, const char *pzWant1, const char *pzWant2)
{
    return onlineWait(pzText, 10000, [&]() {
        while (relayReadText(pReply, nSize))
        {
            std::string cmd = msgField(pReply, 0);
            if (cmd == pzWant1 || cmd == pzWant2 || cmd == "error")
                return true;
        }
        return false;
    });
}

static void lobbyNameFor(char *pzName, int nSize)
{
    std::string s;
    for (const char *p = gOnlineLobby[0] ? gOnlineLobby : playerName(); *p && s.size() < 24; p++)
        if (isalnum((unsigned char)*p) || strchr(" _.-", *p))
            s += *p;
    if (!gOnlineLobby[0] && s.size() < 18)
        s += " game";
    if (s.empty())
        s = "NotBlood game";
    Bstrncpyz(pzName, s.c_str(), nSize);
}

// what the server compares for private lobbies: the password itself never leaves the machine
static std::string passwordHash(void)
{
    std::string pw = cleanField(gOnlineLobbyPassword);
    if (pw.empty())
        return "";
    for (char &c : pw)
        c = tolower((unsigned char)c); // typed on a phone or a PC: case doesn't matter
    pw = "notblood-lobby:" + pw;
    return relaySha256Hex(pw.data(), pw.size());
}

static void chatReset(void)
{
    nChatLines = 0;
    chatInput.clear();
}

bool onlineHostLobby(int nPlayers, bool bConsole)
{
    bConsoleWait = bConsole;
    if (!onlineConnect())
        return false;
    chatReset();
    nBotsSent = -1;
    lobbyNameFor(zLobbyName, sizeof(zLobbyName));
    std::string cmd = std::string("host\t") + zLobbyName + "\t" + std::to_string(nPlayers) + "\t" + cleanField(playerName()) +
        "\t" + cleanField(modCurrentName()) + "\t" + onlineVersion() + "\t" + passwordHash();
    relaySendText(cmd.c_str());
    char reply[512];
    if (!onlineReply("Creating the lobby", reply, sizeof(reply), "hosted", NULL))
    {
        relayClose();
        return false;
    }
    if (msgField(reply, 0) == "error")
    {
        setStatus(msgField(reply, 1).c_str());
        relayClose();
        return false;
    }
    if (!gNetPort)
        gNetPort = kNetDefaultPort;
    relaySetHostGamePort(gNetPort);
    gOnlineMode = kOnlineHost;
    char buffer[128];
    Bsnprintf(buffer, sizeof(buffer), gOnlineLobbyPassword[0] ? "Private lobby \"%s\" is open" : "Lobby \"%s\" is open", zLobbyName);
    setStatus(buffer);
    return true;
}

bool onlineJoinLobby(const char *pzLobby, bool bConsole)
{
    bConsoleWait = bConsole;
    if (!pzLobby || !pzLobby[0])
    {
        setStatus("Enter the lobby name first");
        return false;
    }
    if (!onlineConnect())
        return false;
    chatReset();
    std::string cmd = std::string("join\t") + cleanField(pzLobby) + "\t" + cleanField(playerName()) + "\t" +
        cleanField(modCurrentName()) + "\t" + onlineVersion() + "\t" + passwordHash();
    relaySendText(cmd.c_str());
    char reply[512];
    if (!onlineReply("Joining the lobby", reply, sizeof(reply), "joined", NULL))
    {
        relayClose();
        return false;
    }
    if (msgField(reply, 0) == "error")
    {
        setStatus(msgField(reply, 1).c_str());
        relayClose();
        return false;
    }
    if (!onlineWait("Joining the lobby", 5000, []() { return relayClientPort() != 0; }))
    {
        relayClose();
        return false;
    }
    Bstrncpyz(zLobbyName, msgField(reply, 1).c_str(), sizeof(zLobbyName));
    Bstrncpyz(gNetAddress, "127.0.0.1", sizeof(gNetAddress));
    gNetPort = relayClientPort();
    gOnlineMode = kOnlineClient;
    char buffer[128];
    Bsnprintf(buffer, sizeof(buffer), "Joined \"%s\"", zLobbyName);
    setStatus(buffer);
    return true;
}

int onlineListLobbies(char (*pzNames)[25], char (*pzInfo)[64], int nMax)
{
    bConsoleWait = false;
    if (!onlineConnect())
        return -1;
    relaySendText("list");
    char reply[8192];
    if (!onlineReply("Looking for lobbies", reply, sizeof(reply), "lobbies", NULL))
    {
        relayClose();
        return -1;
    }
    int nCount = 0;
    const char *p = strchr(reply, '\n');
    while (p && nCount < nMax)
    {
        p++;
        const char *pEnd = strchr(p, '\n');
        std::string line(p, pEnd ? pEnd - p : strlen(p));
        // name host players max state mod version [bots private map]: players are people, bots apart
        std::string state = msgField(line.c_str(), 4);
        Bstrncpyz(pzNames[nCount], msgField(line.c_str(), 0).c_str(), 25);
        std::string mod = msgField(line.c_str(), 5);
        int nBots = atoi(msgField(line.c_str(), 7).c_str());
        bool bPrivate = msgField(line.c_str(), 8) == "1";
        std::string map = msgField(line.c_str(), 9);
        std::string bots = nBots > 0 ? "  +" + std::to_string(nBots) + (nBots == 1 ? " BOT" : " BOTS") : "";
        Bsnprintf(pzInfo[nCount], 64, "%s%s  %s/%s%s%s%s%s%s", bPrivate ? "LOCKED  " : "", msgField(line.c_str(), 1).c_str(),
            msgField(line.c_str(), 2).c_str(), msgField(line.c_str(), 3).c_str(), bots.c_str(), state == "waiting" ? "" : "  PLAYING",
            mod.empty() ? "" : "  MOD: ", mod.c_str(), map.empty() ? "" : ("  " + map).c_str());
        nCount++;
        p = pEnd;
    }
    char buffer[64];
    if (nCount)
        Bsnprintf(buffer, sizeof(buffer), "%d lobb%s on the server", nCount, nCount == 1 ? "y" : "ies");
    else
        Bsnprintf(buffer, sizeof(buffer), "No lobbies right now - host one!");
    setStatus(buffer);
    relayClose();
    return nCount;
}

bool onlineConnectionLost(void)
{
    return (gOnlineMode == kOnlineHost || gOnlineMode == kOnlineClient) && relayStatus() == kRelayClosed;
}

const char *onlineWaitText(void)
{
    static char buffer[64];
    if (gOnlineMode == kOnlineHost)
    {
        Bsnprintf(buffer, sizeof(buffer), "Lobby: %s", zLobbyName);
        return buffer;
    }
    if (gNetMode != NETWORK_SERVER)
        return NULL;
    // LAN host: the address the others type in (or find with FIND LAN GAMES)
    static char address[32];
    if (!address[0])
        netLocalAddress(address, sizeof(address));
    Bsnprintf(buffer, sizeof(buffer), "Your address: %s", address);
    return buffer;
}

void onlineGameEnded(void)
{
    if (gOnlineMode != kOnlineNone)
        relayClose();
    gOnlineMode = kOnlineNone;
    bReportSent = false;
    mapSentFor.clear();
    nBotsSent = -1;
    chatReset();
}

// ---- lobby chat -------------------------------------------------------------------------------

static void chatAdd(const std::string &line)
{
    if (nChatLines == kChatLines)
    {
        for (int i = 1; i < kChatLines; i++)
            chatLines[i-1] = chatLines[i];
        nChatLines--;
    }
    chatLines[nChatLines++] = line;
}

static void onlineSay(const std::string &text)
{
    std::string t = cleanField(text.c_str());
    if (t.empty() || relayStatus() != kRelayOnline)
        return;
    relaySendText(("chat\t" + t.substr(0, 120)).c_str());
}

// a relay message for the lobby (chat, players coming and going); true when it was one
static bool lobbyMessage(const char *pzMsg)
{
    std::string cmd = msgField(pzMsg, 0);
    if (cmd == "chat")
    {
        std::string name = msgField(pzMsg, 1), text = msgField(pzMsg, 2);
        std::string line = name.empty() ? text : name + ": " + text;
        chatAdd(line);
        if (gGameStarted)
            viewSetMessage(line.c_str());
        return true;
    }
    if (cmd == "peer_join" && !gGameStarted)
    {
        chatAdd(msgField(pzMsg, 2) + " joined the lobby");
        return true;
    }
    return false;
}

void onlineLobbyWait(bool bConsole)
{
    if ((gOnlineMode != kOnlineHost && gOnlineMode != kOnlineClient) || bConsole)
        return;
    char msg[512];
    bool bChanged = false;
    while (relayReadText(msg, sizeof(msg)))
        bChanged |= lobbyMessage(msg);
    // typing: letters go to the chat line, Enter sends it
    while (char ch = keyGetChar())
    {
        if (ch == '\r' || ch == '\n')
        {
            onlineSay(chatInput);
            chatInput.clear();
        }
        else if (ch == '\b' || ch == 127)
        {
            if (!chatInput.empty())
                chatInput.pop_back();
        }
        else if ((unsigned char)ch >= 32 && ch != '\t' && chatInput.size() < 60)
            chatInput += ch;
        else
            continue;
        bChanged = true;
    }
    static uint32_t nLastDraw;
    uint32_t nNow = timerGetTicks();
    if (!bChanged && nNow - nLastDraw < 500) // the caret blinks
        return;
    nLastDraw = nNow;
    viewLoadingScreenUpdate(NULL, -1);
    int y = 200 - 12 - 9 * nChatLines;
    for (int i = 0; i < nChatLines; i++, y += 9)
        viewDrawText(3, chatLines[i].substr(0, 48).c_str(), 12, y, -128, 0, 0, true);
    std::string prompt = "SAY: " + chatInput + ((nNow / 500) & 1 ? "_" : " ");
    viewDrawText(3, prompt.substr(0, 52).c_str(), 12, 200 - 11, -128, 2, 0, true);
    videoNextPage();
}

void onlineGameStarted(void)
{
    if (gOnlineMode == kOnlineHost)
        relaySendText("state\tplaying");
    else if (gOnlineMode == kOnlineNone && gNetMode == NETWORK_SERVER && gOnlineReportLan && gOnlineServer[0])
    {
        relayConnect(gOnlineServer, gOnlineKey, onlineVersion());
        gOnlineMode = kOnlineReport;
        bReportSent = false;
    }
}

// ---- stats for the dashboard ----------------------------------------------------------------

static void jsonString(std::string &s, const char *pzText)
{
    s += '"';
    for (const unsigned char *p = (const unsigned char *)pzText; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            s += '\\';
            s += (char)*p;
        }
        else if (*p >= 32 && *p < 127)
            s += (char)*p;
    }
    s += '"';
}

static void jsonInt(std::string &s, const char *pzKey, int nValue)
{
    s += '"';
    s += pzKey;
    s += "\":";
    s += std::to_string(nValue);
    s += ',';
}

static const char *levelTitle(void)
{
    static char buffer[96];
    int nEpisode = gGameOptions.nEpisode, nLevel = gGameOptions.nLevel;
    if (nEpisode >= 0 && nEpisode < gEpisodeCount && nLevel >= 0 && nLevel < gEpisodeInfo[nEpisode].nLevels)
    {
        LEVELINFO *pLevel = &gEpisodeInfo[nEpisode].levelsInfo[nLevel];
        if (!Bstrcasecmp(pLevel->Filename, gGameOptions.zLevelName) || !gGameOptions.zLevelName[0])
        {
            Bsnprintf(buffer, sizeof(buffer), "E%dM%d: %s", nEpisode+1, nLevel+1, pLevel->Title[0] ? pLevel->Title : pLevel->Filename);
            return buffer;
        }
    }
    Bsnprintf(buffer, sizeof(buffer), "%s", gGameOptions.zLevelName);
    return buffer;
}

static void buildStats(std::string &s)
{
    static const char *pzModes[] = { "Single player", "Co-op", "BloodBath", "Teams" };
    static const char *pzWeapons[] = { "", "Pitchfork", "Flare Gun", "Shotgun", "Tommy Gun", "Napalm Launcher", "Dynamite", "Spray Can",
        "Tesla Cannon", "Life Leech", "Voodoo Doll", "Proximity Dynamite", "Remote Dynamite", "Beast Hands" };
    static const char *pzPacks[kPackMax] = { "Doctor's Bag", "Diving Suit", "Crystal Ball", "Beast Vision", "Jump Boots" };
    static const struct { int nPowerUp; const char *pzName; } powerUps[] = {
        { kPwUpFeatherFall, "Feather Fall" }, { kPwUpShadowCloak, "Invisibility" }, { kPwUpDeathMask, "Invulnerability" },
        { kPwUpTwoGuns, "Guns Akimbo" }, { kPwUpReflectShots, "Reflective Shots" }, { kPwUpDeliriumShroom, "Delirium" },
        { kPwUpGrowShroom, "Grow Shroom" }, { kPwUpShrinkShroom, "Shrink Shroom" }, { kPwUpAsbestArmor, "Asbestos Armor" },
        { kPwUpDoppleganger, "Doppleganger" },
    };

    s = "{\"v\":1,\"mode\":";
    jsonString(s, pzModes[ClipRange(gGameOptions.nGameType, 0, 3)]);
    s += ",\"level\":";
    jsonString(s, levelTitle());
    s += ",\"diff\":";
    char diff[32];
    Bstrncpyz(diff, zDiffStrings[ClipRange(gGameOptions.nDifficulty, 0, 4)], sizeof(diff));
    for (char *p = diff + 1; *p; p++)  // "WELL DONE" -> "Well Done"
        if (p[-1] != ' ')
            *p = tolower(*p);
    jsonString(s, diff);
    s += ',';
    jsonInt(s, "time", gLevelTime / kTicsPerSec);
    s += "\"kills\":[" + std::to_string(gKillMgr.at4) + "," + std::to_string(gKillMgr.at0) + "],";
    s += "\"secrets\":[" + std::to_string(gSecretMgr.nNormalSecretsFound) + "," + std::to_string(gSecretMgr.nAllSecrets) + "],";
    s += "\"players\":[";
    bool bFirst = true;
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pPlayer = &gPlayer[p];
        if (!pPlayer->pSprite || !pPlayer->pXSprite)
            continue;
        if (!bFirst)
            s += ',';
        bFirst = false;
        s += '{';
        jsonInt(s, "id", p);
        s += "\"name\":";
        jsonString(s, gProfile[p].name);
        s += ',';
        int nHealth = pPlayer->pXSprite->health >> 4;
        jsonInt(s, "alive", nHealth > 0);
        jsonInt(s, "health", nHealth);
        s += "\"armor\":[" + std::to_string(pPlayer->armor[0] >> 4) + "," + std::to_string(pPlayer->armor[1] >> 4) + "," +
            std::to_string(pPlayer->armor[2] >> 4) + "],";
        int nWeapon = ClipRange(pPlayer->curWeapon, 0, 13);
        s += "\"weapon\":";
        jsonString(s, pzWeapons[nWeapon]);
        s += ',';
        jsonInt(s, "wid", nWeapon);
        int nAmmo = -1;
        if (nWeapon && pPlayer->weaponAmmo >= 0 && pPlayer->weaponAmmo < 12)
        {
            nAmmo = pPlayer->ammoCount[pPlayer->weaponAmmo];
            if (pPlayer->weaponAmmo == 6)
                nAmmo /= 10;
        }
        jsonInt(s, "ammo", nAmmo);
        s += "\"weapons\":[";
        for (int i = 1, n = 0; i < 14; i++)
            if (pPlayer->hasWeapon[i])
                s += (n++ ? "," : "") + std::to_string(i);
        s += "],\"keys\":[";
        for (int i = 1, n = 0; i < 8; i++)
            if (pPlayer->hasKey[i])
                s += (n++ ? "," : "") + std::to_string(i);
        s += "],\"items\":[";
        for (int i = 0, n = 0; i < kPackMax; i++)
            if (pPlayer->packSlots[i].curAmount > 0)
            {
                s += n++ ? ",[" : "[";
                jsonString(s, pzPacks[i]);
                s += "," + std::to_string(pPlayer->packSlots[i].curAmount) + "]";
            }
        s += "],\"powerups\":[";
        for (int i = 0, n = 0; i < (int)ARRAY_SIZE(powerUps); i++)
            if (pPlayer->pwUpTime[powerUps[i].nPowerUp] > 0)
            {
                s += n++ ? ",[" : "[";
                jsonString(s, powerUps[i].pzName);
                s += "," + std::to_string(pPlayer->pwUpTime[powerUps[i].nPowerUp] / kTicRate) + "]";
            }
        s += "],";
        int nDeaths = 0;
        for (int k = connecthead; k >= 0; k = connectpoint2[k])
            nDeaths += gPlayer[k].fragInfo[p];
        jsonInt(s, "frags", pPlayer->fragCount);
        jsonInt(s, "deaths", nDeaths);
        jsonInt(s, "x", pPlayer->pSprite->x);
        jsonInt(s, "y", pPlayer->pSprite->y);
        jsonInt(s, "z", pPlayer->pSprite->z);
        s += "\"ang\":" + std::to_string(pPlayer->pSprite->ang & 2047) + "}";
    }
    s += "],\"enemies\":[";
    int nEnemies = 0;
    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0 && nEnemies < 400; nSprite = nextspritestat[nSprite])
    {
        spritetype *pSprite = &sprite[nSprite];
        if (pSprite->type < kDudeBase || pSprite->type >= kDudePlayer1 || pSprite->extra <= 0 || xsprite[pSprite->extra].health <= 0)
            continue;
        s += nEnemies++ ? ",[" : "[";
        s += std::to_string(pSprite->x) + "," + std::to_string(pSprite->y) + "," + std::to_string(pSprite->ang & 2047) + "]";
    }
    s += "]}";
}

static void buildMap(std::string &s)
{
    s = "[";
    bool bFirst = true;
    for (int nSector = 0; nSector < numsectors; nSector++)
    {
        for (int nWall = sector[nSector].wallptr, nEnd = nWall + sector[nSector].wallnum; nWall < nEnd; nWall++)
        {
            walltype *pWall = &wall[nWall];
            int nNext = pWall->nextwall;
            int nType = 0;
            if (nNext >= 0)
            {
                if (nNext < nWall)  // each two-sided wall once
                    continue;
                int nNextSector = pWall->nextsector;
                if (nNextSector < 0 || (sector[nNextSector].floorz == sector[nSector].floorz && sector[nNextSector].ceilingz == sector[nSector].ceilingz))
                    continue;  // like the automap: no line where nothing changes
                nType = 1;
            }
            walltype *pWall2 = &wall[pWall->point2];
            if (!bFirst)
                s += ',';
            bFirst = false;
            s += std::to_string(pWall->x) + "," + std::to_string(pWall->y) + "," + std::to_string(pWall2->x) + "," + std::to_string(pWall2->y) + "," + std::to_string(nType);
        }
    }
    s += "]";
}

void onlineFrame(void)
{
    if (gOnlineMode == kOnlineNone)
        return;

    char msg[512];
    while (relayReadText(msg, sizeof(msg)))
    {
        std::string cmd = msgField(msg, 0);
        if (lobbyMessage(msg))
            continue;
        if (cmd == "peer_join" && gGameStarted)
        {
            char buffer[64];
            Bsnprintf(buffer, sizeof(buffer), "%s joined the lobby", msgField(msg, 2).c_str());
            viewSetMessage(buffer);
        }
        else if (cmd == "hosted" && gOnlineMode == kOnlineReport)
            bReportSent = true;
        else if (cmd == "error" && gOnlineMode == kOnlineReport)
        {
            setStatus(msgField(msg, 1).c_str());
            relayClose();
        }
    }

    if (relayStatus() != kRelayOnline)
        return;
    if (gOnlineMode == kOnlineReport && !bReportSent)
    {
        static uint32_t nLastTry;
        if (timerGetTicks() - nLastTry < 2000)
            return;
        nLastTry = timerGetTicks();
        lobbyNameFor(zLobbyName, sizeof(zLobbyName));
        std::string name = std::string(zLobbyName) + " - LAN";
        std::string cmd = "report\t" + name.substr(0, 24) + "\t" + std::to_string(gNetPlayers) + "\t" + cleanField(playerName()) +
            "\t" + cleanField(modCurrentName()) + "\t" + onlineVersion();
        relaySendText(cmd.c_str());
        relaySendText("state\tplaying");
        bReportSent = true;  // "hosted" or "error" follows
        return;
    }
    if (gOnlineMode == kOnlineHost && nBotsSent != gBotNetCount && gNetMode == NETWORK_SERVER)
    {
        nBotsSent = gBotNetCount;  // the lobby list counts them apart from the people
        relaySendText(("bots\t" + std::to_string(gBotNetCount)).c_str());
    }
    if (gOnlineMode == kOnlineClient || !gGameStarted || gDemo.bPlaying)
        return;

    char mapId[64];
    Bsnprintf(mapId, sizeof(mapId), "%d-%d-%08x-%d", gGameOptions.nEpisode, gGameOptions.nLevel, gGameOptions.uMapCRC, numwalls);
    if (gLevelTime < nLastLevelTime)
        mapSentFor.clear();  // level restarted
    nLastLevelTime = gLevelTime;
    if (mapSentFor != mapId)
    {
        std::string walls;
        buildMap(walls);
        std::string cmd = std::string("map\t") + mapId + "\t" + walls;
        relaySendText(cmd.c_str());
        mapSentFor = mapId;
    }

    uint32_t nNow = timerGetTicks();
    if ((int32_t)(nNow - nNextStats) < 0)
        return;
    nNextStats = nNow + 200;
    std::string stats;
    buildStats(stats);
    stats.insert(0, "stats\t");
    relaySendText(stats.c_str());
}

// ---- the host's user map for players who don't have it -----------------------------------------
//
// Whoever starts a game on a user map hands the file to the relay just before the start packet
// goes out (netBroadcastNewGame()). The relay checks it (server/mapcheck.py) and the others get it
// when their game starts the level (StartLevel()), unless their own copy is the same file. The
// game's loader checks every map again (dbLoadMap()), so a damaged one can't do harm.

#define kMaxSharedMap (2 * 1024 * 1024)

static std::string mapSharedByMe;  // the file this machine handed over for the current game

// the bytes the loader would read for this map, like dbLoadMap() finds them
static bool readMapData(const char *pzMap, std::string &data)
{
    int const nOldMode = pathsearchmode;
    pathsearchmode = 1;
    char name[BMAX_PATH];
    Bstrncpyz(name, pzMap, sizeof(name));
    Bstrupr(name);
    DICTNODE *pNode = *gSysRes.Probe(name, "MAP");
    if (pNode && (pNode->flags & DICT_EXTERNAL))
        gSysRes.RemoveNode(pNode);
    pNode = gSysRes.Lookup(pzMap, "MAP");
    if (!pNode)
    {
        Bstrncpyz(name, pzMap, sizeof(name));
        ChangeExtension(name, "");
        pNode = gSysRes.Lookup(name, "MAP");
    }
    bool bOk = false;
    if (pNode && pNode->size > 0 && pNode->size <= kMaxSharedMap)
    {
        const char *pData = (const char *)gSysRes.Lock(pNode);
        if (pData)
        {
            data.assign(pData, pNode->size);
            bOk = true;
        }
        gSysRes.Unlock(pNode);
    }
    pathsearchmode = nOldMode;
    return bOk;
}

// "e1m1x", "maps/My Map.map" -> "MAP.MAP"-style name the relay accepts
static std::string sharedMapName(const char *pzMap)
{
    const char *pBase = pzMap;
    for (const char *p = pzMap; *p; p++)
        if (*p == '/' || *p == '\\')
            pBase = p + 1;
    std::string name;
    for (const char *p = pBase; *p && name.size() < 56; p++)
    {
        if (isalnum((unsigned char)*p) || *p == '_' || *p == '-')
            name += (char)toupper((unsigned char)*p);
        else if (*p == '.')
        {
            if (!Bstrcasecmp(p, ".map"))
                break;
            name += '.';
        }
        else if (*p == ' ')
            name += '_';
    }
    if (name.empty())
        name = "USERMAP";
    return name + ".MAP";
}

void onlineShareMap(const char *pzMap)
{
    mapSharedByMe.clear();
    if ((gOnlineMode != kOnlineHost && gOnlineMode != kOnlineClient) || relayStatus() != kRelayOnline || !pzMap || !pzMap[0])
        return;
    std::string data;
    if (!readMapData(pzMap, data))
    {
        LOG_F(WARNING, "Online: can't read user map %s to share it", pzMap);
        return;
    }
    std::string name = sharedMapName(pzMap);
    std::string cmd = "mapfile\t" + name + "\t" + relaySha256Hex(data.data(), data.size()) + "\t" + relayBase64(data.data(), data.size());
    relaySendText(cmd.c_str());
    mapSharedByMe = name;
    LOG_F(INFO, "Online: sharing user map %s (%d bytes)", name.c_str(), (int)data.size());
}

bool onlineFetchMap(const char *pzMap, char *pzLoad, int nSize)
{
    if ((gOnlineMode != kOnlineHost && gOnlineMode != kOnlineClient) || relayStatus() != kRelayOnline || !pzMap || !pzMap[0])
        return false;
    std::string name = sharedMapName(pzMap);
    if (!Bstrcasecmp(name.c_str(), mapSharedByMe.c_str()))
        return false;  // this machine started the game with it
    std::string mine, mineSha;
    if (readMapData(pzMap, mine))
        mineSha = relaySha256Hex(mine.data(), mine.size());
    relaySendText(("getmap\t" + name + "\t" + mineSha).c_str());

    std::string reply;
    char text[96];
    Bsnprintf(text, sizeof(text), "Getting the map %s", name.c_str());
    bool bConsoleOld = bConsoleWait;
    bConsoleWait = false;
    bool bGot = onlineWait(text, 60000, [&]() {
        std::string m;
        while (relayReadMapReply(m))
        {
            if (!Bstrcasecmp(msgField(m.c_str(), 1).c_str(), name.c_str()) && msgField(m.c_str(), 0).compare(0, 4, "map_") == 0)
            {
                reply.swap(m);
                return true;
            }
        }
        return false;
    });
    bConsoleWait = bConsoleOld;
    if (!bGot)
    {
        LOG_F(WARNING, "Online: no answer about map %s (%s)", name.c_str(), onlineStatusText());
        return false;
    }
    std::string cmd = msgField(reply.c_str(), 0);
    if (cmd != "map_data")
    {
        LOG_F(INFO, "Online: map %s: %s", name.c_str(), cmd == "map_same" ? "you have the same file" : "the host didn't share it");
        return false;
    }
    // map_data <name> <sha256> <base64>; field() stops at a newline, base64 has none
    size_t nAt = 0;
    for (int i = 0; i < 3 && nAt != std::string::npos; i++)
        nAt = reply.find('\t', nAt + (i ? 1 : 0));
    std::string data;
    std::string sha = msgField(reply.c_str(), 2);
    if (nAt == std::string::npos || !relayUnbase64(reply.substr(nAt + 1), data) || data.size() < 64 || data.size() > kMaxSharedMap
        || relaySha256Hex(data.data(), data.size()) != sha || memcmp(data.data(), "BLM\x1a", 4))
    {
        LOG_F(ERROR, "Online: the map %s arrived damaged", name.c_str());
        return false;
    }
    char dir[BMAX_PATH], path[BMAX_PATH];
    G_ModDirSnprintfLite(dir, sizeof(dir), "downloads");
    Bmkdir(dir, S_IRWXU);
    Bsnprintf(path, sizeof(path), "%s/%s", dir, name.c_str());
    FILE *fp = fopen(path, "wb");
    if (!fp || fwrite(data.data(), 1, data.size(), fp) != data.size())
    {
        if (fp)
            fclose(fp);
        LOG_F(ERROR, "Online: can't save the map to %s", path);
        return false;
    }
    fclose(fp);
    LOG_F(INFO, "Online: got the map %s (%d bytes) -> %s", name.c_str(), (int)data.size(), path);
    Bstrncpyz(pzLoad, path, nSize);
    return true;
}
