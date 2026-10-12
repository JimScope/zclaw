#include "web_ota_policy.h"

#include <string.h>

bool web_ota_pin_matches(const char *given, const char *expected)
{
    size_t glen;
    size_t elen;
    unsigned diff;

    if (!given || !expected) {
        return false;
    }
    glen = strlen(given);
    elen = strlen(expected);
    diff = (unsigned)(glen ^ elen);
    for (size_t i = 0; i < elen; i++) {
        unsigned char g = i < glen ? (unsigned char)given[i] : 0;
        diff |= (unsigned)(g ^ (unsigned char)expected[i]);
    }
    return elen > 0 && diff == 0;
}

int web_ota_request_status(size_t content_len, size_t partition_size, bool pin_ok, bool busy)
{
    if (content_len == 0) {
        return 400;
    }
    if (content_len > partition_size) {
        return 413;
    }
    if (!pin_ok) {
        return 401;
    }
    if (busy) {
        return 409;
    }
    return 0;
}
