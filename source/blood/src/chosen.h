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

// NotBlood Chosen: this fork's name and version. Network games need the same version on every
// machine, so bump CHOSEN_VERSION whenever something that affects the game state changes.

#pragma once

#define CHOSEN_NAME "NotBlood Chosen"
#define CHOSEN_VERSION "1.4"

// the public server ONLINE uses while no server is entered ("" for none): put the address of a
// relay running with PUBLIC=1 here (SERVER.md), e.g. "blood.example.com"
#ifndef CHOSEN_PUBLIC_SERVER
#define CHOSEN_PUBLIC_SERVER "blood.pi-mole.com"
#endif
