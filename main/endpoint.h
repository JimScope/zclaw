#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>

// End-of-speech detection on 10 ms frames (RMS of int16 audio).
#define ENDPOINT_START_FRAMES     6     // loud frames in a row that start speech (60 ms)
#define ENDPOINT_END_FRAMES       120   // quiet frames after speech that end it (1.2 s)
#define ENDPOINT_MAX_FRAMES       1000  // hard limit from the start of listening (10 s)
#define ENDPOINT_NO_SPEECH_FRAMES 400   // give up without speech (4 s)

typedef enum { ENDPOINT_CONTINUE = 0, ENDPOINT_DONE, ENDPOINT_NO_SPEECH } endpoint_result_t;

typedef struct {
    uint32_t threshold;
    uint32_t frames;
    uint32_t loud_run;
    uint32_t quiet_run;
    bool speech;
} endpoint_t;

// threshold = max(noise_rms x ratio, min_level)
void endpoint_init(endpoint_t *e, uint32_t noise_rms, uint32_t ratio, uint32_t min_level);
endpoint_result_t endpoint_frame(endpoint_t *e, uint16_t rms);
bool endpoint_heard_speech(const endpoint_t *e);

#endif // ENDPOINT_H
