#ifndef DISPLAY_MIC_H
#define DISPLAY_MIC_H

#include <stdint.h>

#include "display.h"

// /mic page: static labels, then values (overwritten in place every 250 ms).
void display_mic_draw_frame(void);
void display_mic_draw_values(const display_mic_stats_t *stats, uint8_t level);

#endif // DISPLAY_MIC_H
