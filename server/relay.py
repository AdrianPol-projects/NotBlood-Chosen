#!/usr/bin/env python3
"""NotBlood online relay and live game dashboard.

Games can't reach each other directly over the internet (routers, NAT), so every player opens one
WebSocket to this server and the game's normal network traffic is passed through it:

  host game  <-- ws -->  relay  <-- ws -->  joining games

The game still runs its usual ENet (UDP) connection, only tunnelled: each WebSocket binary frame
carries one UDP datagram. Frames from the host start with the id of the player they are meant
for; frames to the host start with the id of the player they came from.

The same connection carries live game stats (players, health, weapons, positions, the level's
walls) which the dashboard at / shows in a browser.

Everything needs the server key: game and browser prove they know it by answering a random
challenge with HMAC-SHA256(key, challenge), so the key itself never crosses the network.
"""

import asyncio
import hashlib
import hmac
import json
import logging
import os
import re
import secrets
import time
from pathlib import Path

from aiohttp import WSMsgType, web

PROTOCOL = 1

KEY = os.environ.get("BLOOD_KEY", "")
PORT = int(os.environ.get("PORT", "8080"))
DATA_DIR = Path(os.environ.get("DATA_DIR", "/data"))
STATIC_DIR = Path(__file__).resolve().parent / "static"

MAX_CONNECTIONS = 200
MAX_CONNECTIONS_PER_IP = 24
MAX_LOBBIES = 50
MAX_GAME_FRAME = 64 * 1024
MAX_STATS = 256 * 1024
MAX_MAP = 4 * 1024 * 1024
AUTH_TIMEOUT = 15
HISTORY_KEEP = 100
DASH_INTERVAL = 0.2

log = logging.getLogger("relay")

NAME_RE = re.compile(r"^[A-Za-z0-9 _.\-]{1,24}$")


def clean_name(s, limit=24):
    s = "".join(c for c in s if c.isprintable() and c not in "\t\r\n")
    return s.strip()[:limit]


def client_ip(request):
    return (request.headers.get("CF-Connecting-IP")
            or request.headers.get("X-Forwarded-For", "").split(",")[0].strip()
            or request.remote or "?")


class Guard:
    """Connection counting and failed-key lockout, per IP."""

    def __init__(self):
        self.conns = {}
        self.total = 0
        self.failures = {}

    def locked(self, ip):
        now = time.time()
        f = [t for t in self.failures.get(ip, []) if now - t < 600]
        self.failures[ip] = f
        return len(f) >= 10

    def fail(self, ip):
        self.failures.setdefault(ip, []).append(time.time())

    def enter(self, ip):
        if self.total >= MAX_CONNECTIONS or self.conns.get(ip, 0) >= MAX_CONNECTIONS_PER_IP:
            return False
        self.total += 1
        self.conns[ip] = self.conns.get(ip, 0) + 1
        return True

    def leave(self, ip):
        self.total -= 1
        self.conns[ip] -= 1
        if not self.conns[ip]:
            del self.conns[ip]


guard = Guard()


class Lobby:
    def __init__(self, name, kind, host, max_players, host_name, mod, version):
        self.name = name
        self.kind = kind  # "online": joinable through the relay; "lan": stats only
        self.host = host
        self.max_players = max_players
        self.host_name = host_name
        self.mod = mod
        self.version = version
        self.clients = {}  # id -> Conn
        self.next_id = 1
        self.state = "waiting"
        self.created = time.time()
        self.started = None
        self.stats = None
        self.stats_time = 0
        self.map = None
        self.map_id = None
        self.levels = []
        self.player_names = set()

    def summary(self):
        return {
            "name": self.name,
            "kind": self.kind,
            "host": self.host_name,
            "players": [self.host_name] + [c.player_name for c in self.clients.values()],
            "max": self.max_players,
            "state": self.state,
            "mod": self.mod,
            "version": self.version,
            "created": self.created,
            "started": self.started,
            "map_id": self.map_id,
            "stats": self.stats,
            "stats_age": round(time.time() - self.stats_time, 1) if self.stats_time else None,
        }


