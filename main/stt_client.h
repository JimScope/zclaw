#ifndef STT_CLIENT_H
#define STT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// True when CONFIG_ZCLAW_STT_URL and CONFIG_ZCLAW_STT_TOKEN are set.
bool stt_client_configured(void);

// POSTs an ADPCM stream (header + codes) to the STT service. ESP_OK with
// text set (possibly "") on HTTP 200 with a valid reply. *status always gets
// the HTTP status (0 without a response).
esp_err_t stt_client_transcribe(const uint8_t *body, size_t len, char *text, size_t text_len, int *status);

#endif // STT_CLIENT_H
