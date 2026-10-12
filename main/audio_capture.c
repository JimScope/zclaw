#include "audio_capture.h"

#include "config.h"

#if CONFIG_ZCLAW_VOICE_ENABLE

#include "audio_dsp.h"
#include "esp_adc/adc_continuous.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAPTURE_ADC_RATE_HZ  (AUDIO_CAPTURE_RATE_HZ * 2)
#define CAPTURE_READ_RESULTS 320  // 10 ms at 32 kHz
#define CAPTURE_READ_BYTES   (CAPTURE_READ_RESULTS * SOC_ADC_DIGI_RESULT_BYTES)
#define CAPTURE_TASK_STACK   4096
#define CAPTURE_TASK_PRIO    5    // above the display task: the DMA pool must not overflow

static const char *TAG = "audio";
static adc_continuous_handle_t s_adc;
static adc_channel_t s_channel;
static audio_frame_cb_t s_cb;
static void *s_ctx;
static volatile uint32_t s_overruns;
static volatile uint16_t s_raw_mean = 2048;

static bool IRAM_ATTR on_pool_overflow(adc_continuous_handle_t handle,
                                       const adc_continuous_evt_data_t *edata, void *user_data)
{
    (void)handle;
    (void)edata;
    (void)user_data;
    s_overruns++;
    return false;
}

static void capture_task(void *arg)
{
    // 32-bit storage keeps every 4-byte conversion result aligned.
    static uint32_t buf[CAPTURE_READ_RESULTS];
    static uint16_t raw[CAPTURE_READ_RESULTS];
    static int16_t pcm[CAPTURE_READ_RESULTS / 2];
    static int16_t frame[AUDIO_CAPTURE_FRAME_SAMPLES];
    size_t fill = 0;
    int32_t mean_q4 = 2048 << 4;
    audio_dsp_t dsp;
    (void)arg;

    audio_dsp_init(&dsp);
    for (;;) {
        uint32_t got = 0;
        if (adc_continuous_read(s_adc, (uint8_t *)buf, sizeof(buf), &got, 100) != ESP_OK || got == 0) {
            continue;
        }
        size_t m = 0;
        uint32_t sum = 0;
        for (uint32_t i = 0; i < got / SOC_ADC_DIGI_RESULT_BYTES; i++) {
            const adc_digi_output_data_t *p = (const adc_digi_output_data_t *)&buf[i];
            if (p->type2.channel == s_channel) {
                raw[m] = (uint16_t)p->type2.data;
                sum += raw[m];
                m++;
            }
        }
        if (m < 2) {
            continue;
        }
        mean_q4 += ((int32_t)((sum / m) << 4) - mean_q4) / 16;
        s_raw_mean = (uint16_t)(mean_q4 >> 4);
        size_t produced = audio_dsp_convert(&dsp, raw, m & ~(size_t)1, pcm);
        for (size_t i = 0; i < produced; i++) {
            frame[fill++] = pcm[i];
            if (fill == AUDIO_CAPTURE_FRAME_SAMPLES) {
                s_cb(frame, fill, s_ctx);
                fill = 0;
            }
        }
    }
}

esp_err_t audio_capture_start(int gpio, audio_frame_cb_t cb, void *ctx)
{
    adc_unit_t unit;

    if (s_adc) {
        return ESP_OK;
    }
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = adc_continuous_io_to_channel(gpio, &unit, &s_channel);
    if (err != ESP_OK) {
        return err;
    }
    if (unit != ADC_UNIT_1) {
        return ESP_ERR_INVALID_ARG;
    }
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = CAPTURE_READ_BYTES * 4,
        .conv_frame_size = CAPTURE_READ_BYTES,
        .flags = { .flush_pool = 1 },
    };
    err = adc_continuous_new_handle(&handle_cfg, &s_adc);
    if (err != ESP_OK) {
        s_adc = NULL;
        return err;
    }
    adc_digi_pattern_config_t pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = (uint8_t)s_channel,
        .unit = (uint8_t)unit,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    adc_continuous_config_t cfg = {
        .pattern_num = 1,
        .adc_pattern = &pattern,
        .sample_freq_hz = CAPTURE_ADC_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    adc_continuous_evt_cbs_t cbs = { .on_pool_ovf = on_pool_overflow };
    s_cb = cb;
    s_ctx = ctx;
    err = adc_continuous_config(s_adc, &cfg);
    if (err == ESP_OK) {
        err = adc_continuous_register_event_callbacks(s_adc, &cbs, NULL);
    }
    if (err == ESP_OK) {
        err = adc_continuous_start(s_adc);
    }
    if (err == ESP_OK &&
        xTaskCreate(capture_task, "audio", CAPTURE_TASK_STACK, NULL, CAPTURE_TASK_PRIO, NULL) != pdPASS) {
        adc_continuous_stop(s_adc);
        err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        adc_continuous_deinit(s_adc);
        s_adc = NULL;
        return err;
    }
    ESP_LOGI(TAG, "Microphone on GPIO%d (ADC1 channel %d), %d Hz", gpio, (int)s_channel, CAPTURE_ADC_RATE_HZ);
    return ESP_OK;
}

uint32_t audio_capture_overruns(void)
{
    return s_overruns;
}

uint16_t audio_capture_raw_mean(void)
{
    return s_raw_mean;
}

#else  // !CONFIG_ZCLAW_VOICE_ENABLE

esp_err_t audio_capture_start(int gpio, audio_frame_cb_t cb, void *ctx)
{
    (void)gpio;
    (void)cb;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

uint32_t audio_capture_overruns(void)
{
    return 0;
}

uint16_t audio_capture_raw_mean(void)
{
    return 0;
}

#endif
