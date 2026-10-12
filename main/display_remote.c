#include "display_remote.h"

#include "config.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#include <stdlib.h>
#include <string.h>

#include "display_hw.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_gate.h"
#include "jpeg_strip.h"
#include "rom_tjpgd.h"

#define FETCH_TASK_STACK   8192
#define FETCH_TASK_PRIO    2
#define FETCH_GATE_WAIT_MS 40000
#define FETCH_TIMEOUT_MS   15000
#define DECODE_POOL_BYTES  4096
#define URL_MAX            200

static const char *TAG = "display_remote";

typedef struct {
    char url[URL_MAX];
    uint32_t request_id;
    display_remote_done_fn done;
} fetch_job_t;

static volatile bool s_busy = false;

static uint8_t *download(const char *url, size_t *out_len, bool *server_error)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = FETCH_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        // An http:// URL redirected to https:// would do TLS outside the gate.
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    uint8_t *buf = NULL;
    size_t len = 0;

    if (!client) {
        return NULL;
    }
    if (CONFIG_ZCLAW_DASH_TOKEN[0] != '\0') {
        esp_http_client_set_header(client, "X-Dash-Token", CONFIG_ZCLAW_DASH_TOKEN);
    }
    if (esp_http_client_open(client, 0) != ESP_OK) {
        goto done;
    }
    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    char *dash_error = NULL;
    if (server_error && esp_http_client_get_header(client, "X-Dash-Error", &dash_error) == ESP_OK &&
        dash_error) {
        *server_error = true;  // the body is an error card (still shown)
    }
    if (status != 200 || content_length <= 0 || content_length > DISPLAY_REMOTE_MAX_BYTES) {
        ESP_LOGW(TAG, "Dashboard HTTP %d, %ld bytes", status, (long)content_length);
        goto done;
    }
    buf = malloc((size_t)content_length);
    if (!buf) {
        goto done;
    }
    while (len < (size_t)content_length) {
        int n = esp_http_client_read(client, (char *)buf + len, (int)((size_t)content_length - len));
        if (n <= 0) {
            break;
        }
        len += (size_t)n;
    }
    if (len != (size_t)content_length) {
        free(buf);
        buf = NULL;
    }

done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    *out_len = buf ? len : 0;
    return buf;
}

uint8_t *display_remote_download(const char *url, size_t *out_len, bool *server_error)
{
    uint8_t *jpeg = NULL;

    *out_len = 0;
    if (server_error) {
        *server_error = false;
    }
    if (strncmp(url, "https://", 8) == 0) {
        // TLS costs ~40 KB, so it goes through the HTTP gate.
        if (http_gate_acquire("dashboard", pdMS_TO_TICKS(FETCH_GATE_WAIT_MS))) {
            jpeg = download(url, out_len, server_error);
            http_gate_release();
        }
    } else {
        // Plain HTTP needs a few KB: no gate.
        jpeg = download(url, out_len, server_error);
    }
    return jpeg;
}

static void fetch_task(void *arg)
{
    fetch_job_t *job = arg;
    size_t len = 0;
    uint8_t *jpeg = display_remote_download(job->url, &len, NULL);

    if (!jpeg) {
        ESP_LOGW(TAG, "Dashboard download failed: %s", job->url);
    }
    job->done(job->request_id, jpeg, len);
    free(job);
    s_busy = false;
    vTaskDelete(NULL);
}

bool display_remote_fetch(const char *url, uint32_t request_id, display_remote_done_fn done)
{
    fetch_job_t *job;

    if (s_busy || !url || !done || strlen(url) >= URL_MAX) {
        return false;
    }
    job = calloc(1, sizeof(*job));
    if (!job) {
        return false;
    }
    strcpy(job->url, url);
    job->request_id = request_id;
    job->done = done;
    s_busy = true;
    if (xTaskCreate(fetch_task, "dash_fetch", FETCH_TASK_STACK, job, FETCH_TASK_PRIO, NULL) != pdPASS) {
        s_busy = false;
        free(job);
        return false;
    }
    return true;
}

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    jpeg_strip_t strip;
} decode_ctx_t;

static UINT jpeg_input(JDEC *jd, BYTE *buf, UINT nbyte)
{
    decode_ctx_t *ctx = jd->device;
    size_t avail = ctx->len - ctx->pos;
    if (nbyte > avail) {
        nbyte = (UINT)avail;
    }
    if (buf) {
        memcpy(buf, ctx->data + ctx->pos, nbyte);
    }
    ctx->pos += nbyte;
    return nbyte;
}

static UINT jpeg_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    decode_ctx_t *ctx = jd->device;
    return jpeg_strip_block(&ctx->strip, rect->left, rect->top, rect->right, rect->bottom, bitmap)
               ? 1 : 0;
}

static uint16_t *strip_get(void *arg)
{
    (void)arg;
    return display_hw_acquire_buffer();
}

static void strip_flush(void *arg, int y, int rows, uint16_t *pixels)
{
    (void)arg;
    display_hw_blit(0, y, DISPLAY_W, rows, pixels);
}

esp_err_t display_remote_draw(const uint8_t *jpeg, size_t len)
{
    JDEC jd;
    decode_ctx_t ctx = { .data = jpeg, .len = len, .pos = 0 };
    void *pool = malloc(DECODE_POOL_BYTES);
    esp_err_t err = ESP_FAIL;

    if (!pool) {
        return ESP_ERR_NO_MEM;
    }
    jpeg_strip_init(&ctx.strip, DISPLAY_W, DISPLAY_STRIP_ROWS, strip_get, strip_flush, NULL);

    JRESULT res = jd_prepare(&jd, jpeg_input, pool, DECODE_POOL_BYTES, &ctx);
    if (res == JDR_OK && (jd.width != DISPLAY_W || jd.height != DISPLAY_H)) {
        ESP_LOGW(TAG, "Dashboard is %ux%u, expected %dx%d", jd.width, jd.height, DISPLAY_W, DISPLAY_H);
        res = JDR_FMT2;
    }
    if (res == JDR_OK) {
        res = jd_decomp(&jd, jpeg_output, 0);
    }
    if (ctx.strip.buf) {
        display_hw_release_buffer(ctx.strip.buf);  // decode stopped mid-row
    }
    display_hw_wait_idle();
    free(pool);

    if (res == JDR_OK) {
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "JPEG decode failed: %d", (int)res);
    }
    return err;
}

#endif  // CONFIG_ZCLAW_DISPLAY_ENABLE
