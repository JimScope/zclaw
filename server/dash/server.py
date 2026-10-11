#!/usr/bin/env python3
"""HTTP service serving Fidel's dashboard cards as JPEG.

Behind Caddy (`handle_path /dash/*`), so paths here have no /dash prefix:
    GET /clima.jpg?lat=20.02&lon=-75.83
    GET /cripto.jpg?ids=bitcoin,binancecoin,solana,tron
    GET /health
Data comes from Open-Meteo and CoinGecko and is cached in memory. A failing
source still returns a JPEG (an error card) with X-Dash-Error: 1, so the
board always has something to show.
"""

from __future__ import annotations

import datetime as dt
import json
import os
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable
from zoneinfo import ZoneInfo

import dash

PORT = int(os.environ.get("DASH_PORT", "9002"))
WEATHER_TTL = 15 * 60
CRYPTO_TTL = 2 * 60
DEFAULT_COINS = ["bitcoin", "binancecoin", "solana", "tron"]
LOCAL_TZ = ZoneInfo("America/Havana")
FETCH_TIMEOUT_S = 10


def fetch_json(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": "fidel-dash/1"})
    with urllib.request.urlopen(req, timeout=FETCH_TIMEOUT_S) as resp:
        return json.load(resp)


class Dash:
    def __init__(self, fetch: Callable[[str], dict], clock: Callable[[], float] = time.time):
        self._fetch = fetch
        self._clock = clock
        self._cache: dict[str, tuple[float, bytes]] = {}
        self._lock = threading.Lock()

    def _cached(self, key: str, ttl: float, build: Callable[[], bytes]) -> tuple[bytes, bool]:
        now = self._clock()
        with self._lock:
            hit = self._cache.get(key)
            if hit and now - hit[0] < ttl:
                return hit[1], True
        jpeg = build()
        with self._lock:
            self._cache[key] = (now, jpeg)
        return jpeg, True

    def weather(self, lat: float, lon: float) -> tuple[bytes, bool]:
        url = ("https://api.open-meteo.com/v1/forecast?"
               f"latitude={lat:.2f}&longitude={lon:.2f}"
               "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
               "&hourly=temperature_2m,weather_code,is_day&forecast_hours=8"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=3"
               "&timezone=auto")
        try:
            return self._cached(f"w:{lat:.2f},{lon:.2f}", WEATHER_TTL,
                                lambda: dash.render_weather(dash.parse_open_meteo(self._fetch(url))))
        except (OSError, ValueError, KeyError, TypeError):
            return dash.render_error("Clima", "sin datos"), False

    def crypto(self, ids: list[str]) -> tuple[bytes, bool]:
        url = ("https://api.coingecko.com/api/v3/simple/price?"
               f"ids={','.join(ids)}&vs_currencies=usd&include_24hr_change=true")

        def build() -> bytes:
            rows = dash.parse_coingecko(self._fetch(url), ids)
            if not rows:
                raise ValueError("no prices")
            now = dt.datetime.fromtimestamp(self._clock(), LOCAL_TZ)
            label = f"{now.hour % 12 or 12}:{now.minute:02d} {'AM' if now.hour < 12 else 'PM'}"
            return dash.render_crypto(rows, label)

        try:
            return self._cached(f"c:{','.join(ids)}", CRYPTO_TTL, build)
        except (OSError, ValueError, KeyError, TypeError):
            return dash.render_error("Cripto", "sin datos"), False


def make_handler(service: Dash) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def _send(self, status: int, body: bytes, ctype: str, error: bool = False) -> None:
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            if error:
                self.send_header("X-Dash-Error", "1")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802 (http.server API)
            url = urllib.parse.urlsplit(self.path)
            query = urllib.parse.parse_qs(url.query)
            if url.path == "/health":
                self._send(200, b"ok", "text/plain")
            elif url.path == "/clima.jpg":
                try:
                    lat = float(query.get("lat", ["20.02"])[0])
                    lon = float(query.get("lon", ["-75.83"])[0])
                except ValueError:
                    self._send(400, b"bad lat/lon", "text/plain")
                    return
                jpeg, ok = service.weather(lat, lon)
                self._send(200, jpeg, "image/jpeg", error=not ok)
            elif url.path == "/cripto.jpg":
                ids = [i for i in query.get("ids", [",".join(DEFAULT_COINS)])[0].split(",") if i][:5]
                jpeg, ok = service.crypto(ids or DEFAULT_COINS)
                self._send(200, jpeg, "image/jpeg", error=not ok)
            else:
                self._send(404, b"not found", "text/plain")

        def log_message(self, fmt: str, *args) -> None:
            print(f"{self.address_string()} {fmt % args}", flush=True)

    return Handler


def main() -> None:
    httpd = ThreadingHTTPServer(("127.0.0.1", PORT), make_handler(Dash(fetch_json)))
    print(f"fidel-dash listening on 127.0.0.1:{PORT}", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
