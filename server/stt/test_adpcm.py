#!/usr/bin/env python3
"""Tests for the ADPCM decoder (golden vector shared with test/host/test_voice_audio.c)."""

from __future__ import annotations

import math
import unittest

import adpcm

GOLDEN_PCM = [
    -1000, 3508, 7653, 8908, 11413, 12800, 10813, 9908, 8053, 3308, 400, -4708, -7253, -9108,
    -12213, -12000, -10613, -10308, -6853, -2708, -400, 4108, 6053, 9508, 12013, 11200, 11413,
    10508, 6453, 3908, 1000, -4108, -6653, -10708, -11613, -11400, -12213, -9708, -6253, -4308,
    200, 4708, 6653, 10108, 10413, 11800, 12013, 8908, 7053, 4508, -600, -3508, -6053, -10108,
    -11013, -13000, -11613, -9108, -7853, -3708, 800, 3108, 7253, 10708,
]
GOLDEN_ADPCM = bytes([
    0x7f, 0x77, 0x77, 0x77, 0xe3, 0xba, 0xaa, 0x8a, 0x82, 0x36, 0x42, 0x31, 0x93, 0x90, 0xae, 0xda,
    0xaa, 0x09, 0x28, 0x24, 0x35, 0x31, 0x10, 0xc0, 0xaa, 0xaf, 0xb9, 0xa8, 0x31, 0x61, 0x23, 0x33,
])


class DecodeTest(unittest.TestCase):
    def test_golden_vector_tracks_the_signal(self):
        out = adpcm.decode(b"\x00\x00\x00\x00" + GOLDEN_ADPCM)
        self.assertEqual(len(out), 64)
        # the first decoded samples of the golden stream (fixed by the encoder)
        self.assertEqual(out[:4], [-11, 19, 82, 218])
        # after convergence the decoder follows the signal
        err = sum(abs(a - b) for a, b in zip(out[48:], GOLDEN_PCM[48:])) / 16
        self.assertLess(err, 600)

    def test_header_sets_initial_state(self):
        out = adpcm.decode(b"\xfe\xff\x05\x00" + b"\x00")
        self.assertEqual(len(out), 2)
        # code 0 adds step>>3 to the predictor: step[5] = 12 -> +1, then index 4, step 11 -> +1
        self.assertEqual(out, [-1, 0])

    def test_rejects_bad_header(self):
        with self.assertRaises(adpcm.AdpcmError):
            adpcm.decode(b"\x00\x00")
        with self.assertRaises(adpcm.AdpcmError):
            adpcm.decode(b"\x00\x00\x59\x00")  # index 89
        with self.assertRaises(adpcm.AdpcmError):
            adpcm.decode(b"\x00\x00\x00\x01")  # reserved byte set

    def test_encoder_matches_board_golden_vector(self):
        self.assertEqual(adpcm.encode(GOLDEN_PCM), b"\x00\x00\x00\x00" + GOLDEN_ADPCM)

    def test_round_trip_keeps_speech_like_signal(self):
        sig = [int(4800 * math.sin(2 * math.pi * 220 * i / 16000)
                   + 2000 * math.sin(2 * math.pi * 730 * i / 16000)
                   + 2000 * math.sin(2 * math.pi * 2100 * i / 16000)) for i in range(16000)]
        back = adpcm.decode(adpcm.encode(sig))
        num = sum(x * x for x in sig)
        den = sum((a - b) ** 2 for a, b in zip(sig, back))
        self.assertGreater(10 * math.log10(num / den), 20.0)

    def test_pcm16le(self):
        self.assertEqual(adpcm.to_pcm16le([1, -2]), b"\x01\x00\xfe\xff")


if __name__ == "__main__":
    unittest.main()
