# TODO

## Mover WiFi de respaldo y key de Vendel de Kconfig a NVS

Hoy la WiFi primaria vive en NVS (`wifi_ssid`/`wifi_pass`, escrita por
`scripts/provision.sh`), pero la de respaldo (`CONFIG_ZCLAW_WIFI_FALLBACK_SSID`/
`_PASSWORD`) y `CONFIG_ZCLAW_VENDEL_API_KEY` se compilan en el firmware.

Problemas:
- Quedan en texto plano dentro de `zclaw.bin` (`strings` las muestra), y el .bin
  viaja por la red en cada OTA.
- Un build sin el `sdkconfig`/`sdkconfig.defaults.esp32c6` correcto se instala
  sin error y pierde el respaldo y los SMS en silencio (el arranque es estable,
  así que el rollback no salta).
- Cambiarlas exige recompilar + OTA en vez de re-provisionar.

Plan:
- Nuevas claves NVS en `main/nvs_keys.h` (p. ej. `wifi_fb_ssid`, `wifi_fb_pass`,
  `vendel_key`; ojo al límite de 15 caracteres de NVS).
- `main/local_admin.c` y `main/tools_net.c`: leer de NVS y usar el valor de
  Kconfig solo como default si la clave no existe.
- `scripts/provision.sh`: flags opcionales `--fallback-ssid`, `--fallback-pass`,
  `--vendel-key`.
- Al migrar, provisionar los valores en la placa antes de mandar un OTA que ya
  no los lleve compilados.
