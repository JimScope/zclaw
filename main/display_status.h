#ifndef DISPLAY_STATUS_H
#define DISPLAY_STATUS_H

#include <stdbool.h>
#include <stdint.h>

// Draw the full status page (clears the screen).
void display_status_draw(bool wifi_up);

// Call from the display task loop: schedules the 30-minute weather refresh.
void display_status_service(int64_t now_ms, bool wifi_up);

#endif // DISPLAY_STATUS_H
