#include "web_ota.h"

#include "config.h"

#if CONFIG_ZCLAW_WEB_OTA_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cron.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota.h"
#include "task_log.h"
#include "web_ota_policy.h"

#define WEB_OTA_CHUNK      4096
#define WEB_OTA_STACK      6144
#define WEB_OTA_DRAIN_BUF  512
#define WEB_OTA_BAD_PIN_MS 1000
#define WEB_OTA_REBOOT_US  1000000
#define WEB_TASKS_BUF      8192
#define WEB_OTA_LOCKED_MSG "Demasiados PIN incorrectos: reinicia la placa"

static const char *TAG = "web_ota";
static httpd_handle_t s_server = NULL;
static bool s_busy = false;
static web_ota_guard_t s_guard;  // wrong-PIN lock, until reboot

static const char PAGE_HEAD[] =
    "<!doctype html><html lang=es><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Fidel OTA</title><style>body{font-family:sans-serif;max-width:26em;"
    "margin:2em auto;padding:0 1em}input,button{display:block;width:100%;"
    "font-size:1em;margin:.5em 0;padding:.4em;box-sizing:border-box}"
    "pre{white-space:pre-wrap;font-family:inherit;margin:0 0 .8em}.h{color:#777;margin:.8em 0 0}</style>"
    "<h1>Fidel</h1><p>Versi\xC3\xB3n <b>";
static const char PAGE_MID[] = "</b> &middot; partici\xC3\xB3n <b>";
static const char PAGE_TAIL[] =
    "<input id=p type=password placeholder=PIN autocomplete=off>"
    "<h2>Tareas</h2><button id=v>Ver tareas y avisos</button><div id=T></div>"
    "<h2>Firmware</h2>"
    "<input id=f type=file accept=.bin><button id=b>Actualizar</button><p id=r></p>"
    "<script>"
    "function el(t,x,c){const e=document.createElement(t);e.textContent=x;if(c)e.className=c;return e}"
    "function when(s){const t=+s;return t<1.6e9?'sin hora':new Date(t*1e3).toLocaleString('es',"
    "{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'})}"
    "function sched(j){return j.type=='daily'?'diaria '+j.time:j.type=='periodic'?"
    "'cada '+j.interval_minutes+' min':j.type=='once'?'una vez':'condici\xC3\xB3n'}"
    "function un(s){return(s||'').replace(/\\\\(n|t|\\\\)/g,(m,c)=>c=='n'?'\\n':c=='t'?'\\t':'\\\\')}"
    "v.onclick=async()=>{T.textContent='Cargando...';const h={headers:{'X-OTA-PIN':p.value}};"
    "try{const a=await fetch('/tasks',h);if(!a.ok){T.textContent=await a.text();return}"
    "const tasks=await a.json();const l=await(await fetch('/log',h)).text();T.textContent='';"
    "T.append(el('h3','Programadas'));if(!tasks.length)T.append(el('p','Ninguna'));"
    "for(const j of tasks)T.append(el('p','#'+j.id+'  '+sched(j)+(j.enabled?'':' (pausada)')+'  '+j.action));"
    "T.append(el('h3','\xC3\x9Altimos avisos'));const ls=l.split('\\n').filter(x=>x);"
    "if(!ls.length)T.append(el('p','Sin avisos (\xC2\xBFhay SD?)'));"
    "for(const x of ls){const q=x.split('\\t');T.append(el('p',when(q[0])+'  Tarea: '+un(q[1]),'h'));"
    "T.append(el('pre',un(q[2])))}}catch(e){T.textContent='Error de red: '+e}};"
    "b.onclick=async()=>{const x=f.files[0];"
    "if(!x){r.textContent='Elige el archivo .bin';return}"
    "if(x.size>MAX){r.textContent='El archivo no cabe en la partici\xC3\xB3n';return}"
    "b.disabled=true;r.textContent='Subiendo '+Math.round(x.size/1024)+' KB...';"
    "try{const s=await fetch('/ota',{method:'POST',headers:{'X-OTA-PIN':p.value},body:x});"
    "r.textContent=await s.text()}catch(e){r.textContent='Error de red: '+e}"
    "b.disabled=false}</script>";