class Relay:
    def __init__(self):
        self.lobbies = {}  # lower case name -> Lobby
        self.dashboards = set()
        self.history = []
        self.history_file = DATA_DIR / "history.json"
        try:
            self.history = json.loads(self.history_file.read_text())[-HISTORY_KEEP:]
        except Exception:
            self.history = []

    def save_history(self):
        try:
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            tmp = self.history_file.with_suffix(".tmp")
            tmp.write_text(json.dumps(self.history))
            tmp.replace(self.history_file)
        except Exception as e:
            log.warning("could not save history: %s", e)

    def close_lobby(self, lobby):
        if self.lobbies.get(lobby.name.lower()) is lobby:
            del self.lobbies[lobby.name.lower()]
        if lobby.started:
            players = sorted(lobby.player_names)
            self.history.append({
                "name": lobby.name, "kind": lobby.kind, "host": lobby.host_name, "players": players,
                "mod": lobby.mod, "started": lobby.started, "ended": time.time(),
                "levels": lobby.levels[-20:],
                "final": lobby.stats,
            })
            self.history = self.history[-HISTORY_KEEP:]
            self.save_history()
        log.info("lobby %r closed", lobby.name)

    def snapshot(self):
        return {
            "op": "state",
            "time": time.time(),
            "lobbies": [l.summary() for l in self.lobbies.values()],
        }


relay = Relay()


class Conn:
    def __init__(self, ws, ip):
        self.ws = ws
        self.ip = ip
        self.lobby = None
        self.peer_id = 0
        self.player_name = ""
        self.is_host = False
        self.send_lock = asyncio.Lock()

    async def text(self, *fields):
        if not self.ws.closed:
            try:
                await self.ws.send_str("\t".join(str(f) for f in fields))
            except Exception:
                pass

    async def binary(self, data):
        if not self.ws.closed:
            try:
                await self.ws.send_bytes(data)
            except Exception:
                pass


async def authenticate(ws, ip):
    """Challenge/response with the server key. Returns True when the peer knows the key."""
    nonce = secrets.token_hex(16)
    await ws.send_str("challenge\t" + nonce)
    try:
        msg = await ws.receive(timeout=AUTH_TIMEOUT)
    except asyncio.TimeoutError:
        return None
    if msg.type != WSMsgType.TEXT:
        return None
    fields = msg.data.split("\t")
    if len(fields) < 2 or fields[0] != "auth":
        return None
    want = hmac.new(KEY.encode(), nonce.encode(), hashlib.sha256).hexdigest()
    if not KEY or not hmac.compare_digest(want, fields[1].strip().lower()):
        guard.fail(ip)
        await ws.send_str("error\tWrong server key")
        return None
    return fields[2:]


async def game_ws(request):
    ip = client_ip(request)
    if guard.locked(ip) or not guard.enter(ip):
        raise web.HTTPTooManyRequests()
    ws = web.WebSocketResponse(max_msg_size=MAX_MAP + 1024, heartbeat=None, autoping=True)
    await ws.prepare(request)
    conn = Conn(ws, ip)
    try:
        extra = await authenticate(ws, ip)
        if extra is None:
            return ws
        proto = extra[0] if extra else ""
        if proto != str(PROTOCOL):
            await conn.text("error", "Server protocol %d, game protocol %s - update the game or the server" % (PROTOCOL, proto))
            return ws
        await conn.text("ok", PROTOCOL)
        async for msg in ws:
            if msg.type == WSMsgType.BINARY:
                await on_game_binary(conn, msg.data)
            elif msg.type == WSMsgType.TEXT:
                if not await on_game_text(conn, msg.data):
                    break
            elif msg.type in (WSMsgType.ERROR, WSMsgType.CLOSE):
                break
    finally:
        await drop_conn(conn)
        guard.leave(ip)
    return ws


async def drop_conn(conn):
    lobby = conn.lobby
    if not lobby:
        return
    conn.lobby = None
    if conn.is_host:
        relay.close_lobby(lobby)
        for c in list(lobby.clients.values()):
            c.lobby = None
            await c.text("host_left")
            await c.ws.close()
    else:
        lobby.clients.pop(conn.peer_id, None)
        if lobby.host:
            await lobby.host.text("peer_leave", conn.peer_id)
        log.info("%s left lobby %r", conn.player_name, lobby.name)


async def on_game_binary(conn, data):
    lobby = conn.lobby
    if not lobby or lobby.kind != "online" or len(data) > MAX_GAME_FRAME:
        return
    if conn.is_host:
        if len(data) < 2:
            return
        target = lobby.clients.get(data[0])
        if target:
            await target.binary(data[1:])
    else:
        await lobby.host.binary(bytes([conn.peer_id]) + data)


