#!/usr/bin/env python3
"""Protocol test for relay.py: python test_relay.py ws://127.0.0.1:8099 <key>"""
import asyncio, hashlib, hmac, json, sys
import aiohttp

URL, KEY = sys.argv[1], sys.argv[2]


async def login(session, path, key=KEY, extra=("1", "test")):
    ws = await session.ws_connect(URL + path)
    msg = await ws.receive_str()
    nonce = msg.split("\t")[1]
    mac = hmac.new(key.encode(), nonce.encode(), hashlib.sha256).hexdigest()
    await ws.send_str("\t".join(["auth", mac, *extra]))
    return ws


async def main():
    async with aiohttp.ClientSession() as s:
        bad = await login(s, "/ws", key="wrong")
        assert (await bad.receive_str()).startswith("error"), "wrong key accepted"
        host = await login(s, "/ws")
        assert (await host.receive_str()).startswith("ok")
        await host.send_str("host\tTest Lobby\t3\tCaleb\t\tchosen-1")
        assert (await host.receive_str()) == "hosted\tTest Lobby"
        cl = await login(s, "/ws")
        assert (await cl.receive_str()).startswith("ok")
        await cl.send_str("list")
        lst = await cl.receive_str()
        assert "Test Lobby\tCaleb\t1\t3\twaiting" in lst, lst
        await cl.send_str("join\ttest lobby\tOphelia\tcryptic\tchosen-1")
        assert "mod" in (await cl.receive_str())
        await cl.send_str("join\ttest lobby\tOphelia\t\tchosen-2")
        assert "version" in (await cl.receive_str())
        await cl.send_str("join\ttest lobby\tOphelia\t\tchosen-1")
        j = await cl.receive_str()
        assert j.startswith("joined\tTest Lobby\t"), j
        pid = int(j.split("\t")[2])
        assert (await host.receive_str()) == "peer_join\t%d\tOphelia" % pid
        await cl.send_bytes(b"hello host")
        assert (await host.receive_bytes()) == bytes([pid]) + b"hello host"
        await host.send_bytes(bytes([pid]) + b"hello client")
        assert (await cl.receive_bytes()) == b"hello client"
        await host.send_str("state\tplaying")
        stats = {"level": "E1M1: Cradle to Grave", "players": [{"id": 0, "name": "Caleb", "health": 100, "x": 0, "y": 0, "ang": 0}]}
        await host.send_str("stats\t" + json.dumps(stats))
        await host.send_str("map\te1m1-1\t[0,0,1024,0,0,1024,0,1024,1024,1]")
        dash = await login(s, "/dash", extra=())
        ok = json.loads(await dash.receive_str())
        assert ok["op"] == "ok"
        await dash.send_str(json.dumps({"op": "watch", "lobby": "Test Lobby"}))
        got_map = got_state = False
        while not (got_map and got_state):
            m = json.loads(await dash.receive_str())
            if m["op"] == "map":
                assert m["walls"][2] == 1024
                got_map = True
            if m["op"] == "state" and m["lobbies"] and m["lobbies"][0]["stats"]:
                assert m["lobbies"][0]["players"] == ["Caleb", "Ophelia"]
                got_state = True
        await host.close()
        assert (await cl.receive_str()) == "host_left"
        print("all relay tests passed")


asyncio.run(main())
