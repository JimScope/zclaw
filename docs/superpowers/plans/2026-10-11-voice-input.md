# Voice Input (Deliveries B+C) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Talk to Fidel by voice. Two claps open listening, the board records until you stop, and the VPS transcribes the audio with an NVIDIA hosted model. The transcript reaches the agent and the answer shows on the screen under "Dijiste: …".

**Architecture:**
- **Pure modules** (host-tested): conversion of the 32 kHz ADC stream to 16 kHz audio, level metering, double-clap detection, end-of-speech detection, IMA-ADPCM encoding, and parsing of the STT reply.
- **Thin ESP-only glue:**
  - the ADC continuous driver task;
  - a `voice` task that owns the state machine, the HTTPS upload, the screen and LED hooks and the agent queue;
  - display additions: listening face, level bar, `/mic` page and notices.
- **VPS:** a small Python service decodes ADPCM and calls NVIDIA Riva (Parakeet multilingual, `es-US`) through `nvidia-riva-client`.

**Tech Stack:** ESP-IDF v5.5.5 on the ESP32-C6 (`esp_adc` continuous mode, `esp_http_client` + certificate bundle), C99 host tests with gcc, Python 3 stdlib `http.server` + `nvidia-riva-client` on the VPS (systemd user unit behind Caddy).

**Spec:** `docs/superpowers/specs/2026-10-11-voice-input-design.md`

## Global Constraints

- **Repository and builds.** Repo `~/Projects/zclaw`, branch `fidel`. ESP-IDF at `~/esp/esp-idf`.
  - Firmware build: `source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && idf.py build > build/idf-build.log 2>&1; grep -E "Project build complete|error:" build/idf-build.log | head -5`.
  - After any Kconfig change, run `idf.py reconfigure` before the build. A plain build does not re-read Kconfig: Delivery A, Task 1 ruling.
- **Host gate.**
  - At setup, copy `.superpowers/sdd/2026-10-11-display-sd/host-gate.sh` and `baseline-failures.txt` into this plan's workspace `.superpowers/sdd/2026-10-11-voice-input/`.
  - Command: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`. Expected output starts with `host gate OK`.
  - Only these two pre-existing failures are tolerated: `test_agent.c` line 784 and `test_builtin_tools_registry.c:103`. Append new tests to `test_agent.c` only after line 784, before `int test_agent_all(void)`.
- **VPS.** `HOST=jianpedi@107.173.67.243`, using the Mac's default ssh key.
  - Services run as systemd **user** units; no sudo needed.
  - Changes in `/etc/caddy` are applied **by the user** with sudo.
  - Never create dated `*.bak` copies on the VPS.
- **Secrets.** Never print or commit them: the NVIDIA key, `STT_TOKEN`, the OTA PIN, `sdkconfig*`. Pass them through variables or pipes, never `echo` or `cat` them. `sdkconfig` and `sdkconfig.defaults.esp32c6` are git-ignored; never delete `sdkconfig`.
- **Board access.** Ask the user before flashing over USB. Web OTA with the PIN is fine once the board runs Delivery A.
- **Language.** Code, identifiers and commit messages in English. Screen text in Spanish, written in C as `\xNN` UTF-8 escapes. Split a string literal after an escape when the next character is a hex digit, e.g. `"Micr\xC3\xB3" "fono"`.
- **Host tests.** Warning-clean under `-std=c99 -Wall -Wextra -Werror -Wshadow -Wformat=2`.
  - Do not rely on `M_PI` (not guaranteed under c99): tests define `TEST_PI`.
  - `audio_level.c` uses `log10`, so the host runner links `-lm`.
- **No floats in printf.** newlib nano printf on the board cannot print floats. Levels travel as tenths of dB in `int`.
- **Spec values, verbatim:**
  - ADC1 continuous at 32 kHz, 12-bit, 12 dB attenuation; pairs averaged to 16 kHz.
  - 10 ms frames (160 samples).
  - Clap: peak ≥ `CLAP_LEVEL_PCT` % (default 40) and ≥ `CLAP_RATIO` × noise (default 6); decays below half within 80 ms; second onset 150–700 ms after the first; no third clap in the 300 ms after; `CLAP_DOUBLE` 300 ms after the second clap.
  - Speech: starts after 6 frames above max(`SPEECH_RATIO` × noise, minimum) (default ratio 3); ends after 120 quiet frames (1.2 s); 1000 frames (10 s) maximum; 400 frames (4 s) without speech cancels.
  - ADPCM buffer 4 + 80000 bytes. Upload timeout 15 s.
  - Server: body ≤ 120 KB and ≥ 4 + 800 bytes; one request at a time (503 when busy); 12 s deadline to NVIDIA; `last.wav` overwritten on each request.
  - Error notices last 3 s. `/mic` lasts 20 s and refreshes every 250 ms.
  - Level bar: y 232–239, −60…0 dBFS, redrawn every 50 ms at most.
- **Commit trailer.** Every commit message ends with:

  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```

### Design decisions this plan takes

- **D1, endpoint path.** Caddy's `handle_path /stt/*` strips `/stt`, so the service route is `POST /transcribe`. The board URL is `https://bolita-cubana.duckdns.org/stt/transcribe` and health is `…/stt/health`. The spec names it `/stt`.
- **D2, Python encoder.** `server/stt/adpcm.py` also has an encoder. The tests use it to check the board's golden vector from the Python side, and the HTTPS smoke test uses it to encode a real WAV file.
- **D3, side effects stay in the voice task.** The audio task only updates the state and sends task notifications. LED, display, malloc and HTTPS all run in the `voice` task, because the LED helper sleeps one frame and must not stall the ADC reader.
- **D4, listening watchdog.** If listening lasts longer than 12 s of wall-clock time (frames stopped arriving), the voice task cancels it.
- **D5, notice page.** Error messages use a new `PAGE_NOTICE` (title + text, 3 s) rather than the answer page, whose minimum is 8 s.
- **D6, endpoint noise floor.** End-of-speech detection uses the clap detector's noise floor at the moment listening starts.
- **D7, face-asset tests.** `test_face_assets.py` needs Pillow and numpy. The system Python lacks them, so the gate skips that test. Task 6 runs it with `/private/tmp/claude-501/-Users-jianpedi-Projects/e49efb90-9a94-4db4-b892-c83271a5df4b/scratchpad/imgenv/bin/python`.
- **D8, minimum speech level.** `VOICE_SPEECH_MIN_RMS = 300` (about −41 dBFS). A very quiet room cannot drop the speech threshold to nothing.

## Review Focus

1. **Room or WiFi noise lifts the noise floor.** Claps are missed, or noise triggers listening. Expected: `/mic` shows the floor, and the Kconfig thresholds and the gain potentiometer fix it. Task 8 steps 2–3 check it on the board.
2. **A single loud event with an echo** (door slam, dog bark). It may look like a double clap. Expected: at worst, listening opens and cancels after 4 s of silence without uploading anything. The `endpoint_no_speech_gives_up` test covers the cancel; Task 8 step 5 checks the whole path.
3. **The VPS or network is down while sending.** Expected: the "Sin conexión con el servicio de voz" notice within about 15 s, then the face returns, and the next double clap works again. Task 8 step 6.
4. **Claps during a long LLM turn.** Expected: they are ignored (`agent_is_busy()`), and no second turn is queued. Task 8 step 4.
5. **Audio frames stop mid-listening** (ADC driver error). Expected: the 12 s watchdog (D4) returns to idle instead of leaving the listening face on forever. This is a code-review item in `voice.c`.

---

### Task 1: NVIDIA ASR probe on the VPS

**Files:**
- Create: `server/stt/asr.py`, `server/stt/test_asr.py`, `server/stt/probe_asr.py`, `server/stt/requirements.txt`

**Interfaces:**
- Produces:
  - `asr.RivaASR(api_key, function_id, language="es-US", server="grpc.nvcf.nvidia.com:443", mode="offline")`, a callable `bytes (PCM16LE mono 16 kHz) -> str`;
  - `asr.PARAKEET_MULTILINGUAL`.
- On the VPS: `~/fidel-stt/` with a venv and a `.env` holding `NVIDIA_API_KEY=` (mode 600).
- In the ledger: the confirmed `RIVA_FUNCTION_ID`, `RIVA_LANGUAGE` and `RIVA_MODE`.

- [ ] **Step 1: Write the failing test**

`server/stt/test_asr.py`:

```python
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
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd ~/Projects/zclaw/server/stt && python3 -m unittest test_asr.py`
Expected: `ModuleNotFoundError: No module named 'asr'`.

- [ ] **Step 3: Implement**

`server/stt/asr.py`:

```python
"""NVIDIA Riva speech recognition hosted on NVCF, for the STT service."""

from __future__ import annotations

# parakeet-1.1b-rnnt-multilingual-asr on build.nvidia.com (confirmed by probe_asr.py)
PARAKEET_MULTILINGUAL = "71203149-d3b7-4460-8231-1be2543a1fca"
SAMPLE_RATE = 16000
CHUNK_BYTES = 3200  # 100 ms of PCM16 for streaming mode


def _join(results) -> str:
    return " ".join(r.alternatives[0].transcript.strip() for r in results if r.alternatives)


class RivaASR:
    """Callable: PCM16LE mono 16 kHz bytes -> transcript."""

    def __init__(self, api_key: str, function_id: str, language: str = "es-US",
                 server: str = "grpc.nvcf.nvidia.com:443", mode: str = "offline"):
        if mode not in ("offline", "streaming"):
            raise ValueError(f"unknown RIVA_MODE {mode!r}")
        import riva.client  # only the deployed service needs nvidia-riva-client

        self._riva = riva.client
        auth = riva.client.Auth(
            uri=server,
            use_ssl=True,
            metadata_args=[["function-id", function_id], ["authorization", f"Bearer {api_key}"]],
        )
        self._asr = riva.client.ASRService(auth)
        self._config = riva.client.RecognitionConfig(
            encoding=riva.client.AudioEncoding.LINEAR_PCM,
            sample_rate_hertz=SAMPLE_RATE,
            audio_channel_count=1,
            language_code=language,
            max_alternatives=1,
            enable_automatic_punctuation=True,
        )
        self._mode = mode

    def __call__(self, pcm: bytes) -> str:
        if self._mode == "streaming":
            return self._streaming(pcm)
        return _join(self._asr.offline_recognize(pcm, self._config).results)

    def _streaming(self, pcm: bytes) -> str:
        cfg = self._riva.StreamingRecognitionConfig(config=self._config, interim_results=False)
        chunks = [pcm[i:i + CHUNK_BYTES] for i in range(0, len(pcm), CHUNK_BYTES)]
        parts = []
        for response in self._asr.streaming_response_generator(audio_chunks=chunks, streaming_config=cfg):
            text = _join(r for r in response.results if getattr(r, "is_final", True))
            if text:
                parts.append(text)
        return " ".join(parts)
```

- [ ] **Step 4: Run it to see it pass**

Run: `cd ~/Projects/zclaw/server/stt && python3 -m unittest test_asr.py`
Expected: `Ran 3 tests … OK`.

- [ ] **Step 5: Probe tool and requirements**

`server/stt/requirements.txt`:

```
nvidia-riva-client>=2.17
```

`server/stt/probe_asr.py`:

```python
#!/usr/bin/env python3
"""Probe NVIDIA's hosted ASR with a WAV file (16 kHz, mono, PCM16).

    python probe_asr.py question.wav [offline|streaming|both]

Reads NVIDIA_API_KEY, RIVA_FUNCTION_ID, RIVA_LANGUAGE from the environment.
"""

from __future__ import annotations

import os
import sys
import time
import wave

import asr


def main() -> int:
    path = sys.argv[1]
    which = sys.argv[2] if len(sys.argv) > 2 else "both"
    modes = ["offline", "streaming"] if which == "both" else [which]
    with wave.open(path, "rb") as w:
        if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (16000, 1, 2):
            print("need 16 kHz mono PCM16")
            return 2
        pcm = w.readframes(w.getnframes())
    ok = False
    for mode in modes:
        rec = asr.RivaASR(
            api_key=os.environ["NVIDIA_API_KEY"],
            function_id=os.environ.get("RIVA_FUNCTION_ID", asr.PARAKEET_MULTILINGUAL),
            language=os.environ.get("RIVA_LANGUAGE", "es-US"),
            mode=mode,
        )
        start = time.monotonic()
        try:
            text = rec(pcm)
            print(f"{mode}: {text!r} ({time.monotonic() - start:.2f} s)")
            ok = ok or bool(text)
        except Exception as exc:  # noqa: BLE001 (probe: report every failure)
            print(f"{mode}: FAILED {type(exc).__name__}: {str(exc)[:200]}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 6: Prepare the VPS (key never printed)**

```bash
HOST=jianpedi@107.173.67.243
ssh "$HOST" 'mkdir -p ~/fidel-stt && cd ~/fidel-stt && { [ -x venv/bin/python ] || python3 -m venv venv; }'
scp -q ~/Projects/zclaw/server/stt/{asr.py,probe_asr.py,requirements.txt} "$HOST":fidel-stt/
ssh "$HOST" 'cd ~/fidel-stt && venv/bin/pip install -q -r requirements.txt && venv/bin/python -c "import riva.client; print(\"riva ok\")"'
ssh "$HOST" 'test -r /etc/caddy/Caddyfile && grep -c "nvapi-" /etc/caddy/Caddyfile || echo unreadable'
```

Expected: `riva ok`, then a count of `1` or more, or `unreadable`.

- **If readable:** copy the key into `.env` on the VPS itself. It never crosses to the Mac:
  ```bash
  ssh "$HOST" 'umask 077; touch ~/fidel-stt/.env; grep -q "^NVIDIA_API_KEY=" ~/fidel-stt/.env || { k=$(grep -o "nvapi-[A-Za-z0-9_-]*" /etc/caddy/Caddyfile | head -1); [ -n "$k" ] && echo "NVIDIA_API_KEY=$k" >> ~/fidel-stt/.env; }; grep -c "^NVIDIA_API_KEY=nvapi-" ~/fidel-stt/.env'
  ```
  Expected: `1`. Tell the user the key was copied from the Caddyfile into `~/fidel-stt/.env` (mode 600).
- **If unreadable:** ask the user to run on the VPS: `umask 077; echo "NVIDIA_API_KEY=<their key>" >> ~/fidel-stt/.env`.

- [ ] **Step 7: Probe with a real Spanish sentence**

```bash
SCRATCH=/private/tmp/claude-501/-Users-jianpedi-Projects/e49efb90-9a94-4db4-b892-c83271a5df4b/scratchpad
say -v "Eddy (Español (México))" "Hola Fidel, ¿qué hora es en La Habana?" -o "$SCRATCH/q.aiff"
afconvert -f WAVE -d LEI16@16000 -c 1 "$SCRATCH/q.aiff" "$SCRATCH/q.wav"
scp -q "$SCRATCH/q.wav" "$HOST":fidel-stt/q.wav
ssh "$HOST" 'cd ~/fidel-stt && set -a && . ./.env && set +a && venv/bin/python probe_asr.py q.wav both'
```

Expected: at least one mode prints a transcript close to "Hola Fidel, ¿qué hora es en La Habana?". Record in the ledger as a ruling: the working mode (prefer `offline` when both work), its latency, `RIVA_LANGUAGE=es-US` and the function-id.

If both modes fail, retry once per variation and record what worked:
- **Language rejected:** retry with `RIVA_LANGUAGE=es-ES`, then `es`.
- **Function not found or unauthorised:** search build.nvidia.com for the current Spanish ASR function-id (`canary-1b-asr`, then the Parakeet listing). Retry with `RIVA_FUNCTION_ID=<id>` exported before the command.
- **Nothing transcribes:** stop and ask the user. The plan cannot continue without a working model.

- [ ] **Step 8: Commit**

```bash
cd ~/Projects/zclaw && git add server/stt/asr.py server/stt/test_asr.py server/stt/probe_asr.py server/stt/requirements.txt
git commit -m "feat(stt): Riva ASR wrapper and probe for NVIDIA's hosted models

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: STT service on the VPS

