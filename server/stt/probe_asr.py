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
