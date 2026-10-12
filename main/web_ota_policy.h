#ifndef WEB_OTA_POLICY_H
#define WEB_OTA_POLICY_H

#include <stdbool.h>
#include <stddef.h>

#define WEB_OTA_PIN_MIN 4
#define WEB_OTA_PIN_MAX 32

// Constant-time PIN check (time depends only on the expected PIN's length).
// An empty expected PIN never matches.
bool web_ota_pin_matches(const char *given, const char *expected);

// HTTP status for an upload request, checked in this order: 400 without a
// body length, 413 larger than the target partition, 401 wrong PIN, 409
// another update running; 0 when the upload may start.
int web_ota_request_status(size_t content_len, size_t partition_size, bool pin_ok, bool busy);

#endif // WEB_OTA_POLICY_H
