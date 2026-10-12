#include "channel.h"
#include "config.h"
#include "messages.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "channel";

static QueueHandle_t s_input_queue;
static QueueHandle_t s_output_queue;

static void channel_io_init(void)
{
    usb_serial_jtag_driver_config_t config = {
        .rx_buffer_size = CHANNEL_RX_BUF_SIZE,
        .tx_buffer_size = CHANNEL_RX_BUF_SIZE,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
}

static int channel_io_read_byte(uint8_t *byte, TickType_t timeout_ticks)
{
    return usb_serial_jtag_read_bytes(byte, 1, timeout_ticks);
}

// The TX ring buffer (CHANNEL_RX_BUF_SIZE bytes) rejects, and so drops, any
// single write larger than itself: send long replies in pieces.
#define CHANNEL_TX_CHUNK (CHANNEL_RX_BUF_SIZE / 2)

static void channel_io_write_bytes(const uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    while (len > 0) {
        size_t n = len < CHANNEL_TX_CHUNK ? len : CHANNEL_TX_CHUNK;
        if (usb_serial_jtag_write_bytes((uint8_t *)data, n, timeout_ticks) <= 0) {
            return;
        }
        data += n;
        len -= n;
    }
}

static void channel_write_normalized_text(const char *text, TickType_t timeout_ticks)
{
    const char *segment_start = text;
    const char *cursor = text;

    while (*cursor != '\0') {
        if (*cursor == '\n') {
            size_t segment_len = (size_t)(cursor - segment_start);
            if (segment_len > 0) {
                channel_io_write_bytes((const uint8_t *)segment_start, segment_len, timeout_ticks);
            }

            if (cursor > segment_start && *(cursor - 1) == '\r') {
                channel_io_write_bytes((const uint8_t *)"\n", 1, timeout_ticks);
            } else {
                channel_io_write_bytes((const uint8_t *)"\r\n", 2, timeout_ticks);
            }

            cursor++;
            segment_start = cursor;
            continue;
        }

        cursor++;
    }

    if (cursor > segment_start) {
        channel_io_write_bytes((const uint8_t *)segment_start,
                               (size_t)(cursor - segment_start),
                               timeout_ticks);
    }
}

void channel_init(void)
{
    channel_io_init();
    ESP_LOGI(TAG, "USB serial initialized");
}

// Read task: accumulate characters into lines, push to input queue
static void channel_read_task(void *arg)
{
    char line_buf[CHANNEL_RX_BUF_SIZE];
    int line_pos = 0;
    uint8_t byte;

    while (1) {
        int len = channel_io_read_byte(&byte, portMAX_DELAY);
        if (len > 0) {
            if (byte == '\r' || byte == '\n') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    channel_io_write_bytes((const uint8_t *)"\r\n", 2, portMAX_DELAY);

                    // Push to input queue
                    channel_msg_t msg;
                    strncpy(msg.text, line_buf, CHANNEL_RX_BUF_SIZE - 1);
                    msg.text[CHANNEL_RX_BUF_SIZE - 1] = '\0';
                    msg.source = MSG_SOURCE_CHANNEL;

                    if (xQueueSend(s_input_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "Input queue full, dropping message");
                    }
                }

                line_pos = 0;
            } else if (byte == 0x7F || byte == 0x08) {
                // Backspace
                if (line_pos > 0) {
                    line_pos--;
                    channel_io_write_bytes((const uint8_t *)"\b \b", 3, portMAX_DELAY);
                }
            } else {
                if (line_pos < CHANNEL_RX_BUF_SIZE - 1) {
                    line_buf[line_pos++] = (char)byte;
                }
                channel_io_write_bytes(&byte, 1, portMAX_DELAY);
            }
        }
    }
}

// Write task: watch output queue, print responses
static void channel_write_task(void *arg)
{
    channel_output_msg_t msg;

    while (1) {
        if (xQueueReceive(s_output_queue, &msg, portMAX_DELAY) == pdTRUE) {
            // Print response with newlines
            const char *text = msg.text;
            channel_write_normalized_text(text, portMAX_DELAY);
            channel_io_write_bytes((const uint8_t *)"\r\n\r\n", 4, portMAX_DELAY);
        }
    }
}

esp_err_t channel_start(QueueHandle_t input_queue, QueueHandle_t output_queue)
{
    TaskHandle_t read_task = NULL;
    TaskHandle_t write_task = NULL;

    if (!input_queue || !output_queue) {
        ESP_LOGE(TAG, "Invalid queues for channel startup");
        return ESP_ERR_INVALID_ARG;
    }

    s_input_queue = input_queue;
    s_output_queue = output_queue;

    if (xTaskCreate(channel_read_task, "ch_read", CHANNEL_TASK_STACK_SIZE, NULL,
                    CHANNEL_TASK_PRIORITY, &read_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create channel read task");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(channel_write_task, "ch_write", CHANNEL_TASK_STACK_SIZE, NULL,
                    CHANNEL_TASK_PRIORITY, &write_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create channel write task");
        if (read_task) {
            vTaskDelete(read_task);
        }
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Channel tasks started");
    return ESP_OK;
}

void channel_write(const char *text)
{
    channel_write_normalized_text(text, portMAX_DELAY);
}