**Files:**
- Create: `server/stt/adpcm.py`, `server/stt/test_adpcm.py`, `server/stt/server.py`, `server/stt/test_server.py`, `server/stt/fidel-stt.service`, `server/stt/README.md`, `scripts/deploy-stt.sh`

**Interfaces:**
- Consumes: `asr.RivaASR` and `asr.PARAKEET_MULTILINGUAL` (Task 1).
- Produces:
  - `POST https://bolita-cubana.duckdns.org/stt/transcribe`: headers `X-Stt-Token` and `X-Audio-Format: ima-adpcm; rate=16000`; returns `200 {"text","ms"}`, 400, 401, 413, 502 or 503;
  - `GET …/stt/health` → `ok`;
  - `STT_TOKEN` in `~/fidel-stt/.env`, which Task 7 copies into the board defaults.
- The ADPCM stream format (header + nibbles) and the golden vector are shared with Task 3.

- [ ] **Step 1: Write the failing tests**

`server/stt/test_adpcm.py`:

```python
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
```

`server/stt/test_server.py`:

```python
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
        self.assertEqual(len(asr.calls[0]), 2000 * 2)  # PCM16 of 2000 samples

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
```

- [ ] **Step 2: Run them to see them fail**

Run: `cd ~/Projects/zclaw/server/stt && python3 -m unittest test_adpcm.py test_server.py`
Expected: `ModuleNotFoundError: No module named 'adpcm'`.

- [ ] **Step 3: Implement**

`server/stt/adpcm.py`:

```python
"""IMA-ADPCM decoder for the stream Fidel's board sends.

Stream: 4-byte header (predictor int16 LE, step index 0..88, reserved 0), then
4-bit codes, first sample in the low nibble.
"""

from __future__ import annotations

import struct

HEADER_BYTES = 4

STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


class AdpcmError(ValueError):
    pass


def decode(stream: bytes) -> list[int]:
    """Returns the int16 samples of a header + codes stream."""
    if len(stream) < HEADER_BYTES:
        raise AdpcmError("stream shorter than its header")
    predictor, index, reserved = struct.unpack_from("<hBB", stream, 0)
    if index > 88 or reserved != 0:
        raise AdpcmError("bad header")
    out: list[int] = []
    for byte in stream[HEADER_BYTES:]:
        for code in (byte & 0x0F, byte >> 4):
            step = STEP_TABLE[index]
            vpdiff = step >> 3
            if code & 4:
                vpdiff += step
            if code & 2:
                vpdiff += step >> 1
            if code & 1:
                vpdiff += step >> 2
            predictor = predictor - vpdiff if code & 8 else predictor + vpdiff
            predictor = max(-32768, min(32767, predictor))
            index = max(0, min(88, index + INDEX_TABLE[code]))
            out.append(predictor)
    return out


def encode(samples: list[int], predictor: int = 0, index: int = 0) -> bytes:
    """Header + codes for int16 samples: the board's encoder, for tests and smoke checks."""
    out = bytearray(struct.pack("<hBB", predictor, index, 0))
    codes: list[int] = []
    for sample in samples:
        diff = sample - predictor
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        step = STEP_TABLE[index]
        vpdiff = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            code |= 2
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            code |= 1
            vpdiff += step
        predictor = predictor - vpdiff if code & 8 else predictor + vpdiff
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        codes.append(code)
    if len(codes) % 2:
        codes.append(0)
    for i in range(0, len(codes), 2):
        out.append(codes[i] | (codes[i + 1] << 4))
    return bytes(out)


def to_pcm16le(samples: list[int]) -> bytes:
    return struct.pack(f"<{len(samples)}h", *samples)
```

`server/stt/server.py`:

```python
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
            future = self._pool.submit(self._recognize, pcm)
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
```

- [ ] **Step 4: Run them to see them pass**

Run: `cd ~/Projects/zclaw/server/stt && python3 -m unittest test_adpcm.py test_server.py test_asr.py`
Expected: `Ran 18 tests … OK`.

- [ ] **Step 5: Unit, README and deploy script**

`server/stt/fidel-stt.service`:

```
[Unit]
Description=Fidel speech to text (IMA-ADPCM in, NVIDIA Riva transcript out)
After=network-online.target

[Service]
WorkingDirectory=%h/fidel-stt
ExecStart=%h/fidel-stt/venv/bin/python %h/fidel-stt/server.py
Environment=STT_PORT=9003
# STT_TOKEN (created by scripts/deploy-stt.sh), NVIDIA_API_KEY, RIVA_* (mode 600, never committed)
EnvironmentFile=%h/fidel-stt/.env
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
```

`server/stt/README.md`:

```markdown
# fidel-stt

Speech to text for Fidel's board. The board sends IMA-ADPCM (16 kHz, 4-byte
header + nibbles); the service decodes it and asks NVIDIA Riva (hosted on
NVCF, Parakeet 1.1b RNNT multilingual by default) for the transcript.

    POST /transcribe   X-Stt-Token, X-Audio-Format: ima-adpcm; rate=16000
                       -> 200 {"text": "...", "ms": 840} | 400 | 401 | 413 | 502 | 503
    GET  /health       -> ok

`~/fidel-stt/.env` (mode 600): `STT_TOKEN`, `NVIDIA_API_KEY`, optional
`RIVA_FUNCTION_ID`, `RIVA_LANGUAGE` (es-US), `RIVA_SERVER`, `RIVA_MODE`
(offline | streaming). The last recording is kept as `~/fidel-stt/last.wav`
(overwritten each time) to tune the microphone gain.

Deploy (systemd --user, no sudo): `scripts/deploy-stt.sh`.
Caddy (inside the `bolita-cubana.duckdns.org` site block):

    handle_path /stt/* {
        reverse_proxy 127.0.0.1:9003
    }

Tests: `python3 -m unittest test_adpcm.py test_server.py test_asr.py`.
Probe NVIDIA: `python probe_asr.py file.wav both` (with the .env loaded).
```

`scripts/deploy-stt.sh`:

```bash
#!/bin/bash
# Deploy the speech-to-text service to the VPS as a systemd --user unit (no sudo).
# Caddy must route /stt/* to 127.0.0.1:9003 (see server/stt/README.md).
set -euo pipefail

HOST="${STT_HOST:-jianpedi@107.173.67.243}"
SRC="$(cd "$(dirname "$0")/../server/stt" && pwd)"

ssh "$HOST" 'mkdir -p ~/fidel-stt ~/.config/systemd/user'
scp -q "$SRC"/adpcm.py "$SRC"/asr.py "$SRC"/server.py "$SRC"/probe_asr.py "$SRC"/requirements.txt "$HOST":fidel-stt/
scp -q "$SRC"/fidel-stt.service "$HOST":.config/systemd/user/fidel-stt.service
# Shared token for the public /stt route: created once on the VPS, never overwritten.
ssh "$HOST" 'umask 077; touch ~/fidel-stt/.env; grep -q "^STT_TOKEN=" ~/fidel-stt/.env || echo "STT_TOKEN=$(head -c 24 /dev/urandom | od -An -tx1 | tr -d " \\n")" >> ~/fidel-stt/.env'
ssh "$HOST" 'grep -q "^NVIDIA_API_KEY=" ~/fidel-stt/.env || { echo "NVIDIA_API_KEY missing in ~/fidel-stt/.env" >&2; exit 1; }'
ssh "$HOST" 'cd ~/fidel-stt \
  && { [ -x venv/bin/python ] || python3 -m venv venv; } \
  && venv/bin/pip install -q -r requirements.txt \
  && systemctl --user daemon-reload \
  && systemctl --user enable --now fidel-stt \
  && systemctl --user restart fidel-stt \
  && sleep 2 && curl -fsS http://127.0.0.1:9003/health && echo " <- fidel-stt up"'
```

- [ ] **Step 6: Deploy**

If Task 1 chose a non-default setting (mode, language or function-id), add it to `~/fidel-stt/.env` first, e.g. `ssh "$HOST" 'echo RIVA_MODE=streaming >> ~/fidel-stt/.env'`.

Run: `chmod +x ~/Projects/zclaw/scripts/deploy-stt.sh && ~/Projects/zclaw/scripts/deploy-stt.sh`
Expected: `ok <- fidel-stt up`.

- [ ] **Step 7: Caddy (the user applies it)**

Ask the user to add this block inside the `bolita-cubana.duckdns.org` site of `/etc/caddy/Caddyfile`, next to `handle_path /dash/*`, then run `sudo systemctl reload caddy`:

```
handle_path /stt/* {
    reverse_proxy 127.0.0.1:9003
}
```

Then run: `curl -s -m 10 https://bolita-cubana.duckdns.org/stt/health`
Expected: `ok`.

- [ ] **Step 8: HTTPS smoke test with the real model (token never printed)**

```bash
SCRATCH=/private/tmp/claude-501/-Users-jianpedi-Projects/e49efb90-9a94-4db4-b892-c83271a5df4b/scratchpad
cd ~/Projects/zclaw/server/stt && python3 - "$SCRATCH/q.wav" "$SCRATCH/q.adpcm" <<'EOF'
import struct, sys, wave
import adpcm
with wave.open(sys.argv[1], "rb") as w:
    raw = w.readframes(w.getnframes())
samples = list(struct.unpack(f"<{len(raw) // 2}h", raw))
open(sys.argv[2], "wb").write(adpcm.encode(samples))
EOF
TOKEN=$(ssh "$HOST" 'sed -n "s/^STT_TOKEN=//p" ~/fidel-stt/.env')
curl -s -m 30 -H "X-Stt-Token: $TOKEN" -H "X-Audio-Format: ima-adpcm; rate=16000" \
     --data-binary @"$SCRATCH/q.adpcm" https://bolita-cubana.duckdns.org/stt/transcribe; echo
curl -s -o /dev/null -w '%{http_code}\n' -H "X-Stt-Token: wrong" -H "X-Audio-Format: ima-adpcm; rate=16000" \
     --data-binary @"$SCRATCH/q.adpcm" https://bolita-cubana.duckdns.org/stt/transcribe
unset TOKEN
```

Expected:
- `{"text": "Hola Fidel, ¿qué hora es en La Habana?" (or very close), "ms": …}` — the ADPCM round trip did not hurt recognition;
- `401` for the wrong token.

- [ ] **Step 9: Commit**

```bash
cd ~/Projects/zclaw && git add server/stt/adpcm.py server/stt/test_adpcm.py server/stt/server.py server/stt/test_server.py server/stt/fidel-stt.service server/stt/README.md scripts/deploy-stt.sh
git commit -m "feat(stt): VPS service that turns the board's ADPCM into text with NVIDIA Riva

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Audio basics: ADPCM, ADC conversion, levels

**Files:**
- Create: `main/adpcm.h`, `main/adpcm.c`, `main/audio_dsp.h`, `main/audio_dsp.c`, `main/audio_level.h`, `main/audio_level.c`, `test/host/test_voice_audio.c`
- Modify:
  - `test/host/test_runner.c`: extern + call;
  - `scripts/test.sh`: add the test file after `test_sd_access.c`, the three sources after `../../main/sd_access.c`, and `-lm` after `$CJSON_LDFLAGS` in the `build/test_runner` gcc command;
  - `main/CMakeLists.txt`: add `"adpcm.c" "audio_dsp.c" "audio_level.c"` after `"sd_access.c"`.

**Interfaces:**
- Produces (used by Tasks 4 and 7):
  - `adpcm_state_t`, `adpcm_init`, `adpcm_header`, `adpcm_encode`, `ADPCM_HEADER_BYTES`;
  - `audio_dsp_t`, `audio_dsp_init`, `audio_dsp_convert(d, const uint16_t *raw, size_t n_raw, int16_t *out) -> size_t`;
  - `audio_frame_stats_t {peak, rms, clipped}`, `audio_level_frame`, `audio_level_dbfs_tenths(uint32_t) -> int`, `audio_level_bar(int dbfs_tenths, int width) -> int`, `AUDIO_DBFS_FLOOR`.

- [ ] **Step 1: Headers and failing tests**

`main/adpcm.h`:

```c
#ifndef ADPCM_H
#define ADPCM_H

#include <stddef.h>
#include <stdint.h>

// IMA-ADPCM, 4 bits per sample. Stream for the STT service: a 4-byte header
// (predictor int16 LE, step index, 0) followed by the codes, first sample in
// the low nibble.
#define ADPCM_HEADER_BYTES 4

typedef struct {
    int16_t predictor;
    uint8_t index;  // 0..88
} adpcm_state_t;

void adpcm_init(adpcm_state_t *s);
void adpcm_header(const adpcm_state_t *s, uint8_t out[ADPCM_HEADER_BYTES]);
uint8_t adpcm_encode_sample(adpcm_state_t *s, int16_t sample);
// Encodes n samples (n even) into n/2 bytes; returns the bytes written.
size_t adpcm_encode(adpcm_state_t *s, const int16_t *pcm, size_t n, uint8_t *out);

#endif // ADPCM_H
```

`main/audio_dsp.h`:

```c
#ifndef AUDIO_DSP_H
#define AUDIO_DSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_RAW_MID 2048  // 12-bit ADC mid-scale (MAX4466 output idles at VCC/2)

// 32 kHz 12-bit ADC values -> 16 kHz int16: averages each pair (a crude
// low-pass), centres, scales x16 and removes DC with a one-pole high-pass.
typedef struct {
    int32_t prev_in;
    int32_t prev_out;
    bool primed;
} audio_dsp_t;

void audio_dsp_init(audio_dsp_t *d);
// n_raw even; writes n_raw/2 samples to out and returns that count.
size_t audio_dsp_convert(audio_dsp_t *d, const uint16_t *raw, size_t n_raw, int16_t *out);

#endif // AUDIO_DSP_H
```

`main/audio_level.h`:

```c
#ifndef AUDIO_LEVEL_H
#define AUDIO_LEVEL_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_CLIP_LEVEL  32000  // |sample| at or above this counts as clipped
#define AUDIO_DBFS_FLOOR  (-900) // tenths of dB reported for silence

typedef struct {
    uint16_t peak;     // max |sample|
    uint16_t rms;
    uint16_t clipped;  // samples with |x| >= AUDIO_CLIP_LEVEL
} audio_frame_stats_t;

void audio_level_frame(const int16_t *x, size_t n, audio_frame_stats_t *out);

// 20*log10(value/32768) in tenths of dB, floored at AUDIO_DBFS_FLOOR
// (integers only: newlib nano printf cannot print floats).
int audio_level_dbfs_tenths(uint32_t value);

// Bar width for a level: -60 dBFS (or less) -> 0, 0 dBFS -> width.
int audio_level_bar(int dbfs_tenths, int width);

#endif // AUDIO_LEVEL_H
```

`test/host/test_voice_audio.c`:

```c
/*
 * Host tests for voice audio: IMA-ADPCM, ADC conversion, levels.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "adpcm.h"
#include "audio_dsp.h"
#include "audio_level.h"

#define TEST_PI 3.14159265358979

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

/* ---------- adpcm ---------- */
static const int16_t GOLDEN_PCM[64] = {
    -1000, 3508, 7653, 8908, 11413, 12800, 10813, 9908, 8053, 3308, 400, -4708, -7253, -9108,
    -12213, -12000, -10613, -10308, -6853, -2708, -400, 4108, 6053, 9508, 12013, 11200, 11413,
    10508, 6453, 3908, 1000, -4108, -6653, -10708, -11613, -11400, -12213, -9708, -6253, -4308,
    200, 4708, 6653, 10108, 10413, 11800, 12013, 8908, 7053, 4508, -600, -3508, -6053, -10108,
    -11013, -13000, -11613, -9108, -7853, -3708, 800, 3108, 7253, 10708,
};
static const uint8_t GOLDEN_ADPCM[32] = {
    0x7f, 0x77, 0x77, 0x77, 0xe3, 0xba, 0xaa, 0x8a, 0x82, 0x36, 0x42, 0x31, 0x93, 0x90, 0xae, 0xda,
    0xaa, 0x09, 0x28, 0x24, 0x35, 0x31, 0x10, 0xc0, 0xaa, 0xaf, 0xb9, 0xa8, 0x31, 0x61, 0x23, 0x33,
};