static esp_err_t page_get(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    char max[64];

    snprintf(max, sizeof(max), "</b></p><script>const MAX=%lu</script>",
             (unsigned long)(next ? next->size : 0));
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, ota_get_version());
    httpd_resp_sendstr_chunk(req, PAGE_MID);
    httpd_resp_sendstr_chunk(req, running ? running->label : "?");
    httpd_resp_sendstr_chunk(req, max);
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, text);
}

// httpd_req_recv with a few retries on socket timeouts; <= 0 on failure.
static int recv_some(httpd_req_t *req, char *buf, size_t len)
{
    for (int tries = 0; tries < 3; tries++) {
        int n = httpd_req_recv(req, buf, len);
        if (n != HTTPD_SOCK_ERR_TIMEOUT) {
            return n;
        }
    }
    return HTTPD_SOCK_ERR_TIMEOUT;
}

// Reads and drops the rest of the body so the browser gets the reply instead
// of a reset connection.
static void drain_body(httpd_req_t *req, size_t left)
{
    char buf[WEB_OTA_DRAIN_BUF];

    while (left > 0) {
        int n = recv_some(req, buf, left < sizeof(buf) ? left : sizeof(buf));
        if (n <= 0) {
            return;
        }
        left -= (size_t)n;
    }
}

static void reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void schedule_reboot(void)
{
    const esp_timer_create_args_t args = { .callback = reboot_cb, .name = "web_ota_reboot" };
    esp_timer_handle_t timer;

    if (esp_timer_create(&args, &timer) != ESP_OK ||
        esp_timer_start_once(timer, WEB_OTA_REBOOT_US) != ESP_OK) {
        esp_restart();
    }
}

// Wrong-PIN guard for one request: 0 allowed, 401 wrong PIN, 423 locked.
static int guard_pin(httpd_req_t *req)
{
    char pin[WEB_OTA_PIN_MAX + 2];
    bool match = httpd_req_get_hdr_value_str(req, "X-OTA-PIN", pin, sizeof(pin)) == ESP_OK &&
                 web_ota_pin_matches(pin, CONFIG_ZCLAW_WEB_OTA_PIN);
    int status = web_ota_guard_check(&s_guard, match);
    if (status != 0) {
        ESP_LOGW(TAG, "%s", status == 423 ? "Locked: too many wrong PINs" : "Wrong PIN");
    }
    return status;
}

// True with the right PIN; otherwise waits, replies 401/423 and returns false.
static bool check_pin(httpd_req_t *req)
{
    int status = guard_pin(req);

    if (status == 0) {
        return true;
    }
    vTaskDelay(pdMS_TO_TICKS(WEB_OTA_BAD_PIN_MS));
    if (status == 423) {
        reply(req, "423 Locked", WEB_OTA_LOCKED_MSG);
    } else {
        reply(req, "401 Unauthorized", "PIN incorrecto");
    }
    return false;
}

static esp_err_t tasks_get(httpd_req_t *req)
{
    if (!check_pin(req)) {
        return ESP_OK;
    }
    char *buf = malloc(WEB_TASKS_BUF);
    if (!buf) {
        return reply(req, "500 Internal Server Error", "Error: sin memoria");
    }
    cron_list(buf, WEB_TASKS_BUF);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, buf);
    free(buf);
    return err;
}

static bool send_log_line(const char *line, void *ctx)
{
    httpd_req_t *req = ctx;
    return httpd_resp_sendstr_chunk(req, line) == ESP_OK &&
           httpd_resp_sendstr_chunk(req, "\n") == ESP_OK;
}

