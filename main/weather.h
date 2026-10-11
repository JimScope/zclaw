#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"

typedef struct {
    float temp_c;
    float humidity;
    float wind_kmh;
    int code;  // WMO weather code
} weather_current_t;

// Current weather from Open-Meteo for lat/lon, or for the saved u_ubicacion
// memory when has_coords is false. Waits up to gate_wait for the HTTP gate.
bool weather_fetch_current(bool has_coords, float lat, float lon, TickType_t gate_wait,
                           weather_current_t *out, char *err, size_t err_len);

// Short Spanish description of a WMO code.
const char *weather_code_desc(int code);

#endif // WEATHER_H
