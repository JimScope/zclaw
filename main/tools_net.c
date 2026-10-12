#include "tools_handlers.h"
#include "weather.h"
#include "tools_common.h"
#include "config.h"
#include "http_gate.h"
#include "memory.h"
#include "sd_card.h"
#include "web_page.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NET_TOOL_TIMEOUT_MS 15000
#define NET_TOOL_GATE_MS    12000
#define NET_TOOL_BODY_MAX   1024

static const char *TAG = "tools_net";

typedef struct {
    char *buf;
    size_t len;
    size_t max;
    FILE *file;     // when set, the body goes to the file (up to max bytes)
    bool failed;    // a file write came up short; later data is dropped
} net_resp_ctx_t;

static esp_err_t net_http_event(esp_http_client_event_t *evt)
{
    net_resp_ctx_t *ctx = (net_resp_ctx_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !ctx || evt->data_len <= 0) {
        return ESP_OK;
    }
    if (ctx->file) {
        if (ctx->failed) {
            return ESP_OK;
        }
        size_t space = ctx->max - ctx->len;
        size_t n = ((size_t)evt->data_len < space) ? (size_t)evt->data_len : space;
        if (n > 0) {
            if (fwrite(evt->data, 1, n, ctx->file) == n) {
                ctx->len += n;
            } else {
                ctx->failed = true;
            }
        }
    } else if (ctx->buf) {
        size_t space = ctx->max - 1 - ctx->len;
        size_t n = ((size_t)evt->data_len < space) ? (size_t)evt->data_len : space;
        if (n > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, n);
            ctx->len += n;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

// Returns HTTP status, or -1 on transport error. The body goes to ctx (buffer or file).
static int net_http_perform(const char *url,
                            esp_http_client_method_t method,
                            const char *header_key,
                            const char *header_value,
                            const char *post_body,
                            net_resp_ctx_t *ctx)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = net_http_event,
        .user_data = ctx,
        .timeout_ms = NET_TOOL_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return -1;
    }

    esp_http_client_set_method(client, method);
    if (header_key && header_value && header_value[0] != '\0') {
        esp_http_client_set_header(client, header_key, header_value);
    }
    if (post_body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_body, strlen(post_body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return status;
}

// Body (truncated) lands in out.
static int net_http_request(const char *url, esp_http_client_method_t method,
                            const char *header_key, const char *header_value,
                            const char *post_body, char *out, size_t out_max)
{
    net_resp_ctx_t ctx = { .buf = out, .len = 0, .max = out_max, .file = NULL, .failed = false };
    out[0] = '\0';
    return net_http_perform(url, method, header_key, header_value, post_body, &ctx);
}

// GET into an open file (at most max bytes kept). *write_failed is set when a
// file write came up short, in which case the file is incomplete.
static int net_http_get_to_file(const char *url, const char *header_key, const char *header_value,
                                FILE *file, size_t max, bool *write_failed)
{
    net_resp_ctx_t ctx = { .buf = NULL, .len = 0, .max = max, .file = file, .failed = false };
    int status = net_http_perform(url, HTTP_METHOD_GET, header_key, header_value, NULL, &ctx);
    *write_failed = ctx.failed;
    return status;
}

bool tools_sms_send_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *to = cJSON_GetObjectItem(input, "to");
    const cJSON *message = cJSON_GetObjectItem(input, "message");

    if (!to || !cJSON_IsString(to) || to->valuestring[0] == '\0' ||
        !message || !cJSON_IsString(message) || message->valuestring[0] == '\0') {
        snprintf(result, result_len, "Error: 'to' (phone) and 'message' required");
        return false;
    }

    if (CONFIG_ZCLAW_VENDEL_API_KEY[0] == '\0') {
        snprintf(result, result_len, "Error: Vendel API key not configured");
        return false;
    }

    cJSON *payload = cJSON_CreateObject();
    cJSON *recipients = cJSON_CreateArray();
    cJSON_AddItemToArray(recipients, cJSON_CreateString(to->valuestring));
    cJSON_AddItemToObject(payload, "recipients", recipients);
    cJSON_AddStringToObject(payload, "body", message->valuestring);
    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!body) {
        snprintf(result, result_len, "Error: failed to build request");
        return false;
    }

    if (!http_gate_acquire("sms_send", pdMS_TO_TICKS(NET_TOOL_GATE_MS))) {
        free(body);
        snprintf(result, result_len, "Error: HTTP busy, try again");
        return false;
    }

    char resp[NET_TOOL_BODY_MAX];
    int status = net_http_request(CONFIG_ZCLAW_VENDEL_URL, HTTP_METHOD_POST,
                                  "X-API-Key", CONFIG_ZCLAW_VENDEL_API_KEY,
                                  body, resp, sizeof(resp));
    http_gate_release();
    free(body);

    if (status < 0) {
        snprintf(result, result_len, "Error: SMS request failed (network)");
        return false;
    }
    if (status < 200 || status >= 300) {
        snprintf(result, result_len, "Error: Vendel HTTP %d: %.300s", status, resp);
        return false;
    }

    snprintf(result, result_len, "SMS queued to %s (HTTP %d)", to->valuestring, status);
    return true;
}

