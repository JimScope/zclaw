#!/usr/bin/env python3
"""HTTP service serving Fidel's dashboard cards as JPEG.

Behind Caddy (`handle_path /dash/*`), so paths here have no /dash prefix:
    GET /clima.jpg?lat=20.02&lon=-75.83
    GET /cripto.jpg?ids=bitcoin,binancecoin,solana,tron
    GET /image.jpg?q=mario+kart+world   (or ?url=https://...)
    GET /health
Data comes from Open-Meteo and CoinGecko and is cached in memory. A failing
source still returns a JPEG (an error card) with X-Dash-Error: 1, so the
board always has something to show.
"""

from __future__ import annotations

import collections
import datetime as dt
import hmac
import html
import io
import ipaddress
import json
import re
import socket
import os
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable
from zoneinfo import ZoneInfo

from PIL import Image

import dash

Image.MAX_IMAGE_PIXELS = 12_500_000  # DecompressionBombError above 25 MP

PORT = int(os.environ.get("DASH_PORT", "9002"))
WEATHER_TTL = 15 * 60
CRYPTO_TTL = 2 * 60
DEFAULT_COINS = ["bitcoin", "binancecoin", "solana", "tron"]
LOCAL_TZ = ZoneInfo("America/Havana")
FETCH_TIMEOUT_S = 10
IMAGE_TTL = 24 * 60 * 60
IMAGE_DEADLINE_S = 9          # the board waits 15 s for the whole response
CANDIDATE_TIMEOUT_S = 4
SEARCH_TIMEOUT_S = 5
CACHE_MAX_ENTRIES = 64
HEAVY_JOBS = 2                # concurrent card builds (image decode is the costly part)
SAFE_FORMATS = ("JPEG", "PNG", "GIF", "WEBP")
COIN_ID = re.compile(r"^[a-z0-9-]{1,40}$")
IMAGE_MAX_BYTES = 8 * 1024 * 1024
IMAGE_CANDIDATES = 6
BROWSER_UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605.1.15 "
              "(KHTML, like Gecko) Version/17.0 Safari/605.1.15")


