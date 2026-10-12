#include "display_mic.h"

#include "config.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#include <stdio.h>

#include "display_gfx.h"
#include "display_hw.h"

#define MIC_VALUE_X (9 * 8)
#define MIC_ROW_Y   44
#define MIC_ROW_H   22
#define MIC_BAR_Y   164
#define MIC_BAR_H   24

static void format_db(int tenths, char *buf, size_t len)
{
    int a = tenths < 0 ? -tenths : tenths;
    snprintf(buf, len, "%s%d.%d dBFS   ", tenths < 0 ? "-" : "", a / 10, a % 10);
}

void display_mic_draw_frame(void)
{
    static const char *const labels[] = {"Pico", "Nivel", "Ruido", "Recorte", "ADC"};

    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);
    gfx_text(4, 6, "Micr\xC3\xB3" "fono", GFX_AMBER, GFX_BLACK, 2);
    for (int i = 0; i < 5; i++) {
        gfx_text(4, MIC_ROW_Y + i * MIC_ROW_H, labels[i], GFX_AMBER, GFX_BLACK, 1);
    }
    gfx_text(4, 200, "voz a 1 m: pico ~ -6 dBFS", GFX_GRAY, GFX_BLACK, 1);
    gfx_text(4, 218, "ruido < -45 dBFS, 0 % recorte", GFX_GRAY, GFX_BLACK, 1);
    display_hw_wait_idle();
}

void display_mic_draw_values(const display_mic_stats_t *s, uint8_t level)
{
    char b[40];
    int w = (DISPLAY_W - 8) * (level > 100 ? 100 : level) / 100;

    format_db(s->peak_dbfs10, b, sizeof(b));
    gfx_text(MIC_VALUE_X, MIC_ROW_Y, b, GFX_WHITE, GFX_BLACK, 1);
    format_db(s->rms_dbfs10, b, sizeof(b));
    gfx_text(MIC_VALUE_X, MIC_ROW_Y + MIC_ROW_H, b, GFX_WHITE, GFX_BLACK, 1);
    format_db(s->noise_dbfs10, b, sizeof(b));
    gfx_text(MIC_VALUE_X, MIC_ROW_Y + 2 * MIC_ROW_H, b, GFX_WHITE, GFX_BLACK, 1);
    snprintf(b, sizeof(b), "%u.%u %%   ", (unsigned)(s->clip_permille / 10), (unsigned)(s->clip_permille % 10));
    gfx_text(MIC_VALUE_X, MIC_ROW_Y + 3 * MIC_ROW_H, b, GFX_WHITE, GFX_BLACK, 1);
    snprintf(b, sizeof(b), "%u (perdidos %lu)   ", (unsigned)s->adc_offset, (unsigned long)s->overruns);
    gfx_text(MIC_VALUE_X, MIC_ROW_Y + 4 * MIC_ROW_H, b, GFX_WHITE, GFX_BLACK, 1);
    if (w > 0) {
        gfx_fill_rect(4, MIC_BAR_Y, w, MIC_BAR_H, GFX_AMBER);
    }
    if (w < DISPLAY_W - 8) {
        gfx_fill_rect(4 + w, MIC_BAR_Y, DISPLAY_W - 8 - w, MIC_BAR_H, GFX_GRAY);
    }
    display_hw_wait_idle();
}

#endif  // CONFIG_ZCLAW_DISPLAY_ENABLE