#define WEB_SEARCH_HTML_MAX 6144

static void url_encode(const char *in, char *out, size_t out_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;

    for (const unsigned char *p = (const unsigned char *)in;
         *p && o + 4 < out_len; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.') {
            out[o++] = (char)*p;
        } else if (*p == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%';
            out[o++] = hex[*p >> 4];
            out[o++] = hex[*p & 0x0F];
        }
    }
    out[o] = '\0';
}

static void html_strip_into(const char *html, char *out, size_t out_len)
{
    size_t o = 0;
    bool in_tag = false;
    bool last_space = true;

    for (const char *p = html; *p && o + 1 < out_len; p++) {
        if (*p == '<') { in_tag = true;  continue; }
        if (*p == '>') { in_tag = false; continue; }
        if (in_tag) continue;

        char c = *p;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ') {
            if (last_space) continue;
            last_space = true;
        } else {
            last_space = false;
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

bool tools_web_search_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *query = cJSON_GetObjectItem(input, "query");

    if (!query || !cJSON_IsString(query) || query->valuestring[0] == '\0') {
        snprintf(result, result_len, "Error: 'query' required");
        return false;
    }

    char encoded[192];
    char url[256];
    url_encode(query->valuestring, encoded, sizeof(encoded));
    snprintf(url, sizeof(url), "https://lite.duckduckgo.com/lite/?q=%s", encoded);

    if (!http_gate_acquire("web_search", pdMS_TO_TICKS(NET_TOOL_GATE_MS))) {
        snprintf(result, result_len, "Error: HTTP busy, try again");
        return false;
    }

    char *html = malloc(WEB_SEARCH_HTML_MAX);
    if (!html) {
        http_gate_release();
        snprintf(result, result_len, "Error: out of memory for search");
        return false;
    }

    int status = net_http_request(url, HTTP_METHOD_GET,
                                  "User-Agent", "Mozilla/5.0 (compatible; zclaw-esp32)",
                                  NULL, html, WEB_SEARCH_HTML_MAX);
    http_gate_release();

    if (status < 0) {
        free(html);
        snprintf(result, result_len, "Error: search request failed (network)");
        return false;
    }
    if (status < 200 || status >= 300) {
        free(html);
        snprintf(result, result_len, "Error: search HTTP %d", status);
        return false;
    }

    char text[480];
    html_strip_into(html, text, sizeof(text));
    snprintf(result, result_len, "Search results (truncated): %s", text);
    free(html);
    return true;
}

// read_web: fetch a page as clean text via the self-hosted reader on the VPS and
// hand it to the model in WEB_PAGE_SIZE pages. With an SD card the whole text
// (up to 64 KB) is kept in /sdcard/web, so paging costs one download; without
// one, every call downloads up to 4 KB into the heap and frees it.
#define READ_WEB_SD_MAX   (64 * 1024)
#define READ_WEB_RAM_MAX  4096
#define READ_WEB_DIR      SD_MOUNT_POINT "/web"
#define READ_WEB_TEXT     READ_WEB_DIR "/page.txt"
#define READ_WEB_URL      READ_WEB_DIR "/url.txt"

static void reader_url_for(const char *url, char *out, size_t out_len)
{
    snprintf(out, out_len, "https://bolita-cubana.duckdns.org/reader/%s", url);
}

static bool sd_cached_url_is(const char *url)
{
    char cached[256] = {0};
    FILE *f = fopen(READ_WEB_URL, "r");
    if (!f) {
        return false;
    }
    size_t n = fread(cached, 1, sizeof(cached) - 1, f);
    fclose(f);
    cached[n] = '\0';
    return strcmp(cached, url) == 0;
}

typedef enum {
    SD_FETCH_OK,         // *status/*text_len are valid (status may be an HTTP/transport error)
    SD_FETCH_FILE_ERROR  // the SD file could not be written or read; use the RAM path
} sd_fetch_result_t;

static size_t sd_text_len(bool *ok)
{
    struct stat st;
    *ok = stat(READ_WEB_TEXT, &st) == 0;
    return *ok ? (size_t)st.st_size : 0;
}

// Fetches into the SD cache. HTTP and transport outcomes land in *status; an SD
// file problem returns SD_FETCH_FILE_ERROR and url.txt is not written, so an
// incomplete file is never served. *io_error is set for a real I/O error (short
// write or failed close), not for a plain fopen failure.
static sd_fetch_result_t sd_fetch(const char *url, int *status, size_t *text_len, bool *io_error)
{
    char reader_url[256];
    reader_url_for(url, reader_url, sizeof(reader_url));
    mkdir(READ_WEB_DIR, 0755);  // EEXIST is fine
    remove(READ_WEB_URL);       // no stale URL if the download fails
    FILE *f = fopen(READ_WEB_TEXT, "w");
    if (!f) {
        return SD_FETCH_FILE_ERROR;
    }
    bool write_failed = false;
    *status = net_http_get_to_file(reader_url, "User-Agent", "Mozilla/5.0 (zclaw-esp32)",
                                   f, READ_WEB_SD_MAX, &write_failed);
    bool close_failed = fclose(f) != 0;
    if (write_failed || close_failed) {
        *io_error = true;
        return SD_FETCH_FILE_ERROR;
    }
    if (*status >= 200 && *status < 300) {
        bool ok;
        *text_len = sd_text_len(&ok);  // same source as cached calls, so page counts agree
        if (!ok) {
            return SD_FETCH_FILE_ERROR;
        }
        FILE *u = fopen(READ_WEB_URL, "w");
        if (u) {
            fputs(url, u);
            fclose(u);
        }
    }
    return SD_FETCH_OK;
}

static void format_page(const web_page_slice_t *s, const char *text, char *result, size_t result_len)
{
    if (s->page < s->total_pages) {
        snprintf(result, result_len, "[pag %d/%d, pide page=%d para seguir]\n%s",
                 s->page, s->total_pages, s->page + 1, text);
    } else {
        snprintf(result, result_len, "[pag %d/%d, fin]\n%s", s->page, s->total_pages, text);
    }
}

bool tools_read_web_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *url = cJSON_GetObjectItem(input, "url");
    const cJSON *page_json = cJSON_GetObjectItem(input, "page");
    int page = (page_json && cJSON_IsNumber(page_json)) ? page_json->valueint : 1;

    if (!url || !cJSON_IsString(url) ||
        (strncmp(url->valuestring, "http://", 7) != 0 &&
         strncmp(url->valuestring, "https://", 8) != 0)) {
        snprintf(result, result_len, "Error: 'url' required (http/https)");
        return false;
    }

    char chunk[WEB_PAGE_SIZE + 1];
    web_page_slice_t s;
    int status = 200;
    bool done = false;

    if (sd_card_begin()) {
        size_t len = 0;
        bool io_error = false;
        bool file_error = false;
        if (sd_cached_url_is(url->valuestring)) {
            bool ok;
            len = sd_text_len(&ok);
            file_error = !ok;
        } else {
            if (!http_gate_acquire("read_web", pdMS_TO_TICKS(NET_TOOL_GATE_MS))) {
                sd_card_end();
                snprintf(result, result_len, "Error: HTTP busy, try again");
                return false;
            }
            file_error = sd_fetch(url->valuestring, &status, &len, &io_error) == SD_FETCH_FILE_ERROR;
            http_gate_release();
        }
        s = web_page_slice(len, page, WEB_PAGE_SIZE);
        chunk[0] = '\0';
        if (!file_error && status >= 200 && status < 300 && !s.past_end && s.len > 0) {
            FILE *f = fopen(READ_WEB_TEXT, "r");
            size_t n = 0;
            if (f) {
                if (fseek(f, (long)s.start, SEEK_SET) == 0) {
                    n = fread(chunk, 1, s.len, f);
                }
                fclose(f);
            }
            chunk[n] = '\0';
            if (n != s.len) {
                file_error = true;
                io_error = io_error || (f != NULL);
            }
        }
        if (io_error) {
            sd_card_mark_failed(esp_timer_get_time() / 1000);
        }
        sd_card_end();
        done = !file_error;
    }

    // No card, a pulled card or an SD file failure: download into the heap.
    if (!done) {
        char *text = malloc(READ_WEB_RAM_MAX);
        if (!text) {
            snprintf(result, result_len, "Error: out of memory for read_web");
            return false;
        }
        char reader_url[256];
        reader_url_for(url->valuestring, reader_url, sizeof(reader_url));
        if (!http_gate_acquire("read_web", pdMS_TO_TICKS(NET_TOOL_GATE_MS))) {
            free(text);
            snprintf(result, result_len, "Error: HTTP busy, try again");
            return false;
        }
        status = net_http_request(reader_url, HTTP_METHOD_GET, "User-Agent",
                                  "Mozilla/5.0 (zclaw-esp32)", NULL, text, READ_WEB_RAM_MAX);
        http_gate_release();
        s = web_page_slice(status >= 200 && status < 300 ? strlen(text) : 0, page, WEB_PAGE_SIZE);
        memcpy(chunk, text + s.start, s.len);
        chunk[s.len] = '\0';
        free(text);
    }

    if (status < 0) {
        snprintf(result, result_len, "Error: reader request failed (network)");
        return false;
    }
    if (status < 200 || status >= 300) {
        snprintf(result, result_len, "Error: reader HTTP %d", status);
        return false;
    }
    if (s.past_end) {
        snprintf(result, result_len, "Fin del contenido (%d paginas en total)", s.total_pages);
        return true;
    }
    format_page(&s, chunk, result, result_len);
    return true;
}

