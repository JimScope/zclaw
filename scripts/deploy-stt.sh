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
