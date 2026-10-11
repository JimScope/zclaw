#include "tools_handlers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "dash_url.h"
#include "display.h"
#include "display_remote.h"

#define DISPLAY_IMAGE_MS 30000
#define IMAGE_URL_MAX    300

bool tools_display_image_handler(const cJSON *input, char *result, size_t result_len)
{
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    const cJSON *query = cJSON_GetObjectItem(input, "query");
    const cJSON *link = cJSON_GetObjectItem(input, "url");
    bool by_url = link && cJSON_IsString(link) && link->valuestring[0] != '\0';
    bool by_query = query && cJSON_IsString(query) && query->valuestring[0] != '\0';
    char url[IMAGE_URL_MAX];
    size_t len = 0;
    bool not_found = false;

    if (!by_url && !by_query) {
        snprintf(result, result_len, "Error: pass 'query' (what to search) or 'url' (image link)");
        return false;
    }
    if (!dash_url_build_param(url, sizeof(url), CONFIG_ZCLAW_DASH_BASE_URL, "image",
                              by_url ? "url" : "q",
                              by_url ? link->valuestring : query->valuestring)) {
        snprintf(result, result_len, "Error: query or url too long");
        return false;
    }

    // The VPS searches/converts the picture; the board only gets a small JPEG.
    uint8_t *jpeg = display_remote_download(url, &len, &not_found);
    if (!jpeg) {
        snprintf(result, result_len, "Error: no pude descargar la imagen (red o servidor)");
        return false;
    }
    if (!display_show_jpeg(jpeg, len, DISPLAY_IMAGE_MS)) {
        free(jpeg);
        snprintf(result, result_len, "Error: la pantalla no est\xC3\xA1 disponible");
        return false;
    }
    if (not_found) {
        snprintf(result, result_len, "No encontr\xC3\xA9 una imagen para eso (la pantalla lo indica).");
        return false;
    }
    snprintf(result, result_len, "Mostrando la imagen en la pantalla durante 30 s.");
    return true;
#else
    (void)input;
    snprintf(result, result_len, "Error: this device has no display");
    return false;
#endif
}
