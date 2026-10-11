#!/usr/bin/env python3
"""Tests for the dashboard HTTP service (fake fetcher, no network)."""

from __future__ import annotations

import http.client
import threading
import unittest
from http.server import ThreadingHTTPServer

import server
from test_dash import CRYPTO_FIXTURE, WEATHER_FIXTURE


class FakeClock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self) -> float:
        return self.t


class FakeFetch:
    def __init__(self, fail: bool = False):
        self.urls: list[str] = []
        self.fail = fail

    def __call__(self, url: str) -> dict:
        self.urls.append(url)
        if self.fail:
            raise OSError("boom")
        return WEATHER_FIXTURE if "open-meteo" in url else CRYPTO_FIXTURE


class DashTest(unittest.TestCase):
    def setUp(self):
        self.fetch = FakeFetch()
        self.clock = FakeClock()
        self.dash = server.Dash(self.fetch, clock=self.clock)

    def test_weather_uses_location_and_caches(self):
        jpeg, ok = self.dash.weather(20.02, -75.83)
        self.assertTrue(ok and jpeg.startswith(b"\xff\xd8"))
        self.assertIn("latitude=20.02", self.fetch.urls[0])
        self.assertIn("is_day", self.fetch.urls[0])
        self.dash.weather(20.02, -75.83)
        self.assertEqual(len(self.fetch.urls), 1)
        self.clock.t += server.WEATHER_TTL + 1
        self.dash.weather(20.02, -75.83)
        self.assertEqual(len(self.fetch.urls), 2)

    def test_crypto_ids_and_cache(self):
        jpeg, ok = self.dash.crypto(["bitcoin", "tron"])
        self.assertTrue(ok)
        self.assertIn("ids=bitcoin,tron", self.fetch.urls[0])
        self.dash.crypto(["bitcoin", "tron"])
        self.assertEqual(len(self.fetch.urls), 1)
        self.clock.t += server.CRYPTO_TTL + 1
        self.dash.crypto(["bitcoin", "tron"])
        self.assertEqual(len(self.fetch.urls), 2)

    def test_fetch_failure_gives_error_card(self):
        dash = server.Dash(FakeFetch(fail=True), clock=self.clock)
        jpeg, ok = dash.weather(20.0, -75.0)
        self.assertFalse(ok)
        self.assertTrue(jpeg.startswith(b"\xff\xd8"))


class HttpTest(unittest.TestCase):
    def setUp(self):
        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.make_handler(server.Dash(FakeFetch())))
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()

    def get(self, path: str) -> http.client.HTTPResponse:
        conn = http.client.HTTPConnection("127.0.0.1", self.httpd.server_address[1], timeout=10)
        conn.request("GET", path)
        return conn.getresponse()

    def test_clima_endpoint(self):
        resp = self.get("/clima.jpg?lat=20.02&lon=-75.83")
        body = resp.read()
        self.assertEqual(resp.status, 200)
        self.assertEqual(resp.getheader("Content-Type"), "image/jpeg")
        self.assertEqual(int(resp.getheader("Content-Length")), len(body))

    def test_cripto_endpoint_defaults(self):
        resp = self.get("/cripto.jpg")
        resp.read()
        self.assertEqual(resp.status, 200)

    def test_bad_coordinates_are_rejected(self):
        resp = self.get("/clima.jpg?lat=abc&lon=1")
        resp.read()
        self.assertEqual(resp.status, 400)

    def test_unknown_path(self):
        resp = self.get("/nope")
        resp.read()
        self.assertEqual(resp.status, 404)


def png_bytes(color=(10, 200, 10), size=(320, 200)) -> bytes:
    import io
    from PIL import Image
    buf = io.BytesIO()
    Image.new("RGB", size, color).save(buf, "PNG")
    return buf.getvalue()


class ImageTest(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.searched: list[str] = []
        self.fetched: list[str] = []
        self.files = {"https://bad.example/x.jpg": b"not an image",
                      "https://ok.example/cover.png": png_bytes()}

    def search(self, query: str) -> list[str]:
        self.searched.append(query)
        return ["https://bad.example/x.jpg", "https://ok.example/cover.png"]

    def fetch_bytes(self, url: str) -> bytes:
        self.fetched.append(url)
        if url not in self.files:
            raise OSError("404")
        return self.files[url]

    def make(self):
        return server.Dash(FakeFetch(), clock=self.clock, search=self.search, fetch_bytes=self.fetch_bytes)

    def test_query_skips_broken_candidates_and_caches(self):
        dash_service = self.make()
        jpeg, ok = dash_service.image(query="mario kart world")
        self.assertTrue(ok and jpeg.startswith(b"\xff\xd8"))
        self.assertEqual(self.fetched, ["https://bad.example/x.jpg", "https://ok.example/cover.png"])
        dash_service.image(query="mario kart world")
        self.assertEqual(len(self.searched), 1)

    def test_direct_url(self):
        jpeg, ok = self.make().image(url="https://ok.example/cover.png")
        self.assertTrue(ok)
        self.assertEqual(self.searched, [])

    def test_nothing_found_gives_error_card(self):
        self.files = {}
        jpeg, ok = self.make().image(query="zzzz")
        self.assertFalse(ok)
        self.assertTrue(jpeg.startswith(b"\xff\xd8"))

    def test_http_endpoint_reports_error_header(self):
        self.files = {}
        httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.make_handler(self.make()))
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        try:
            conn = http.client.HTTPConnection("127.0.0.1", httpd.server_address[1], timeout=10)
            conn.request("GET", "/image.jpg?q=nada")
            resp = conn.getresponse()
            resp.read()
            self.assertEqual(resp.status, 200)
            self.assertEqual(resp.getheader("X-Dash-Error"), "1")
            conn.request("GET", "/image.jpg")
            resp = conn.getresponse()
            resp.read()
            self.assertEqual(resp.status, 400)
        finally:
            httpd.shutdown()
            httpd.server_close()


if __name__ == "__main__":
    unittest.main()
