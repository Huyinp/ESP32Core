#include <limits.h>
#include <string.h>

#include "recorder_board.h"

#ifndef RECORDER_BOARD_TEST
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#endif

button_action_t button_classify(uint32_t held_ms)
{
    if (held_ms < RECORDER_BUTTON_DEBOUNCE_MS) {
        return BUTTON_ACTION_IGNORED;
    }
    if (held_ms < RECORDER_BUTTON_LONG_PRESS_MS) {
        return BUTTON_ACTION_SHORT_PRESS;
    }
    return BUTTON_ACTION_LONG_PRESS;
}

esp_err_t recorder_board_init(void)
{
#ifdef RECORDER_BOARD_TEST
    return ESP_OK;
#else
    return bsp_i2c_init();
#endif
}

esp_err_t recorder_board_mount_sd(const char **mount_path)
{
    if (mount_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef RECORDER_BOARD_TEST
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t error = bsp_sdcard_mount();
    if (error == ESP_OK) {
        *mount_path = BSP_SD_MOUNT_POINT;
    }
    return error;
#endif
}

esp_err_t recorder_board_open_mic(recorder_audio_source_t *out, uint32_t rate_hz)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rate_hz != 16000) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    memset(out, 0, sizeof(*out));
#ifdef RECORDER_BOARD_TEST
    return ESP_ERR_NOT_SUPPORTED;
#else
    /* V2 BSP 2.0.3 exports ES8311 on MCLK16/BCLK9/WS45/DOUT8/DIN10. */
    i2s_std_config_t i2s = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
            .invert_flags = {0},
        },
    };
    esp_err_t error = bsp_audio_init(&i2s);
    if (error != ESP_OK) {
        return error;
    }
    esp_codec_dev_handle_t codec = bsp_audio_codec_microphone_init();
    if (codec == NULL) {
        return ESP_FAIL;
    }
    esp_codec_dev_sample_info_t format = {
        .sample_rate = rate_hz,
        .channel = 1,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(codec, &format) != 0) {
        return ESP_FAIL;
    }
    if (esp_codec_dev_set_in_gain(codec, 30.0f) != 0) {
        esp_codec_dev_close(codec);
        return ESP_FAIL;
    }
    out->codec = codec;
    out->open = true;
    return ESP_OK;
#endif
}

esp_err_t recorder_audio_read(recorder_audio_source_t *source, int16_t *samples,
                              size_t capacity, size_t *read)
{
    if (source == NULL || !source->open || samples == NULL || read == NULL ||
        capacity == 0 || capacity > INT_MAX / sizeof(*samples)) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef RECORDER_BOARD_TEST
    return ESP_ERR_NOT_SUPPORTED;
#else
    const int bytes = (int)(capacity * sizeof(*samples));
    if (esp_codec_dev_read(source->codec, samples, bytes) != 0) {
        return ESP_FAIL;
    }
    *read = capacity;
    return ESP_OK;
#endif
}

esp_err_t recorder_audio_close(recorder_audio_source_t *source)
{
    if (source == NULL || !source->open) {
        return ESP_ERR_INVALID_STATE;
    }
#ifndef RECORDER_BOARD_TEST
    if (esp_codec_dev_close(source->codec) != 0) {
        return ESP_FAIL;
    }
#endif
    source->codec = NULL;
    source->open = false;
    return ESP_OK;
}
