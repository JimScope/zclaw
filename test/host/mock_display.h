#ifndef MOCK_DISPLAY_H
#define MOCK_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

void mock_display_reset(void);
void mock_display_set_running(bool running);
uint32_t mock_display_last_status_ms(void);
int mock_display_talking_calls(void);

#endif // MOCK_DISPLAY_H
