"""Dashboard cards for Fidel's 240x240 display, rendered as small JPEGs.

The board only downloads and decodes these images (ESP32-C6 ROM TJpgDec:
baseline JPEG, RGB888 output), so every card is encoded baseline with 4:4:4
chroma (8x8 MCUs that match the firmware's 8-row strips) and kept under
MAX_JPEG_BYTES.
"""

from __future__ import annotations

import datetime as dt
import io
import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SIZE = 240
MAX_JPEG_BYTES = 48 * 1024
JPEG_QUALITY = 88

FONT_DIR = Path(__file__).resolve().parent / "fonts"
BG = (16, 22, 28)
FG = (240, 240, 240)
DIM = (150, 160, 170)
AMBER = (255, 191, 0)
GREEN = (60, 210, 110)
RED = (240, 80, 80)
LINE = (45, 55, 65)

DAYS_ES = ["lun", "mar", "mié", "jue", "vie", "sáb", "dom"]

# WMO weather codes -> (short Spanish description, icon)
_WMO = [
    ((0,), "despejado", "sun"),
    ((1,), "casi despejado", "sun"),
    ((2,), "parcial nublado", "partly"),
    ((3,), "nublado", "cloud"),
    ((45, 48), "niebla", "cloud"),
    ((51, 53, 55, 56, 57), "llovizna", "rain"),
    ((61, 63, 65, 66, 67), "lluvia", "rain"),
    ((71, 73, 75, 77, 85, 86), "nieve", "snow"),
    ((80, 81, 82), "chubascos", "rain"),
    ((95,), "tormenta", "storm"),
    ((96, 99), "tormenta y granizo", "storm"),
]

COIN_SYMBOLS = {
    "bitcoin": "BTC",
    "binancecoin": "BNB",
    "solana": "SOL",
    "tron": "TRX",
    "ethereum": "ETH",
    "tether": "USDT",
}


@dataclass
class Hour:
    label: str
    temp: int
    icon: str


@dataclass
class Day:
    label: str
    tmax: int
    tmin: int
    desc: str
    icon: str


@dataclass
class Weather:
    temp: int
    desc: str
    icon: str
    humidity: int
    wind: int
    hours: list[Hour]
    days: list[Day]


@dataclass
class CoinRow:
    symbol: str
    price: float
    change: float


# ---------------------------------------------------------------- formatting

def format_price(value: float) -> str:
    if value >= 1000:
        return f"${value:,.0f}"
    if value >= 1:
        return f"${value:,.2f}"
    return f"${value:.4f}"


def format_change(pct: float) -> str:
    return f"{pct:+.2f}%"


def hour_label(hour24: int) -> str:
    h = hour24 % 12 or 12
    return f"{h}{'AM' if hour24 < 12 else 'PM'}"


def day_label(date_iso: str, today_iso: str) -> str:
    if date_iso == today_iso:
        return "hoy"
    return DAYS_ES[dt.date.fromisoformat(date_iso).weekday()]


def weather_desc(code: int) -> tuple[str, str]:
    for codes, desc, icon in _WMO:
        if code in codes:
            return desc, icon
    return "--", "cloud"


def night_icon(icon: str, is_day: int) -> str:
    """Clear-sky icons become moons at night; clouds, rain and storms stay."""
    if is_day:
        return icon
    return {"sun": "moon", "partly": "moon_partly"}.get(icon, icon)


def _round(value: float) -> int:
    return int(value + 0.5) if value >= 0 else -int(-value + 0.5)


# ------------------------------------------------------------------- parsing

def parse_open_meteo(data: dict) -> Weather:
    cur = data["current"]
    desc, icon = weather_desc(int(cur["weather_code"]))
    now = cur["time"]
    today = now[:10]

    hourly = data["hourly"]
    is_day = hourly.get("is_day") or [1] * len(hourly["time"])
    hours: list[Hour] = []
    for t, temp, code, day in zip(hourly["time"], hourly["temperature_2m"], hourly["weather_code"], is_day):
        if t <= now:
            continue
        hours.append(Hour(hour_label(int(t[11:13])), _round(temp), night_icon(weather_desc(int(code))[1], day)))
        if len(hours) == 6:
            break

    daily = data["daily"]
    days = []
    for t, code, tmax, tmin in zip(daily["time"], daily["weather_code"],
                                   daily["temperature_2m_max"], daily["temperature_2m_min"]):
        d_desc, d_icon = weather_desc(int(code))
        days.append(Day(day_label(t, today), _round(tmax), _round(tmin), d_desc, d_icon))

    return Weather(_round(cur["temperature_2m"]), desc, icon, _round(cur["relative_humidity_2m"]),
                   _round(cur["wind_speed_10m"]), hours, days[:3])


