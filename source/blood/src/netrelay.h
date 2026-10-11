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

// Connection to an online relay server (server/relay.py), run on a background thread.
//
// Online games keep using ENet exactly like LAN games; only the UDP datagrams travel through
// one WebSocket per player:
//   joining player: ENet client -> 127.0.0.1:<relayClientPort()> -> relay -> host
//   host:           relay -> one local UDP socket per remote player -> ENet server port
// So the game's network code doesn't know the difference, and a host can even mix LAN and
// online players.
//
// Text messages (lobby commands, stats) are tab separated, see relay.py.

#pragma once

enum
{
    kRelayIdle = 0,
    kRelayConnecting,  // resolving, connecting, WebSocket handshake, server key check
    kRelayOnline,      // connected and accepted by the server
    kRelayClosed,      // connection ended; relayError() says why
};

// starts connecting in the background (closes an earlier connection first)
void relayConnect(const char *pzServer, const char *pzKey, const char *pzGameVersion);
void relayClose(void);
int relayStatus(void);
const char *relayError(void);

// queues a text message for the server
void relaySendText(const char *pzText);
// next text message from the server not handled by the tunnel itself; false when there is none
bool relayReadText(char *pBuffer, int nSize);

// host: the local ENet server port remote players are passed to
void relaySetHostGamePort(int nPort);
// joining player: local port the ENet client has to connect to (0 until the lobby is joined)
int relayClientPort(void);
// number of datagrams passed through so far (both ways), for the connection screens
unsigned relayTraffic(void);