TEST(adpcm_matches_golden_vector)
{
    adpcm_state_t s;
    uint8_t out[32];
    uint8_t hdr[ADPCM_HEADER_BYTES];
    adpcm_init(&s);
    adpcm_header(&s, hdr);
    ASSERT(hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 0 && hdr[3] == 0);
    ASSERT(adpcm_encode(&s, GOLDEN_PCM, 64, out) == 32);
    ASSERT(memcmp(out, GOLDEN_ADPCM, sizeof(out)) == 0);
    return 0;
}

TEST(adpcm_header_layout)
{
    adpcm_state_t s = { .predictor = -2, .index = 5 };
    uint8_t hdr[ADPCM_HEADER_BYTES];
    adpcm_header(&s, hdr);
    ASSERT(hdr[0] == 0xFE && hdr[1] == 0xFF && hdr[2] == 5 && hdr[3] == 0);
    return 0;
}

/* Test-only decoder (the board never decodes). */
static const int16_t STEPS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};
static const int8_t IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static void decode(const uint8_t *in, size_t bytes, int16_t *out)
{
    int32_t pred = 0;
    int idx = 0;
    for (size_t i = 0; i < bytes * 2; i++) {
        uint8_t code = (i & 1) ? (in[i / 2] >> 4) : (in[i / 2] & 0x0F);
        int32_t step = STEPS[idx];
        int32_t vp = step >> 3;
        if (code & 4) vp += step;
        if (code & 2) vp += step >> 1;
        if (code & 1) vp += step >> 2;
        pred = (code & 8) ? pred - vp : pred + vp;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += IDX[code];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        out[i] = (int16_t)pred;
    }
}

TEST(adpcm_round_trip_keeps_speech_like_signal)
{
    static int16_t pcm[16000];
    static int16_t back[16000];
    static uint8_t enc[8000];
    adpcm_state_t s;
    double num = 0, den = 0;
    for (int i = 0; i < 16000; i++) {
        double t = (double)i / 16000.0;
        pcm[i] = (int16_t)(4800 * sin(2 * TEST_PI * 220 * t) + 2000 * sin(2 * TEST_PI * 730 * t) +
                           2000 * sin(2 * TEST_PI * 2100 * t));
    }
    adpcm_init(&s);
    ASSERT(adpcm_encode(&s, pcm, 16000, enc) == 8000);
    decode(enc, 8000, back);
    for (int i = 0; i < 16000; i++) {
        num += (double)pcm[i] * pcm[i];
        den += (double)(pcm[i] - back[i]) * (pcm[i] - back[i]);
    }
    ASSERT(10 * log10(num / den) > 20.0);
    return 0;
}

/* ---------- audio_dsp ---------- */
TEST(dsp_constant_input_is_silence)
{
    static uint16_t raw[400];
    static int16_t out[200];
    audio_dsp_t d;
    for (int i = 0; i < 400; i++) raw[i] = 2548;
    audio_dsp_init(&d);
    ASSERT(audio_dsp_convert(&d, raw, 400, out) == 200);
    for (int i = 0; i < 200; i++) ASSERT(out[i] == 0);
    return 0;
}

TEST(dsp_removes_a_dc_step)
{
    static uint16_t raw[4002];
    static int16_t out[2001];
    audio_dsp_t d;
    raw[0] = raw[1] = 2048;
    for (int i = 2; i < 4002; i++) raw[i] = 2548;
    audio_dsp_init(&d);
    audio_dsp_convert(&d, raw, 4002, out);
    ASSERT(out[0] == 0);
    ASSERT(out[1] == 8000);          /* the step passes once ... */
    ASSERT(out[2000] < 50 && out[2000] > -50);  /* ... and decays */
    return 0;
}

static int16_t peak_after(const int16_t *x, size_t from, size_t n)
{
    int16_t p = 0;
    for (size_t i = from; i < n; i++) {
        int16_t a = (int16_t)(x[i] < 0 ? -x[i] : x[i]);
        if (a > p) p = a;
    }
    return p;
}

TEST(dsp_keeps_speech_band_and_cuts_high_tones)
{
    static uint16_t raw[6400];
    static int16_t out[3200];
    audio_dsp_t d;
    for (int i = 0; i < 6400; i++) raw[i] = (uint16_t)(2048 + 1000 * sin(2 * TEST_PI * 1000 * i / 32000.0));
    audio_dsp_init(&d);
    audio_dsp_convert(&d, raw, 6400, out);
    int16_t p = peak_after(out, 1600, 3200);
    ASSERT(p > 15000 && p < 16600);  /* 1 kHz: ~16000 */
    for (int i = 0; i < 6400; i++) raw[i] = (uint16_t)(2048 + 1000 * sin(2 * TEST_PI * 15000 * i / 32000.0));
    audio_dsp_init(&d);
    audio_dsp_convert(&d, raw, 6400, out);
    ASSERT(peak_after(out, 1600, 3200) < 3000);  /* 15 kHz: mostly gone */
    return 0;
}

/* ---------- audio_level ---------- */
TEST(level_of_silence_and_full_scale)
{
    static int16_t x[160];
    audio_frame_stats_t s;
    memset(x, 0, sizeof(x));
    audio_level_frame(x, 160, &s);
    ASSERT(s.peak == 0 && s.rms == 0 && s.clipped == 0);
    ASSERT(audio_level_dbfs_tenths(s.rms) == AUDIO_DBFS_FLOOR);
    for (int i = 0; i < 160; i++) x[i] = (int16_t)((i & 1) ? 32767 : -32767);
    audio_level_frame(x, 160, &s);
    ASSERT(s.peak == 32767 && s.rms == 32767 && s.clipped == 160);
    ASSERT(audio_level_dbfs_tenths(s.rms) == 0);
    return 0;
}

TEST(level_of_a_sine)
{
    static int16_t x[160];
    audio_frame_stats_t s;
    for (int i = 0; i < 160; i++) x[i] = (int16_t)(16384 * sin(2 * TEST_PI * i / 16.0));
    audio_level_frame(x, 160, &s);
    ASSERT(s.peak >= 16383 && s.peak <= 16384);
    ASSERT(s.rms >= 11580 && s.rms <= 11590);       /* 16384 / sqrt(2) */
    ASSERT(audio_level_dbfs_tenths(s.rms) == -90);  /* -9.0 dBFS */
    ASSERT(s.clipped == 0);
    return 0;
}

TEST(level_bar_width)
{
    ASSERT(audio_level_bar(AUDIO_DBFS_FLOOR, 200) == 0);
    ASSERT(audio_level_bar(-600, 200) == 0);
    ASSERT(audio_level_bar(-300, 200) == 100);
    ASSERT(audio_level_bar(0, 200) == 200);
    ASSERT(audio_level_bar(15, 200) == 200);
    return 0;
}

int test_voice_audio_all(void)
{
    int failures = 0;

    printf("\nVoice Audio Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(adpcm_matches_golden_vector);
    RUN(adpcm_header_layout);
    RUN(adpcm_round_trip_keeps_speech_like_signal);
    RUN(dsp_constant_input_is_silence);
    RUN(dsp_removes_a_dc_step);
    RUN(dsp_keeps_speech_band_and_cuts_high_tones);
    RUN(level_of_silence_and_full_scale);
    RUN(level_of_a_sine);
    RUN(level_bar_width);

#undef RUN
    return failures;
}
```

Registration:
- `test_runner.c`: add `extern int test_voice_audio_all(void);` after `extern int test_sd_access_all(void);`, and `    failures += test_voice_audio_all();` after `    failures += test_sd_access_all();`.
- `scripts/test.sh`: add `        test_voice_audio.c \` after `        test_sd_access.c \`.
- `scripts/test.sh`: add `        ../../main/adpcm.c \`, `        ../../main/audio_dsp.c \` and `        ../../main/audio_level.c \` after `        ../../main/sd_access.c \`.
- `scripts/test.sh`: change the `build/test_runner` command's `$CJSON_LDFLAGS 2>&1 || {` to `$CJSON_LDFLAGS -lm 2>&1 || {`.
- Create `main/adpcm.c`, `main/audio_dsp.c` and `main/audio_level.c`, each holding only its `#include "<name>.h"` line.

- [ ] **Step 2: Run the gate to see it fail**

Run: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`
Expected: `host gate FAILED`, with undefined symbols `adpcm_*`, `audio_dsp_*` and `audio_level_*`.

- [ ] **Step 3: Implement**

`main/adpcm.c`:

```c
#include "adpcm.h"

static const int16_t STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};
static const int8_t INDEX_TABLE[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

void adpcm_init(adpcm_state_t *s)
{
    s->predictor = 0;
    s->index = 0;
}

void adpcm_header(const adpcm_state_t *s, uint8_t out[ADPCM_HEADER_BYTES])
{
    uint16_t p = (uint16_t)s->predictor;
    out[0] = (uint8_t)(p & 0xFF);
    out[1] = (uint8_t)(p >> 8);
    out[2] = s->index;
    out[3] = 0;
}

uint8_t adpcm_encode_sample(adpcm_state_t *s, int16_t sample)
{
    int32_t diff = (int32_t)sample - s->predictor;
    int32_t step = STEP_TABLE[s->index];
    int32_t vpdiff = step >> 3;
    uint8_t code = 0;

    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 2;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 1;
        vpdiff += step;
    }
    int32_t pred = (code & 8) ? s->predictor - vpdiff : s->predictor + vpdiff;
    if (pred > 32767) {
        pred = 32767;
    } else if (pred < -32768) {
        pred = -32768;
    }
    s->predictor = (int16_t)pred;
    int idx = (int)s->index + INDEX_TABLE[code];
    s->index = (uint8_t)(idx < 0 ? 0 : idx > 88 ? 88 : idx);
    return code;
}

size_t adpcm_encode(adpcm_state_t *s, const int16_t *pcm, size_t n, uint8_t *out)
{
    size_t w = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint8_t lo = adpcm_encode_sample(s, pcm[i]);
        uint8_t hi = adpcm_encode_sample(s, pcm[i + 1]);
        out[w++] = (uint8_t)(lo | (hi << 4));
    }
    return w;
}
```

`main/audio_dsp.c`:

```c
#include "audio_dsp.h"

void audio_dsp_init(audio_dsp_t *d)
{
    d->prev_in = 0;
    d->prev_out = 0;
    d->primed = false;
}

size_t audio_dsp_convert(audio_dsp_t *d, const uint16_t *raw, size_t n_raw, int16_t *out)
{
    size_t w = 0;

    for (size_t i = 0; i + 1 < n_raw; i += 2) {
        int32_t avg = ((int32_t)raw[i] + (int32_t)raw[i + 1]) / 2;
        int32_t x = (avg - AUDIO_RAW_MID) * 16;
        if (!d->primed) {
            d->prev_in = x;  // the first sample sets the DC level: no start-up thump
            d->primed = true;
        }
        // y[n] = x[n] - x[n-1] + 0.996 * y[n-1]  (corner ~10 Hz at 16 kHz)
        int32_t y = x - d->prev_in + (d->prev_out * 255) / 256;
        d->prev_in = x;
        if (y > 32767) {
            y = 32767;
        } else if (y < -32768) {
            y = -32768;
        }
        d->prev_out = y;
        out[w++] = (int16_t)y;
    }
    return w;
}
```

`main/audio_level.c`:

```c
#include "audio_level.h"

#include <math.h>

static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0;
    uint64_t bit = (uint64_t)1 << 62;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

void audio_level_frame(const int16_t *x, size_t n, audio_frame_stats_t *out)
{
    uint64_t sum = 0;
    uint32_t peak = 0;
    uint16_t clipped = 0;

    for (size_t i = 0; i < n; i++) {
        int32_t v = x[i];
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        if (a > peak) {
            peak = a;
        }
        if (a >= AUDIO_CLIP_LEVEL) {
            clipped++;
        }
        sum += (uint64_t)((int64_t)v * v);
    }
    out->peak = (uint16_t)(peak > 65535 ? 65535 : peak);
    out->rms = (uint16_t)(n ? isqrt64(sum / n) : 0);
    out->clipped = clipped;
}

int audio_level_dbfs_tenths(uint32_t value)
{
    if (value == 0) {
        return AUDIO_DBFS_FLOOR;
    }
    double db10 = 200.0 * log10((double)value / 32768.0);
    int t = (int)(db10 < 0 ? db10 - 0.5 : db10 + 0.5);
    return t < AUDIO_DBFS_FLOOR ? AUDIO_DBFS_FLOOR : t;
}

int audio_level_bar(int dbfs_tenths, int width)
{
    if (dbfs_tenths <= -600) {
        return 0;
    }
    if (dbfs_tenths >= 0) {
        return width;
    }
    return (int)((long)(dbfs_tenths + 600) * width / 600);
}
```

Add `"adpcm.c"`, `"audio_dsp.c"` and `"audio_level.c"` to `main/CMakeLists.txt` after `"sd_access.c"`.

- [ ] **Step 4: Run the gate to see it pass**

Run: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`
Expected: `host gate OK`, with 9 `OK` lines under `Voice Audio Tests`.

- [ ] **Step 5: Commit**

```bash
git add main/adpcm.h main/adpcm.c main/audio_dsp.h main/audio_dsp.c main/audio_level.h main/audio_level.c main/CMakeLists.txt test/host/test_voice_audio.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(voice): IMA-ADPCM encoder, 32->16 kHz ADC conversion and level metering

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Detection: double clap and end of speech

**Files:**
- Create: `main/clap_detect.h`, `main/clap_detect.c`, `main/endpoint.h`, `main/endpoint.c`, `test/host/test_voice_detect.c`
- Modify:
  - `test/host/test_runner.c`;
  - `scripts/test.sh`: add the test file after `test_voice_audio.c`, and the sources after `../../main/audio_level.c`;
  - `main/CMakeLists.txt`: add `"clap_detect.c" "endpoint.c"` after `"audio_level.c"`.

**Interfaces:**
- Produces (used by Task 7):
  - `clap_config_t {level, ratio}`, `clap_detect_t`, `clap_detect_init`, `clap_detect_frame(d, peak, rms) -> clap_event_t` (`CLAP_NONE`/`CLAP_DOUBLE`), `clap_detect_noise(d) -> uint16_t`;
  - `endpoint_t`, `endpoint_init(e, noise_rms, ratio, min_level)`, `endpoint_frame(e, rms) -> endpoint_result_t` (`ENDPOINT_CONTINUE`/`DONE`/`NO_SPEECH`), `endpoint_heard_speech`, `ENDPOINT_MAX_FRAMES`.

- [ ] **Step 1: Headers and failing tests**

`main/clap_detect.h`:

```c
#ifndef CLAP_DETECT_H
#define CLAP_DETECT_H

#include <stdbool.h>
#include <stdint.h>

// Double-clap detector on 10 ms frames (peak and RMS of int16 audio).
// A clap is a burst that starts with peak >= level and >= ratio x noise floor
// and decays below half its peak within CLAP_MAX_BURST_FRAMES. Two claps whose
// onsets are CLAP_MIN_GAP..CLAP_MAX_GAP frames apart, followed by
// CLAP_QUIET_FRAMES without a third burst, give CLAP_DOUBLE.
#define CLAP_MAX_BURST_FRAMES 8
#define CLAP_MIN_GAP_FRAMES   15
#define CLAP_MAX_GAP_FRAMES   70
#define CLAP_QUIET_FRAMES     30

typedef enum { CLAP_NONE = 0, CLAP_DOUBLE } clap_event_t;

typedef struct {
    uint16_t level;  // absolute peak threshold (int16 units)
    uint16_t ratio;  // peak must also be >= ratio x noise floor
} clap_config_t;

typedef struct {
    clap_config_t cfg;
    int32_t noise_q8;      // noise floor (RMS) in Q8; < 0 until the first quiet frame
    uint32_t frame;        // frames seen
    bool in_burst;
    bool burst_long;       // the current burst lasted too long: not a clap
    uint32_t burst_start;
    uint16_t burst_peak;
    uint8_t claps;         // claps in the current sequence (0..2)
    uint32_t first_onset;
    uint32_t second_end;
} clap_detect_t;

void clap_detect_init(clap_detect_t *d, const clap_config_t *cfg);
clap_event_t clap_detect_frame(clap_detect_t *d, uint16_t peak, uint16_t rms);
// Current noise floor (RMS), 0 before any quiet frame.
uint16_t clap_detect_noise(const clap_detect_t *d);

#endif // CLAP_DETECT_H
```

`main/endpoint.h`:

```c
#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>

// End-of-speech detection on 10 ms frames (RMS of int16 audio).
#define ENDPOINT_START_FRAMES     6     // loud frames in a row that start speech (60 ms)
#define ENDPOINT_END_FRAMES       120   // quiet frames after speech that end it (1.2 s)
#define ENDPOINT_MAX_FRAMES       1000  // hard limit from the start of listening (10 s)
#define ENDPOINT_NO_SPEECH_FRAMES 400   // give up without speech (4 s)

typedef enum { ENDPOINT_CONTINUE = 0, ENDPOINT_DONE, ENDPOINT_NO_SPEECH } endpoint_result_t;

typedef struct {
    uint32_t threshold;
    uint32_t frames;
    uint32_t loud_run;
    uint32_t quiet_run;
    bool speech;
} endpoint_t;

// threshold = max(noise_rms x ratio, min_level)
void endpoint_init(endpoint_t *e, uint32_t noise_rms, uint32_t ratio, uint32_t min_level);
endpoint_result_t endpoint_frame(endpoint_t *e, uint16_t rms);
bool endpoint_heard_speech(const endpoint_t *e);

#endif // ENDPOINT_H
```

`test/host/test_voice_detect.c`:

```c
/*
 * Host tests for voice detection: double clap and end of speech.
 */

#include <stdio.h>

#include "clap_detect.h"
#include "endpoint.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

/* ---------- clap_detect ---------- */
static clap_detect_t s_clap;
static const clap_config_t CLAP_CFG = { .level = 13107, .ratio = 6 };  /* 40 % of full scale */

/* Feeds n frames; returns the index of the first CLAP_DOUBLE, or -1. */
static int feed(int n, uint16_t peak, uint16_t rms)
{
    int ev = -1;
    for (int i = 0; i < n; i++) {
        if (clap_detect_frame(&s_clap, peak, rms) == CLAP_DOUBLE && ev < 0) ev = i;
    }
    return ev;
}
static int quiet(int n) { return feed(n, 300, 100); }
static int clap(void)
{
    int ev = feed(1, 20000, 8000);
    int ev2 = feed(1, 12000, 4000);
    return ev >= 0 ? ev : ev2;
}

TEST(clap_single_is_ignored)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    ASSERT(quiet(100) < 0);
    ASSERT(clap() < 0);
    ASSERT(quiet(200) < 0);
    return 0;
}

