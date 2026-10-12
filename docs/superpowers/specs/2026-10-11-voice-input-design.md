# Voice project — Deliveries B+C: double clap, listening, speech to text

Date: 2026-10-11
Branch: `fidel`
Status: design approved in conversation, pending written-spec review
Depends on: Delivery A (`2026-10-11-screen-answers-design.md`), already on the branch

## 1. Goal

Fidel's board is talked to by voice:

1. Two claps open listening.
2. The board records until the speaker stops.
3. The audio goes to a small service on the VPS, which transcribes it with an NVIDIA hosted speech model.
4. The text goes into the agent as if it were typed, and the answer shows on the screen under "Dijiste: …".

The user chose to build B (capture) and C (transcription) together.

Success criteria:

- **Clap trigger.** Two claps 0.15–0.7 s apart, at normal room distance, open listening: listening face, level bar and red LED. Speech, music and a single clap do not trigger it.
- **End of speech.** Listening ends on its own about 1.2 s after the speaker stops (10 s maximum). If nobody speaks within 4 s it is cancelled without sending anything.
- **End to end.** A spoken question in Spanish is answered on the screen. The header shows "Dijiste: <transcript>" and the answer pages as in Delivery A.
- **Calibration.** `/mic` shows a live level meter and the numbers needed to set the MAX4466 gain potentiometer.
- **Errors.** A VPS or network failure shows a short message on the screen and the face returns. The firmware keeps working.
- **Other builds.** With `CONFIG_ZCLAW_VOICE_ENABLE=n` (the default) other boards behave exactly as before.

## 2. Scope

In scope:
- board audio capture;
- double-clap detection;
- end-of-speech detection;
- IMA-ADPCM compression;
- HTTPS upload;
- the VPS STT service;
- agent and screen integration;
- the listening face, level bar and red LED;
- `/mic` calibration;
- `/escucha` for tests.

