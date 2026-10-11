#!/bin/bash
# Deploy the dashboard service to the VPS as a systemd --user unit (no sudo).
# Caddy must route /dash/* to 127.0.0.1:9002 (see server/dash/README.md).
set -euo pipefail

HOST="${DASH_HOST:-jianpedi@107.173.67.243}"
SRC="$(cd "$(dirname "$0")/../server/dash" && pwd)"

ssh "$HOST" 'mkdir -p ~/fidel-dash ~/.config/systemd/user'
scp -q "$SRC"/dash.py "$SRC"/server.py "$SRC"/requirements.txt "$HOST":fidel-dash/
scp -q -r "$SRC"/fonts "$HOST":fidel-dash/
scp -q "$SRC"/fidel-dash.service "$HOST":.config/systemd/user/fidel-dash.service
ssh "$HOST" 'cd ~/fidel-dash \
  && { [ -x venv/bin/python ] || python3 -m venv venv; } \
  && venv/bin/pip install -q -r requirements.txt \
  && systemctl --user daemon-reload \
  && systemctl --user enable --now fidel-dash \
  && systemctl --user restart fidel-dash \
  && sleep 1 && curl -fsS http://127.0.0.1:9002/health && echo " <- fidel-dash up"'
