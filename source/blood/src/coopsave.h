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

// Co-op progress saves ("carry-over" saves).
//
// Network games run in lockstep, so a regular mid-level save can't be restored for several
// players. Instead a co-op save stores what co-op already carries from one level to the next:
// every player's weapons, ammo, health, armor and inventory, plus the level and all game
// settings. Continuing one restarts that level for everybody with those loadouts.
//
// The host reads the save, sends each connected player's loadout (packet 246) and then the
// usual start game packet flagged as a continue; every machine applies the loadouts at the same
// point of StartLevel(), so the game stays in sync.

#pragma once

enum
{
    kCoopSaveManual = 0,
    kCoopSaveAuto,  // written by every machine at the start of each co-op level
    kCoopSaveMax
};

// any player: everybody in the game writes their manual co-op save now
void coopRequestSave(void);
// packet 245 from another player asking for a save
void coopReceiveSaveRequest(int nPlayer);

// writes the co-op save of this machine (no-op outside of co-op games)
bool coopWriteSave(int nSlot, const char *pzSavedBy);

// menu text for a save (level, then date and player count); false if there is no usable save
bool coopDescribeSave(int nSlot, char *pzLevel, int nLevelSize, char *pzInfo, int nInfoSize);

// host: start a network game from a co-op save
bool coopContinueGame(int nSlot);

// packet 246: loadout of one player for the next continued game
void coopReceiveLoadout(char *pPacket);
// a start game packet arrived (bContinue is its co-op continue flag)
void coopStartPacketReceived(bool bContinue);

// called by StartLevel() on every machine once all players have spawned in a new game
void coopApplyLoadouts(void);