static esp_err_t log_get(httpd_req_t *req)
{
    if (!check_pin(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    (void)task_log_read_recent(TASK_LOG_RECENT, send_log_line, req);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t ota_post(httpd_req_t *req)
{
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    int guard = guard_pin(req);

    if (guard == 423) {
        vTaskDelay(pdMS_TO_TICKS(WEB_OTA_BAD_PIN_MS));
        reply(req, "423 Locked", WEB_OTA_LOCKED_MSG);
        return ESP_FAIL;  // closes the connection
    }
    if (!next) {
        reply(req, "500 Internal Server Error", "Error: no hay partici\xC3\xB3n OTA");
        return ESP_FAIL;  // closes the connection
    }
    switch (web_ota_request_status(req->content_len, next->size, guard == 0, s_busy)) {
    case 0:
        break;
    case 400:
        reply(req, "400 Bad Request", "Error: falta Content-Length");
        return ESP_FAIL;
    case 413:
        reply(req, "413 Payload Too Large", "Error: el archivo no cabe en la partici\xC3\xB3n");
        return ESP_FAIL;
    case 401:
        vTaskDelay(pdMS_TO_TICKS(WEB_OTA_BAD_PIN_MS));
        drain_body(req, req->content_len);
        return reply(req, "401 Unauthorized", "PIN incorrecto");
    default:
        reply(req, "409 Conflict", "Ya hay una actualizaci\xC3\xB3n en curso");
        return ESP_FAIL;
    }

    s_busy = true;
    ESP_LOGI(TAG, "Receiving %u bytes into %s", (unsigned)req->content_len, next->label);
    char *buf = malloc(WEB_OTA_CHUNK);
    esp_ota_handle_t handle = 0;
    esp_err_t err = buf ? esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &handle) : ESP_ERR_NO_MEM;
    bool begun = err == ESP_OK;
    bool recv_failed = false;
    size_t left = req->content_len;

    while (err == ESP_OK && left > 0) {
        int n = recv_some(req, buf, left < WEB_OTA_CHUNK ? left : WEB_OTA_CHUNK);
        if (n <= 0) {
            recv_failed = true;
            err = ESP_ERR_TIMEOUT;
            break;
        }
        err = esp_ota_write(handle, buf, (size_t)n);
        left -= (size_t)n;
    }
    if (begun) {
        if (err == ESP_OK) {
            err = esp_ota_end(handle);  // validates magic, chip and SHA-256
        } else {
            esp_ota_abort(handle);
        }
    }
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(next);
    }
    free(buf);
    s_busy = false;

    if (err != ESP_OK) {
        char msg[96];
        ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
        if (recv_failed) {
            return ESP_FAIL;  // the connection is gone
        }
        drain_body(req, left);
        snprintf(msg, sizeof(msg), "Error: %s (el firmware actual sigue)", esp_err_to_name(err));
        return reply(req, "500 Internal Server Error", msg);
    }
    ESP_LOGI(TAG, "Update written to %s, rebooting", next->label);
    reply(req, "200 OK", "Actualizado. Reiniciando...");
    schedule_reboot();
    return ESP_OK;
}

esp_err_t web_ota_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    const httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = page_get };
    const httpd_uri_t upload = { .uri = "/ota", .method = HTTP_POST, .handler = ota_post };
    const httpd_uri_t tasks = { .uri = "/tasks", .method = HTTP_GET, .handler = tasks_get };
    const httpd_uri_t log_uri = { .uri = "/log", .method = HTTP_GET, .handler = log_get };
    size_t pin_len = strlen(CONFIG_ZCLAW_WEB_OTA_PIN);

    if (s_server) {
        return ESP_OK;
    }
    if (pin_len < WEB_OTA_PIN_MIN || pin_len > WEB_OTA_PIN_MAX) {
        ESP_LOGW(TAG, "Web OTA off: CONFIG_ZCLAW_WEB_OTA_PIN must have %d-%d characters",
                 WEB_OTA_PIN_MIN, WEB_OTA_PIN_MAX);
        return ESP_ERR_INVALID_ARG;
    }
    config.max_open_sockets = 2;
    config.stack_size = WEB_OTA_STACK;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        s_server = NULL;
        return err;
    }
    httpd_register_uri_handler(s_server, &page);
    httpd_register_uri_handler(s_server, &upload);
    httpd_register_uri_handler(s_server, &tasks);
    httpd_register_uri_handler(s_server, &log_uri);
    ESP_LOGI(TAG, "Firmware upload page on port %d", config.server_port);
    return ESP_OK;
}

#else  // !CONFIG_ZCLAW_WEB_OTA_ENABLE

esp_err_t web_ota_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
