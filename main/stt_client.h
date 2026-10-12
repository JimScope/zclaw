#ifndef STT_CLIENT_H
#define STT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// True when CONFIG_ZCLAW_STT_URL and CONFIG_ZCLAW_STT_TOKEN are set.
bool stt_client_configured(void);

// One piece of the request body; the pieces are sent in order.
typedef struct {
    const uint8_t *data;
    size_t len;
} stt_segment_t;

// POSTs an ADPCM stream (header + codes), given as consecutive segments, to
// the STT service. ESP_OK with text set (possibly "") on HTTP 200 with a valid
// reply. *status always gets the HTTP status (0 without a response).
esp_err_t stt_client_transcribe(const stt_segment_t *segs, size_t nsegs, char *text, size_t text_len,
                                int *status);

#endif // STT_CLIENT_H