def fetch_json(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": "fidel-dash/1"})
    with urllib.request.urlopen(req, timeout=FETCH_TIMEOUT_S) as resp:
        return json.load(resp)


def public_url(url: str) -> bool:
    """True when url is http(s) and every address its host resolves to is public.

    Stops /image.jpg?url= (and search results) from making this server fetch
    loopback, private, link-local (cloud metadata) or other internal addresses.
    """
    parts = urllib.parse.urlsplit(url)
    if parts.scheme not in ("http", "https") or not parts.hostname:
        return False
    try:
        port = parts.port or (443 if parts.scheme == "https" else 80)
        infos = socket.getaddrinfo(parts.hostname, port, proto=socket.IPPROTO_TCP)
    except (socket.gaierror, ValueError, UnicodeError):
        return False
    try:
        return bool(infos) and all(ipaddress.ip_address(info[4][0]).is_global for info in infos)
    except ValueError:
        return False


class _CheckedRedirects(urllib.request.HTTPRedirectHandler):
    """Re-validate every redirect hop with public_url()."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not public_url(newurl):
            raise urllib.error.URLError(f"redirect to non-public address blocked: {newurl}")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


_image_opener = urllib.request.build_opener(_CheckedRedirects())


def fetch_bytes(url: str) -> bytes:
    if not public_url(url):
        raise ValueError("non-public address")
    req = urllib.request.Request(url, headers={"User-Agent": BROWSER_UA})
    with _image_opener.open(req, timeout=CANDIDATE_TIMEOUT_S) as resp:
        data = resp.read(IMAGE_MAX_BYTES + 1)
    if len(data) > IMAGE_MAX_BYTES:
        raise ValueError("image too large")
    return data


def _get_text(url: str, referer: str | None = None) -> str:
    headers = {"User-Agent": BROWSER_UA}
    if referer:
        headers["Referer"] = referer
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=SEARCH_TIMEOUT_S) as resp:
        return resp.read().decode("utf-8", "replace")


def search_images(query: str) -> list[str]:
    """Image URLs for query: Bing first, DuckDuckGo as fallback (both unofficial)."""
    urls: list[str] = []
    try:
        page = _get_text("https://www.bing.com/images/search?" + urllib.parse.urlencode({"q": query, "form": "HDRSC2"}))
        urls += [html.unescape(u) for u in re.findall(r"murl&quot;:&quot;(.*?)&quot;", page)]
    except Exception:  # noqa: BLE001 - search is best effort
        pass
    if len(urls) < IMAGE_CANDIDATES:
        try:
            page = _get_text("https://duckduckgo.com/?" + urllib.parse.urlencode({"q": query, "iax": "images", "ia": "images"}))
            vqd = re.search(r"vqd=[\"']?([\d-]+)", page)
            if vqd:
                data = json.loads(_get_text("https://duckduckgo.com/i.js?" + urllib.parse.urlencode(
                    {"l": "us-en", "o": "json", "q": query, "vqd": vqd.group(1), "f": ",,,,,", "p": "1"}),
                    referer="https://duckduckgo.com/"))
                urls += [r["image"] for r in data.get("results", []) if r.get("image")]
        except Exception:  # noqa: BLE001 - search is best effort
            pass
    return urls


class Dash:
    def __init__(self, fetch: Callable[[str], dict], clock: Callable[[], float] = time.time,
                 search: Callable[[str], list[str]] = search_images,
                 fetch_bytes: Callable[[str], bytes] = fetch_bytes,
                 is_public: Callable[[str], bool] = public_url):
        self._fetch = fetch
        self._clock = clock
        self._search = search
        self._fetch_bytes = fetch_bytes
        self._is_public = is_public
        self._cache: collections.OrderedDict[str, tuple[float, bytes]] = collections.OrderedDict()
        self._lock = threading.Lock()
        self._heavy = threading.BoundedSemaphore(HEAVY_JOBS)

    def _cached(self, key: str, ttl: float, build: Callable[[], bytes]) -> tuple[bytes, bool]:
        now = self._clock()
        with self._lock:
            hit = self._cache.get(key)
            if hit and now - hit[0] < ttl:
                self._cache.move_to_end(key)
                return hit[1], True
        if not self._heavy.acquire(timeout=10):
            raise TimeoutError("busy")
        try:
            jpeg = build()
        finally:
            self._heavy.release()
        with self._lock:
            self._cache[key] = (now, jpeg)
            self._cache.move_to_end(key)
            while len(self._cache) > CACHE_MAX_ENTRIES:
                self._cache.popitem(last=False)
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
        except Exception:  # noqa: BLE001 - any failure becomes an error card
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
        except Exception:  # noqa: BLE001 - any failure becomes an error card
            return dash.render_error("Cripto", "sin datos"), False


    def image(self, query: str | None = None, url: str | None = None) -> tuple[bytes, bool]:
        """A searched (query) or given (url) picture as a dashboard JPEG."""
        def build() -> bytes:
            start = self._clock()
            candidates = [url] if url else self._search(query or "")[:IMAGE_CANDIDATES]
            for candidate in candidates:
                if self._clock() - start > IMAGE_DEADLINE_S:
                    break
                if not self._is_public(candidate):
                    continue
                try:
                    picture = Image.open(io.BytesIO(self._fetch_bytes(candidate)), formats=SAFE_FORMATS)
                    if picture.format == "JPEG":
                        picture.draft("RGB", (dash.SIZE * 2, dash.SIZE * 2))  # decode big JPEGs scaled down
                    picture.load()
                    return dash.render_picture(picture)
                except Exception:  # noqa: BLE001 - try the next candidate whatever went wrong
                    continue
            raise ValueError("no usable image")

        key = f"u:{url}" if url else f"q:{(query or '').strip().lower()}"
        try:
            return self._cached(key, IMAGE_TTL, build)
        except Exception:  # noqa: BLE001 - any failure becomes an error card
            return dash.render_error("Imagen", "no encontrada"), False


def make_handler(service: Dash, token: str | None = None) -> type[BaseHTTPRequestHandler]:
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
            if url.path != "/health" and token:
                given = self.headers.get("X-Dash-Token", "")
                if not hmac.compare_digest(given.encode(), token.encode()):
                    self._send(401, b"token required", "text/plain")
                    return
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
                if not all(COIN_ID.match(i) for i in ids):
                    self._send(400, b"bad ids", "text/plain")
                    return
                jpeg, ok = service.crypto(ids or DEFAULT_COINS)
                self._send(200, jpeg, "image/jpeg", error=not ok)
            elif url.path == "/image.jpg":
                q = (query.get("q") or [""])[0].strip()
                src = (query.get("url") or [""])[0].strip()
                if (src and not src.startswith(("http://", "https://"))) or (not q and not src):
                    self._send(400, b"q or http(s) url required", "text/plain")
                    return
                jpeg, ok = service.image(query=q or None, url=src or None)
                self._send(200, jpeg, "image/jpeg", error=not ok)
            else:
                self._send(404, b"not found", "text/plain")

        def log_message(self, fmt: str, *args) -> None:
            print(f"{self.address_string()} {fmt % args}", flush=True)

    return Handler


def main() -> None:
    token = os.environ.get("DASH_TOKEN", "").strip()
    if len(token) < 16:
        raise SystemExit("DASH_TOKEN (>= 16 chars) is required: the /dash routes are public")
    ThreadingHTTPServer.daemon_threads = True
    httpd = ThreadingHTTPServer(("127.0.0.1", PORT), make_handler(Dash(fetch_json), token=token))
    print(f"fidel-dash listening on 127.0.0.1:{PORT}", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
