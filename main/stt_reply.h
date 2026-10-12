#ifndef STT_REPLY_H
#define STT_REPLY_H

#include <stdbool.h>
#include <stddef.h>

// Reads {"text": "..."} from the STT service into out (whitespace trimmed,
// clipped to out_len - 1 bytes without splitting a UTF-8 character). False
// when the JSON is invalid or has no string "text"; an empty transcript is
// valid and gives "".
bool stt_reply_parse(const char *json, char *out, size_t out_len);

#endif // STT_REPLY_H
