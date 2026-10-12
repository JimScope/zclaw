# fidel-dash

Renders Fidel's dashboard cards (`/clima`, `/cripto`) as 240x240 baseline
JPEGs (4:4:4, < 48 KB) that the ESP32-C6 decodes with its ROM TJpgDec.
Data: Open-Meteo and CoinGecko, cached in memory (15 min / 2 min).

    GET /clima.jpg?lat=20.02&lon=-75.83
    GET /cripto.jpg?ids=bitcoin,binancecoin,solana,tron
    GET /health

Deploy (systemd --user unit, no sudo): `scripts/deploy-dash.sh`. Every route except
`/health` requires the `X-Dash-Token` header; the token lives in `~/fidel-dash/.env`
on the VPS (created by the deploy script) and in the board's git-ignored
`sdkconfig.defaults.esp32c6` as `CONFIG_ZCLAW_DASH_TOKEN`.

Caddy (inside the `bolita-cubana.duckdns.org` site block):

    handle_path /dash/* {
        reverse_proxy 127.0.0.1:9002
    }

Tests (Pillow required): `python -m unittest test_dash.py test_server.py`.
Fonts: DejaVu Sans (see `fonts/DejaVu-LICENSE.txt`).
