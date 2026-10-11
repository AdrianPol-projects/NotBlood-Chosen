#!/usr/bin/env python3
"""Fake game for trying the dashboard: python simulate.py ws://127.0.0.1:8099 <key> [lobby]"""
import asyncio, hashlib, hmac, json, math, random, sys, time
import aiohttp

URL, KEY = sys.argv[1], sys.argv[2]
LOBBY = sys.argv[3] if len(sys.argv) > 3 else "Friday co-op"


def make_map():
    walls = []
    rooms = [(0, 0, 4096, 3072), (4096, 512, 7168, 2560), (7168, 0, 10240, 4096), (2048, 3072, 3072, 6144), (0, 6144, 6144, 8192)]
    for x1, y1, x2, y2 in rooms:
        walls += [x1, y1, x2, y1, 0, x2, y1, x2, y2, 0, x2, y2, x1, y2, 0, x1, y2, x1, y1, 0]
    for i in range(12):
        cx, cy = 1000 + i * 700, 7000
        walls += [cx, cy, cx + 300, cy, 1, cx + 300, cy, cx + 300, cy + 300, 1, cx + 300, cy + 300, cx, cy + 300, 1, cx, cy + 300, cx, cy, 1]
    return walls


async def main():
    async with aiohttp.ClientSession() as s:
        ws = await s.ws_connect(URL + "/ws")
        nonce = (await ws.receive_str()).split("\t")[1]
        await ws.send_str("auth\t%s\t1\tsim" % hmac.new(KEY.encode(), nonce.encode(), hashlib.sha256).hexdigest())
        print(await ws.receive_str())
        await ws.send_str("host\t%s\t4\tCaleb\t\tchosen-1" % LOBBY)
        print(await ws.receive_str())
        await ws.send_str("state\tplaying")
        await ws.send_str("map\te1m3-1\t" + json.dumps(make_map()))
        names = ["Caleb", "Ophelia", "Ishmael", "Gabriel"]
        t0 = time.time()
        enemies = [[random.randint(0, 10000), random.randint(0, 8000), 0] for _ in range(25)]
        while True:
            t = time.time() - t0
            players = []
            for i, n in enumerate(names):
                a = t * 0.4 + i * 1.6
                x = 5000 + math.cos(a) * (2500 + i * 400)
                y = 4000 + math.sin(a * 1.3) * 3000
                hp = int(60 + 50 * math.sin(t * 0.2 + i))
                players.append({
                    "id": i, "name": n, "alive": 1 if hp > 15 else 0, "health": max(0, hp),
                    "armor": [10 * i, 100 - 20 * i, 0], "weapon": ["Shotgun", "Tommy Gun", "Flare Gun", "Napalm"][i], "wid": [3, 4, 2, 5][i],
                    "ammo": 40 - i * 7, "weapons": list(range(1, 4 + i + int(t / 20) % 3)), "keys": [1, 2][: int(t / 15) % 3] if i < 2 else [],
                    "items": [["Doctor's Bag", 80]] if i == 1 else [], "powerups": [["Invisibility", 23]] if i == 2 else [],
                    "frags": 0, "deaths": 0, "x": int(x), "y": int(y), "z": 0, "ang": int((a * 2048 / (2 * math.pi) + 512) % 2048),
                })
            for e in enemies:
                e[0] += random.randint(-60, 60); e[1] += random.randint(-60, 60)
            stats = {"v": 1, "mode": "Co-op", "level": "E1M3: Wrong Side of the Tracks", "diff": "Well Done", "time": t,
                     "kills": [int(t / 4), 61], "secrets": [int(t / 40), 6], "players": players, "enemies": enemies}
            await ws.send_str("stats\t" + json.dumps(stats))
            await asyncio.sleep(0.2)


asyncio.run(main())
