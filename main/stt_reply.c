#include "stt_reply.h"

#include <ctype.h>
#include <string.h>

#include "cJSON.h"
#include "text_layout.h"

bool stt_reply_parse(const char *json, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!json) {
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    const cJSON *text = root ? cJSON_GetObjectItem(root, "text") : NULL;
    if (!text || !cJSON_IsString(text)) {
        cJSON_Delete(root);
        return false;
    }
    const char *start = text->valuestring;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    size_t len = strlen(start);
    while (len > 0 && isspace((unsigned char)start[len - 1])) {
        len--;
    }
    char *trimmed = cJSON_malloc(len + 1);
    if (!trimmed) {
        cJSON_Delete(root);
        return false;
    }
    memcpy(trimmed, start, len);
    trimmed[len] = '\0';
    text_layout_clip_utf8(trimmed, out_len - 1, out, out_len);
    cJSON_free(trimmed);
    cJSON_Delete(root);
    return true;
}
