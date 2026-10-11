#!/usr/bin/env python3
"""Host tests for scripts/face_assets.py (skipped when Pillow/numpy are missing)."""

from __future__ import annotations

import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
SCRIPT = TEST_DIR.parent.parent / "scripts" / "face_assets.py"

try:
    import numpy as np
    from PIL import Image, ImageDraw
    HAVE_DEPS = True
except ImportError:  # pragma: no cover - depends on the environment
    HAVE_DEPS = False


def load_tool():
    spec = importlib.util.spec_from_file_location("face_assets", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_frame(path: Path):
    data = path.read_bytes()
    magic = data[:4]
    x, y, w, h, flags, _ = struct.unpack("<HHHHHH", data[4:16])
    pixels = np.frombuffer(data[16:], dtype=">u2").reshape(h, w)
    return magic, x, y, w, h, flags, pixels, len(data)


def portrait(eyes_open: bool = True, mouth_open: bool = False) -> "Image.Image":
    img = Image.new("RGB", (320, 420), (40, 50, 50))
    draw = ImageDraw.Draw(img)
    draw.ellipse((70, 60, 250, 330), fill=(205, 160, 130))           # face
    draw.rectangle((70, 260, 250, 400), fill=(30, 25, 20))            # beard
    eye = (250, 250, 250) if eyes_open else (180, 130, 110)
    draw.ellipse((110, 160, 150, 185), fill=eye)
    draw.ellipse((170, 160, 210, 185), fill=eye)
    if mouth_open:
        draw.ellipse((135, 280, 185, 315), fill=(90, 20, 20))
    return img


@unittest.skipUnless(HAVE_DEPS, "Pillow and numpy are required")
class FaceAssetsTest(unittest.TestCase):
    def setUp(self):
        self.tool = load_tool()
        self.tmp = tempfile.TemporaryDirectory()
        self.src = Path(self.tmp.name) / "in"
        self.out = Path(self.tmp.name) / "out"
        self.src.mkdir()
        portrait().save(self.src / "neutral.png")
        portrait(eyes_open=False).save(self.src / "eyes_closed.png")
        portrait(mouth_open=True).save(self.src / "speaking.png")
        portrait(mouth_open=True).save(self.src / "speaking_2.png")
        portrait().save(self.src / "thinking.png")

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *extra: str) -> int:
        return self.tool.run(["--input", str(self.src), "--output", str(self.out), *extra])

    def test_full_frames_have_valid_headers(self):
        self.assertEqual(self.run_tool(), 0)
        for name in ("neutral", "thinking"):
            magic, x, y, w, h, flags, _, size = read_frame(self.out / f"{name}.r565")
            self.assertEqual(magic, b"R565")
            self.assertEqual((x, y, w, h, flags), (0, 0, 240, 240, 0))
            self.assertEqual(size, 16 + 240 * 240 * 2)

    def test_patches_are_small_and_flagged(self):
        self.assertEqual(self.run_tool(), 0)
        for name in ("eyes_closed", "speaking", "speaking_2"):
            magic, x, y, w, h, flags, _, size = read_frame(self.out / f"{name}.r565")
            self.assertEqual(magic, b"R565")
            self.assertEqual(flags & 1, 1)
            self.assertLess(w * h, 240 * 240 // 4)
            self.assertLessEqual(x + w, 240)
            self.assertLessEqual(y + h, 240)
            self.assertEqual(size, 16 + w * h * 2)

    def test_patch_border_matches_neutral(self):
        self.assertEqual(self.run_tool(), 0)
        *_, neutral, _ = read_frame(self.out / "neutral.r565")
        _, x, y, w, h, _, patch, _ = read_frame(self.out / "eyes_closed.r565")
        np.testing.assert_array_equal(patch[0, :], neutral[y, x:x + w])
        np.testing.assert_array_equal(patch[:, 0], neutral[y:y + h, x])
        self.assertFalse(np.array_equal(patch, neutral[y:y + h, x:x + w]))

    def test_manual_band_override(self):
        self.assertEqual(self.run_tool("--eyes", "60,80,120,30"), 0)
        _, x, y, w, h, *_ = read_frame(self.out / "eyes_closed.r565")
        self.assertEqual((x, y, w, h), (60, 80, 120, 30))

    def test_missing_neutral_fails_cleanly(self):
        (self.src / "neutral.png").unlink()
        self.assertEqual(self.run_tool(), 2)
        self.assertFalse(self.out.exists() and any(self.out.glob("*.r565")))

    def test_contact_sheet_written(self):
        self.assertEqual(self.run_tool(), 0)
        self.assertTrue((self.out / "contact.png").exists())


if __name__ == "__main__":
    unittest.main()