async def on_game_text(conn, text):
    f = text.split("\t")
    cmd = f[0]
    lobby = conn.lobby

    if cmd == "list":
        lines = ["lobbies"]
        for l in relay.lobbies.values():
            if l.kind != "online":
                continue
            lines.append("\t".join(str(x) for x in (
                l.name, l.host_name, 1 + len(l.clients), l.max_players, l.state, l.mod, l.version)))
        await conn.ws.send_str("\n".join(lines))

    elif cmd in ("host", "report") and not lobby:
        # host <lobby> <max players> <player name> <mod> <game version>
        if len(f) < 6:
            await conn.text("error", "Bad request")
            return True
        name, player, mod, version = clean_name(f[1]), clean_name(f[3], 16), clean_name(f[4], 64), clean_name(f[5], 64)
        try:
            max_players = max(2, min(8, int(f[2])))
        except ValueError:
            max_players = 2
        if not NAME_RE.match(name):
            await conn.text("error", "Lobby names can use letters, digits, space, _ . -")
            return True
        if name.lower() in relay.lobbies:
            await conn.text("error", "A lobby called '%s' already exists" % name)
            return True
        if len(relay.lobbies) >= MAX_LOBBIES:
            await conn.text("error", "Too many lobbies on this server")
            return True
        lobby = Lobby(name, "online" if cmd == "host" else "lan", conn, max_players, player or "Host", mod, version)
        lobby.player_names.add(lobby.host_name)
        relay.lobbies[name.lower()] = lobby
        conn.lobby, conn.is_host, conn.player_name = lobby, True, lobby.host_name
        log.info("%s %s lobby %r (%d players, mod %r, %s) from %s", lobby.host_name,
                 "hosts" if cmd == "host" else "reports", name, max_players, mod, version, conn.ip)
        await conn.text("hosted", name)

    elif cmd == "join" and not lobby:
        # join <lobby> <player name> <mod> <game version>
        if len(f) < 5:
            await conn.text("error", "Bad request")
            return True
        lobby = relay.lobbies.get(clean_name(f[1]).lower())
        player, mod, version = clean_name(f[2], 16) or "Player", clean_name(f[3], 64), clean_name(f[4], 64)
        if not lobby or lobby.kind != "online":
            await conn.text("error", "No lobby called '%s'" % clean_name(f[1]))
            return True
        if version != lobby.version:
            await conn.text("error", "The host runs %s, you run %s - everybody needs the same version" % (lobby.version, version))
            return True
        if mod != lobby.mod:
            await conn.text("error", "The host plays with mod '%s', you have '%s'" % (lobby.mod or "none", mod or "none"))
            return True
        if lobby.state != "waiting":
            await conn.text("error", "That game has already started")
            return True
        if 1 + len(lobby.clients) >= lobby.max_players:
            await conn.text("error", "That lobby is full")
            return True
        while lobby.next_id in lobby.clients or lobby.next_id == 0:
            lobby.next_id = lobby.next_id % 255 + 1
        conn.peer_id = lobby.next_id
        lobby.next_id = lobby.next_id % 255 + 1
        conn.lobby, conn.player_name = lobby, player
        lobby.clients[conn.peer_id] = conn
        lobby.player_names.add(player)
        log.info("%s joined lobby %r as %d", player, lobby.name, conn.peer_id)
        await lobby.host.text("peer_join", conn.peer_id, player)
        await conn.text("joined", lobby.name, conn.peer_id, lobby.host_name)

    elif cmd == "state" and lobby and conn.is_host and len(f) >= 2:
        state = clean_name(f[1], 32)
        if state == "playing" and not lobby.started:
            lobby.started = time.time()
        lobby.state = state

    elif cmd == "stats" and lobby and conn.is_host and len(f) >= 2:
        if len(f[1]) <= MAX_STATS:
            try:
                stats = json.loads(f[1])
            except ValueError:
                return True
            if isinstance(stats, dict):
                lobby.stats = stats
                lobby.stats_time = time.time()
                if not lobby.started:
                    lobby.started = time.time()
                for p in stats.get("players", []):
                    if isinstance(p, dict) and isinstance(p.get("name"), str):
                        lobby.player_names.add(p["name"][:16])
                level = stats.get("level")
                if isinstance(level, str) and (not lobby.levels or lobby.levels[-1] != level):
                    lobby.levels.append(level[:64])

    elif cmd == "map" and lobby and conn.is_host and len(f) >= 3:
        if len(f[2]) <= MAX_MAP:
            try:
                walls = json.loads(f[2])
            except ValueError:
                return True
            lobby.map = f[2]
            lobby.map_id = clean_name(f[1], 64)
            for d in list(relay.dashboards):
                if d.watching == lobby.name:
                    await d.send_map(lobby)

    elif cmd == "ping":
        await conn.text("pong", *f[1:2])

    elif cmd == "bye":
        return False

    return True


