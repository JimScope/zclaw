#include "endpoint.h"

void endpoint_init(endpoint_t *e, uint32_t noise_rms, uint32_t ratio, uint32_t min_level)
{
    uint32_t t = noise_rms * ratio;
    e->threshold = t > min_level ? t : min_level;
    e->frames = 0;
    e->loud_run = 0;
    e->quiet_run = 0;
    e->speech = false;
}

endpoint_result_t endpoint_frame(endpoint_t *e, uint16_t rms)
{
    bool loud = rms >= e->threshold;

    e->frames++;
    if (!e->speech) {
        e->loud_run = loud ? e->loud_run + 1 : 0;
        if (e->loud_run >= ENDPOINT_START_FRAMES) {
            e->speech = true;
            e->quiet_run = 0;
        } else if (e->frames >= ENDPOINT_NO_SPEECH_FRAMES) {
            return ENDPOINT_NO_SPEECH;
        }
    } else {
        e->quiet_run = loud ? 0 : e->quiet_run + 1;
        if (e->quiet_run >= ENDPOINT_END_FRAMES) {
            return ENDPOINT_DONE;
        }
    }
    if (e->frames >= ENDPOINT_MAX_FRAMES) {
        return e->speech ? ENDPOINT_DONE : ENDPOINT_NO_SPEECH;
    }
    return ENDPOINT_CONTINUE;
}

bool endpoint_heard_speech(const endpoint_t *e)
{
    return e->speech;
}
