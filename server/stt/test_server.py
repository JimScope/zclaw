#!/usr/bin/env python3
"""Tests for the STT HTTP service (the NVIDIA recognizer is a stub)."""

from __future__ import annotations

import http.client
import json
import os
import tempfile
import threading
import time
import unittest
import wave
from http.server import ThreadingHTTPServer

import server

TOKEN = "t" * 24
HEADERS = {"X-Stt-Token": TOKEN, "X-Audio-Format": "ima-adpcm; rate=16000"}
BODY = b"\x00\x00\x00\x00" + bytes(1000)  # header + 2000 samples of near-silence


class FakeAsr:
    def __init__(self, text: str = "hola fidel", delay: float = 0.0, fail: bool = False):
        self.text, self.delay, self.fail = text, delay, fail
        self.calls: list[bytes] = []
        self.release = threading.Event()
        self.release.set()

    def __call__(self, pcm: bytes) -> str:
        self.calls.append(pcm)
        self.release.wait(5)
        time.sleep(self.delay)
        if self.fail:
            raise RuntimeError("nvidia down")
        return self.text


class SttServerTest(unittest.TestCase):
    def start(self, asr: FakeAsr, deadline: float = 2.0, last_wav: str | None = None):
        stt = server.Stt(asr, last_wav=last_wav, deadline_s=deadline)
        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.make_handler(stt, TOKEN))
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        self.addCleanup(self.httpd.shutdown)

    def post(self, body: bytes = BODY, headers: dict | None = None, length: int | None = None):
        conn = http.client.HTTPConnection("127.0.0.1", self.httpd.server_address[1], timeout=10)
        hdrs = dict(HEADERS if headers is None else headers)
        hdrs["Content-Length"] = str(len(body) if length is None else length)
        conn.request("POST", "/transcribe", body=body if length is None else b"", headers=hdrs)
        resp = conn.getresponse()
        data = resp.read()
        conn.close()
        return resp.status, json.loads(data) if data.startswith(b"{") else data

    def test_health(self):
        self.start(FakeAsr())
        conn = http.client.HTTPConnection("127.0.0.1", self.httpd.server_address[1], timeout=5)
        conn.request("GET", "/health")
        resp = conn.getresponse()
        self.assertEqual((resp.status, resp.read()), (200, b"ok"))

    def test_transcribes_and_collapses_whitespace(self):
        asr = FakeAsr(text="  hola   fidel \n")
        self.start(asr)
        status, body = self.post()
        self.assertEqual(status, 200)
        self.assertEqual(body["text"], "hola fidel")
        self.assertIsInstance(body["ms"], int)
        # 250 ms of leading silence (4000 samples) + PCM16 of the 2000 decoded samples
        self.assertEqual(len(asr.calls[0]), (4000 + 2000) * 2)
        self.assertEqual(asr.calls[0][:8000], bytes(8000))

    def test_moves_opening_marks_riva_glues_after_the_word(self):
        cases = {
            "Cuándo¿ terminará su mandato?": "¿Cuándo terminará su mandato?",
            "Qué¿ hora es en La Habana?": "¿Qué hora es en La Habana?",
            "Hola, Fidel. Qué¿ hora es?": "Hola, Fidel. ¿Qué hora es?",
            "Qué¡ calor hace!": "¡Qué calor hace!",
            "¿Qué hora es?": "¿Qué hora es?",
            "hola fidel": "hola fidel",
        }
        for raw, want in cases.items():
            self.assertEqual(server.tidy_text(raw), want)
        self.start(FakeAsr(text="Qué¿ hora es?"))
        status, body = self.post()
        self.assertEqual((status, body["text"]), (200, "¿Qué hora es?"))

    def test_wrong_token(self):
        self.start(FakeAsr())
        status, _ = self.post(headers={**HEADERS, "X-Stt-Token": "nope"})
        self.assertEqual(status, 401)

    def test_wrong_format(self):
        self.start(FakeAsr())
        status, _ = self.post(headers={**HEADERS, "X-Audio-Format": "pcm"})
        self.assertEqual(status, 400)

    def test_size_limits(self):
        asr = FakeAsr()
        self.start(asr)
        self.assertEqual(self.post(length=server.MAX_BODY + 1)[0], 413)
        self.assertEqual(self.post(body=b"\x00\x00\x00\x00" + bytes(10))[0], 400)
        self.assertEqual(asr.calls, [])

    def test_bad_header_bytes(self):
        self.start(FakeAsr())
        status, body = self.post(body=b"\x00\x00\x59\x00" + bytes(1000))
        self.assertEqual(status, 400)

    def test_asr_failure_and_timeout(self):
        self.start(FakeAsr(fail=True))
        status, body = self.post()
        self.assertEqual((status, body["error"]), (502, "asr: RuntimeError"))
        self.httpd.shutdown()
        self.start(FakeAsr(delay=0.5), deadline=0.1)
        status, body = self.post()
        self.assertEqual((status, body["error"]), (502, "timeout"))

    def test_one_at_a_time(self):
        asr = FakeAsr()
        asr.release.clear()
        self.start(asr)
        first: list = []
        t = threading.Thread(target=lambda: first.append(self.post()))
        t.start()
        for _ in range(50):
            if asr.calls:
                break
            time.sleep(0.02)
        self.assertEqual(self.post()[0], 503)
        asr.release.set()
        t.join(5)
        self.assertEqual(first[0][0], 200)

    def test_keeps_last_recording(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "last.wav")
            self.start(FakeAsr(), last_wav=path)
            self.assertEqual(self.post()[0], 200)
            with wave.open(path, "rb") as w:
                self.assertEqual((w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()),
                                 (1, 2, 16000, 2000))


if __name__ == "__main__":
    unittest.main()