class Dash:
    def __init__(self, ws):
        self.ws = ws
        self.watching = None
        self.sent_map = None

    async def send(self, obj):
        if not self.ws.closed:
            try:
                await self.ws.send_str(json.dumps(obj, separators=(",", ":")))
            except Exception:
                pass

    async def send_map(self, lobby):
        if lobby.map and self.sent_map != (lobby.name, lobby.map_id) and not self.ws.closed:
            self.sent_map = (lobby.name, lobby.map_id)
            try:
                await self.ws.send_str('{"op":"map","lobby":%s,"map_id":%s,"walls":%s}' % (
                    json.dumps(lobby.name), json.dumps(lobby.map_id), lobby.map))
            except Exception:
                pass


async def dash_ws(request):
    ip = client_ip(request)
    if guard.locked(ip) or not guard.enter(ip):
        raise web.HTTPTooManyRequests()
    ws = web.WebSocketResponse(max_msg_size=64 * 1024, heartbeat=30)
    await ws.prepare(request)
    dash = Dash(ws)
    try:
        if await authenticate(ws, ip) is None:
            return ws
        await dash.send({"op": "ok", "history": relay.history[-30:]})
        relay.dashboards.add(dash)
        async for msg in ws:
            if msg.type != WSMsgType.TEXT:
                continue
            try:
                req = json.loads(msg.data)
            except ValueError:
                continue
            if req.get("op") == "watch":
                dash.watching = req.get("lobby")
                dash.sent_map = None
                lobby = relay.lobbies.get(str(dash.watching).lower())
                if lobby:
                    await dash.send_map(lobby)
            elif req.get("op") == "history":
                await dash.send({"op": "history", "history": relay.history[-30:]})
    finally:
        relay.dashboards.discard(dash)
        guard.leave(ip)
    return ws


async def dash_pusher(app):
    async def loop():
        while True:
            await asyncio.sleep(DASH_INTERVAL)
            if not relay.dashboards:
                continue
            snap = json.dumps(relay.snapshot(), separators=(",", ":"))
            for d in list(relay.dashboards):
                if not d.ws.closed:
                    try:
                        await d.ws.send_str(snap)
                    except Exception:
                        pass
                    if d.watching:
                        lobby = relay.lobbies.get(str(d.watching).lower())
                        if lobby:
                            await d.send_map(lobby)
    task = asyncio.create_task(loop())
    yield
    task.cancel()


async def index(request):
    return web.FileResponse(STATIC_DIR / "index.html", headers={"Cache-Control": "no-cache"})


async def health(request):
    return web.json_response({"ok": True, "lobbies": len(relay.lobbies), "protocol": PROTOCOL})


@web.middleware
async def security_headers(request, handler):
    resp = await handler(request)
    resp.headers.setdefault("X-Content-Type-Options", "nosniff")
    resp.headers.setdefault("Referrer-Policy", "no-referrer")
    resp.headers.setdefault("X-Frame-Options", "DENY")
    return resp


def make_app():
    app = web.Application(middlewares=[security_headers], client_max_size=64 * 1024)
    app.router.add_get("/", index)
    app.router.add_get("/ws", game_ws)
    app.router.add_get("/dash", dash_ws)
    app.router.add_get("/healthz", health)
    app.router.add_static("/static/", STATIC_DIR, show_index=False)
    app.cleanup_ctx.append(dash_pusher)
    return app


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if not KEY:
        log.warning("BLOOD_KEY is not set - nobody can connect")
    web.run_app(make_app(), port=PORT, access_log=None)
