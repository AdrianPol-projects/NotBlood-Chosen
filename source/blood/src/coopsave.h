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

// Co-op saves.
//
// Saving (any player, from the in-game menu) writes two kinds of save on every machine:
//
// * carry-over (coopsaves/carryover/slotN.txt): what co-op already carries from one level to
//   the next - every player's weapons, ammo, health, armor and inventory - plus the level and
//   all game settings. Continuing one restarts that level with those loadouts, and works with
//   any players: loadouts go to players with the same name, everybody else starts with the
//   pitchfork.
//
// * mid-level (coopsaves/midlevel/slotN.sav): the complete game state (positions, keys,
//   enemies, doors, ...), using the normal save code. To load one the host sends its file to
//   everybody, then all machines load it at the same moment, so the game stays in sync.
//   Players are matched by name; saved players who aren't there are removed, new players
//   start at the level start with the pitchfork.

#pragma once

#define kCoopSlots 8
#define kCoopSlotAuto kCoopSlots  // carry-over: start of the current level; mid-level: when a player left

enum
{
    kCoopSaveCarry = 0,
    kCoopSaveMid,
    kCoopSaveTypes
};

extern int gCoopMatchOrder;      // players without a save of their own get leftover loadouts
extern int gCoopAutosaveLeave;   // mid-level autosave when a player leaves the game

// any player: everybody in the game saves to slot nSlot now
void coopRequestSave(int nSlot);
// packet 245 from another player asking for a save
void coopReceiveSaveRequest(int nPlayer, char *pPacket);

// carry-over save of the current level start (every machine, from StartLevel())
void coopWriteLevelStartSave(void);

// menu text for a save (level, then date and players); false if there is no usable save
bool coopDescribeSave(int nType, int nSlot, char *pzLevel, int nLevelSize, char *pzInfo, int nInfoSize);

// host: start a network game from a carry-over save
bool coopContinueGame(int nSlot);
// host: load a mid-level save for everybody
bool coopLoadMidLevel(int nSlot);

// packet 246: loadout of one player for the next continued game
void coopReceiveLoadout(char *pPacket);
// a start game packet arrived (bContinue is its co-op continue flag)
void coopStartPacketReceived(bool bContinue);
// packets 244 (part of the host's mid-level save) and 243 (load it now)
void coopReceiveFilePart(char *pPacket);
void coopReceiveLoadNow(char *pPacket);

// called by StartLevel() on every machine once all players have spawned in a new game
void coopApplyLoadouts(void);
// main loop: runs a mid-level load once the save has arrived
void coopFrame(void);
// a player left a co-op game
void coopPlayerLeft(int nPlayer);
// saves from older builds (coopsave.txt) move to the new folders
void coopMigrateOldSaves(void);
