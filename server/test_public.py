#!/usr/bin/env python3
"""Public server rules and joining a running game, for relay.py started with
PUBLIC=1 BLOOD_KEY=<key> PUBLIC_MAX_LOBBIES=2: python test_public.py ws://127.0.0.1:8099 <key>"""
import asyncio, hashlib, hmac, sys
import aiohttp

URL, KEY = sys.argv[1], sys.argv[2]


async def login(session, path, key=KEY, extra=("1", "test")):
    ws = await session.ws_connect(URL + path)
    nonce = (await ws.receive_str()).split("\t")[1]
    mac = hmac.new(key.encode(), nonce.encode(), hashlib.sha256).hexdigest()
    await ws.send_str("\t".join(["auth", mac, *extra]))
    return ws


async def ok(ws):
    m = await ws.receive_str()
    assert m.startswith("ok"), m
    return ws


async def main():
    async with aiohttp.ClientSession() as s:
        # without the key: at most PUBLIC_MAX_LOBBIES (2) lobbies
        pub = []
        for i in range(2):
            h = await ok(await login(s, "/ws", key=""))
            await h.send_str("host\tPublic %d\t4\tP%d\t\tv1" % (i, i))
            assert (await h.receive_str()) == "hosted\tPublic %d" % i
            pub.append(h)
        third = await ok(await login(s, "/ws", key="not the key"))
        await third.send_str("host\tPublic 3\t4\tP3\t\tv1")
        m = await third.receive_str()
        assert m.startswith("error") and "most" in m, m
        # with the key: always
        vip = await ok(await login(s, "/ws"))
        await vip.send_str("host\tVIP\t4\tBoss\t\tv1\t\task")
        assert (await vip.receive_str()) == "hosted\tVIP"
        lst = await ok(await login(s, "/ws", key=""))
        await lst.send_str("list")
        lines = (await lst.receive_str()).split("\n")
        vip_line = [l for l in lines if l.startswith("VIP\t")][0].split("\t")
        assert vip_line[10] == "ask", vip_line

        # "ask": a player asks while the game runs, the host says yes / no
        await vip.send_str("state\tplaying")
        a = await ok(await login(s, "/ws", key=""))
        await a.send_str("join\tVIP\tAnna\t\tv1")
        assert (await a.receive_str()).startswith("join_wait\tVIP"), "no join_wait"
        req = (await vip.receive_str()).split("\t")
        assert req[0] == "join_request" and req[2] == "Anna", req
        await vip.send_str("join_answer\t%s\tyes" % req[1])
        assert (await a.receive_str()).startswith("joined\tVIP"), "not let in"
        assert (await vip.receive_str()).startswith("peer_join"), "no peer_join"
        b = await ok(await login(s, "/ws", key=""))
        await b.send_str("join\tVIP\tBert\t\tv1")
        assert (await b.receive_str()).startswith("join_wait")
        req = (await vip.receive_str()).split("\t")
        await vip.send_str("join_answer\t%s\tno" % req[1])
        m = await b.receive_str()
        assert m.startswith("error") and "let you in" in m, m

        # "closed": nobody during the game; "open": anybody
        await vip.send_str("join_policy\tclosed")
        c = await ok(await login(s, "/ws", key=""))
        await c.send_str("join\tVIP\tCora\t\tv1")
        m = await c.receive_str()
        assert m.startswith("error") and "already started" in m, m
        await vip.send_str("join_policy\topen")
        await c.send_str("join\tVIP\tCora\t\tv1")
        assert (await c.receive_str()).startswith("joined\tVIP"), "open lobby refused"
    print("all public server tests passed")


asyncio.run(main())
