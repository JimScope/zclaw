#!/usr/bin/env python3
"""Convert face photos into .r565 frames for Fidel's ST7789 display.

Input folder: neutral.png (required) plus any of eyes_closed, speaking,
thinking, sleeping, happy, angry, speech (.png/.jpg), and numbered variants
such as speaking_2.png ... speaking_8.png.

Output: <name>.r565 files to copy into the SD card's face/ folder, and a
contact sheet for review. eyes_closed/speaking become small patches drawn
over neutral, so blinking and talking never shimmer.

    ~/.venvs/zclaw-face/bin/python scripts/face_assets.py \\
        --input assets/face --output build/face
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

FULL_FRAMES = ("neutral", "thinking", "sleeping", "happy", "angry", "speech")
PATCH_FRAMES = {"eyes_closed": "eyes", "speaking": "mouth"}
ALIGNED_FRAMES = ("eyes_closed", "speaking", "happy", "angry")
MAX_VARIANTS = 8
EXTENSIONS = (".png", ".jpg", ".jpeg")
FLAG_PATCH = 0x0001
BAND_PAD = 10
FEATHER = 6
# Search windows for automatic band detection, as fractions of the frame.
BAND_WINDOWS = {"eyes": (0.25, 0.60), "mouth": (0.50, 0.85)}
BAND_X_WINDOW = (0.15, 0.85)


def find_inputs(folder: Path) -> dict[str, list[tuple[int, Path]]]:
    """Map base name -> [(variant number, path)] sorted by variant."""
    found: dict[str, list[tuple[int, Path]]] = {}
    for base in (*FULL_FRAMES, *PATCH_FRAMES):
        for variant in range(1, MAX_VARIANTS + 1):
            stem = base if variant == 1 else f"{base}_{variant}"
            for ext in EXTENSIONS:
                path = folder / f"{stem}{ext}"
                if path.exists():
                    found.setdefault(base, []).append((variant, path))
                    break
    return found


def output_name(base: str, variant: int) -> str:
    return f"{base}.r565" if variant == 1 else f"{base}_{variant}.r565"


def border_color(img: Image.Image) -> tuple[int, int, int]:
    top = np.asarray(img)[:5].reshape(-1, 3)
    return tuple(int(v) for v in np.median(top, axis=0))


def blurred_gray(img: Image.Image) -> np.ndarray:
    return np.asarray(img.convert("L").filter(ImageFilter.GaussianBlur(2)), dtype=np.float32)


def phase_shift(ref: np.ndarray, moving: np.ndarray) -> tuple[int, int, float]:
    """Integer (dx, dy) that aligns `moving` onto `ref`, and the correlation peak."""
    h, w = ref.shape
    window = np.outer(np.hanning(h), np.hanning(w)).astype(np.float32)
    a = np.fft.fft2((ref - ref.mean()) * window)
    b = np.fft.fft2((moving - moving.mean()) * window)
    cross = a * np.conj(b)
    corr = np.fft.ifft2(cross / (np.abs(cross) + 1e-9)).real
    dy, dx = np.unravel_index(int(np.argmax(corr)), corr.shape)
    if dy > h // 2:
        dy -= h
    if dx > w // 2:
        dx -= w
    return int(dx), int(dy), float(corr.max())


def align_to(ref: Image.Image, img: Image.Image, min_peak: float, name: str) -> Image.Image:
    fill = border_color(ref)
    canvas = Image.new("RGB", ref.size, fill)
    canvas.paste(img, (0, 0))
    dx, dy, peak = phase_shift(blurred_gray(ref), blurred_gray(canvas))
    if peak < min_peak:
        print(f"warning: {name}: alignment not confident (peak {peak:.3f}), using as is")
        return canvas
    return canvas.transform(canvas.size, Image.AFFINE, (1, 0, -dx, 0, 1, -dy), fillcolor=fill)


def square_crop(img: Image.Image, size: int, center_y: float) -> Image.Image:
    w, h = img.size
    side = min(w, h)
    left = (w - side) // 2
    top = int(round(h * center_y - side / 2))
    top = max(0, min(top, h - side))
    return img.crop((left, top, left + side, top + side)).resize((size, size), Image.LANCZOS)


def detect_band(base: Image.Image, frame: Image.Image, region: str) -> tuple[int, int, int, int]:
    """(x, y, w, h) of the strongest difference inside the region's window."""
    size = base.size[0]
    diff = np.abs(np.asarray(base, dtype=np.float32) - np.asarray(frame, dtype=np.float32)).sum(axis=2)
    diff8 = np.clip(diff / 3, 0, 255).astype(np.uint8)
    blurred = np.asarray(Image.fromarray(diff8, "L").filter(ImageFilter.BoxBlur(6)), dtype=np.float32)

    y0, y1 = (int(size * f) for f in BAND_WINDOWS[region])
    x0, x1 = (int(size * f) for f in BAND_X_WINDOW)
    window = blurred[y0:y1, x0:x1]
    if window.max() <= 0:
        raise ValueError(f"no difference found in the {region} window")
    ys, xs = np.nonzero(window > window.max() * 0.45)
    left = max(int(xs.min()) + x0 - BAND_PAD, 0)
    top = max(int(ys.min()) + y0 - BAND_PAD, 0)
    right = min(int(xs.max()) + x0 + BAND_PAD + 1, size)
    bottom = min(int(ys.max()) + y0 + BAND_PAD + 1, size)
    return left, top, right - left, bottom - top


