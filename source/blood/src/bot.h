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
#pragma once

// NotBlood Chosen bots: computer players for bloodbath/teams (and co-op) matches.
// A bot is an ordinary player slot whose input is made by botGetInput() instead of a
// keyboard or the network, so the game itself treats it exactly like a human player.

#include "controls.h"

enum {
    kBotSkillEasy = 0,
    kBotSkillNormal,
    kBotSkillHard,
    kBotSkillNightmare,
    kBotSkillMax
};

enum {
    kBotPersonaMixed = 0, // every bot gets one of the others
    kBotPersonaBalanced,
    kBotPersonaRusher,
    kBotPersonaCamper,
    kBotPersonaTrickster,
    kBotPersonaCoward,
    kBotPersonaJumper,
    kBotPersonaMax
};

#define kBotMaxBots 7

extern int gBotCount;
extern int gBotSkill;
extern int gBotPersona;
extern int gBotEachSkill[kBotMaxBots];   // -1: same as gBotSkill
extern int gBotEachPersona[kBotMaxBots]; // -1: same as gBotPersona
extern int gBotAccuracy;   // percent of the difficulty's value
extern int gBotReaction;
extern int gBotAggression;
extern int gBotJumping;
extern int gBotAmbush;
extern int gBotFlank;
extern int gBotCamping;
extern int gBotTaunts;
extern int gBotDebug;
extern int gBotCultists;
extern int gBotHandicap;  // 0-4: how much damage bots take (2 normal), shown next to their names
extern int gBotHighGround;
extern int gBotGrudges;
extern int gBotAdaptive;

extern bool gBotMatch;
extern const char *gBotSkillNames[kBotSkillMax];
extern const char *gBotPersonaNames[kBotPersonaMax];

enum { kBotOrderFree = 0, kBotOrderFollow, kBotOrderHold };
void botOrder(int nOrder); // for the bots on the human's side

// watching a bots-only match as a spectator
extern bool gBotWatchMode;

// the career: a ladder of matches against named bots
#define kCareerStages 10
extern int gCareerStage, gCareerWins, gCareerMatches, gCareerKills, gCareerDeaths, gCareerBestStreak;
extern char gCareerRivals[128];
extern int gBotCareer; // the stage being played (1-10), 0 none
void careerDescribe(int nStage, char *pzLine1, char *pzLine2, int nSize);
void careerNemesis(char *pzBuffer, int nSize);
void careerPlay(void);
void careerReset(void);

bool botIsBot(int nPlayer);
void botSetupMatch(void);   // called by StartNetGame() when offline: adds the bots as players
void botEndMatch(void);     // back to a normal single player game
void botLevelStart(void);   // after StartLevel() spawned everybody
void botGetInput(int nPlayer, GINPUT *pInput);
void botDrawDebug(void);
void botRegisterCvars(void);
const char *botStateName(int nPlayer);
