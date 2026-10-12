#include "llm_auth.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

bool llm_build_bearer_auth_header(const char *api_key, char *auth_header, size_t auth_header_size)
{
    int written;
    size_t key_len;

    if (!api_key || !auth_header || auth_header_size == 0) {
        return false;
    }

    key_len = strlen(api_key);
    if (key_len == 0 || key_len > LLM_API_KEY_MAX_LEN) {
        auth_header[0] = '\0';
        return false;
    }

    written = snprintf(auth_header, auth_header_size, "Bearer %s", api_key);
    if (written < 0 || (size_t)written >= auth_header_size) {
        auth_header[0] = '\0';
        return false;
    }

    return true;
}