TEST(clap_double_fires_after_quiet_window)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    ASSERT(quiet(100) < 0);
    ASSERT(clap() < 0);
    ASSERT(quiet(28) < 0);          /* onsets 30 frames (300 ms) apart */
    ASSERT(clap() < 0);
    ASSERT(quiet(100) == 30);       /* 300 ms of quiet after the second clap */
    ASSERT(quiet(200) < 0);         /* only once */
    return 0;
}

TEST(clap_gap_out_of_range_is_ignored)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    clap();
    quiet(8);                       /* 100 ms: too close */
    clap();
    ASSERT(quiet(200) < 0);
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    clap();
    quiet(88);                      /* 900 ms: too far */
    clap();
    ASSERT(quiet(200) < 0);
    return 0;
}

TEST(clap_three_is_applause)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    clap();
    quiet(28);
    clap();
    quiet(28);
    clap();
    ASSERT(quiet(200) < 0);
    return 0;
}

TEST(clap_sustained_sound_is_not_a_clap)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    ASSERT(feed(50, 20000, 8000) < 0);  /* half a second of loud sound */
    ASSERT(quiet(28) < 0);
    clap();
    ASSERT(quiet(200) < 0);
    /* a proper double clap afterwards still works */
    clap();
    quiet(28);
    clap();
    ASSERT(quiet(100) == 30);
    return 0;
}

TEST(clap_needs_level_and_ratio)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    feed(2, 9000, 3000);            /* below the 40 % level */
    quiet(28);
    feed(2, 9000, 3000);
    ASSERT(quiet(200) < 0);
    clap_detect_init(&s_clap, &CLAP_CFG);
    ASSERT(feed(400, 3000, 2000) < 0);  /* noisy room: floor ~2000 */
    ASSERT(clap_detect_noise(&s_clap) >= 1990 && clap_detect_noise(&s_clap) <= 2000);
    feed(2, 11000, 4000);           /* 11000 < 6 x 2000 */
    feed(28, 3000, 2000);
    feed(2, 11000, 4000);
    ASSERT(feed(200, 3000, 2000) < 0);
    return 0;
}

/* ---------- endpoint ---------- */
static int run_until(endpoint_t *e, int n, uint16_t rms, endpoint_result_t *res)
{
    for (int i = 0; i < n; i++) {
        endpoint_result_t r = endpoint_frame(e, rms);
        if (r != ENDPOINT_CONTINUE) {
            *res = r;
            return i;
        }
    }
    *res = ENDPOINT_CONTINUE;
    return -1;
}

TEST(endpoint_speech_then_silence)
{
    endpoint_t e;
    endpoint_result_t r;
    endpoint_init(&e, 100, 3, 300);
    ASSERT(e.threshold == 300);
    ASSERT(run_until(&e, 50, 100, &r) < 0);
    ASSERT(run_until(&e, 100, 2000, &r) < 0);
    ASSERT(endpoint_heard_speech(&e));
    ASSERT(run_until(&e, 500, 100, &r) == 119);  /* the 120th quiet frame ends it */
    ASSERT(r == ENDPOINT_DONE);
    return 0;
}

TEST(endpoint_no_speech_gives_up)
{
    endpoint_t e;
    endpoint_result_t r;
    endpoint_init(&e, 200, 3, 300);
    ASSERT(e.threshold == 600);
    ASSERT(run_until(&e, 1000, 100, &r) == 399);
    ASSERT(r == ENDPOINT_NO_SPEECH);
    return 0;
}

TEST(endpoint_short_blips_do_not_start_speech)
{
    endpoint_t e;
    endpoint_result_t r = ENDPOINT_CONTINUE;
    int frames = 0;
    endpoint_init(&e, 100, 3, 300);
    while (r == ENDPOINT_CONTINUE && frames < 1000) {
        int n = run_until(&e, 5, 2000, &r);    /* 50 ms loud */
        frames += 5;
        if (n >= 0) break;
        run_until(&e, 10, 100, &r);            /* 100 ms quiet */
        frames += 10;
    }
    ASSERT(r == ENDPOINT_NO_SPEECH);
    ASSERT(!endpoint_heard_speech(&e));
    return 0;
}

TEST(endpoint_stops_at_ten_seconds)
{
    endpoint_t e;
    endpoint_result_t r;
    endpoint_init(&e, 100, 3, 300);
    ASSERT(run_until(&e, 2000, 3000, &r) == 999);
    ASSERT(r == ENDPOINT_DONE);
    return 0;
}

int test_voice_detect_all(void)
{
    int failures = 0;

    printf("\nVoice Detection Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(clap_single_is_ignored);
    RUN(clap_double_fires_after_quiet_window);
    RUN(clap_gap_out_of_range_is_ignored);
    RUN(clap_three_is_applause);
    RUN(clap_sustained_sound_is_not_a_clap);
    RUN(clap_needs_level_and_ratio);
    RUN(endpoint_speech_then_silence);
    RUN(endpoint_no_speech_gives_up);
    RUN(endpoint_short_blips_do_not_start_speech);
    RUN(endpoint_stops_at_ten_seconds);

#undef RUN
    return failures;
}
```

Registration:
- `test_runner.c`: add `extern int test_voice_detect_all(void);` and `    failures += test_voice_detect_all();` after the voice-audio lines.
- `scripts/test.sh`: add `        test_voice_detect.c \` after `        test_voice_audio.c \`.
- `scripts/test.sh`: add `        ../../main/clap_detect.c \` and `        ../../main/endpoint.c \` after `        ../../main/audio_level.c \`.
- Create `main/clap_detect.c` and `main/endpoint.c`, each holding only its include line.

- [ ] **Step 2: Run the gate to see it fail**

Run: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`
Expected: `host gate FAILED`, with undefined symbols `clap_detect_*` and `endpoint_*`.

- [ ] **Step 3: Implement**

`main/clap_detect.c`:

```c
#include "clap_detect.h"

void clap_detect_init(clap_detect_t *d, const clap_config_t *cfg)
{
    d->cfg = *cfg;
    d->noise_q8 = -1;
    d->frame = 0;
    d->in_burst = false;
    d->burst_long = false;
    d->burst_start = 0;
    d->burst_peak = 0;
    d->claps = 0;
    d->first_onset = 0;
    d->second_end = 0;
}

uint16_t clap_detect_noise(const clap_detect_t *d)
{
    return d->noise_q8 < 0 ? 0 : (uint16_t)(d->noise_q8 >> 8);
}

static void update_noise(clap_detect_t *d, uint16_t rms)
{
    int32_t target = (int32_t)rms << 8;
    if (d->noise_q8 < 0) {
        d->noise_q8 = target;
    } else if (target > d->noise_q8) {
        d->noise_q8 += (target - d->noise_q8) / 64;  // rises slowly (speech must not lift it)
    } else {
        d->noise_q8 -= (d->noise_q8 - target) / 8;   // falls quickly
    }
}

static void on_clap(clap_detect_t *d, uint32_t onset, uint32_t end)
{
    if (d->claps == 0) {
        d->claps = 1;
        d->first_onset = onset;
    } else if (d->claps == 1) {
        uint32_t gap = onset - d->first_onset;
        if (gap >= CLAP_MIN_GAP_FRAMES && gap <= CLAP_MAX_GAP_FRAMES) {
            d->claps = 2;
            d->second_end = end;
        } else if (gap > CLAP_MAX_GAP_FRAMES) {
            d->first_onset = onset;  // too late: this one starts a new sequence
        } else {
            d->claps = 0;            // too close to be a double clap
        }
    } else {
        d->claps = 0;                // a third clap: applause, not a command
    }
}

clap_event_t clap_detect_frame(clap_detect_t *d, uint16_t peak, uint16_t rms)
{
    uint32_t t = d->frame++;
    uint32_t noise = clap_detect_noise(d);
    bool loud = peak >= d->cfg.level && (uint32_t)peak >= (uint32_t)d->cfg.ratio * noise;

    if (d->in_burst) {
        if (peak > d->burst_peak) {
            d->burst_peak = peak;
        }
        if ((uint32_t)peak * 2 >= d->burst_peak) {
            if (t - d->burst_start + 1 > CLAP_MAX_BURST_FRAMES) {
                d->burst_long = true;
            }
            return CLAP_NONE;
        }
        d->in_burst = false;
        if (d->burst_long) {
            d->claps = 0;  // sustained sound (speech, music): drop the sequence
        } else {
            on_clap(d, d->burst_start, t);
        }
    }
    if (loud) {
        d->in_burst = true;
        d->burst_long = false;
        d->burst_start = t;
        d->burst_peak = peak;
        return CLAP_NONE;
    }
    update_noise(d, rms);
    if (d->claps == 1 && t - d->first_onset > CLAP_MAX_GAP_FRAMES) {
        d->claps = 0;
    }
    if (d->claps == 2 && t - d->second_end >= CLAP_QUIET_FRAMES) {
        d->claps = 0;
        return CLAP_DOUBLE;
    }
    return CLAP_NONE;
}
```

`main/endpoint.c`:

```c
#include "endpoint.h"

void endpoint_init(endpoint_t *e, uint32_t noise_rms, uint32_t ratio, uint32_t min_level)
{
    uint32_t t = noise_rms * ratio;
    e->threshold = t > min_level ? t : min_level;
    e->frames = 0;
    e->loud_run = 0;
    e->quiet_run = 0;
    e->speech = false;
}

endpoint_result_t endpoint_frame(endpoint_t *e, uint16_t rms)
{
    bool loud = rms >= e->threshold;

    e->frames++;
    if (!e->speech) {
        e->loud_run = loud ? e->loud_run + 1 : 0;
        if (e->loud_run >= ENDPOINT_START_FRAMES) {
            e->speech = true;
            e->quiet_run = 0;
        } else if (e->frames >= ENDPOINT_NO_SPEECH_FRAMES) {
            return ENDPOINT_NO_SPEECH;
        }
    } else {
        e->quiet_run = loud ? 0 : e->quiet_run + 1;
        if (e->quiet_run >= ENDPOINT_END_FRAMES) {
            return ENDPOINT_DONE;
        }
    }
    if (e->frames >= ENDPOINT_MAX_FRAMES) {
        return e->speech ? ENDPOINT_DONE : ENDPOINT_NO_SPEECH;
    }
    return ENDPOINT_CONTINUE;
}

bool endpoint_heard_speech(const endpoint_t *e)
{
    return e->speech;
}
```

Add `"clap_detect.c"` and `"endpoint.c"` to `main/CMakeLists.txt` after `"audio_level.c"`.

- [ ] **Step 4: Run the gate to see it pass**

Run: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`
Expected: `host gate OK`, with 10 `OK` lines under `Voice Detection Tests`.

- [ ] **Step 5: Commit**

```bash
git add main/clap_detect.h main/clap_detect.c main/endpoint.h main/endpoint.c main/CMakeLists.txt test/host/test_voice_detect.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(voice): double-clap detector and end-of-speech detection

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Agent voice source, STT reply parsing and commands

**Files:**
- Create: `main/stt_reply.h`, `main/stt_reply.c`, `main/voice.h`, `test/host/test_stt_reply.c`, `test/host/mock_voice.h`, `test/host/mock_voice.c`
- Modify: `main/messages.h`, `main/agent.h`, `main/agent.c`, `test/host/test_agent.c` (append), `test/host/test_runner.c`, `scripts/test.sh`, `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `text_layout_clip_utf8` (Delivery A).
- Produces:
  - `MSG_SOURCE_VOICE = 3`;
  - `bool stt_reply_parse(const char *json, char *out, size_t out_len)`;
  - `bool agent_is_busy(void)`;
  - `voice.h`: `esp_err_t voice_start(QueueHandle_t agent_queue)`, `bool voice_request_listen(void)`, `bool voice_request_probe(uint32_t duration_ms)`. Task 7 implements them; this task only declares them and mocks them on the host.
  - Test hook `agent_test_process_voice_message(const char *)`.

- [ ] **Step 1: Failing tests**

`main/stt_reply.h`:

```c
#ifndef STT_REPLY_H
#define STT_REPLY_H

#include <stdbool.h>
#include <stddef.h>

// Reads {"text": "..."} from the STT service into out (whitespace trimmed,
// clipped to out_len - 1 bytes without splitting a UTF-8 character). False
// when the JSON is invalid or has no string "text"; an empty transcript is
// valid and gives "".
bool stt_reply_parse(const char *json, char *out, size_t out_len);

#endif // STT_REPLY_H
```

`test/host/test_stt_reply.c`:

```c
/*
 * Host tests for parsing the STT service reply.
 */

#include <stdio.h>
#include <string.h>

#include "stt_reply.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(reads_and_trims_text)
{
    char out[64];
    ASSERT(stt_reply_parse("{\"text\": \"  hola fidel \\n\", \"ms\": 840}", out, sizeof(out)));
    ASSERT(strcmp(out, "hola fidel") == 0);
    return 0;
}

TEST(empty_transcript_is_valid)
{
    char out[64] = "x";
    ASSERT(stt_reply_parse("{\"text\": \"\"}", out, sizeof(out)));
    ASSERT(out[0] == '\0');
    ASSERT(stt_reply_parse("{\"text\": \"   \"}", out, sizeof(out)));
    ASSERT(out[0] == '\0');
    return 0;
}

TEST(rejects_missing_or_invalid)
{
    char out[64] = "x";
    ASSERT(!stt_reply_parse("{\"error\": \"busy\"}", out, sizeof(out)));
    ASSERT(out[0] == '\0');
    ASSERT(!stt_reply_parse("{\"text\": 3}", out, sizeof(out)));
    ASSERT(!stt_reply_parse("not json", out, sizeof(out)));
    ASSERT(!stt_reply_parse(NULL, out, sizeof(out)));
    return 0;
}

TEST(clips_without_splitting_utf8)
{
    char out[10];
    ASSERT(stt_reply_parse("{\"text\": \"canci\\u00f3n muy larga\"}", out, sizeof(out)));
    ASSERT(strcmp(out, "canci...") == 0);  /* the cut before the two-byte o-acute */
    return 0;
}

int test_stt_reply_all(void)
{
    int failures = 0;

    printf("\nSTT Reply Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(reads_and_trims_text);
    RUN(empty_transcript_is_valid);
    RUN(rejects_missing_or_invalid);
    RUN(clips_without_splitting_utf8);

#undef RUN
    return failures;
}
```

`main/voice.h`:

```c
#ifndef VOICE_H
#define VOICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Microphone capture, double-clap detection and the speech-to-text pipeline.
// Transcripts go to agent_queue as MSG_SOURCE_VOICE messages. Never fatal:
// ESP_ERR_NOT_SUPPORTED when CONFIG_ZCLAW_VOICE_ENABLE is off.
esp_err_t voice_start(QueueHandle_t agent_queue);

// Open listening now, without claps (serial /escucha). False when voice is
// off or a voice turn is already running.
bool voice_request_listen(void);

// Microphone calibration (/mic): level page plus serial stats for
// duration_ms. False when voice is off.
bool voice_request_probe(uint32_t duration_ms);

#endif // VOICE_H
```

`test/host/mock_voice.h`:

```c
#ifndef MOCK_VOICE_H
#define MOCK_VOICE_H

#include <stdbool.h>
#include <stdint.h>

void mock_voice_reset(void);
void mock_voice_set_running(bool running);
int mock_voice_listen_calls(void);
uint32_t mock_voice_last_probe_ms(void);

#endif // MOCK_VOICE_H
```

`test/host/mock_voice.c`:

```c
#include "voice.h"
#include "mock_voice.h"

static bool s_running = false;
static int s_listen_calls = 0;
static uint32_t s_probe_ms = 0;

void mock_voice_reset(void)
{
    s_running = false;
    s_listen_calls = 0;
    s_probe_ms = 0;
}

void mock_voice_set_running(bool running)
{
    s_running = running;
}

int mock_voice_listen_calls(void)
{
    return s_listen_calls;
}

uint32_t mock_voice_last_probe_ms(void)
{
    return s_probe_ms;
}

esp_err_t voice_start(QueueHandle_t agent_queue)
{
    (void)agent_queue;
    return ESP_ERR_NOT_SUPPORTED;
}

bool voice_request_listen(void)
{
    s_listen_calls++;
    return s_running;
}

bool voice_request_probe(uint32_t duration_ms)
{
    if (!s_running) {
        return false;
    }
    s_probe_ms = duration_ms;
    return true;
}
```

Append to `test/host/test_agent.c`, before `int test_agent_all(void)`. Put the include line at the top of this block, not at the top of the file, so line 784 does not move:

```c
#include "mock_voice.h"

TEST(voice_reply_shows_dijiste_header)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Son las tres.\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    mock_display_reset();
    mock_display_set_running(true);
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q, NULL);
    ASSERT(mock_llm_push_result(ESP_OK, reply));

    agent_test_process_voice_message("que hora es");

    ASSERT(mock_display_answer_calls() == 1);
    ASSERT_STR_EQ(mock_display_last_answer_label(), "Dijiste:");
    ASSERT_STR_EQ(mock_display_last_answer_prompt(), "que hora es");
    ASSERT_STR_EQ(mock_display_last_answer_reply(), "Son las tres.");

    vQueueDelete(channel_q);
    mock_display_reset();
    return 0;
}

