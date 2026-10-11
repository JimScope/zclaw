#include "tools_handlers.h"
#include "weather.h"
#include "tools_common.h"
#include "config.h"
#include "http_gate.h"
#include "memory.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NET_TOOL_TIMEOUT_MS 15000
#define NET_TOOL_GATE_MS    12000
#define NET_TOOL_BODY_MAX   1024

static const char *TAG = "tools_net";

typedef struct {
    char *buf;
    size_t len;
    size_t max;
} net_resp_ctx_t;

static esp_err_t net_http_event(esp_http_client_event_t *evt)
{
    net_resp_ctx_t *ctx = (net_resp_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx && ctx->buf && evt->data_len > 0) {
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

// Returns HTTP status, or -1 on transport error. Body (truncated) lands in out.
static int net_http_request(const char *url,
                            esp_http_client_method_t method,
                            const char *header_key,
                            const char *header_value,
                            const char *post_body,
                            char *out,
                            size_t out_max)
{
    net_resp_ctx_t ctx = { .buf = out, .len = 0, .max = out_max };
    out[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = net_http_event,
        .user_data = &ctx,
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

    static char html[WEB_SEARCH_HTML_MAX];
    int status = net_http_request(url, HTTP_METHOD_GET,
                                  "User-Agent", "Mozilla/5.0 (compatible; zclaw-esp32)",
                                  NULL, html, sizeof(html));
    http_gate_release();

    if (status < 0) {
        snprintf(result, result_len, "Error: search request failed (network)");
        return false;
    }
    if (status < 200 || status >= 300) {
        snprintf(result, result_len, "Error: search HTTP %d", status);
        return false;
    }

    char text[480];
    html_strip_into(html, text, sizeof(text));
    snprintf(result, result_len, "Search results (truncated): %s", text);
    return true;
}

// read_web: fetch a page as clean text via the self-hosted reader on the VPS
// and hand it to the model in ~450-byte pages. The full text is
// cached statically so paging through a long article costs one download.
#define READ_WEB_CACHE_MAX 4096
#define READ_WEB_PAGE_SIZE 450

static char s_web_cache[READ_WEB_CACHE_MAX];
static char s_web_cache_url[192];
static size_t s_web_cache_len;

bool tools_read_web_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *url = cJSON_GetObjectItem(input, "url");
    const cJSON *page_json = cJSON_GetObjectItem(input, "page");
    int page = (page_json && cJSON_IsNumber(page_json)) ? page_json->valueint : 1;
    if (page < 1) {
        page = 1;
    }

    if (!url || !cJSON_IsString(url) ||
        (strncmp(url->valuestring, "http://", 7) != 0 &&
         strncmp(url->valuestring, "https://", 8) != 0)) {
        snprintf(result, result_len, "Error: 'url' required (http/https)");
        return false;
    }

    // Serve from cache when re-reading the same URL (paging); otherwise fetch.
    bool cached = (s_web_cache_len > 0 &&
                   strcmp(s_web_cache_url, url->valuestring) == 0);
    if (!cached) {
        char reader_url[256];
        // Self-hosted trafilatura reader on the VPS.
        snprintf(reader_url, sizeof(reader_url),
                 "https://bolita-cubana.duckdns.org/reader/%s", url->valuestring);

        if (!http_gate_acquire("read_web", pdMS_TO_TICKS(NET_TOOL_GATE_MS))) {
            snprintf(result, result_len, "Error: HTTP busy, try again");
            return false;
        }
        int status = net_http_request(reader_url, HTTP_METHOD_GET,
                                      "User-Agent", "Mozilla/5.0 (zclaw-esp32)",
                                      NULL, s_web_cache, sizeof(s_web_cache));
        http_gate_release();

        if (status < 0) {
            s_web_cache_len = 0;
            snprintf(result, result_len, "Error: reader request failed (network)");
            return false;
        }
        if (status < 200 || status >= 300) {
            s_web_cache_len = 0;
            snprintf(result, result_len, "Error: reader HTTP %d", status);
            return false;
        }
        s_web_cache_len = strlen(s_web_cache);
        strncpy(s_web_cache_url, url->valuestring, sizeof(s_web_cache_url) - 1);
        s_web_cache_url[sizeof(s_web_cache_url) - 1] = '\0';
    }

    int total_pages = (int)((s_web_cache_len + READ_WEB_PAGE_SIZE - 1) / READ_WEB_PAGE_SIZE);
    if (total_pages < 1) {
        total_pages = 1;
    }
    if (page > total_pages) {
        snprintf(result, result_len, "Fin del contenido (%d paginas en total)", total_pages);
        return true;
    }

    size_t start = (size_t)(page - 1) * READ_WEB_PAGE_SIZE;
    char chunk[READ_WEB_PAGE_SIZE + 1];
    size_t n = s_web_cache_len - start;
    if (n > READ_WEB_PAGE_SIZE) {
        n = READ_WEB_PAGE_SIZE;
    }
    memcpy(chunk, s_web_cache + start, n);
    chunk[n] = '\0';

    if (page < total_pages) {
        snprintf(result, result_len, "[pag %d/%d, pide page=%d para seguir]\n%s",
                 page, total_pages, page + 1, chunk);
    } else {
        snprintf(result, result_len, "[pag %d/%d, fin]\n%s", page, total_pages, chunk);
    }
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
