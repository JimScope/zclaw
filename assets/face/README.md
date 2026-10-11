# Face source photos

Put the source photos for Fidel's face here (they are git-ignored). Then run:

    python3 -m venv ~/.venvs/zclaw-face
    ~/.venvs/zclaw-face/bin/pip install -r scripts/requirements-face.txt
    ~/.venvs/zclaw-face/bin/python scripts/face_assets.py --input assets/face --output build/face

Copy `build/face/*.r565` into a `face/` folder at the root of the SD card and
review `build/face/contact.png`.

| File | Use |
|---|---|
| `neutral` (required) | Idle face; base for the patches |
| `eyes_closed` | Same photo with eyes closed → blink patch |
| `speaking` (+ `speaking_2`…) | Mouth open → talking patch |
| `thinking` (+ variants) | While the LLM works |
| `sleeping` | WiFi down |
| `happy`, `angry`, `speech` | Expressions for the LLM (Delivery 3) |

Guidelines: same base photo for all expressions, only the expression edited
(head, framing, background and light unchanged); square-ish close-up with the
face filling ~70% of the frame; plain background. If the automatic eye/mouth
detection picks the wrong area, pass `--eyes X,Y,W,H` / `--mouth X,Y,W,H`
(coordinates in the 240×240 output).