TEST(repeated_voice_message_is_answered_twice)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Hola.\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q, NULL);
    ASSERT(mock_llm_push_result(ESP_OK, reply));
    ASSERT(mock_llm_push_result(ESP_OK, reply));

    agent_test_process_voice_message("hola fidel");
    agent_test_process_voice_message("hola fidel");

    ASSERT(mock_llm_request_count() == 2);

    vQueueDelete(channel_q);
    return 0;
}

TEST(escucha_and_mic_commands)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();
    mock_display_reset();
    mock_voice_reset();
    channel_q = xQueueCreate(8, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q, NULL);

    mock_voice_set_running(true);
    agent_test_process_message("/escucha");
    ASSERT(mock_voice_listen_calls() == 1);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "escucho") != NULL);
    agent_test_process_message("/mic");
    ASSERT(mock_voice_last_probe_ms() == 20000);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Micr") != NULL);
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_display_answer_calls() == 0);

    mock_voice_set_running(false);
    agent_test_process_message("/escucha");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Voz no disponible") != NULL);

    vQueueDelete(channel_q);
    mock_voice_reset();
    return 0;
}
```

Register the three tests at the end of `test_agent_all`, before `return failures;`, in the existing `printf` + `if` style:

```c
    printf("  voice_reply_shows_dijiste_header... ");
    if (test_voice_reply_shows_dijiste_header() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  repeated_voice_message_is_answered_twice... ");
    if (test_repeated_voice_message_is_answered_twice() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  escucha_and_mic_commands... ");
    if (test_escucha_and_mic_commands() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }
```

Also:
- `main/messages.h`: add `    MSG_SOURCE_VOICE = 3,` after `MSG_SOURCE_CRON = 2,`.
- `main/agent.h`: add `void agent_test_process_voice_message(const char *user_message);` inside the `#ifdef TEST_BUILD` block, and `bool agent_is_busy(void);` before it (outside the block):

  ```c
  // True while the agent task processes a message (voice ignores claps then).
  bool agent_is_busy(void);
  ```

- `main/agent.c`, inside `#ifdef TEST_BUILD`, after `agent_test_process_message_for_chat`:

  ```c
  void agent_test_process_voice_message(const char *user_message)
  {
      process_message(user_message, MSG_SOURCE_VOICE, 0);
  }
  ```

- `main/agent.c`, near the other statics, add a temporary stub `bool agent_is_busy(void) { return false; }` (replaced in Step 3).
- Create `main/stt_reply.c` with only `#include "stt_reply.h"`.
- Registration:
  - `test_runner.c`: add `extern int test_stt_reply_all(void);` and `    failures += test_stt_reply_all();`;
  - `scripts/test.sh`: add `        test_stt_reply.c \` after `        test_voice_detect.c \`, `        mock_voice.c \` after `        mock_task_log.c \`, and `        ../../main/stt_reply.c \` after `        ../../main/endpoint.c \`.

- [ ] **Step 2: Run the gate to see it fail**

Run: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`
Expected: `host gate FAILED`. The link fails on `stt_reply_parse`. Once that resolves, these fail:
- the label is `T\xC3\xBA:` instead of `Dijiste:`;
- `mock_llm_request_count() == 2`, because the replay suppression drops the repeated voice message;
- `mock_voice_listen_calls() == 1`, because `/escucha` falls through to the LLM.

- [ ] **Step 3: Implement**

`main/stt_reply.c`:

```c
#include "stt_reply.h"

#include <ctype.h>
#include <string.h>

#include "cJSON.h"
#include "text_layout.h"

bool stt_reply_parse(const char *json, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!json) {
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    const cJSON *text = root ? cJSON_GetObjectItem(root, "text") : NULL;
    if (!text || !cJSON_IsString(text)) {
        cJSON_Delete(root);
        return false;
    }
    const char *start = text->valuestring;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    size_t len = strlen(start);
    while (len > 0 && isspace((unsigned char)start[len - 1])) {
        len--;
    }
    char *trimmed = cJSON_malloc(len + 1);
    if (!trimmed) {
        cJSON_Delete(root);
        return false;
    }
    memcpy(trimmed, start, len);
    trimmed[len] = '\0';
    text_layout_clip_utf8(trimmed, out_len - 1, out, out_len);
    cJSON_free(trimmed);
    cJSON_Delete(root);
    return true;
}
```

In `main/agent.c`:

1. Add `#include "voice.h"` and `#define MIC_PROBE_MS 20000` next to `PANEL_DISPLAY_MS`.
2. Replace the stub with:

   ```c
   static volatile bool s_busy = false;  // agent task inside process_message

   bool agent_is_busy(void)
   {
       return s_busy;
   }
   ```

3. In `process_message`, change the label line to:

   ```c
       s_reply_label = is_cron_trigger ? "Tarea:"
                     : source == MSG_SOURCE_VOICE ? "Dijiste:" : "T\xC3\xBA:";
   ```

4. Change `    if (is_non_command_message) {` (the replay-suppression block, around line 553) to `    if (is_non_command_message && source != MSG_SOURCE_VOICE) {`.
5. Just before the `/panel` handler (`if (agent_is_command(user_message, "panel")) {`), add:

   ```c
       if (agent_is_command(user_message, "escucha")) {
           // No answer page: the listening face must stay on screen.
           const char *reply = voice_request_listen() ? "Te escucho." : "Voz no disponible u ocupada.";
           queue_channel_response(reply);
           queue_telegram_response(reply, reply_chat_id);
           metrics_log_request(&metrics, "escucha_handled");
           return;
       }

       if (agent_is_command(user_message, "mic")) {
           const char *reply = voice_request_probe(MIC_PROBE_MS)
                                   ? "Micr\xC3\xB3" "fono en pantalla 20 s (y por serie)."
                                   : "Voz no disponible.";
           queue_channel_response(reply);
           queue_telegram_response(reply, reply_chat_id);
           metrics_log_request(&metrics, "mic_handled");
           return;
       }
   ```

6. In `agent_task`, wrap the call:

   ```c
           if (xQueueReceive(s_input_queue, &msg, portMAX_DELAY) == pdTRUE) {
               s_busy = true;
               process_message(msg.text, msg.source, response_chat_id_for_source(msg.source, msg.chat_id));
   ```

   and add `            s_busy = false;` after `            s_reply_is_task = false;`.

Add `"stt_reply.c"` to `main/CMakeLists.txt` after `"endpoint.c"`.

- [ ] **Step 4: Run the gate to see it pass**

Run: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`
Expected: `host gate OK`, with the 4 STT reply tests and the 3 new agent tests OK, and the baseline line 784 unchanged.

- [ ] **Step 5: Commit**

```bash
git add main/stt_reply.h main/stt_reply.c main/voice.h main/messages.h main/agent.h main/agent.c main/CMakeLists.txt test/host/test_stt_reply.c test/host/mock_voice.h test/host/mock_voice.c test/host/test_agent.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(voice): voice message source with Dijiste: header, /escucha and /mic

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Listening face, level bar, `/mic` page, notices, red LED

**Files:**
- Modify:
  - `main/face_state.h`, `main/face_state.c`, `main/display_face.c`;
  - `main/page_rotation.h`, `main/display.h`, `main/display.c`;
  - `main/tools_led.c`, `main/tools_handlers.h`;
  - `scripts/face_assets.py`;
  - `test/host/test_face_state.c`, `test/host/test_face_assets.py`;
  - `main/CMakeLists.txt`.
- Create: `main/display_mic.h`, `main/display_mic.c`

**Interfaces:**
- Consumes: `audio_level_bar` (Task 3).
- Produces (used by Task 7):
  - `FACE_EXPR_LISTENING`, `FACE_MODE_LISTENING`, `face_state_set_listening(face_state_t *, bool)`;
  - `PAGE_MIC`, `PAGE_NOTICE`;
  - `display_notify_listening(bool)`, `display_set_level(uint8_t percent)`;
  - `display_mic_stats_t`, `display_show_mic(uint32_t)`, `display_set_mic_stats(const display_mic_stats_t *)`;
  - `display_show_notice(const char *title, const char *text, uint32_t duration_ms)`;
  - `led_listening_set(bool on)`.

- [ ] **Step 1: Failing tests**

Append to `test/host/test_face_state.c`, before its `int test_face_state_all(void)`:

```c
static void listening_catalog(face_state_t *s, uint8_t listening_variants)
{
    uint8_t expr[FACE_EXPR_COUNT];
    uint8_t patch[FACE_PATCH_COUNT] = {0, 1, 1};
    memset(expr, 1, sizeof(expr));
    expr[FACE_EXPR_LISTENING] = listening_variants;
    face_state_set_catalog(s, expr, patch);
}

TEST(listening_shows_listening_face)
{
    face_state_t s;
    face_state_init(&s, 7u, 0);
    listening_catalog(&s, 1);
    face_state_set_wifi(&s, true);
    face_state_set_listening(&s, true);
    ASSERT(face_state_mode(&s, 100) == FACE_MODE_LISTENING);
    face_frame_t f = face_state_frame(&s, 100);
    ASSERT(f.expr == FACE_EXPR_LISTENING);
    ASSERT(f.patch == FACE_PATCH_NONE);
    face_state_set_listening(&s, false);
    ASSERT(face_state_mode(&s, 200) == FACE_MODE_IDLE);
    return 0;
}

TEST(listening_without_photo_uses_neutral)
{
    face_state_t s;
    face_state_init(&s, 7u, 0);
    listening_catalog(&s, 0);
    face_state_set_wifi(&s, true);
    face_state_set_listening(&s, true);
    face_frame_t f = face_state_frame(&s, 100);
    ASSERT(f.expr == FACE_EXPR_NEUTRAL);
    ASSERT(!f.text_fallback);
    return 0;
}

TEST(listening_counts_as_activity_and_wins)
{
    face_state_t s;
    face_state_init(&s, 7u, 0);
    listening_catalog(&s, 1);
    ASSERT(!face_state_is_active(&s, 0));
    face_state_set_thinking(&s, true);        /* also offline: wifi never came up */
    face_state_set_listening(&s, true);
    ASSERT(face_state_mode(&s, 0) == FACE_MODE_LISTENING);
    ASSERT(face_state_is_active(&s, 0));
    return 0;
}
```

Add `RUN(listening_shows_listening_face);`, `RUN(listening_without_photo_uses_neutral);` and `RUN(listening_counts_as_activity_and_wins);` to its `RUN` list.

Append to `test/host/test_face_assets.py`, inside `FaceAssetsTest`:

```python
    def test_listening_frame_is_converted(self):
        portrait(mouth_open=True).save(self.src / "listening.png")
        self.assertEqual(self.run_tool(), 0)
        magic, x, y, w, h, flags, _, size = read_frame(self.out / "listening.r565")
        self.assertEqual((magic, x, y, w, h, flags), (b"R565", 0, 0, 240, 240, 0))
```

- [ ] **Step 2: Run them to see them fail**

Run: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`
Expected: `host gate FAILED`, because `FACE_EXPR_LISTENING`, `FACE_MODE_LISTENING` and `face_state_set_listening` are undeclared.

Run: `cd ~/Projects/zclaw/test/host && /private/tmp/claude-501/-Users-jianpedi-Projects/e49efb90-9a94-4db4-b892-c83271a5df4b/scratchpad/imgenv/bin/python -m unittest test_face_assets.FaceAssetsTest.test_listening_frame_is_converted`
Expected: an error, `FileNotFoundError … listening.r565`.

- [ ] **Step 3: Implement the face state and asset name**

`main/face_state.h`:
- add `    FACE_EXPR_LISTENING,` right before `    FACE_EXPR_COUNT`;
- add `    FACE_MODE_LISTENING` after `    FACE_MODE_OFFLINE` (with a comma after `FACE_MODE_OFFLINE`);
- add `    bool listening;` after `    bool thinking;` in `face_state_t`;
- declare `void face_state_set_listening(face_state_t *s, bool active);` after `face_state_set_thinking`.

`main/face_state.c`:
- `face_state_init` already `memset`s the struct, so `listening` starts false (no change there);
- add:

  ```c
  void face_state_set_listening(face_state_t *s, bool active)
  {
      s->listening = active;
  }
  ```

- first line of `face_state_mode`:

  ```c
      if (s->listening) {
          return FACE_MODE_LISTENING;  // wins: the user is talking to the board
      }
  ```

- `face_state_is_active` returns `mode == FACE_MODE_THINKING || mode == FACE_MODE_TALKING || mode == FACE_MODE_LISTENING;`;
- in the `face_state_frame` switch, add before `case FACE_MODE_IDLE:`:

  ```c
      case FACE_MODE_LISTENING:
          f.expr = FACE_EXPR_LISTENING;
          f.expr_variant = cycle_variant(elapsed, s->expr_variants[FACE_EXPR_LISTENING]);
          break;
  ```

`main/display_face.c`:
- `s_expr_names` becomes `"neutral", "thinking", "sleeping", "happy", "angry", "speech", "listening",`;
- in `mode_label`, add `    case FACE_MODE_LISTENING:\n        return "escuchando...";` before `case FACE_MODE_IDLE:`.

`scripts/face_assets.py`:
- `FULL_FRAMES = ("neutral", "thinking", "sleeping", "happy", "angry", "speech", "listening")`;
- in the module docstring, add `listening` to the list of optional inputs.

Run: `bash .superpowers/sdd/2026-10-11-voice-input/host-gate.sh`
Expected: `host gate OK`, with the 3 new face-state tests OK.

Run the face-assets test with the imgenv Python (Step 2 command).
Expected: `OK`.

- [ ] **Step 4: Display, page and LED API**

`main/page_rotation.h`: in `page_t`, after `PAGE_ANSWER`:

```c
    PAGE_ANSWER,   // Fidel's reply as text (question on top)
    PAGE_MIC,      // microphone calibration (/mic)
    PAGE_NOTICE    // short message, e.g. a voice error
```

(Change `PAGE_ANSWER` to end with a comma.)

`main/display.h`: add before `#endif`:

```c
typedef struct {
    int16_t peak_dbfs10;    // tenths of dBFS
    int16_t rms_dbfs10;
    int16_t noise_dbfs10;
    uint16_t clip_permille;
    uint16_t adc_offset;    // mean raw 12-bit ADC value (ideal ~2048)
    uint32_t overruns;      // ADC frames lost since boot
} display_mic_stats_t;

// Voice: listening face + level bar while active. Non-blocking.
void display_notify_listening(bool active);

// Voice level for the bar, 0-100 (a plain store, no queue event).
void display_set_level(uint8_t percent);

// Microphone calibration page for duration_ms; numbers come from
// display_set_mic_stats (copied under a lock). False when not running.
bool display_show_mic(uint32_t duration_ms);
void display_set_mic_stats(const display_mic_stats_t *stats);

// Short message page (title + text, both copied) for duration_ms.
bool display_show_notice(const char *title, const char *text, uint32_t duration_ms);
```

`main/display_mic.h`:

```c
#ifndef DISPLAY_MIC_H
#define DISPLAY_MIC_H

#include <stdint.h>

#include "display.h"

// /mic page: static labels, then values (overwritten in place every 250 ms).
void display_mic_draw_frame(void);
void display_mic_draw_values(const display_mic_stats_t *stats, uint8_t level);

#endif // DISPLAY_MIC_H
```

`main/display_mic.c`:

```c
#include "display_mic.h"

#include "config.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#include <stdio.h>

#include "display_gfx.h"
#include "display_hw.h"

#define MIC_VALUE_X (9 * 8)
#define MIC_ROW_Y   44
#define MIC_ROW_H   22
#define MIC_BAR_Y   164
#define MIC_BAR_H   24

static void format_db(int tenths, char *buf, size_t len)
{
    int a = tenths < 0 ? -tenths : tenths;
    snprintf(buf, len, "%s%d.%d dBFS   ", tenths < 0 ? "-" : "", a / 10, a % 10);
}

void display_mic_draw_frame(void)
{
    static const char *const labels[] = {"Pico", "Nivel", "Ruido", "Recorte", "ADC"};

    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);
    gfx_text(4, 6, "Micr\xC3\xB3" "fono", GFX_AMBER, GFX_BLACK, 2);
    for (int i = 0; i < 5; i++) {
        gfx_text(4, MIC_ROW_Y + i * MIC_ROW_H, labels[i], GFX_AMBER, GFX_BLACK, 1);
    }
    gfx_text(4, 200, "voz a 1 m: pico ~ -6 dBFS", GFX_GRAY, GFX_BLACK, 1);
    gfx_text(4, 218, "ruido < -45 dBFS, 0 % recorte", GFX_GRAY, GFX_BLACK, 1);
    display_hw_wait_idle();
}

void display_mic_draw_values(const display_mic_stats_t *s, uint8_t level)
{
    char b[40];
    int w = (DISPLAY_W - 8) * (level > 100 ? 100 : level) / 100;

    format_db(s->peak_dbfs10, b, sizeof(b));
    gfx_text(MIC_VALUE_X, MIC_ROW_Y, b, GFX_WHITE, GFX_BLACK, 1);
    format_db(s->rms_dbfs10, b, sizeof(b));
    gfx_text(MIC_VALUE_X, MIC_ROW_Y + MIC_ROW_H, b, GFX_WHITE, GFX_BLACK, 1);
    format_db(s->noise_dbfs10, b, sizeof(b));
    gfx_text(MIC_VALUE_X, MIC_ROW_Y + 2 * MIC_ROW_H, b, GFX_WHITE, GFX_BLACK, 1);
    snprintf(b, sizeof(b), "%u.%u %%   ", (unsigned)(s->clip_permille / 10), (unsigned)(s->clip_permille % 10));
    gfx_text(MIC_VALUE_X, MIC_ROW_Y + 3 * MIC_ROW_H, b, GFX_WHITE, GFX_BLACK, 1);
    snprintf(b, sizeof(b), "%u (perdidos %lu)   ", (unsigned)s->adc_offset, (unsigned long)s->overruns);
    gfx_text(MIC_VALUE_X, MIC_ROW_Y + 4 * MIC_ROW_H, b, GFX_WHITE, GFX_BLACK, 1);
    if (w > 0) {
        gfx_fill_rect(4, MIC_BAR_Y, w, MIC_BAR_H, GFX_AMBER);
    }
    if (w < DISPLAY_W - 8) {
        gfx_fill_rect(4 + w, MIC_BAR_Y, DISPLAY_W - 8 - w, MIC_BAR_H, GFX_GRAY);
    }
    display_hw_wait_idle();
}

#endif  // CONFIG_ZCLAW_DISPLAY_ENABLE
```

`main/display.c`, inside `#if CONFIG_ZCLAW_DISPLAY_ENABLE`:

1. Add `#include "display_mic.h"` and these defines:

   ```c
   #define DISPLAY_BAR_Y     232
   #define DISPLAY_BAR_H     8
   #define DISPLAY_BAR_MS    50
   #define DISPLAY_MIC_MS    250
   ```

2. Add `DISPLAY_EV_LISTENING, DISPLAY_EV_SHOW_MIC, DISPLAY_EV_SHOW_NOTICE,` to `display_event_type_t`.
3. Add these statics next to the answer statics:

   ```c
   // Voice: level bar over the listening face, /mic page, notice page.
   static volatile uint8_t s_level;        // 0-100, stored by the voice task
   static int64_t s_bar_ms;
   static bool s_bar_shown;
   static display_mic_stats_t s_mic;       // under s_mic_mux
   static portMUX_TYPE s_mic_mux = portMUX_INITIALIZER_UNLOCKED;
   static int64_t s_mic_ms;
   static char *s_notice;                  // "title\0text\0", owned
   ```

4. Add a helper above `apply_event`:

   ```c
   static void draw_level_bar(uint8_t percent)
   {
       int w = percent >= 100 ? DISPLAY_W : DISPLAY_W * percent / 100;
       if (w > 0) {
           gfx_fill_rect(0, DISPLAY_BAR_Y, w, DISPLAY_BAR_H, GFX_AMBER);
       }
       if (w < DISPLAY_W) {
           gfx_fill_rect(w, DISPLAY_BAR_Y, DISPLAY_W - w, DISPLAY_BAR_H, GFX_BLACK);
       }
   }

   static display_mic_stats_t mic_stats(void)
   {
       display_mic_stats_t copy;
       taskENTER_CRITICAL(&s_mic_mux);
       copy = s_mic;
       taskEXIT_CRITICAL(&s_mic_mux);
       return copy;
   }
   ```

5. In `apply_event`:

   ```c
       case DISPLAY_EV_LISTENING:
           face_state_set_listening(&s_face, ev->value != 0);
           break;
       case DISPLAY_EV_SHOW_MIC:
           page_rotation_show(&s_pages, PAGE_MIC, ev->value, now);
           break;
       case DISPLAY_EV_SHOW_NOTICE:
           free(s_notice);
           s_notice = ev->ptr;
           page_rotation_show(&s_pages, PAGE_NOTICE, ev->value, now);
           break;
   ```

6. In `display_task`, after the `display_answer_clear()` check, add:

   ```c
           if (page != PAGE_NOTICE && s_notice) {
               free(s_notice);
               s_notice = NULL;
           }
   ```

7. In the page-change block, before the final `} else {` (the face), add:

   ```c
               } else if (page == PAGE_MIC) {
                   display_mic_stats_t stats = mic_stats();
                   display_mic_draw_frame();
                   display_mic_draw_values(&stats, s_level);
                   s_mic_ms = now;
               } else if (page == PAGE_NOTICE) {
                   draw_message(s_notice ? s_notice : "", s_notice ? s_notice + strlen(s_notice) + 1 : "");
   ```

8. After the `PAGE_ANSWER` steady-state branch, chain:

   ```c
           } else if (page == PAGE_MIC && now - s_mic_ms >= DISPLAY_MIC_MS) {
               display_mic_stats_t stats = mic_stats();
               display_mic_draw_values(&stats, s_level);
               s_mic_ms = now;
           }
   ```

9. Replace the final face-draw `if` at the bottom of the loop with:

   ```c
           if (page == PAGE_FACE) {
               if (!display_face_draw(&s_face, now)) {
                   sd_card_mark_failed(now);
                   display_face_clear(&s_face);
               }
               if (s_face.listening && now - s_bar_ms >= DISPLAY_BAR_MS) {
                   draw_level_bar(s_level);
                   s_bar_ms = now;
                   s_bar_shown = true;
               } else if (!s_face.listening && s_bar_shown) {
                   display_face_invalidate();  // repaint the rows under the bar
                   s_bar_shown = false;
               }
           }
   ```

10. Add the public functions after `display_show_answer`:

    ```c
    void display_notify_listening(bool active)
    {
        post(DISPLAY_EV_LISTENING, active ? 1u : 0u, 0);
    }

    void display_set_level(uint8_t percent)
    {
        s_level = percent > 100 ? 100 : percent;
    }

    bool display_show_mic(uint32_t duration_ms)
    {
        if (!s_queue) {
            return false;
        }
        post(DISPLAY_EV_SHOW_MIC, duration_ms, 0);
        return true;
    }

    void display_set_mic_stats(const display_mic_stats_t *stats)
    {
        if (!stats) {
            return;
        }
        taskENTER_CRITICAL(&s_mic_mux);
        s_mic = *stats;
        taskEXIT_CRITICAL(&s_mic_mux);
    }

    bool display_show_notice(const char *title, const char *text, uint32_t duration_ms)
    {
        if (!s_queue || !title || !text) {
            return false;
        }
        size_t tl = strlen(title);
        size_t xl = strlen(text);
        char *block = malloc(tl + 1 + xl + 1);
        if (!block) {
            return false;
        }
        memcpy(block, title, tl + 1);
        memcpy(block + tl + 1, text, xl + 1);
        display_event_t ev = {
            .type = DISPLAY_EV_SHOW_NOTICE, .value = duration_ms, .extra = 0, .ptr = block,
        };
        if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
            free(block);
            return false;
        }
        return true;
    }
    ```

11. Add stubs to the `#else  // !CONFIG_ZCLAW_DISPLAY_ENABLE` block:

    ```c
    void display_notify_listening(bool active) { (void)active; }
    void display_set_level(uint8_t percent) { (void)percent; }
    bool display_show_mic(uint32_t duration_ms) { (void)duration_ms; return false; }
    void display_set_mic_stats(const display_mic_stats_t *stats) { (void)stats; }
    bool display_show_notice(const char *title, const char *text, uint32_t duration_ms)
    {
        (void)title;
        (void)text;
        (void)duration_ms;
        return false;
    }
    ```

`main/tools_led.c`: after the static `led_anim_hands_off()` function, add:

```c
void led_listening_set(bool on)
{
    // Solid red while listening; the thinking animation takes over afterwards.
    if (!s_led_mutex) {
        s_led_mutex = xSemaphoreCreateMutex();
    }
    s_anim_active = false;
    led_anim_hands_off();
    led_anim_draw(on ? 80 : 0, 0, 0);
    if (!on) {
        s_anim_hands_off = false;
    }
}
```

`main/tools_handlers.h`: add `void led_listening_set(bool on);` after `led_thinking_stop`.

`main/CMakeLists.txt`: add `"display_mic.c"` after `"display_answer.c"`.

- [ ] **Step 5: Build, gate, commit**

Run the firmware build. Expected: `Project build complete`, no `error:`.
Run the host gate. Expected: `host gate OK`.

```bash
git add main/face_state.h main/face_state.c main/display_face.c main/page_rotation.h main/display.h main/display.c main/display_mic.h main/display_mic.c main/tools_led.c main/tools_handlers.h main/CMakeLists.txt scripts/face_assets.py test/host/test_face_state.c test/host/test_face_assets.py
git commit -m "feat(voice): listening face with level bar, /mic page, notices and red LED

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Firmware glue: ADC capture, STT client, voice task

**Files:**
- Create: `main/audio_capture.h`, `main/audio_capture.c`, `main/stt_client.h`, `main/stt_client.c`, `main/voice.c`
- Modify: `main/Kconfig.projbuild`, `main/gpio_policy.c`, `main/main.c`, `main/CMakeLists.txt`
- Modify (git-ignored, local only): `sdkconfig.defaults.esp32c6`

**Interfaces:**
- Consumes:
  - Tasks 3–6: `audio_dsp_*`, `audio_level_*`, `clap_detect_*`, `endpoint_*`, `adpcm_*`, `stt_reply_parse`;
  - `display_notify_listening`, `display_set_level`, `display_show_mic`, `display_set_mic_stats`, `display_show_notice`;
  - `led_listening_set`, `led_thinking_start`/`stop`, `agent_is_busy`, `MSG_SOURCE_VOICE`;
  - the `voice.h` declarations.
- Produces: the real `voice_start`, `voice_request_listen` and `voice_request_probe` (the host keeps using `mock_voice.c`).

This task is ESP-only glue with no host-testable logic left: the decisions live in the pure modules of Tasks 3–5. Verification is the firmware build here and the board in Task 8.

- [ ] **Step 1: Kconfig and GPIO reservation**

In `main/Kconfig.projbuild`, insert this menu right after the `endmenu` of `menu "Display and SD card"`:

```
    menu "Voice input"
        config ZCLAW_VOICE_ENABLE
            bool "Voice input: microphone, double clap, speech to text"
            default n
            help
                MAX4466 on an ADC1 pin. Two claps open listening; the speech is
                sent (IMA-ADPCM over HTTPS) to server/stt on the VPS and the
                transcript reaches the agent like a typed message.

        config ZCLAW_MIC_GPIO
            int "Microphone GPIO (ADC1 channel)"
            range 0 6
            default 0
            depends on ZCLAW_VOICE_ENABLE

        config ZCLAW_STT_URL
            string "Speech-to-text service URL"
            default ""
            depends on ZCLAW_VOICE_ENABLE
            help
                For example https://<VPS host>/stt/transcribe

        config ZCLAW_STT_TOKEN
            string "Token for the speech-to-text service (X-Stt-Token)"
            default ""
            depends on ZCLAW_VOICE_ENABLE
            help
                Keep it in the git-ignored sdkconfig.defaults.<target>.

        config ZCLAW_CLAP_LEVEL_PCT
            int "Clap peak level (% of full scale)"
            range 5 100
            default 40
            depends on ZCLAW_VOICE_ENABLE

        config ZCLAW_CLAP_RATIO
            int "Clap peak / noise floor ratio"
            range 2 50
            default 6
            depends on ZCLAW_VOICE_ENABLE

        config ZCLAW_SPEECH_RATIO
            int "Speech level / noise floor ratio"
            range 2 20
            default 3
            depends on ZCLAW_VOICE_ENABLE
    endmenu

```

In `main/gpio_policy.c`, `reserved_rebuild`: after the `#if CONFIG_ZCLAW_SD_ENABLE … #endif` block, add:

```c
#if CONFIG_ZCLAW_VOICE_ENABLE
    reserved_add(CONFIG_ZCLAW_MIC_GPIO);
#endif
```

- [ ] **Step 2: ADC capture**

`main/audio_capture.h`:

```c
#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_CAPTURE_RATE_HZ       16000
#define AUDIO_CAPTURE_FRAME_SAMPLES 160   // 10 ms

typedef void (*audio_frame_cb_t)(const int16_t *pcm, size_t n, void *ctx);

// Samples `gpio` (ADC1) at 32 kHz and calls cb from the audio task with
// 10 ms frames of 16 kHz int16 audio. ESP_ERR_INVALID_ARG for a non-ADC1 pin.
esp_err_t audio_capture_start(int gpio, audio_frame_cb_t cb, void *ctx);
uint32_t audio_capture_overruns(void);   // conversion frames lost (pool full)
uint16_t audio_capture_raw_mean(void);   // mean raw 12-bit value (~2048 when centred)

#endif // AUDIO_CAPTURE_H
```

`main/audio_capture.c`:

```c
#include "audio_capture.h"

#include "config.h"

#if CONFIG_ZCLAW_VOICE_ENABLE

#include "audio_dsp.h"
#include "esp_adc/adc_continuous.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAPTURE_ADC_RATE_HZ  (AUDIO_CAPTURE_RATE_HZ * 2)
#define CAPTURE_READ_RESULTS 320  // 10 ms at 32 kHz
#define CAPTURE_READ_BYTES   (CAPTURE_READ_RESULTS * SOC_ADC_DIGI_RESULT_BYTES)
#define CAPTURE_TASK_STACK   4096
#define CAPTURE_TASK_PRIO    5    // above the display task: the DMA pool must not overflow

static const char *TAG = "audio";
static adc_continuous_handle_t s_adc;
static adc_channel_t s_channel;
static audio_frame_cb_t s_cb;
static void *s_ctx;
static volatile uint32_t s_overruns;
static volatile uint16_t s_raw_mean = 2048;

static bool IRAM_ATTR on_pool_overflow(adc_continuous_handle_t handle,
                                       const adc_continuous_evt_data_t *edata, void *user_data)
{
    (void)handle;
    (void)edata;
    (void)user_data;
    s_overruns++;
    return false;
}

static void capture_task(void *arg)
{
    // 32-bit storage keeps every 4-byte conversion result aligned.
    static uint32_t buf[CAPTURE_READ_RESULTS];
    static uint16_t raw[CAPTURE_READ_RESULTS];
    static int16_t pcm[CAPTURE_READ_RESULTS / 2];
    static int16_t frame[AUDIO_CAPTURE_FRAME_SAMPLES];
    size_t fill = 0;
    int32_t mean_q4 = 2048 << 4;
    audio_dsp_t dsp;
    (void)arg;

    audio_dsp_init(&dsp);
    for (;;) {
        uint32_t got = 0;
        if (adc_continuous_read(s_adc, (uint8_t *)buf, sizeof(buf), &got, 100) != ESP_OK || got == 0) {
            continue;
        }
        size_t m = 0;
        uint32_t sum = 0;
        for (uint32_t i = 0; i < got / SOC_ADC_DIGI_RESULT_BYTES; i++) {
            const adc_digi_output_data_t *p = (const adc_digi_output_data_t *)&buf[i];
            if (p->type2.channel == s_channel) {
                raw[m] = (uint16_t)p->type2.data;
                sum += raw[m];
                m++;
            }
        }
        if (m < 2) {
            continue;
        }
        mean_q4 += ((int32_t)((sum / m) << 4) - mean_q4) / 16;
        s_raw_mean = (uint16_t)(mean_q4 >> 4);
        size_t produced = audio_dsp_convert(&dsp, raw, m & ~(size_t)1, pcm);
        for (size_t i = 0; i < produced; i++) {
            frame[fill++] = pcm[i];
            if (fill == AUDIO_CAPTURE_FRAME_SAMPLES) {
                s_cb(frame, fill, s_ctx);
                fill = 0;
            }
        }
    }
}

esp_err_t audio_capture_start(int gpio, audio_frame_cb_t cb, void *ctx)
{
    adc_unit_t unit;

    if (s_adc) {
        return ESP_OK;
    }
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = adc_continuous_io_to_channel(gpio, &unit, &s_channel);
    if (err != ESP_OK) {
        return err;
    }
    if (unit != ADC_UNIT_1) {
        return ESP_ERR_INVALID_ARG;
    }
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = CAPTURE_READ_BYTES * 4,
        .conv_frame_size = CAPTURE_READ_BYTES,
        .flags = { .flush_pool = 1 },
    };
    err = adc_continuous_new_handle(&handle_cfg, &s_adc);
    if (err != ESP_OK) {
        s_adc = NULL;
        return err;
    }
    adc_digi_pattern_config_t pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = (uint8_t)s_channel,
        .unit = (uint8_t)unit,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    adc_continuous_config_t cfg = {
        .pattern_num = 1,
        .adc_pattern = &pattern,
        .sample_freq_hz = CAPTURE_ADC_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    adc_continuous_evt_cbs_t cbs = { .on_pool_ovf = on_pool_overflow };
    s_cb = cb;
    s_ctx = ctx;
    err = adc_continuous_config(s_adc, &cfg);
    if (err == ESP_OK) {
        err = adc_continuous_register_event_callbacks(s_adc, &cbs, NULL);
    }
    if (err == ESP_OK) {
        err = adc_continuous_start(s_adc);
    }
    if (err == ESP_OK &&
        xTaskCreate(capture_task, "audio", CAPTURE_TASK_STACK, NULL, CAPTURE_TASK_PRIO, NULL) != pdPASS) {
        adc_continuous_stop(s_adc);
        err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        adc_continuous_deinit(s_adc);
        s_adc = NULL;
        return err;
    }
    ESP_LOGI(TAG, "Microphone on GPIO%d (ADC1 channel %d), %d Hz", gpio, (int)s_channel, CAPTURE_ADC_RATE_HZ);
    return ESP_OK;
}

uint32_t audio_capture_overruns(void)
{
    return s_overruns;
}

uint16_t audio_capture_raw_mean(void)
{
    return s_raw_mean;
}

#else  // !CONFIG_ZCLAW_VOICE_ENABLE

esp_err_t audio_capture_start(int gpio, audio_frame_cb_t cb, void *ctx)
{
    (void)gpio;
    (void)cb;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

uint32_t audio_capture_overruns(void)
{
    return 0;
}

uint16_t audio_capture_raw_mean(void)
{
    return 0;
}

#endif
```

- [ ] **Step 3: STT client**

`main/stt_client.h`:

```c
#ifndef STT_CLIENT_H
#define STT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// True when CONFIG_ZCLAW_STT_URL and CONFIG_ZCLAW_STT_TOKEN are set.
bool stt_client_configured(void);

// POSTs an ADPCM stream (header + codes) to the STT service. ESP_OK with
// text set (possibly "") on HTTP 200 with a valid reply. *status always gets
// the HTTP status (0 without a response).
esp_err_t stt_client_transcribe(const uint8_t *body, size_t len, char *text, size_t text_len, int *status);

#endif // STT_CLIENT_H
```

`main/stt_client.c`:

```c
#include "stt_client.h"

#include "config.h"

#if CONFIG_ZCLAW_VOICE_ENABLE

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "http_gate.h"
#include "stt_reply.h"

#define STT_TIMEOUT_MS   15000
#define STT_GATE_WAIT_MS 20000
#define STT_REPLY_MAX    1024

static const char *TAG = "stt";

bool stt_client_configured(void)
{
    return CONFIG_ZCLAW_STT_URL[0] != '\0' && CONFIG_ZCLAW_STT_TOKEN[0] != '\0';
}

static esp_err_t post(const uint8_t *body, size_t len, char *reply, size_t reply_len, int *status)
{
    esp_http_client_config_t cfg = {
        .url = CONFIG_ZCLAW_STT_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = STT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "X-Stt-Token", CONFIG_ZCLAW_STT_TOKEN);
    esp_http_client_set_header(client, "X-Audio-Format", "ima-adpcm; rate=16000");
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_err_t err = esp_http_client_open(client, (int)len);
    size_t sent = 0;
    while (err == ESP_OK && sent < len) {
        int n = esp_http_client_write(client, (const char *)body + sent, (int)(len - sent));
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        sent += (size_t)n;
    }
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        *status = esp_http_client_get_status_code(client);
        int total = 0;
        while (total < (int)reply_len - 1) {
            int n = esp_http_client_read(client, reply + total, (int)reply_len - 1 - total);
            if (n <= 0) {
                break;
            }
            total += n;
        }
        reply[total] = '\0';
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t stt_client_transcribe(const uint8_t *body, size_t len, char *text, size_t text_len, int *status)
{
    *status = 0;
    if (text_len > 0) {
        text[0] = '\0';
    }
    if (!stt_client_configured()) {
        return ESP_ERR_INVALID_STATE;
    }
    char *reply = malloc(STT_REPLY_MAX);
    if (!reply) {
        return ESP_ERR_NO_MEM;
    }
    reply[0] = '\0';
    if (!http_gate_acquire("stt", pdMS_TO_TICKS(STT_GATE_WAIT_MS))) {
        free(reply);
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = post(body, len, reply, STT_REPLY_MAX, status);
    http_gate_release();
    if (err == ESP_OK && *status != 200) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && !stt_reply_parse(reply, text, text_len)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "HTTP %d, %u bytes sent: %s", *status, (unsigned)len, esp_err_to_name(err));
    free(reply);
    return err;
}

#else  // !CONFIG_ZCLAW_VOICE_ENABLE

bool stt_client_configured(void)
{
    return false;
}

esp_err_t stt_client_transcribe(const uint8_t *body, size_t len, char *text, size_t text_len, int *status)
{
    (void)body;
    (void)len;
    if (text_len > 0) {
        text[0] = '\0';
    }
    *status = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
```

- [ ] **Step 4: Voice task**

`main/voice.c`:

```c
#include "voice.h"

#include "config.h"

#if CONFIG_ZCLAW_VOICE_ENABLE

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adpcm.h"
#include "agent.h"
#include "audio_capture.h"
#include "audio_level.h"
#include "clap_detect.h"
#include "display.h"
#include "endpoint.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "messages.h"
#include "stt_client.h"
#include "tools_handlers.h"

#define VOICE_FRAME_SAMPLES   AUDIO_CAPTURE_FRAME_SAMPLES
#define VOICE_BUF_BYTES       (ADPCM_HEADER_BYTES + ENDPOINT_MAX_FRAMES * VOICE_FRAME_SAMPLES / 2)
#define VOICE_TASK_STACK      8192   // the TLS handshake of the upload runs here
#define VOICE_TASK_PRIO       3
#define VOICE_NOTICE_MS       3000
#define VOICE_SPEECH_MIN_RMS  300    // ~-41 dBFS (D8)
#define VOICE_LISTEN_LIMIT_MS 12000  // watchdog if audio frames stop (D4)
#define VOICE_PROBE_PERIOD_MS 250

#define EV_START  (1u << 0)
#define EV_SEND   (1u << 1)
#define EV_CANCEL (1u << 2)

typedef enum { VOICE_IDLE = 0, VOICE_LISTENING, VOICE_SENDING } voice_state_t;

static const char *TAG = "voice";
static QueueHandle_t s_agent_queue;
static TaskHandle_t s_task;
static volatile voice_state_t s_state = VOICE_IDLE;
static volatile bool s_listen_request;
static uint8_t *volatile s_buf;      // ADPCM stream, allocated and freed by the voice task
static volatile size_t s_buf_len;
static int64_t s_listen_since_ms;

// Audio-task state (D3: the audio task only updates state and notifies).
static clap_detect_t s_clap;
static endpoint_t s_endpoint;
static adpcm_state_t s_adpcm;

// /mic probe: accumulated by the audio task, published by the voice task.
static portMUX_TYPE s_probe_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int64_t s_probe_until_ms;
static uint32_t s_probe_frames;
static uint32_t s_probe_peak;
static uint64_t s_probe_sq;
static uint32_t s_probe_clipped;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void probe_reset_locked(void)
{
    s_probe_frames = 0;
    s_probe_peak = 0;
    s_probe_sq = 0;
    s_probe_clipped = 0;
}

static void probe_add(const audio_frame_stats_t *st)
{
    if (s_probe_until_ms == 0) {
        return;
    }
    taskENTER_CRITICAL(&s_probe_mux);
    s_probe_frames++;
    if (st->peak > s_probe_peak) {
        s_probe_peak = st->peak;
    }
    s_probe_sq += (uint64_t)st->rms * st->rms;
    s_probe_clipped += st->clipped;
    taskEXIT_CRITICAL(&s_probe_mux);
}

// Audio task: one 10 ms frame.
static void on_frame(const int16_t *pcm, size_t n, void *ctx)
{
    audio_frame_stats_t st;
    (void)ctx;

    audio_level_frame(pcm, n, &st);
    probe_add(&st);
    if (s_probe_until_ms != 0 || s_state == VOICE_LISTENING) {
        display_set_level((uint8_t)audio_level_bar(audio_level_dbfs_tenths(st.rms), 100));
    }

    switch (s_state) {
    case VOICE_IDLE: {
        clap_event_t ev = clap_detect_frame(&s_clap, st.peak, st.rms);
        if (s_listen_request || (ev == CLAP_DOUBLE && !agent_is_busy())) {
            s_listen_request = false;
            endpoint_init(&s_endpoint, clap_detect_noise(&s_clap), CONFIG_ZCLAW_SPEECH_RATIO,
                          VOICE_SPEECH_MIN_RMS);
            adpcm_init(&s_adpcm);
            s_state = VOICE_LISTENING;
            xTaskNotify(s_task, EV_START, eSetBits);
        }
        break;
    }
    case VOICE_LISTENING: {
        uint8_t *buf = s_buf;
        if (buf && s_buf_len + n / 2 <= VOICE_BUF_BYTES) {
            s_buf_len += adpcm_encode(&s_adpcm, pcm, n, buf + s_buf_len);
        }
        endpoint_result_t r = endpoint_frame(&s_endpoint, st.rms);
        if (r != ENDPOINT_CONTINUE) {
            s_state = VOICE_SENDING;  // the voice task cleans up and returns to IDLE
            xTaskNotify(s_task, r == ENDPOINT_DONE ? EV_SEND : EV_CANCEL, eSetBits);
        }
        break;
    }
    case VOICE_SENDING:
    default:
        break;
    }
}

static void show_error(const char *text)
{
    display_show_notice("Voz", text, VOICE_NOTICE_MS);
}

static void release_buffer(void)
{
    uint8_t *buf = s_buf;
    s_buf = NULL;
    s_buf_len = 0;
    free(buf);
}

static void end_listening_ui(void)
{
    display_notify_listening(false);
    display_set_level(0);
    led_listening_set(false);
}

static void send_recording(void)
{
    char text[CHANNEL_RX_BUF_SIZE];
    int status = 0;
    esp_err_t err;

    text[0] = '\0';
    display_notify_thinking(true);
    led_thinking_start();
    if (!stt_client_configured()) {
        err = ESP_ERR_INVALID_STATE;
    } else if (!s_buf || s_buf_len <= ADPCM_HEADER_BYTES) {
        err = ESP_ERR_INVALID_SIZE;
    } else {
        err = stt_client_transcribe(s_buf, s_buf_len, text, sizeof(text), &status);
    }
    release_buffer();  // before the agent's LLM request needs the heap

    if (err == ESP_OK && text[0] != '\0') {
        channel_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.text, text, sizeof(msg.text) - 1);
        msg.source = MSG_SOURCE_VOICE;
        if (xQueueSend(s_agent_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "Heard: %s", msg.text);
            return;  // the agent turns thinking off when it finishes
        }
        err = ESP_ERR_TIMEOUT;
    }
    display_notify_thinking(false);
    led_thinking_stop();
    if (err == ESP_OK || err == ESP_ERR_INVALID_SIZE) {
        show_error("No te entend\xC3\xAD");
    } else if (err == ESP_ERR_INVALID_STATE) {
        show_error("Error de voz (configuraci\xC3\xB3n)");
    } else if (status >= 400 && status < 500) {
        char b[32];
        snprintf(b, sizeof(b), "Error de voz (%d)", status);
        show_error(b);
    } else {
        show_error("Sin conexi\xC3\xB3n con el servicio de voz");
    }
    ESP_LOGW(TAG, "Voice turn failed: %s (HTTP %d)", esp_err_to_name(err), status);
}

static void probe_publish(bool log_it)
{
    uint32_t frames, peak, clipped;
    uint64_t sq;

    taskENTER_CRITICAL(&s_probe_mux);
    frames = s_probe_frames;
    peak = s_probe_peak;
    sq = s_probe_sq;
    clipped = s_probe_clipped;
    probe_reset_locked();
    taskEXIT_CRITICAL(&s_probe_mux);
    if (frames == 0) {
        return;
    }
    display_mic_stats_t st = {
        .peak_dbfs10 = (int16_t)audio_level_dbfs_tenths(peak),
        .rms_dbfs10 = (int16_t)audio_level_dbfs_tenths((uint32_t)sqrt((double)sq / frames)),
        .noise_dbfs10 = (int16_t)audio_level_dbfs_tenths(clap_detect_noise(&s_clap)),
        .clip_permille = (uint16_t)((uint64_t)clipped * 1000 / ((uint64_t)frames * VOICE_FRAME_SAMPLES)),
        .adc_offset = audio_capture_raw_mean(),
        .overruns = audio_capture_overruns(),
    };
    display_set_mic_stats(&st);
    if (log_it) {
        ESP_LOGI(TAG, "mic: peak %d rms %d noise %d (dBFS x10) clip %u/1000 adc %u lost %lu",
                 st.peak_dbfs10, st.rms_dbfs10, st.noise_dbfs10, (unsigned)st.clip_permille,
                 (unsigned)st.adc_offset, (unsigned long)st.overruns);
    }
}

static void voice_task(void *arg)
{
    unsigned publishes = 0;
    (void)arg;

    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(VOICE_PROBE_PERIOD_MS));

        if (bits & EV_START) {
            uint8_t *buf = malloc(VOICE_BUF_BYTES);
            if (buf) {
                adpcm_state_t first;
                adpcm_init(&first);
                adpcm_header(&first, buf);
                s_buf_len = ADPCM_HEADER_BYTES;
                s_buf = buf;  // published last: the audio task starts encoding now
                s_listen_since_ms = now_ms();
                display_notify_listening(true);
                led_listening_set(true);
                ESP_LOGI(TAG, "Listening");
            } else {
                s_state = VOICE_IDLE;
                show_error("Error de voz (memoria)");
            }
        }
        if (bits & (EV_SEND | EV_CANCEL)) {
            end_listening_ui();
            if (bits & EV_SEND) {
                send_recording();
            } else {
                release_buffer();
                ESP_LOGI(TAG, "No speech: cancelled");
            }
            s_state = VOICE_IDLE;
        }
        if (s_state == VOICE_LISTENING && s_buf && now_ms() - s_listen_since_ms > VOICE_LISTEN_LIMIT_MS) {
            s_state = VOICE_IDLE;
            end_listening_ui();
            release_buffer();
            ESP_LOGW(TAG, "Listening timed out (no audio frames)");
        }
        if (s_probe_until_ms != 0) {
            if (now_ms() >= s_probe_until_ms) {
                s_probe_until_ms = 0;
                display_set_level(0);
            } else {
                probe_publish(++publishes % 4 == 0);  // serial once a second
            }
        }
    }
}

esp_err_t voice_start(QueueHandle_t agent_queue)
{
    if (s_task) {
        return ESP_OK;
    }
    if (!agent_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    s_agent_queue = agent_queue;
    clap_config_t cfg = {
        .level = (uint16_t)(CONFIG_ZCLAW_CLAP_LEVEL_PCT * 32767 / 100),
        .ratio = CONFIG_ZCLAW_CLAP_RATIO,
    };
    clap_detect_init(&s_clap, &cfg);
    if (xTaskCreate(voice_task, "voice", VOICE_TASK_STACK, NULL, VOICE_TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = audio_capture_start(CONFIG_ZCLAW_MIC_GPIO, on_frame, NULL);
    if (err != ESP_OK) {
        vTaskDelete(s_task);
        s_task = NULL;
        return err;
    }
    if (!stt_client_configured()) {
        ESP_LOGW(TAG, "CONFIG_ZCLAW_STT_URL / CONFIG_ZCLAW_STT_TOKEN not set: voice turns will fail");
    }
    ESP_LOGI(TAG, "Voice ready: double clap to talk");
    return ESP_OK;
}

bool voice_request_listen(void)
{
    if (!s_task || s_state != VOICE_IDLE) {
        return false;
    }
    s_listen_request = true;
    return true;
}

bool voice_request_probe(uint32_t duration_ms)
{
    if (!s_task) {
        return false;
    }
    taskENTER_CRITICAL(&s_probe_mux);
    probe_reset_locked();
    taskEXIT_CRITICAL(&s_probe_mux);
    s_probe_until_ms = now_ms() + duration_ms;
    (void)display_show_mic(duration_ms);  // without a display the stats still go to serial
    return true;
}

#else  // !CONFIG_ZCLAW_VOICE_ENABLE

esp_err_t voice_start(QueueHandle_t agent_queue)
{
    (void)agent_queue;
    return ESP_ERR_NOT_SUPPORTED;
}

bool voice_request_listen(void)
{
    return false;
}

bool voice_request_probe(uint32_t duration_ms)
{
    (void)duration_ms;
    return false;
}

#endif
```

- [ ] **Step 5: Start voice in `main.c`, CMake**

In `main/main.c`, add `#include "voice.h"` and, right after the web-OTA block (`// 12b.`), add:

```c
    // 12c. Optional voice input (microphone, double clap, speech to text). Never fatal.
    esp_err_t voice_err = voice_start(input_queue);
    if (voice_err != ESP_OK && voice_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Voice unavailable: %s", esp_err_to_name(voice_err));
    }
```

In `main/CMakeLists.txt`:
- add `"audio_capture.c"`, `"stt_client.c"` and `"voice.c"` after `"stt_reply.c"`;
- add `esp_adc` to `REQUIRES` after `esp_http_server`.

- [ ] **Step 6: Local board defaults (token never printed)**

```bash
cd ~/Projects/zclaw && git check-ignore -q sdkconfig.defaults.esp32c6 && \
  if ! grep -q '^CONFIG_ZCLAW_VOICE_ENABLE=' sdkconfig.defaults.esp32c6; then \
    T=$(ssh jianpedi@107.173.67.243 'sed -n "s/^STT_TOKEN=//p" ~/fidel-stt/.env'); \
    printf 'CONFIG_ZCLAW_VOICE_ENABLE=y\nCONFIG_ZCLAW_MIC_GPIO=0\nCONFIG_ZCLAW_STT_URL="https://bolita-cubana.duckdns.org/stt/transcribe"\nCONFIG_ZCLAW_STT_TOKEN="%s"\n' "$T" >> sdkconfig.defaults.esp32c6; \
    unset T; fi; \
  grep -c '^CONFIG_ZCLAW_STT_TOKEN="[0-9a-f]\{48\}"$' sdkconfig.defaults.esp32c6
```

Expected: `1`.

- [ ] **Step 7: Build both configurations**

Run:
```bash
source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && idf.py reconfigure > build/idf-reconf.log 2>&1 && idf.py build > build/idf-build.log 2>&1; grep -E "Project build complete|error:" build/idf-build.log | head -5; grep -E '^CONFIG_ZCLAW_VOICE_ENABLE=y$' sdkconfig; grep "binary size" build/idf-build.log | tail -1
```

Expected: `Project build complete`, `CONFIG_ZCLAW_VOICE_ENABLE=y`, and a binary well inside the 0x170000 slot.

Then build the configuration that only uses the tracked defaults (voice, display and SD off):
```bash
SCRATCH=/private/tmp/claude-501/-Users-jianpedi-Projects/e49efb90-9a94-4db4-b892-c83271a5df4b/scratchpad
mkdir -p "$SCRATCH/alt" && cp sdkconfig.defaults "$SCRATCH/alt/defaults" && \
idf.py -B "$SCRATCH/alt/build" -D SDKCONFIG="$SCRATCH/alt/sdkconfig" -D SDKCONFIG_DEFAULTS="$SCRATCH/alt/defaults" -D IDF_TARGET=esp32c6 build > "$SCRATCH/alt/build.log" 2>&1; grep -E "Project build complete|error:" "$SCRATCH/alt/build.log" | sort -u | head -3
```

Expected: `Project build complete`.

Run the host gate. Expected: `host gate OK`.

- [ ] **Step 8: Commit**

```bash
git add main/audio_capture.h main/audio_capture.c main/stt_client.h main/stt_client.c main/voice.c main/Kconfig.projbuild main/gpio_policy.c main/main.c main/CMakeLists.txt
git commit -m "feat(voice): ADC capture, STT upload and the voice task (double clap -> Dijiste:)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Board verification and tuning

**Files:** none planned. A threshold change edits `sdkconfig.defaults.esp32c6` (local) or the Kconfig defaults, and is recorded as a ruling.

The user watches the screen. Ask them before each visual check.

Helpers:
- `SCRATCH=/private/tmp/claude-501/-Users-jianpedi-Projects/e49efb90-9a94-4db4-b892-c83271a5df4b/scratchpad`.
- `python "$SCRATCH/serial_ask.py" /dev/cu.usbmodem21201 "<text>" <s>` sends a line and waits for the turn's METRIC line.
- `serial_listen.py` reads without sending.
- `serial_cmd.py` sends a line and listens for a fixed time.
- All three need the ESP-IDF Python: `source ~/esp/esp-idf/export.sh`.

- [ ] **Step 1: Flash (ask first)**

Ask: "¿Flasheo por USB el firmware con la voz?" On a yes, flash (`idf.py -p /dev/cu.usbmodem21201 flash`). Then capture 30 s of the monitor, as in Delivery A.

Expected in the log:
- `Microphone on GPIO0 (ADC1 channel 0), 32000 Hz`;
- `Voice ready: double clap to talk`;
- `Connected:` and `Ready! Free heap: …` (write the heap down).

- [ ] **Step 2: Calibrate with `/mic`**

Send `/mic` with `serial_cmd.py … "/mic" 22`. Watch the `mic:` log lines in silence, then ask the user to speak at about 1 m.

Expected:
- `adc` around 2048 (1700–2400);
- `lost 0` or steady;
- noise below −450 (−45 dBFS) with WiFi on;
- speech peaks around −60 without clipping.

Ask the user to turn the MAX4466 potentiometer until that holds, then repeat `/mic` to confirm. Ledger the numbers. If the noise floor stays above −40 dBFS with the gain at minimum, **stop and ask**: the ADC sees WiFi or board noise, and the spec's §3 risk has come true.

- [ ] **Step 3: Double clap**

The user claps twice at 1–2 m. Expected: the listening face (or neutral) with a moving amber bar, and the red LED.

Then check that these do **not** open listening:
- a single clap;
- normal speech;
- music from a phone.

If claps are missed or noise triggers listening, adjust `CONFIG_ZCLAW_CLAP_LEVEL_PCT` or `CONFIG_ZCLAW_CLAP_RATIO` in the local defaults. Then run `idf.py reconfigure`, build, update by web OTA, and record a ruling.

- [ ] **Step 4: End to end**

Double clap, then ask aloud: "¿Qué hora es en La Habana?". Expected:
- the bar moves while speaking;
- about 1.2 s after stopping: thinking face, then the answer page `Dijiste: <transcript>` with the reply;
- the serial log shows `Heard: …`, `stt: HTTP 200`, and the agent turn.

Also check `/escucha`: listening opens without claps.

While a long LLM turn is running, double clap. Expected: nothing happens (Review Focus 4).

- [ ] **Step 5: Silence after the claps**

Double clap and stay quiet. Expected: after about 4 s the face returns, `No speech: cancelled` appears in the log, and the VPS gets no request: `ssh "$HOST" 'ls -l --time-style=+%T ~/fidel-stt/last.wav'` shows an unchanged time.

- [ ] **Step 6: VPS down**

```bash
ssh jianpedi@107.173.67.243 'systemctl --user stop fidel-stt'
```

Double clap and speak. Expected: within about 15 s, the notice "Sin conexión con el servicio de voz" for 3 s, then the face. A later double clap opens listening again.

Restore the service: `ssh jianpedi@107.173.67.243 'systemctl --user start fidel-stt && sleep 2 && curl -fsS http://127.0.0.1:9003/health'` → `ok`.

- [ ] **Step 7: Heap and the listening photo**

`/status` after a voice turn: write down the free and minimum heap. The minimum must stay above 20 KB; otherwise, ledger a finding.

If the user provides `listening.png`:
1. Run `scripts/face_assets.py` with the imgenv Python (`--input <folder> --output <folder>`).
2. Ask the user to copy `listening.r565` into `/face` on the SD card.
3. Double clap. Expected: the photo shows with the bar.

- [ ] **Step 8: Record**

Append the results of steps 1–7, the final thresholds and the `/mic` numbers to the ledger.

