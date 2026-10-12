#include "display.h"

#include "config.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#include <stdlib.h>
#include <string.h>

#include "dash_state.h"
#include "dash_url.h"
#include "display_face.h"
#include "display_gfx.h"
#include "display_hw.h"
#include "display_remote.h"
#include "display_status.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "face_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "memory.h"
#include "page_rotation.h"
#include "sd_card.h"
#include "tools_handlers.h"

#define DISPLAY_TASK_STACK 5120
#define DISPLAY_TASK_PRIO  2
#define DISPLAY_QUEUE_LEN  8
#define DISPLAY_TICK_MS    30
#define DISPLAY_FADE_MS    150
#define DASH_URL_MAX       200

typedef enum {
    DISPLAY_EV_THINKING,
    DISPLAY_EV_TALKING,
    DISPLAY_EV_WIFI,
    DISPLAY_EV_SHOW_STATUS,
    DISPLAY_EV_SHOW_DASH,
    DISPLAY_EV_DASH_DONE,
    DISPLAY_EV_SHOW_JPEG,
} display_event_type_t;

typedef struct {
    display_event_type_t type;
    uint32_t value;  // flag, reply length, duration or request id
    uint32_t extra;  // dashboard index or JPEG length
    void *ptr;       // DISPLAY_EV_DASH_DONE: JPEG buffer, owned by the receiver
} display_event_t;

static const char *const s_dash_names[] = {"clima", "cripto"};
static const char *const s_dash_titles[] = {"Clima", "Cripto"};
#define DASH_COUNT (sizeof(s_dash_names) / sizeof(s_dash_names[0]))

static const char *TAG = "display";
static QueueHandle_t s_queue = NULL;
static face_state_t s_face;
static page_rotation_t s_pages;

// Dashboard currently requested (PAGE_REMOTE).
static dash_state_t s_dash;             // remote page request + the JPEG it owns
static const char *s_dash_title = "Imagen";

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void fade_out(void)
{
    display_hw_fade(0, DISPLAY_FADE_MS);
}

static void fade_in(void)
{
    display_hw_fade(display_hw_brightness(), DISPLAY_FADE_MS);
}

static void draw_message(const char *title, const char *message)
{
    int w;

    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);
    w = gfx_text_width(title, 2);
    gfx_text(w < DISPLAY_W ? (DISPLAY_W - w) / 2 : 0, 92, title, GFX_AMBER, GFX_BLACK, 2);
    w = gfx_text_width(message, 1);
    gfx_text(w < DISPLAY_W ? (DISPLAY_W - w) / 2 : 0, 132, message, GFX_GRAY, GFX_BLACK, 1);
    display_hw_wait_idle();
}

// Draws the dashboard result if one is waiting; returns whether it drew.
static bool draw_dash_result(void)
{
    const char *title = s_dash_title;

    if (s_dash.jpeg) {
        if (display_remote_draw(s_dash.jpeg, s_dash.len) != ESP_OK) {
            draw_message(title, "imagen no v\xC3\xA1lida");
        }
        dash_state_clear_result(&s_dash);
        return true;
    }
    if (s_dash.failed) {
        draw_message(title, "sin datos");
        dash_state_clear_result(&s_dash);
        return true;
    }
    return false;
}

static void dash_done(uint32_t request_id, uint8_t *jpeg, size_t len)
{
    display_event_t ev = {
        .type = DISPLAY_EV_DASH_DONE, .value = request_id, .extra = (uint32_t)len, .ptr = jpeg,
    };
    if (!s_queue || xQueueSend(s_queue, &ev, pdMS_TO_TICKS(1000)) != pdTRUE) {
        free(jpeg);
    }
}

static void start_dash_fetch(void)
{
    char url[DASH_URL_MAX];
    char location[64] = {0};

    if (s_dash.index < 0) {
        s_dash.need_fetch = false;
        return;
    }
    (void)memory_get("u_ubicacion", location, sizeof(location));
    if (!dash_url_build(url, sizeof(url), CONFIG_ZCLAW_DASH_BASE_URL, s_dash_names[s_dash.index],
                        location)) {
        s_dash.need_fetch = false;
        (void)dash_state_done(&s_dash, s_dash.request, NULL, 0);
        return;
    }
    if (display_remote_fetch(url, s_dash.request, dash_done)) {
        s_dash.need_fetch = false;  // otherwise a previous download is still running: retry
    }
}

static void apply_event(const display_event_t *ev, int64_t now)
{
    switch (ev->type) {
    case DISPLAY_EV_THINKING:
        face_state_set_thinking(&s_face, ev->value != 0);
        break;
    case DISPLAY_EV_TALKING:
        face_state_start_talking(&s_face, ev->value, now);
        break;
    case DISPLAY_EV_WIFI:
        face_state_set_wifi(&s_face, ev->value != 0);
        break;
    case DISPLAY_EV_SHOW_STATUS:
        page_rotation_show(&s_pages, PAGE_STATUS, ev->value, now);
        break;
    case DISPLAY_EV_SHOW_DASH:
        (void)dash_state_request(&s_dash, (int)ev->extra);
        s_dash_title = s_dash_titles[ev->extra];
        page_rotation_show(&s_pages, PAGE_REMOTE, ev->value, now);
        break;
    case DISPLAY_EV_SHOW_JPEG:
        // Picture already downloaded by the caller (display_image tool).
        (void)dash_state_picture(&s_dash, ev->ptr, ev->extra);
        s_dash_title = "Imagen";
        page_rotation_show(&s_pages, PAGE_REMOTE, ev->value, now);
        break;
    case DISPLAY_EV_DASH_DONE:
        if (!dash_state_done(&s_dash, ev->value, ev->ptr, ev->extra)) {
            free(ev->ptr);  // stale download for an older request
        }
        break;
    }
}

