// display_dashboard tool: shows the weather or crypto dashboard (rendered by
// the VPS) or the local status panel on the device screen for 30 s.

#include "tools_handlers.h"

#include <stdio.h>
#include <string.h>

#include "display.h"

#define DASHBOARD_MS 30000

bool tools_display_dashboard_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *name = cJSON_GetObjectItem(input, "name");
    const char *value;
    bool shown;

    if (!name || !cJSON_IsString(name)) {
        snprintf(result, result_len, "Error: 'name' must be clima, cripto or panel");
        return false;
    }
    value = name->valuestring;
    if (strcmp(value, "panel") == 0) {
        shown = display_request_status(DASHBOARD_MS);
    } else if (strcmp(value, "clima") == 0 || strcmp(value, "cripto") == 0) {
        shown = display_request_dashboard(value, DASHBOARD_MS);
    } else {
        snprintf(result, result_len, "Error: unknown dashboard (use clima, cripto or panel)");
        return false;
    }
    if (!shown) {
        snprintf(result, result_len, "Error: la pantalla no est\xC3\xA1 disponible");
        return false;
    }
    snprintf(result, result_len, "Mostrando %s 30 s.", value);
    return true;
}
