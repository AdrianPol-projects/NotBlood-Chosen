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
        # chat goes to everybody in the lobby, the sender too
        await cl.send_str("chat\thello\x01 everybody")
        assert (await cl.receive_str()) == "chat\tOphelia\thello everybody"
        assert (await host.receive_str()) == "chat\tOphelia\thello everybody"
        # bots are counted apart from the players
        await host.send_str("bots\t3")
        await cl.send_str("list")
        lst = await cl.receive_str()
        assert "Test Lobby\tCaleb\t2\t3\tplaying\t\tchosen-1\t3\t0\t" in lst, lst
        # the host's user map: refused when damaged, handed on when good, "same" when the player has it
        import base64, os, glob
        maps = glob.glob(os.environ.get("TEST_MAPS", "/nonexistent") + "/*.MAP")
        if maps:
            data = open(maps[0], "rb").read()
            bad = bytearray(data); bad[60] ^= 0x55; bad = bytes(bad)
            await host.send_str("mapfile\tBAD.MAP\t%s\t%s" % (hashlib.sha256(bad).hexdigest(), base64.b64encode(bad).decode()))
            r = await host.receive_str()
            assert r.startswith("mapfile_error\tBAD.MAP"), r
            sha = hashlib.sha256(data).hexdigest()
            await cl.send_str("getmap\tMY.MAP\t")  # asked before the upload: waits for it
            await host.send_str("mapfile\tMY.MAP\t%s\t%s" % (sha, base64.b64encode(data).decode()))
            assert (await host.receive_str()) == "mapfile_ok\tMY.MAP"
            r = (await cl.receive_str()).split("\t")
            assert r[:3] == ["map_data", "MY.MAP", sha] and base64.b64decode(r[3]) == data, r[:3]
            await cl.send_str("getmap\tmy.map\t" + sha)
            assert (await cl.receive_str()) == "map_same\tMY.MAP"
            await cl.send_str("list")
            assert "\tMY.MAP" in (await cl.receive_str())
        else:
            print("(map sharing not tested: set TEST_MAPS to a folder with Blood .MAP files)")
        await host.close()
        assert (await cl.receive_str()) == "host_left"

        # a private lobby needs its password
        host = await login(s, "/ws")
        assert (await host.receive_str()).startswith("ok")
        pw = hashlib.sha256(b"secret").hexdigest()
        await host.send_str("host\tPrivate\t4\tCaleb\t\tchosen-1\t" + pw)
        assert (await host.receive_str()) == "hosted\tPrivate"
        cl = await login(s, "/ws")
        assert (await cl.receive_str()).startswith("ok")
        await cl.send_str("list")
        assert "Private\tCaleb\t1\t4\twaiting\t\tchosen-1\t0\t1\t" in (await cl.receive_str())
        await cl.send_str("join\tPrivate\tOphelia\t\tchosen-1")
        assert "password" in (await cl.receive_str())
        await cl.send_str("join\tPrivate\tOphelia\t\tchosen-1\t" + hashlib.sha256(b"wrong").hexdigest())
        assert "Wrong lobby password" in (await cl.receive_str())
        await cl.send_str("join\tPrivate\tOphelia\t\tchosen-1\t" + pw)
        assert (await cl.receive_str()).startswith("joined\tPrivate\t")
        await host.close()
        print("all relay tests passed")


asyncio.run(main())
