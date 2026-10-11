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


if __name__ == "__main__":
    unittest.main()