// Maps Open-Meteo WMO weather codes to short Spanish descriptions.
static const char *wmo_desc(int code)
{
    if (code == 0) return "despejado";
    if (code <= 2) return "parcialmente nublado";
    if (code == 3) return "nublado";
    if (code <= 48) return "niebla";
    if (code <= 57) return "llovizna";
    if (code <= 67) return "lluvia";
    if (code <= 77) return "nieve";
    if (code <= 82) return "chubascos";
    if (code <= 99) return "tormenta";
    return "desconocido";
}

// Pulls a float that follows a marker in strings like "lat20.02lon-75.83".
static bool parse_after(const char *s, const char *marker, float *out)
{
    const char *p = strstr(s, marker);
    if (!p) {
        return false;
    }
    *out = strtof(p + strlen(marker), NULL);
    return true;
}

bool weather_fetch_current(bool has_coords, float lat, float lon, TickType_t gate_wait,
                           weather_current_t *out, char *err, size_t err_len)
{
    if (!has_coords) {
        // Fall back to the saved location memory (e.g. "lat20.02lon-75.83").
        char loc[64] = {0};
        if (!memory_get("u_ubicacion", loc, sizeof(loc)) ||
            !parse_after(loc, "lat", &lat) || !parse_after(loc, "lon", &lon)) {
            snprintf(err, err_len,
                     "Error: sin ubicacion. Guarda u_ubicacion (ej lat20.02lon-75.83) "
                     "o pasa lat/lon.");
            return false;
        }
    }

    char url[224];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m",
             lat, lon);

    if (!http_gate_acquire("weather", gate_wait)) {
        snprintf(err, err_len, "Error: HTTP busy, try again");
        return false;
    }
    char resp[NET_TOOL_BODY_MAX];
    int status = net_http_request(url, HTTP_METHOD_GET, NULL, NULL, NULL,
                                  resp, sizeof(resp));
    http_gate_release();

    if (status < 0) {
        snprintf(err, err_len, "Error: no pude consultar el clima (red)");
        return false;
    }
    if (status != 200) {
        snprintf(err, err_len, "Error: clima HTTP %d", status);
        return false;
    }

    // Minimal parse of the flat "current" object from Open-Meteo.
    cJSON *root = cJSON_Parse(resp);
    cJSON *cur = root ? cJSON_GetObjectItem(root, "current") : NULL;
    if (!cur) {
        if (root) cJSON_Delete(root);
        snprintf(err, err_len, "Error: respuesta de clima invalida");
        return false;
    }
    out->temp_c = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "temperature_2m"));
    out->humidity = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "relative_humidity_2m"));
    out->wind_kmh = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "wind_speed_10m"));
    out->code = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "weather_code"));
    cJSON_Delete(root);
    return true;
}

