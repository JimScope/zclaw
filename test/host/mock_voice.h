#ifndef MOCK_VOICE_H
#define MOCK_VOICE_H

#include <stdbool.h>
#include <stdint.h>

void mock_voice_reset(void);
void mock_voice_set_running(bool running);
int mock_voice_listen_calls(void);
uint32_t mock_voice_last_probe_ms(void);

#endif // MOCK_VOICE_H
