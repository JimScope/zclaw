#!/usr/bin/env python3
"""Tests for the dashboard renderer (no network)."""

from __future__ import annotations

import io
import unittest

from PIL import Image, JpegImagePlugin

import dash

WEATHER_FIXTURE = {
    "current": {
        "time": "2026-10-11T18:45",
        "temperature_2m": 28.4,
        "relative_humidity_2m": 84,
        "weather_code": 3,
        "wind_speed_10m": 7.2,
    },
    "hourly": {
        "time": [f"2026-10-11T{h:02d}:00" for h in range(18, 25)][:6]
        + ["2026-10-12T00:00", "2026-10-12T01:00"],
        "temperature_2m": [28.4, 27.9, 27.1, 26.5, 26.0, 25.6, 25.3, 25.1],
        "weather_code": [3, 3, 61, 61, 2, 1, 0, 0],
        "is_day": [1, 1, 0, 0, 0, 0, 0, 0],
    },
    "daily": {
        "time": ["2026-10-11", "2026-10-12", "2026-10-14"],
        "weather_code": [3, 61, 0],
        "temperature_2m_max": [31.2, 32.0, 30.4],
        "temperature_2m_min": [24.0, 24.4, 23.1],
    },
}

CRYPTO_FIXTURE = {
    "bitcoin": {"usd": 83578.0, "usd_24h_change": 0.6865},
    "binancecoin": {"usd": 604.1234, "usd_24h_change": -1.2},
    "solana": {"usd": 151.3, "usd_24h_change": 2.1},
    "tron": {"usd": 0.25123, "usd_24h_change": 0.0},
}


def decode(jpeg: bytes) -> Image.Image:
    img = Image.open(io.BytesIO(jpeg))
    img.load()
    return img


class FormatTest(unittest.TestCase):
    def test_price(self):
        self.assertEqual(dash.format_price(83578.0), "$83,578")
        self.assertEqual(dash.format_price(12345.6), "$12,346")
        self.assertEqual(dash.format_price(604.1234), "$604.12")
        self.assertEqual(dash.format_price(1.5), "$1.50")
        self.assertEqual(dash.format_price(0.25123), "$0.2512")

    def test_change(self):
        self.assertEqual(dash.format_change(0.6865), "+0.69%")
        self.assertEqual(dash.format_change(-1.2), "-1.20%")
        self.assertEqual(dash.format_change(0.0), "+0.00%")

    def test_hour_label(self):
        self.assertEqual(dash.hour_label(0), "12AM")
        self.assertEqual(dash.hour_label(9), "9AM")
        self.assertEqual(dash.hour_label(12), "12PM")
        self.assertEqual(dash.hour_label(15), "3PM")

    def test_day_label(self):
        self.assertEqual(dash.day_label("2026-10-11", "2026-10-11"), "hoy")
        self.assertEqual(dash.day_label("2026-10-12", "2026-10-11"), "lun")
        self.assertEqual(dash.day_label("2026-10-14", "2026-10-11"), "mié")

    def test_weather_desc(self):
        self.assertEqual(dash.weather_desc(0), ("despejado", "sun"))
        self.assertEqual(dash.weather_desc(3), ("nublado", "cloud"))
        self.assertEqual(dash.weather_desc(63)[1], "rain")
        self.assertEqual(dash.weather_desc(95)[1], "storm")
        self.assertEqual(dash.weather_desc(1234), ("--", "cloud"))


