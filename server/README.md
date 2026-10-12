# NotBlood online relay and live dashboard

Lets NotBlood games play over the internet without port forwarding, and shows running games
(players, health, weapons, keys, a live level map) in a browser.

* Games connect to `ws://<server>/ws`; every player's normal game traffic is tunnelled through
  the server, so only the server has to be reachable.
* The dashboard is at `https://<server>/`.
* Everything needs the server key. Game and browser answer a random challenge with
  HMAC-SHA256(key, challenge), so the key itself is never sent.

## Running it

```sh
echo "BLOOD_KEY=$(openssl rand -hex 12)" > server.env
docker compose up -d --build
```

The container listens on 127.0.0.1:8099; put a reverse proxy (Caddy, nginx, a Cloudflare tunnel)
in front of it. WebSockets must be passed through.

Behind a Cloudflare tunnel every connection reaches the relay from 127.0.0.1, so the limits per
address (connections, the lockout after 10 wrong keys) would count everybody together. With
`TRUST_PROXY=1` in `server.env` the relay uses the `CF-Connecting-IP` header that Cloudflare sets.
Leave it off when the relay can be reached without Cloudflare, or anyone could pick their address.

## What the log shows

`docker logs notblood-relay` has a line for every lobby and player, and when a player leaves:
why (left the game, connection lost, no answer for 45 s, the host left), how long they were on,
their ping to the server (average and worst), and pauses where their game packets stopped for
over a second. Pauses of 2 s or more are also logged as they happen. The same numbers are kept
per player in the game history (`net` in `/data/history.json`), and the dashboard can show the
ping on each player's card (settings: "Ping to the server").

`test_relay.py` checks the protocol against a running server, `simulate.py` sends a fake game so
the dashboard has something to show.