const char *weather_code_desc(int code)
{
    return wmo_desc(code);
}

bool tools_weather_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *jlat = cJSON_GetObjectItem(input, "lat");
    const cJSON *jlon = cJSON_GetObjectItem(input, "lon");
    bool has_coords = jlat && cJSON_IsNumber(jlat) && jlon && cJSON_IsNumber(jlon);
    weather_current_t w;

    if (!weather_fetch_current(has_coords,
                               has_coords ? (float)jlat->valuedouble : 0.0f,
                               has_coords ? (float)jlon->valuedouble : 0.0f,
                               pdMS_TO_TICKS(NET_TOOL_GATE_MS), &w, result, result_len)) {
        return false;
    }

    snprintf(result, result_len,
             "Clima ahora: %.1f C, %s, humedad %.0f%%, viento %.0f km/h",
             w.temp_c, wmo_desc(w.code), w.humidity, w.wind_kmh);
    return true;
}

bool tools_http_get_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *url = cJSON_GetObjectItem(input, "url");

    if (!url || !cJSON_IsString(url) ||
        (strncmp(url->valuestring, "http://", 7) != 0 &&
         strncmp(url->valuestring, "https://", 8) != 0)) {
        snprintf(result, result_len, "Error: 'url' required (http/https)");
        return false;
    }

    if (!http_gate_acquire("http_get", pdMS_TO_TICKS(NET_TOOL_GATE_MS))) {
        snprintf(result, result_len, "Error: HTTP busy, try again");
        return false;
    }

    char resp[NET_TOOL_BODY_MAX];
    int64_t t0 = esp_timer_get_time();
    int status = net_http_request(url->valuestring, HTTP_METHOD_GET,
                                  NULL, NULL, NULL, resp, sizeof(resp));
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    http_gate_release();

    if (status < 0) {
        snprintf(result, result_len, "DOWN: no responde en %d ms (sin conexion o timeout)", ms);
        return false;
    }

    // Latency up front so it doubles as an uptime/health check.
    snprintf(result, result_len, "HTTP %d en %d ms: %.420s", status, ms, resp);
    return status >= 200 && status < 300;
}