def feathered_mask(size: int, box: tuple[int, int, int, int]) -> Image.Image:
    x, y, w, h = box
    mask = Image.new("L", (size, size), 0)
    inner = (x + FEATHER, y + FEATHER, x + w - FEATHER, y + h - FEATHER)
    if inner[2] > inner[0] and inner[3] > inner[1]:
        mask.paste(255, inner)
    mask = mask.filter(ImageFilter.GaussianBlur(FEATHER / 2))
    clipped = Image.new("L", (size, size), 0)
    clipped.paste(mask.crop((x, y, x + w, y + h)), (x, y))
    # The blur tail can leave a few levels on the box edge; force the edge to
    # neutral so restoring the patch area is seamless.
    ImageDraw.Draw(clipped).rectangle((x, y, x + w - 1, y + h - 1), outline=0)
    return clipped


def to_rgb565_be(img: Image.Image) -> bytes:
    a = np.asarray(img.convert("RGB"), dtype=np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    return v.astype(">u2").tobytes()


def write_r565(path: Path, img: Image.Image, x: int = 0, y: int = 0, flags: int = 0) -> None:
    w, h = img.size
    header = b"R565" + struct.pack("<HHHHHH", x, y, w, h, flags, 0)
    path.write_bytes(header + to_rgb565_be(img))


def parse_box(text: str | None) -> tuple[int, int, int, int] | None:
    if not text:
        return None
    parts = [int(p) for p in text.split(",")]
    if len(parts) != 4 or parts[2] <= 0 or parts[3] <= 0:
        raise argparse.ArgumentTypeError("expected X,Y,W,H with positive W and H")
    return parts[0], parts[1], parts[2], parts[3]


def contact_sheet(tiles: list[tuple[str, Image.Image]], path: Path) -> None:
    size = tiles[0][1].size[0]
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * size, rows * (size + 16)), "white")
    draw = ImageDraw.Draw(sheet)
    for i, (label, img) in enumerate(tiles):
        x, y = (i % cols) * size, (i // cols) * (size + 16)
        sheet.paste(img, (x, y + 16))
        draw.text((x + 4, y + 2), label, fill="black")
    sheet.save(path)


def run(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Build .r565 face frames for the SD card.")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--size", type=int, default=240)
    parser.add_argument("--center-y", type=float, default=0.5,
                        help="vertical crop centre as a fraction of the image height")
    parser.add_argument("--eyes", type=parse_box, help="eye band X,Y,W,H (overrides detection)")
    parser.add_argument("--mouth", type=parse_box, help="mouth band X,Y,W,H (overrides detection)")
    parser.add_argument("--min-peak", type=float, default=0.02,
                        help="minimum phase-correlation peak to apply alignment")
    parser.add_argument("--contact-sheet", type=Path, default=None)
    args = parser.parse_args(argv)

    inputs = find_inputs(args.input)
    if "neutral" not in inputs:
        print(f"error: {args.input}/neutral.png (or .jpg) is required", file=sys.stderr)
        return 2

    args.output.mkdir(parents=True, exist_ok=True)
    neutral_src = Image.open(inputs["neutral"][0][1]).convert("RGB")
    neutral = square_crop(neutral_src, args.size, args.center_y)
    overrides = {"eyes": args.eyes, "mouth": args.mouth}
    tiles: list[tuple[str, Image.Image]] = []

    for base, variants in inputs.items():
        for variant, path in variants:
            name = output_name(base, variant)
            src = Image.open(path).convert("RGB")
            if base == "neutral" and variant == 1:
                frame = neutral
            else:
                if base in ALIGNED_FRAMES:
                    src = align_to(neutral_src, src, args.min_peak, name)
                frame = square_crop(src, args.size, args.center_y)

            if base in PATCH_FRAMES:
                region = PATCH_FRAMES[base]
                try:
                    box = overrides[region] or detect_band(neutral, frame, region)
                except ValueError as exc:
                    print(f"error: {name}: {exc}; pass --{region} X,Y,W,H", file=sys.stderr)
                    return 2
                x, y, w, h = box
                if x < 0 or y < 0 or x + w > args.size or y + h > args.size:
                    print(f"error: {region} band {box} is outside the {args.size}px frame",
                          file=sys.stderr)
                    return 2
                composed = Image.composite(frame, neutral, feathered_mask(args.size, box))
                write_r565(args.output / name, composed.crop((x, y, x + w, y + h)), x, y, FLAG_PATCH)
                tiles.append((f"{name} {box}", composed))
                print(f"{name}: patch {box}")
            else:
                write_r565(args.output / name, frame)
                tiles.append((name, frame))
                print(f"{name}: full frame")

    contact_sheet(tiles, args.contact_sheet or (args.output / "contact.png"))
    print(f"done: {len(tiles)} frames in {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
