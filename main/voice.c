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
        // Hand the unused tail of the buffer back before the TLS handshake, the
        // turn's heap low point. The audio task no longer touches s_buf (SENDING).
        uint8_t *fit = realloc(s_buf, s_buf_len);
        if (fit) {
            s_buf = fit;
        }
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