def parse_coingecko(data: dict, ids: list[str]) -> list[CoinRow]:
    rows = []
    for coin_id in ids:
        entry = data.get(coin_id)
        if not entry or "usd" not in entry:
            continue
        rows.append(CoinRow(COIN_SYMBOLS.get(coin_id, coin_id[:4].upper()), float(entry["usd"]),
                            float(entry.get("usd_24h_change") or 0.0)))
    return rows


# ----------------------------------------------------------------- rendering

def _font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    return ImageFont.truetype(str(FONT_DIR / name), size)


def _canvas() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (SIZE, SIZE), BG)
    return img, ImageDraw.Draw(img)


def _encode(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=JPEG_QUALITY, subsampling=0, optimize=True, progressive=False)
    data = buf.getvalue()
    if len(data) >= MAX_JPEG_BYTES:  # busy content: trade quality for size
        buf = io.BytesIO()
        img.save(buf, "JPEG", quality=70, subsampling=0, optimize=True, progressive=False)
        data = buf.getvalue()
    return data


def _cloud(draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float, fill=(200, 205, 215)) -> None:
    draw.ellipse((cx - r * 1.1, cy - r * 0.2, cx - r * 0.1, cy + r * 0.7), fill=fill)
    draw.ellipse((cx - r * 0.6, cy - r * 0.7, cx + r * 0.5, cy + r * 0.5), fill=fill)
    draw.ellipse((cx + r * 0.1, cy - r * 0.3, cx + r * 1.1, cy + r * 0.7), fill=fill)
    draw.rectangle((cx - r * 0.6, cy + r * 0.1, cx + r * 0.6, cy + r * 0.7), fill=fill)


def _sun(draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float) -> None:
    for i in range(8):
        a = i * 3.14159 / 4
        x1, y1 = cx + r * 0.75 * math.cos(a), cy + r * 0.75 * math.sin(a)
        x2, y2 = cx + r * 1.05 * math.cos(a), cy + r * 1.05 * math.sin(a)
        draw.line((x1, y1, x2, y2), fill=AMBER, width=max(1, int(r / 7)))
    draw.ellipse((cx - r * 0.55, cy - r * 0.55, cx + r * 0.55, cy + r * 0.55), fill=AMBER)


def _moon(draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float) -> None:
    draw.ellipse((cx - r * 0.6, cy - r * 0.6, cx + r * 0.6, cy + r * 0.6), fill=(235, 230, 200))
    draw.ellipse((cx - r * 0.25, cy - r * 0.75, cx + r * 0.85, cy + r * 0.35), fill=BG)


def draw_icon(draw: ImageDraw.ImageDraw, kind: str, cx: float, cy: float, r: float) -> None:
    if kind == "sun":
        _sun(draw, cx, cy, r)
    elif kind == "moon":
        _moon(draw, cx, cy, r)
    elif kind == "moon_partly":
        _moon(draw, cx - r * 0.3, cy - r * 0.3, r * 0.7)
        _cloud(draw, cx + r * 0.15, cy + r * 0.15, r * 0.75)
    elif kind == "partly":
        _sun(draw, cx - r * 0.35, cy - r * 0.35, r * 0.7)
        _cloud(draw, cx + r * 0.15, cy + r * 0.15, r * 0.75)
    elif kind == "rain":
        _cloud(draw, cx, cy - r * 0.25, r * 0.8, fill=(170, 180, 195))
        for dx in (-0.45, 0.0, 0.45):
            x = cx + r * dx
            draw.line((x, cy + r * 0.55, x - r * 0.15, cy + r * 0.95), fill=(90, 160, 255), width=max(1, int(r / 8)))
    elif kind == "storm":
        _cloud(draw, cx, cy - r * 0.25, r * 0.8, fill=(130, 135, 150))
        bolt = [(cx, cy + r * 0.3), (cx - r * 0.25, cy + r * 0.7), (cx + r * 0.02, cy + r * 0.7),
                (cx - r * 0.15, cy + r * 1.05), (cx + r * 0.3, cy + r * 0.55), (cx + r * 0.05, cy + r * 0.55)]
        draw.polygon(bolt, fill=AMBER)
    elif kind == "snow":
        _cloud(draw, cx, cy - r * 0.25, r * 0.8)
        for dx in (-0.4, 0.0, 0.4):
            x = cx + r * dx
            draw.ellipse((x - r * 0.08, cy + r * 0.7, x + r * 0.08, cy + r * 0.86), fill=FG)
    else:
        _cloud(draw, cx, cy, r)