static void display_task(void *arg)
{
    (void)arg;
    bool have_page = false;
    page_t shown = PAGE_FACE;
    display_event_t ev;

    dash_state_init(&s_dash, free);
    face_state_init(&s_face, esp_random(), now_ms());
    page_rotation_init(&s_pages, CONFIG_ZCLAW_FACE_SECONDS * 1000u,
                       CONFIG_ZCLAW_STATUS_SECONDS * 1000u, now_ms());

    while (1) {
        if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(DISPLAY_TICK_MS)) == pdTRUE) {
            do {
                apply_event(&ev, now_ms());
            } while (xQueueReceive(s_queue, &ev, 0) == pdTRUE);
        }

        int64_t now = now_ms();
        if (sd_card_service(now)) {
            display_face_rescan(&s_face);
        }
        page_t page = page_rotation_update(&s_pages, face_state_is_active(&s_face, now), now);
        // Releases a JPEG (or makes an in-flight download stale) the moment the
        // remote page is not active, including requests cancelled before shown.
        dash_state_page(&s_dash, page == PAGE_REMOTE);
        if (page == PAGE_REMOTE && s_dash.need_fetch) {
            start_dash_fetch();
        }

        if (!have_page || page != shown) {
            if (have_page) {
                fade_out();
            }
            have_page = true;
            shown = page;
            if (page == PAGE_STATUS) {
                display_status_draw(s_face.wifi_up);
                display_hw_wait_idle();
            } else if (page == PAGE_REMOTE) {
                if (!draw_dash_result()) {
                    draw_message(s_dash_title, "cargando...");
                }
            } else {
                display_face_invalidate();
                if (!display_face_draw(&s_face, now)) {
                    sd_card_mark_failed(now);
                    display_face_clear(&s_face);
                }
                display_hw_wait_idle();
            }
            fade_in();
        } else if (page == PAGE_REMOTE && (s_dash.jpeg || s_dash.failed)) {
            fade_out();
            draw_dash_result();
            fade_in();
        }

        if (page == PAGE_FACE && !display_face_draw(&s_face, now)) {
            sd_card_mark_failed(now);
            display_face_clear(&s_face);
        }
    }
}

esp_err_t display_start(void)
{
    if (s_queue) {
        return ESP_OK;
    }

    esp_err_t err = display_hw_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display disabled: %s", esp_err_to_name(err));
        return err;
    }

    float warm = 0;
    (void)tools_chip_temp_read(&warm);  // install the sensor once, before two tasks use it

    s_queue = xQueueCreate(DISPLAY_QUEUE_LEN, sizeof(display_event_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(display_task, "display", DISPLAY_TASK_STACK, NULL, DISPLAY_TASK_PRIO, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Display task started");
    return ESP_OK;
}

static void post(display_event_type_t type, uint32_t value, uint32_t extra)
{
    display_event_t ev = { .type = type, .value = value, .extra = extra, .ptr = NULL };
    if (s_queue) {
        (void)xQueueSend(s_queue, &ev, 0);  // drop when full; the next event corrects the state
    }
}

void display_notify_thinking(bool active)
{
    post(DISPLAY_EV_THINKING, active ? 1u : 0u, 0);
}

void display_notify_talking(size_t reply_chars)
{
    post(DISPLAY_EV_TALKING, reply_chars > UINT32_MAX ? UINT32_MAX : (uint32_t)reply_chars, 0);
}

void display_notify_wifi(bool connected)
{
    post(DISPLAY_EV_WIFI, connected ? 1u : 0u, 0);
}

bool display_request_status(uint32_t duration_ms)
{
    if (!s_queue) {
        return false;
    }
    post(DISPLAY_EV_SHOW_STATUS, duration_ms, 0);
    return true;
}

bool display_show_jpeg(uint8_t *jpeg, size_t len, uint32_t duration_ms)
{
    display_event_t ev = {
        .type = DISPLAY_EV_SHOW_JPEG, .value = duration_ms, .extra = (uint32_t)len, .ptr = jpeg,
    };
    return s_queue && jpeg && xQueueSend(s_queue, &ev, pdMS_TO_TICKS(1000)) == pdTRUE;
}

bool display_request_dashboard(const char *name, uint32_t duration_ms)
{
    if (!s_queue || !name) {
        return false;
    }
    for (size_t i = 0; i < DASH_COUNT; i++) {
        if (strcmp(name, s_dash_names[i]) == 0) {
            post(DISPLAY_EV_SHOW_DASH, duration_ms, (uint32_t)i);
            return true;
        }
    }
    return false;
}

#else  // !CONFIG_ZCLAW_DISPLAY_ENABLE

esp_err_t display_start(void) { return ESP_ERR_NOT_SUPPORTED; }
void display_notify_thinking(bool active) { (void)active; }
void display_notify_talking(size_t reply_chars) { (void)reply_chars; }
void display_notify_wifi(bool connected) { (void)connected; }
bool display_request_status(uint32_t duration_ms) { (void)duration_ms; return false; }
bool display_show_jpeg(uint8_t *jpeg, size_t len, uint32_t duration_ms)
{
    (void)jpeg;
    (void)len;
    (void)duration_ms;
    return false;
}
bool display_request_dashboard(const char *name, uint32_t duration_ms)
{
    (void)name;
    (void)duration_ms;
    return false;
}

#endif
