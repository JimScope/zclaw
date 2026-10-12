#!/usr/bin/env python3
"""Speech to text for Fidel's board: IMA-ADPCM in, NVIDIA Riva transcript out.

    POST /transcribe   (X-Stt-Token, X-Audio-Format: ima-adpcm; rate=16000)
    GET  /health

Caddy serves it as https://<host>/stt/... (handle_path strips /stt).
"""

from __future__ import annotations

import concurrent.futures
import hmac
import json
import os
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable

import adpcm

PORT = int(os.environ.get("STT_PORT", "9003"))
SAMPLE_RATE = 16000
AUDIO_FORMAT = "ima-adpcm;rate=16000"  # compared without spaces
MAX_BODY = 120 * 1024                  # ~15 s
MIN_BODY = adpcm.HEADER_BYTES + 800    # 0.1 s
DEADLINE_S = 12.0
# Parakeet (offline) drops the first words when speech starts at t=0; the
# board's recordings usually begin with silence, this makes it certain.
LEAD_SILENCE = bytes(SAMPLE_RATE // 4 * 2)  # 250 ms of PCM16

Recognizer = Callable[[bytes], str]    # PCM16LE mono 16 kHz -> transcript


def write_wav(path: str, pcm: bytes) -> None:
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)


class Stt:
    """One transcription at a time, with a deadline on the recognizer."""

    def __init__(self, recognize: Recognizer, last_wav: str | None = None,
                 deadline_s: float = DEADLINE_S):
        self._recognize = recognize
        self._last_wav = last_wav
        self._deadline_s = deadline_s
        self._busy = threading.Lock()
        self._pool = concurrent.futures.ThreadPoolExecutor(max_workers=2)

    def transcribe(self, stream: bytes) -> tuple[int, dict]:
        if not self._busy.acquire(blocking=False):
            return 503, {"error": "busy"}
        try:
            try:
                pcm = adpcm.to_pcm16le(adpcm.decode(stream))
            except adpcm.AdpcmError as exc:
                return 400, {"error": str(exc)}
            if self._last_wav:
                try:
                    write_wav(self._last_wav, pcm)
                except OSError:
                    pass  # the copy for gain tuning is optional
            start = time.monotonic()
            future = self._pool.submit(self._recognize, LEAD_SILENCE + pcm)
            try:
                text = future.result(timeout=self._deadline_s)
            except concurrent.futures.TimeoutError:
                return 502, {"error": "timeout"}
            except Exception as exc:  # noqa: BLE001 (any ASR failure is a 502)
                return 502, {"error": f"asr: {type(exc).__name__}"}
            ms = int((time.monotonic() - start) * 1000)
            return 200, {"text": " ".join(str(text).split()), "ms": ms}
        finally:
            self._busy.release()


def make_handler(stt: Stt, token: str) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "fidel-stt"

        def log_message(self, fmt: str, *args) -> None:  # noqa: A003 (http.server API)
            pass  # no access log: requests carry private audio

        def _send(self, status: int, body: dict) -> None:
            data = json.dumps(body, ensure_ascii=False).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self) -> None:  # noqa: N802 (http.server API)
            if self.path == "/health":
                data = b"ok"
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            self._send(404, {"error": "not found"})

        def do_POST(self) -> None:  # noqa: N802 (http.server API)
            if self.path != "/transcribe":
                self._send(404, {"error": "not found"})
                return
            given = self.headers.get("X-Stt-Token", "")
            if not hmac.compare_digest(given.encode(), token.encode()):
                self.close_connection = True
                self._send(401, {"error": "bad token"})
                return
            fmt = self.headers.get("X-Audio-Format", "").replace(" ", "").lower()
            if fmt != AUDIO_FORMAT:
                self.close_connection = True
                self._send(400, {"error": "unsupported audio format"})
                return
            try:
                length = int(self.headers.get("Content-Length", ""))
            except ValueError:
                length = -1
            if length > MAX_BODY:
                self.close_connection = True
                self._send(413, {"error": "too long"})
                return
            if length < MIN_BODY:
                self.close_connection = True
                self._send(400, {"error": "too short"})
                return
            status, body = stt.transcribe(self.rfile.read(length))
            self._send(status, body)

    return Handler


def main() -> None:
    token = os.environ.get("STT_TOKEN", "").strip()
    if len(token) < 16:
        raise SystemExit("STT_TOKEN (>= 16 chars) is required: /stt is public")
    import asr  # needs nvidia-riva-client; kept out of the tests

    recognize = asr.RivaASR(
        api_key=os.environ["NVIDIA_API_KEY"],
        function_id=os.environ.get("RIVA_FUNCTION_ID", asr.PARAKEET_MULTILINGUAL),
        language=os.environ.get("RIVA_LANGUAGE", "es-US"),
        server=os.environ.get("RIVA_SERVER", "grpc.nvcf.nvidia.com:443"),
        mode=os.environ.get("RIVA_MODE", "offline"),
    )
    last_wav = os.path.join(os.path.dirname(os.path.abspath(__file__)), "last.wav")
    ThreadingHTTPServer.daemon_threads = True
    httpd = ThreadingHTTPServer(("127.0.0.1", PORT), make_handler(Stt(recognize, last_wav), token))
    httpd.serve_forever()


if __name__ == "__main__":
    main()
