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
