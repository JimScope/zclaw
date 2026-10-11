#include "display.h"

#include "config.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#include "display_face.h"
#include "display_hw.h"
#include "display_status.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "face_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "page_rotation.h"
#include "sd_card.h"
#include "tools_handlers.h"

#define DISPLAY_TASK_STACK 5120
#define DISPLAY_TASK_PRIO  2
#define DISPLAY_QUEUE_LEN  8
#define DISPLAY_TICK_MS    30

typedef enum {
    DISPLAY_EV_THINKING,
    DISPLAY_EV_TALKING,
    DISPLAY_EV_WIFI,
    DISPLAY_EV_SHOW_STATUS,
} display_event_type_t;

typedef struct {
    display_event_type_t type;
    uint32_t value;
} display_event_t;

static const char *TAG = "display";
static QueueHandle_t s_queue = NULL;
static face_state_t s_face;
static page_rotation_t s_pages;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
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
        page_rotation_show_status(&s_pages, ev->value, now);
        break;
    }
}

static void display_task(void *arg)
{
    (void)arg;
    bool have_page = false;
    page_t shown = PAGE_FACE;
    display_event_t ev;

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
        display_status_service(now, s_face.wifi_up);

        page_t page = page_rotation_update(&s_pages, face_state_is_active(&s_face, now), now);
        if (!have_page || page != shown) {
            have_page = true;
            shown = page;
            if (page == PAGE_STATUS) {
                display_status_draw(s_face.wifi_up);
            } else {
                display_face_invalidate();
            }
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

static void post(display_event_type_t type, uint32_t value)
{
    display_event_t ev = { .type = type, .value = value };
    if (s_queue) {
        (void)xQueueSend(s_queue, &ev, 0);  // drop when full; the next event corrects the state
    }
}

void display_notify_thinking(bool active)
{
    post(DISPLAY_EV_THINKING, active ? 1u : 0u);
}

void display_notify_talking(size_t reply_chars)
{
    post(DISPLAY_EV_TALKING, reply_chars > UINT32_MAX ? UINT32_MAX : (uint32_t)reply_chars);
}

void display_notify_wifi(bool connected)
{
    post(DISPLAY_EV_WIFI, connected ? 1u : 0u);
}

bool display_request_status(uint32_t duration_ms)
{
    if (!s_queue) {
        return false;
    }
    post(DISPLAY_EV_SHOW_STATUS, duration_ms);
    return true;
}

#else  // !CONFIG_ZCLAW_DISPLAY_ENABLE

esp_err_t display_start(void) { return ESP_ERR_NOT_SUPPORTED; }
void display_notify_thinking(bool active) { (void)active; }
void display_notify_talking(size_t reply_chars) { (void)reply_chars; }
void display_notify_wifi(bool connected) { (void)connected; }
bool display_request_status(uint32_t duration_ms) { (void)duration_ms; return false; }

#endif
