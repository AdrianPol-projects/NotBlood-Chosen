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

class CGameMenu;
class CGameMenuItemChain;

extern CGameMenu menuOnline;

void SetupChosenMenus(void);
void SetupOnlineMenu(CGameMenuItemChain *pItem);
