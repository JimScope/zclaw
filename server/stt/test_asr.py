#!/usr/bin/env python3
"""Tests for the Riva wrapper with a fake riva.client module (no network)."""

from __future__ import annotations

import sys
import types
import unittest


class _Alt:
    def __init__(self, text):
        self.transcript = text


class _Result:
    def __init__(self, text, final=True):
        self.alternatives = [_Alt(text)] if text is not None else []
        self.is_final = final


class _Response:
    def __init__(self, results):
        self.results = results


def install_fake_riva(log: dict) -> None:
    class Auth:
        def __init__(self, **kw):
            log["auth"] = kw

    class ASRService:
        def __init__(self, auth):
            pass

        def offline_recognize(self, pcm, config):
            log["offline"] = (pcm, config)
            return _Response([_Result(" hola "), _Result("fidel"), _Result(None)])

        def streaming_response_generator(self, audio_chunks, streaming_config):
            log["chunks"] = [len(c) for c in audio_chunks]
            log["streaming_config"] = streaming_config
            yield _Response([_Result("hola", final=False)])
            yield _Response([_Result("hola fidel")])

    class _Config:
        def __init__(self, **kw):
            self.__dict__.update(kw)

    client = types.SimpleNamespace(
        Auth=Auth,
        ASRService=ASRService,
        RecognitionConfig=_Config,
        StreamingRecognitionConfig=_Config,
        AudioEncoding=types.SimpleNamespace(LINEAR_PCM="LINEAR_PCM"),
    )
    riva = types.ModuleType("riva")
    riva.client = client
    sys.modules["riva"] = riva
    sys.modules["riva.client"] = client


class RivaAsrTest(unittest.TestCase):
    def setUp(self):
        self.log: dict = {}
        install_fake_riva(self.log)
        import asr
        self.asr = asr

    def test_offline_joins_transcripts_and_authenticates(self):
        rec = self.asr.RivaASR(api_key="nvapi-x", function_id="fid", language="es-US")
        self.assertEqual(rec(b"\x00\x00" * 10), "hola fidel")
        auth = self.log["auth"]
        self.assertEqual(auth["uri"], "grpc.nvcf.nvidia.com:443")
        self.assertTrue(auth["use_ssl"])
        self.assertIn(["function-id", "fid"], auth["metadata_args"])
        self.assertIn(["authorization", "Bearer nvapi-x"], auth["metadata_args"])
        config = self.log["offline"][1]
        self.assertEqual((config.sample_rate_hertz, config.language_code, config.audio_channel_count),
                         (16000, "es-US", 1))

    def test_streaming_keeps_final_results_in_100ms_chunks(self):
        rec = self.asr.RivaASR(api_key="k", function_id="f", mode="streaming")
        self.assertEqual(rec(bytes(8000)), "hola fidel")
        self.assertEqual(self.log["chunks"], [3200, 3200, 1600])
        self.assertFalse(self.log["streaming_config"].interim_results)

    def test_unknown_mode(self):
        with self.assertRaises(ValueError):
            self.asr.RivaASR(api_key="k", function_id="f", mode="batch")


if __name__ == "__main__":
    unittest.main()