def fit_text(draw: ImageDraw.ImageDraw, text: str, font, max_width: float) -> str:
    """Text as is if it fits in max_width pixels, else cut with an ellipsis."""
    if draw.textlength(text, font=font) <= max_width:
        return text
    while text and draw.textlength(text + "…", font=font) > max_width:
        text = text[:-1]
    return text.rstrip() + "…"


def _text_right(draw: ImageDraw.ImageDraw, right: float, y: float, text: str, font, fill) -> None:
    draw.text((right - draw.textlength(text, font=font), y), text, font=font, fill=fill)


def _text_center(draw: ImageDraw.ImageDraw, cx: float, y: float, text: str, font, fill) -> None:
    draw.text((cx - draw.textlength(text, font=font) / 2, y), text, font=font, fill=fill)


def render_weather(w: Weather) -> bytes:
    img, draw = _canvas()
    draw_icon(draw, w.icon, 42, 40, 30)
    draw.text((86, 4), f"{w.temp}°", font=_font(50, bold=True), fill=FG)
    draw.text((88, 62), fit_text(draw, w.desc, _font(16), SIZE - 88 - 6), font=_font(16), fill=FG)
    draw.text((10, 84), f"Hum {w.humidity}%   Viento {w.wind} km/h", font=_font(14), fill=DIM)
    draw.line((8, 106, SIZE - 8, 106), fill=LINE, width=1)

    col = (SIZE - 12) / 6
    for i, h in enumerate(w.hours[:6]):
        cx = 6 + col * i + col / 2
        _text_center(draw, cx, 110, h.label, _font(11), DIM)
        draw_icon(draw, h.icon, cx, 136, 9)
        _text_center(draw, cx, 150, f"{h.temp}°", _font(14, bold=True), FG)
    draw.line((8, 172, SIZE - 8, 172), fill=LINE, width=1)

    for i, d in enumerate(w.days[:3]):
        y = 177 + i * 21
        draw.text((10, y), d.label, font=_font(14, bold=True), fill=AMBER)
        draw_icon(draw, d.icon, 62, y + 9, 8)
        draw.text((76, y), f"{d.tmax}°/{d.tmin}°", font=_font(14), fill=FG)
        draw.text((146, y + 1), fit_text(draw, d.desc, _font(12), SIZE - 146 - 6), font=_font(12), fill=DIM)
    return _encode(img)


def render_crypto(rows: list[CoinRow], time_label: str) -> bytes:
    img, draw = _canvas()
    draw.text((10, 8), "Cripto", font=_font(16, bold=True), fill=AMBER)
    _text_right(draw, SIZE - 10, 11, f"USD · {time_label}", _font(12), DIM)
    draw.line((8, 34, SIZE - 8, 34), fill=LINE, width=1)

    row_h = (SIZE - 42) // max(len(rows), 4)
    for i, row in enumerate(rows[:5]):
        y = 40 + i * row_h
        draw.text((10, y + 6), row.symbol, font=_font(20, bold=True), fill=AMBER)
        _text_right(draw, SIZE - 10, y + 2, format_price(row.price), _font(24, bold=True), FG)
        color = GREEN if row.change >= 0 else RED
        _text_right(draw, SIZE - 10, y + 30, format_change(row.change), _font(14), color)
        if i < len(rows) - 1:
            draw.line((8, y + row_h - 2, SIZE - 8, y + row_h - 2), fill=LINE, width=1)
    return _encode(img)


def render_error(title: str, message: str) -> bytes:
    img, draw = _canvas()
    _text_center(draw, SIZE / 2, 90, title, _font(22, bold=True), AMBER)
    _text_center(draw, SIZE / 2, 126, message, _font(15), DIM)
    return _encode(img)
