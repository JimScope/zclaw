#ifndef WEB_OTA_H
#define WEB_OTA_H

#include "esp_err.h"

// Start the LAN firmware upload page (port 80): GET / and POST /ota with an
// X-OTA-PIN header. ESP_ERR_NOT_SUPPORTED when CONFIG_ZCLAW_WEB_OTA_ENABLE
// is off; ESP_ERR_INVALID_ARG when the PIN length is outside 4-32.
esp_err_t web_ota_start(void);

#endif // WEB_OTA_H
