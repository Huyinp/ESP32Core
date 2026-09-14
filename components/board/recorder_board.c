#include <limits.h>
#include <string.h>

#include "recorder_board.h"

#ifndef RECORDER_BOARD_TEST
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

bool recorder_button_track_edge(recorder_button_tracker_t *tracker, bool pressed,
                                uint32_t now_ms, button_action_t *action)
{
    if (tracker == NULL || action == NULL || tracker->pressed == pressed) {
        return false;
    }
    tracker->pressed = pressed;
    if (pressed) {
        tracker->pressed_at_ms = now_ms;
        return false;
    }
    *action = button_classify(now_ms - tracker->pressed_at_ms);
    return true;
}

#ifndef RECORDER_BOARD_TEST
#define AXP2101_ADDRESS 0x34
#define AXP2101_CHIP_ID_REG 0x03
#define AXP2101_CHIP_ID 0x4A
#define AXP2101_IRQ_ENABLE2_REG 0x41
#define AXP2101_IRQ_STATUS2_REG 0x49
#define AXP2101_PKEY_POSITIVE_BIT (1U << 0)
#define AXP2101_PKEY_NEGATIVE_BIT (1U << 1)

static const char *TAG = "recorder_board";
static i2c_master_dev_handle_t button_pmu;
static recorder_button_cb_t button_callback;
static void *button_callback_context;
static esp_codec_dev_handle_t microphone_codec;

static esp_err_t pmu_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(button_pmu, &reg, 1, value, 1, 100);
}

static esp_err_t pmu_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(button_pmu, data, sizeof(data), 100);
}

static void button_task(void *argument)
{
    (void)argument;
    recorder_button_tracker_t tracker = {0};
    while (true) {
        uint8_t status = 0;
        if (pmu_read(AXP2101_IRQ_STATUS2_REG, &status) == ESP_OK) {
            const uint8_t edges = status &
                (AXP2101_PKEY_POSITIVE_BIT | AXP2101_PKEY_NEGATIVE_BIT);
            if (edges != 0) {
                pmu_write(AXP2101_IRQ_STATUS2_REG, edges);
                const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
                button_action_t action;
                if (edges & AXP2101_PKEY_NEGATIVE_BIT) {
                    ESP_LOGI(TAG, "PWR pressed irq=0x%02x", status);
                    recorder_button_track_edge(&tracker, true, now_ms, &action);
                }
                if (edges & AXP2101_PKEY_POSITIVE_BIT) {
                    ESP_LOGI(TAG, "PWR released irq=0x%02x", status);
                    if (recorder_button_track_edge(&tracker, false, now_ms, &action)) {
                        ESP_LOGI(TAG, "PWR action=%d", action);
                        if (action != BUTTON_ACTION_IGNORED) {
                            button_callback(action, button_callback_context);
                        }
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif

esp_err_t recorder_board_init(void)
{
#ifdef RECORDER_BOARD_TEST
    return ESP_OK;
#else
    return bsp_i2c_init();
#endif
}

esp_err_t recorder_board_register_button(recorder_button_cb_t cb, void *ctx)
{
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef RECORDER_BOARD_TEST
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (button_callback != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        return ESP_FAIL;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDRESS,
        .scl_speed_hz = 100000,
    };
    esp_err_t error = i2c_master_bus_add_device(bus, &config, &button_pmu);
    if (error != ESP_OK) {
        return error;
    }
    uint8_t chip_id = 0;
    error = pmu_read(AXP2101_CHIP_ID_REG, &chip_id);
    if (error != ESP_OK || chip_id != AXP2101_CHIP_ID) {
        i2c_master_bus_rm_device(button_pmu);
        button_pmu = NULL;
        return error == ESP_OK ? ESP_ERR_NOT_FOUND : error;
    }
    uint8_t enabled = 0;
    error = pmu_read(AXP2101_IRQ_ENABLE2_REG, &enabled);
    if (error == ESP_OK) {
        enabled |= AXP2101_PKEY_POSITIVE_BIT | AXP2101_PKEY_NEGATIVE_BIT;
        error = pmu_write(AXP2101_IRQ_ENABLE2_REG, enabled);
    }
    if (error == ESP_OK) {
        error = pmu_write(AXP2101_IRQ_STATUS2_REG,
                          AXP2101_PKEY_POSITIVE_BIT | AXP2101_PKEY_NEGATIVE_BIT);
    }
    if (error != ESP_OK) {
        i2c_master_bus_rm_device(button_pmu);
        button_pmu = NULL;
        return error;
    }
    button_callback = cb;
    button_callback_context = ctx;
    if (xTaskCreate(button_task, "pwr_button", 3072, NULL, 4, NULL) != pdPASS) {
        button_callback = NULL;
        button_callback_context = NULL;
        i2c_master_bus_rm_device(button_pmu);
        button_pmu = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "AXP2101 PWR edge polling ready");
    return ESP_OK;
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
    if (microphone_codec == NULL) {
        microphone_codec = bsp_audio_codec_microphone_init();
        if (microphone_codec == NULL) {
            return ESP_FAIL;
        }
    }
    esp_codec_dev_sample_info_t format = {
        .sample_rate = rate_hz,
        .channel = 1,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(microphone_codec, &format) != 0) {
        return ESP_FAIL;
    }
    if (esp_codec_dev_set_in_gain(microphone_codec, 30.0f) != 0) {
        esp_codec_dev_close(microphone_codec);
        return ESP_FAIL;
    }
    out->codec = microphone_codec;
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
