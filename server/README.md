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

`test_relay.py` checks the protocol against a running server, `simulate.py` sends a fake game so
the dashboard has something to show.
