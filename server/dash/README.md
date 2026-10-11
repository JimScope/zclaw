# fidel-dash

Renders Fidel's dashboard cards (`/clima`, `/cripto`) as 240x240 baseline
JPEGs (4:4:4, < 48 KB) that the ESP32-C6 decodes with its ROM TJpgDec.
Data: Open-Meteo and CoinGecko, cached in memory (15 min / 2 min).

    GET /clima.jpg?lat=20.02&lon=-75.83
    GET /cripto.jpg?ids=bitcoin,binancecoin,solana,tron
    GET /health

Deploy (systemd --user unit, no sudo): `scripts/deploy-dash.sh`.

Caddy (inside the `bolita-cubana.duckdns.org` site block):

    handle_path /dash/* {
        reverse_proxy 127.0.0.1:9002
    }

Tests (Pillow required): `python -m unittest test_dash.py test_server.py`.
Fonts: DejaVu Sans (see `fonts/DejaVu-LICENSE.txt`).