class ParseTest(unittest.TestCase):
    def test_open_meteo(self):
        w = dash.parse_open_meteo(WEATHER_FIXTURE)
        self.assertEqual(w.temp, 28)
        self.assertEqual(w.desc, "nublado")
        self.assertEqual(w.humidity, 84)
        self.assertEqual(w.wind, 7)
        # next six hours start after the current hour (18:45 -> 19:00)
        self.assertEqual([h.label for h in w.hours], ["7PM", "8PM", "9PM", "10PM", "11PM", "12AM"])
        self.assertEqual(w.hours[0].temp, 28)
        self.assertEqual([d.label for d in w.days], ["hoy", "lun", "mié"])
        self.assertEqual((w.days[1].tmax, w.days[1].tmin), (32, 24))

    def test_coingecko_keeps_requested_order(self):
        rows = dash.parse_coingecko(CRYPTO_FIXTURE, ["bitcoin", "binancecoin", "solana", "tron"])
        self.assertEqual([r.symbol for r in rows], ["BTC", "BNB", "SOL", "TRX"])
        self.assertAlmostEqual(rows[1].change, -1.2)

    def test_coingecko_skips_missing_ids(self):
        rows = dash.parse_coingecko({"bitcoin": CRYPTO_FIXTURE["bitcoin"]}, ["bitcoin", "tron"])
        self.assertEqual([r.symbol for r in rows], ["BTC"])

    def test_clear_night_hours_use_moon(self):
        w = dash.parse_open_meteo(WEATHER_FIXTURE)
        self.assertEqual(w.hours[4].icon, "moon")      # 11PM, code 1, night
        self.assertEqual(w.hours[3].icon, "moon_partly")  # 10PM, code 2, night
        self.assertEqual(w.hours[1].icon, "rain")      # rain stays rain at night

    def test_is_day_is_optional(self):
        data = {**WEATHER_FIXTURE, "hourly": {k: v for k, v in WEATHER_FIXTURE["hourly"].items() if k != "is_day"}}
        self.assertEqual(dash.parse_open_meteo(data).hours[4].icon, "sun")


class RenderTest(unittest.TestCase):
    def check_jpeg(self, jpeg: bytes):
        self.assertTrue(jpeg.startswith(b"\xff\xd8"))
        self.assertLess(len(jpeg), dash.MAX_JPEG_BYTES)
        img = decode(jpeg)
        self.assertEqual(img.size, (240, 240))
        self.assertFalse(img.info.get("progressive") or img.info.get("progression"))
        self.assertEqual(JpegImagePlugin.get_sampling(img), 0)  # 4:4:4 -> 8x8 MCUs

    def test_weather_card(self):
        self.check_jpeg(dash.render_weather(dash.parse_open_meteo(WEATHER_FIXTURE)))

    def test_crypto_card(self):
        rows = dash.parse_coingecko(CRYPTO_FIXTURE, ["bitcoin", "binancecoin", "solana", "tron"])
        self.check_jpeg(dash.render_crypto(rows, "6:52 PM"))

    def test_error_card(self):
        self.check_jpeg(dash.render_error("Clima", "sin datos"))

    def test_fit_text_truncates_by_width(self):
        from PIL import Image, ImageDraw
        draw = ImageDraw.Draw(Image.new("RGB", (10, 10)))
        font = dash._font(12)
        self.assertEqual(dash.fit_text(draw, "nublado", font, 200), "nublado")
        cut = dash.fit_text(draw, "casi despejado con viento", font, 88)
        self.assertTrue(cut.endswith("…"))
        self.assertLessEqual(draw.textlength(cut, font=font), 88)

class PictureTest(unittest.TestCase):
    def test_portrait_cover_is_contained_on_blurred_background(self):
        cover = Image.new("RGB", (300, 400), (200, 30, 30))
        jpeg = dash.render_picture(cover)
        img = decode(jpeg)
        self.assertEqual(img.size, (240, 240))
        self.assertEqual(JpegImagePlugin.get_sampling(img), 0)
        r, g, b = img.getpixel((120, 120))
        self.assertGreater(r, 150)  # the cover itself in the middle
        r2, g2, b2 = img.getpixel((3, 120))
        self.assertLess(r2, r)      # darkened background at the sides
        self.assertLess(len(jpeg), dash.MAX_JPEG_BYTES)

    def test_worst_case_noise_still_fits(self):
        import os
        noise = Image.frombytes("RGB", (240, 240), os.urandom(240 * 240 * 3))
        self.assertLess(len(dash.render_picture(noise)), dash.MAX_JPEG_BYTES)


if __name__ == "__main__":
    unittest.main()
