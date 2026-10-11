#ifndef TELEGRAM_H
#define TELEGRAM_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>

// Initialize Telegram client
esp_err_t telegram_init(void);

// Start Telegram polling task
esp_err_t telegram_start(QueueHandle_t input_queue, QueueHandle_t output_queue);

// Temporarily prevent new Telegram long-polls from starting.
void telegram_pause_polling(void);
void telegram_resume_polling(void);

// Send a message to the primary configured chat
esp_err_t telegram_send(const char *text);

// Send startup notification
esp_err_t telegram_send_startup(void);

// Check if Telegram is configured (token exists)
bool telegram_is_configured(void);

// Get the primary chat ID
int64_t telegram_get_chat_id(void);

// Confirm already-processed updates with Telegram. Call before esp_restart():
// the boot-time flush keeps the newest pending update, so an unconfirmed one
// (e.g. the .bin that triggered an OTA) would be processed again after boot.
esp_err_t telegram_ack_processed_updates(void);

#endif // TELEGRAM_H