Out of scope:
- a wake word;
- a speaker or text to speech;
- streaming while speaking;
- recordings on the SD card;
- several languages at once;
- running models on the VPS (it only calls NVIDIA's hosted API).

## 3. Hardware (as of this spec)

- **Microphone.** MAX4466 module: VCC on 3V3, OUT on GPIO0 (ADC1_CH0). Its output sits at about VCC/2 with the audio on top. The gain is the module's potentiometer.
- **Pins.** GPIO3 and GPIO4 stay board-reserved (see Delivery A). GPIO0 becomes reserved for the microphone when voice is enabled, so the GPIO tools cannot drive it.
- **Risk: ADC noise.** The ESP32-C6 has a single ADC, shared by all channels, and WiFi radio noise may reach it. The first board step measures the noise floor with `/mic` before anything else is tuned (§11).

## 4. Architecture

```
GPIO0 ─ ADC1 continuous, 32 kHz, DMA ─► audio_capture (task)
          averages pairs → 16 kHz, removes DC, int16, frames of 10 ms
              │                         └► audio_level (peak, RMS, dBFS, clipping)
              ▼
            voice (task, state machine)
              IDLE ──clap_detect: double clap──► LISTENING
              LISTENING: endpoint decides start/end of speech
                         adpcm encodes into an 80 KB heap buffer
                         display: listening face + level bar; LED red
              LISTENING ──end of speech──► SENDING (thinking face)
              SENDING: HTTPS POST <STT_URL> (token) ─► VPS server/stt
                         ADPCM → PCM16 → NVIDIA Parakeet (es-US) → {"text"}
              SENDING ──text──► agent input queue (source VOICE) ──► IDLE
                        └─ empty / error ──► message page ──► IDLE
```

Pure modules (no ESP-IDF, host-tested):
- `audio_level` (frame statistics and dBFS);
- `clap_detect`;
- `endpoint`;
- `adpcm` (encoder).

ESP-only modules:
- `audio_capture` (ADC continuous driver);
- `voice` (states, upload, screen and LED hooks).

VPS: `server/stt` (Python).

## 5. Audio capture and level

- **ADC.** `adc_continuous` on ADC1_CH0, 12-bit, 12 dB attenuation, 32 kHz.
- **Resampling.** Each pair of samples is averaged into one 16 kHz sample. This is a crude low-pass that reduces aliasing and noise.
- **DC removal.** A one-pole high-pass, about 20 Hz, removes the offset. The result is scaled from 12-bit to int16.
- **Frames.** Every 10 ms (160 samples) the task hands a frame to `voice`.
- **Task.** Its own task, priority above the display task, so the DMA buffers never overflow. The driver uses 4 conversion frames of 10 ms.
- **`audio_level`** (pure) computes per frame:
  - peak;
  - RMS;
  - level in dBFS: 20·log10(RMS/32768), floored at −90;
  - count of clipped samples (|x| ≥ 32000);
  - DC offset of the raw ADC values (averaged by the capture task, for `/mic`).
- **Lifetime.** Capture runs whenever voice is enabled. Frames are ignored while the board is thinking or sending.

## 6. Double-clap detector (`clap_detect`, pure)

Input: one 10 ms frame's peak and RMS.

**Noise floor.** A slow average of RMS over frames without a clap or speech.

**One clap.** A frame whose peak is at least:
- `CLAP_LEVEL_PCT` % of full scale (default 40); and
- `CLAP_RATIO` times the noise floor (default 6).

The level must also fall below half of that peak within 80 ms. Speech and music stay loud longer, so they fail this test.

**Double clap.** A second clap starts 150–700 ms after the first, and no third clap comes in the 300 ms after the second. Applause is several claps, so the third clap rejects it.

The detector reports `CLAP_DOUBLE` 300 ms after the second clap. Listening starts there, so the claps are not in the recording.

Detection is active only in IDLE: on the face, an answer page, a dashboard or the status page. A double clap is a new activity, so it cancels any requested page (Delivery A rule).

## 7. End of speech (`endpoint`, pure)

Input: one frame's RMS, plus the noise floor measured in IDLE just before listening started.

- **Speech starts** when the RMS exceeds max(`SPEECH_RATIO` × noise, a minimum level) for 6 frames in a row (60 ms). `SPEECH_RATIO` defaults to 3.
- **Speech ends** after 120 frames (1.2 s) below that threshold.
- **Limits:**
  - 1000 frames (10 s) from the start of listening end it in any case;
  - with no speech start within 400 frames (4 s), listening is cancelled.
- **Result:** `ENDPOINT_CONTINUE`, `ENDPOINT_DONE` or `ENDPOINT_NO_SPEECH`.

Audio is recorded from the start of listening. Leading silence of up to 4 s costs at most 32 KB of ADPCM and does not change the transcript.

## 8. Compression and protocol

**IMA-ADPCM (`adpcm`, pure).** The standard IMA step and index tables: 4 bits per sample, 8 KB/s at 16 kHz. The encoder keeps its predictor and index in a small state struct.

**Stream format** (one HTTP body):

| Bytes | Content |
|---|---|
| 0–1 | initial predictor, int16 little-endian |
| 2 | initial step index (0–88) |
| 3 | 0 (reserved) |
| 4… | 4-bit codes, the first sample in the low nibble |

The sample count is (body length − 4) × 2.

**Buffer.** One heap block of 4 + 80000 bytes (10 s), allocated when listening starts. It is freed right after the upload, before the text reaches the agent, so the agent's LLM request has the memory back.

**Request:**

```
POST <CONFIG_ZCLAW_STT_URL>
X-Stt-Token: <CONFIG_ZCLAW_STT_TOKEN>
Content-Type: application/octet-stream
X-Audio-Format: ima-adpcm; rate=16000
```

- HTTPS through the existing HTTP gate and the certificate bundle. Telegram is gone, so nothing holds the gate for long.
- Timeout: 15 s.

**Response:**

| Code | Body or meaning |
|---|---|
| 200 | `{"text": "<transcript>", "ms": <server time>}` |
| 400 | invalid format |
| 401 | wrong token |
| 413 | body larger than 120 KB |
| 502 | NVIDIA failure |
| 503 | busy |

## 9. Voice state machine and screen

`voice` runs in its own task, consuming the frames from §5.

| State | Screen | LED | Leaves on |
|---|---|---|---|
| IDLE | whatever is shown | as is | double clap or `/escucha` → LISTENING |
| LISTENING | `listening` face + level bar | red, solid | done → SENDING; no speech → IDLE |
| SENDING | thinking face | thinking animation | text → agent; error → IDLE |

**Listening face.** New expression `listening`: a full frame on the SD card, made with `scripts/face_assets.py`, which learns the name. Without the photo, the neutral face is used.

**Level bar.** An 8-pixel band at the bottom of the screen (y 232–239). Its width follows the frame level from −60 dBFS (empty) to 0 dBFS (full). It is redrawn at most every 50 ms over the face, from a level value the voice task stores atomically, so no queue events are used. The face page is forced while listening; listening counts as activity, like thinking.

**Error pages.** These are message pages of 3 s, then the face returns:
- "No te entendí": empty transcript;
- "Sin conexión con el servicio de voz": network, timeout or 5xx;
- "Error de voz (<code>)": 4xx.

**Busy rules.**
- While the agent is processing a message (thinking), claps are ignored and `/escucha` answers "ocupado".
- A message that arrives over serial while listening is processed normally after the voice turn: the agent queue is FIFO.

## 10. Agent integration

- New source `MSG_SOURCE_VOICE = 3` in `messages.h`. `voice` pushes the transcript (clipped to the input buffer, 511 bytes) into the agent input queue.
- The answer header for this source is `Dijiste:`.
- The prompt is the transcript, and is not treated as a cron trigger.
- Voice messages skip the repeated-message suppression: saying the same question twice is answered twice.
- Replies are not logged in the task log (Delivery A §7b logs cron only).

## 11. Calibration: `/mic`

The serial command `/mic` shows a requested page `PAGE_MIC` for 20 s, refreshed every 250 ms:
- a large level bar;
- peak and RMS in dBFS;
- the noise floor;
- the percentage of clipped samples;
- the raw ADC offset (it should be close to mid-scale, ~2048).

The same numbers go to serial once per second.

Guidance, also shown on the page:
- speech at 1 m should peak around −6 dBFS with 0 % clipping;
- the noise floor should be below −45 dBFS.

The clap and speech thresholds are Kconfig options. They are tuned once from these readings and shipped by web OTA.

## 12. VPS service `server/stt`

- **Code.** Python 3, stdlib `http.server` (`ThreadingHTTPServer`), as `server/dash`, plus `nvidia-riva-client` in a venv at `~/fidel-stt/venv`.
- **Service.** systemd user unit `fidel-stt` on 127.0.0.1:9003.
- **Caddy.** `handle_path /stt/*` in the HTTPS site that already serves `/dash`. The user applies that change with sudo.
- **Configuration** in `~/fidel-stt/.env`:

  | Variable | Default |
  |---|---|
  | `STT_TOKEN` | — |
  | `NVIDIA_API_KEY` | — |
  | `RIVA_FUNCTION_ID` | Parakeet 1.1b RNNT multilingual, `71203149-d3b7-4460-8231-1be2543a1fca` |
  | `RIVA_LANGUAGE` | `es-US` |
  | `RIVA_SERVER` | `grpc.nvcf.nvidia.com:443` |

  The model and function-id are confirmed with a real call from the VPS before any other step. `canary-1b-asr` (also Spanish) is the fallback if Parakeet is unavailable.
- **`POST /stt`:**
  1. Check the token with `hmac.compare_digest`.
  2. Check `X-Audio-Format` and the body size (≤ 120 KB, ≥ 4 + 800 bytes, i.e. 0.1 s).
  3. Decode the ADPCM to PCM16.
  4. Call `offline_recognize` (LINEAR_PCM, 16 kHz, mono, automatic punctuation), with a 12 s deadline.
  5. Join the best alternatives into one line.
  6. Return the JSON.
- **Limits.** One transcription at a time; a second gets 503.
- **Last recording.** Each request overwrites `~/fidel-stt/last.wav` (PCM16), so the user can listen to the latest recording while tuning the gain (`scp`). No other audio is kept.
- **`GET /health`:** `ok`.

## 13. Configuration (board)

| Option | Default | Fidel's board |
|---|---|---|
| `ZCLAW_VOICE_ENABLE` | n | y |
| `ZCLAW_MIC_GPIO` | 0 | 0 |
| `ZCLAW_STT_URL` | "" | `https://` + the HTTPS host that serves `/dash` today + `/stt` |
| `ZCLAW_STT_TOKEN` | "" | secret (git-ignored defaults) |
| `ZCLAW_CLAP_LEVEL_PCT` | 40 | tuned |
| `ZCLAW_CLAP_RATIO` | 6 | tuned |
| `ZCLAW_SPEECH_RATIO` | 3 | tuned |

Voice does not depend on the display: without it, the answers simply go to serial.

## 14. Resource budget

- **RAM:**
  - fixed: ~6 KB (ADC driver buffers, audio task stack ~3 KB, voice task stack ~4 KB);
  - while listening: 80 KB;
  - while sending: 80 KB + ~35–45 KB TLS.
  - Today 175 KB are free, and the buffer is freed before the agent's LLM call.
- **CPU:** 32 k ADC samples/s with simple integer processing, a few percent of the 160 MHz core.
- **Flash:** a few KB; esp_http_client and TLS are already linked.

## 15. Error handling

- **ADC driver fails to start:** voice is disabled with a log line; the rest boots normally.
- **No heap for the 80 KB buffer:** "Error de voz (memoria)" and back to IDLE.
- **ADC overflow (frames lost):** counted and shown by `/mic`; a lost frame inside a recording is skipped, not fatal.
- **VPS errors:** §9 error pages. The audio is discarded, never retried automatically.
- **Transcript too long:** clipped at the agent input size.
- **Wrong or missing STT URL or token:** "Error de voz (configuración)" the first time listening ends; logged at boot.

## 16. Testing

Host, C (TDD):
- `audio_level`: silence (−90), full-scale sine (≈ −3 dBFS), clipping count, peak.
- `clap_detect`:
  - single clap: none;
  - double clap at 300 ms: `CLAP_DOUBLE` reported 300 ms after the second;
  - gaps of 100 ms and 900 ms: none;
  - three claps: none;
  - sustained loud speech-like noise: none;
  - noise floor adaptation.
- `endpoint`: speech then silence ends after 1.2 s; no speech within 4 s gives `NO_SPEECH`; continuous speech stops at 10 s; short blips under 60 ms do not start speech.
- `adpcm`:
  - a golden vector (a fixed synthetic signal and its expected bytes, shared with the Python tests);
  - round trip through a test-only decoder with SNR above 20 dB on a speech-like signal;
  - header layout.
- Agent: a VOICE message shows `Dijiste:` with the transcript as the prompt, and a repeated VOICE message is processed twice.

Python (VPS):
- the decoder matches the golden vector;
- the WAV writer;
- request validation: token, size, format;
- the NVIDIA call is replaced by a stub.

On the board:
1. `/mic`: offset, noise floor with WiFi on, speech level; set the gain.
2. Double clap at 1–2 m triggers; speech, music and a single clap do not.
3. A spoken question is answered with "Dijiste: …".
4. Silence after the claps cancels without sending.
5. With the VPS service stopped, the error page shows and the face returns.
6. Heap while listening and sending.
7. The build with voice disabled is unchanged.
