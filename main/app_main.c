#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "recorder_board.h"
#include "recording_store.h"

static const char *TAG = "recorder";

typedef struct {
    TaskHandle_t waiter;
    esp_err_t result;
} diagnostic_task_context_t;

static diagnostic_task_context_t diagnostic_context;

static void button_probe(button_action_t action, void *context)
{
    (void)context;
    ESP_LOGI(TAG, "PWR probe classified action=%s",
             action == BUTTON_ACTION_SHORT_PRESS ? "short" : "long");
}

static bool diagnostic_requested(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&config) == ESP_OK && gpio_get_level(GPIO_NUM_0) == 0;
}

static esp_err_t capture_diagnostic(void)
{
    const char *mount_path = NULL;
    ESP_RETURN_ON_ERROR(recorder_board_init(), TAG, "board init failed");
    ESP_RETURN_ON_ERROR(recorder_board_mount_sd(&mount_path), TAG, "SD mount failed");

    recorder_audio_source_t source = {0};
    ESP_RETURN_ON_ERROR(recorder_board_open_mic(&source, 16000), TAG, "microphone open failed");

    recording_store_t store = {0};
    const recording_clock_t clock = {.boot_count = 0, .sequence = esp_random()};
    const wav_format_t format = {.sample_rate_hz = 16000, .bits_per_sample = 16, .channels = 1};
    esp_err_t error = recording_store_begin(&store, mount_path, &clock, format);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "recording store begin failed: %s (%s)",
                 esp_err_to_name(error), store.temp_path);
        recorder_audio_close(&source);
        return error;
    }

    int16_t samples[512];
    uint32_t captured = 0;
    uint32_t zeros = 0;
    uint32_t clipped = 0;
    uint32_t peak = 0;
    while (captured < 80000) {
        const size_t wanted = captured + 512 <= 80000 ? 512 : 80000 - captured;
        size_t count = 0;
        error = recorder_audio_read(&source, samples, wanted, &count);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "microphone read failed after %" PRIu32 " samples", captured);
            break;
        }
        error = recording_store_append(&store, samples, count * sizeof(samples[0]));
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "SD append failed after %" PRIu32 " samples", captured);
            break;
        }
        for (size_t i = 0; i < count; ++i) {
            const int32_t value = samples[i];
            const uint32_t magnitude = value < 0 ? (uint32_t)-value : (uint32_t)value;
            if (magnitude > peak) peak = magnitude;
            if (value == 0) ++zeros;
            if (value == INT16_MIN || value == INT16_MAX) ++clipped;
        }
        captured += (uint32_t)count;
    }
    esp_err_t close_error = recorder_audio_close(&source);
    recording_info_t info = {0};
    if (error == ESP_OK) error = close_error;
    if (error == ESP_OK) error = recording_store_commit(&store, &info);
    if (error == ESP_OK) {
        char diagnostic_path[RECORDING_PATH_MAX];
        snprintf(diagnostic_path, sizeof(diagnostic_path), "%s/diagnostic.wav", mount_path);
        remove(diagnostic_path);
        if (rename(info.path, diagnostic_path) != 0) {
            error = ESP_FAIL;
        } else {
            struct stat file_info;
            if (stat(diagnostic_path, &file_info) != 0 || file_info.st_size != 160044) {
                ESP_LOGE(TAG, "diagnostic WAV size is not 160044 bytes");
                error = ESP_FAIL;
            } else {
                ESP_LOGI(TAG, "diagnostic WAV=%s bytes=%ld", diagnostic_path,
                         (long)file_info.st_size);
            }
        }
    }
    ESP_LOGI(TAG, "DIAGNOSTIC samples=%" PRIu32 " pcm_bytes=%" PRIu32
             " peak=%" PRIu32 " clipped=%" PRIu32 " zeros=%" PRIu32,
             captured, captured * 2, peak, clipped, zeros);
    return error;
}

static void diagnostic_task(void *argument)
{
    diagnostic_task_context_t *context = argument;
    context->result = capture_diagnostic();
    xTaskNotifyGive(context->waiter);
    vTaskDelete(NULL);
}

static esp_err_t run_diagnostic_task(void)
{
    diagnostic_context = (diagnostic_task_context_t){
        .waiter = xTaskGetCurrentTaskHandle(),
        .result = ESP_FAIL,
    };
    if (xTaskCreate(diagnostic_task, "diagnostic", 8192, &diagnostic_context, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return diagnostic_context.result;
}

void app_main(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_bytes = 0;

    esp_chip_info(&chip_info);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_bytes));

    const size_t psram_bytes = esp_psram_get_size();
    if (psram_bytes == 0) {
        ESP_LOGE(TAG, "required PSRAM was not detected");
        ESP_ERROR_CHECK(ESP_ERR_NOT_FOUND);
    }

    if (!BSP_CAPS_AUDIO || !BSP_CAPS_SDCARD || !BSP_CAPS_TOUCH) {
        ESP_LOGE(TAG, "required BSP capability is absent: audio=%d sdcard=%d touch=%d",
                 BSP_CAPS_AUDIO, BSP_CAPS_SDCARD, BSP_CAPS_TOUCH);
        ESP_ERROR_CHECK(ESP_ERR_NOT_SUPPORTED);
    }

    ESP_LOGI(TAG, "RECORDER_BOARD_READY");
    ESP_LOGI(TAG, "target=esp32s3 cores=%d", chip_info.cores);
    ESP_LOGI(TAG, "flash=%" PRIu32 " bytes psram=%u bytes",
             flash_bytes, (unsigned)psram_bytes);
    ESP_LOGI(TAG, "display=%dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    ESP_LOGI(TAG, "audio=%d sdcard=%d touch=%d",
             BSP_CAPS_AUDIO, BSP_CAPS_SDCARD, BSP_CAPS_TOUCH);

    esp_err_t button_error = recorder_board_init();
    if (button_error == ESP_OK) {
        button_error = recorder_board_register_button(button_probe, NULL);
    }
    if (button_error != ESP_OK) {
        ESP_LOGE(TAG, "PWR probe unavailable: %s", esp_err_to_name(button_error));
    }

    if (diagnostic_requested()) {
        ESP_LOGI(TAG, "BOOT held: starting five-second microphone diagnostic");
        ESP_ERROR_CHECK(run_diagnostic_task());
    }

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "display initialization failed");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    if (!bsp_display_lock(0)) {
        ESP_LOGE(TAG, "failed to lock display");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070A), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "RECORDER READY\nV2");
    lv_obj_set_style_text_color(title, lv_color_hex(0x4ADE80), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(title);

    bsp_display_unlock();
    ESP_ERROR_CHECK(bsp_display_backlight_on());
}
