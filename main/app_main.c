#include <errno.h>
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
#include "recorder.h"
#include "recorder_board.h"
#include "recorder_ui_model.h"
#include "recording_store.h"

static const char *TAG = "recorder";

typedef struct {
    TaskHandle_t waiter;
    esp_err_t result;
} diagnostic_task_context_t;

static diagnostic_task_context_t diagnostic_context;

typedef struct {
    const char *mount_path;
    uint32_t sequence;
    recorder_audio_source_t source;
    recording_store_t store;
} recorder_runtime_t;

static recorder_runtime_t recorder_runtime;
static recorder_failure_t recorder_failure;
static lv_obj_t *status_label;
static lv_obj_t *detail_label;

static esp_err_t runtime_storage_begin(void *context, recording_mode_t mode,
                                       char *id, size_t id_size)
{
    (void)mode;
    recorder_runtime_t *runtime = context;
    recorder_failure = RECORDER_FAILURE_STORAGE;
    const recording_clock_t clock = {
        .boot_count = 0,
        .sequence = ++runtime->sequence,
    };
    const wav_format_t format = {
        .sample_rate_hz = 16000,
        .bits_per_sample = 16,
        .channels = 1,
    };
    esp_err_t error = recording_store_begin(&runtime->store, runtime->mount_path,
                                            &clock, format);
    if (error == ESP_OK) {
        recorder_failure = RECORDER_FAILURE_NONE;
        const char *name = strrchr(runtime->store.final_path, '/');
        snprintf(id, id_size, "%s", name == NULL ? runtime->store.final_path : name + 1);
    } else {
        ESP_LOGE(TAG, "SD begin failed: %s errno=%d (%s) path=%s",
                 esp_err_to_name(error), errno, strerror(errno),
                 runtime->store.temp_path);
    }
    return error;
}

static esp_err_t runtime_audio_open(void *context)
{
    recorder_runtime_t *runtime = context;
    const esp_err_t error = recorder_board_open_mic(&runtime->source, 16000);
    if (error != ESP_OK) {
        recorder_failure = RECORDER_FAILURE_AUDIO;
    }
    return error;
}

static esp_err_t runtime_audio_read(void *context, int16_t *samples,
                                    size_t capacity, size_t *read)
{
    recorder_runtime_t *runtime = context;
    const esp_err_t error = recorder_audio_read(&runtime->source, samples, capacity, read);
    if (error != ESP_OK) {
        recorder_failure = RECORDER_FAILURE_AUDIO;
        ESP_LOGE(TAG, "microphone read failed: %s", esp_err_to_name(error));
    }
    return error;
}

static esp_err_t runtime_storage_append(void *context, const void *pcm, size_t bytes)
{
    recorder_runtime_t *runtime = context;
    const esp_err_t error = recording_store_append(&runtime->store, pcm, bytes);
    if (error != ESP_OK) {
        recorder_failure = RECORDER_FAILURE_STORAGE;
        ESP_LOGE(TAG, "SD append failed: %s bytes_written=%" PRIu32,
                 esp_err_to_name(error), runtime->store.pcm_bytes);
    }
    return error;
}

static esp_err_t runtime_audio_close(void *context)
{
    recorder_runtime_t *runtime = context;
    return recorder_audio_close(&runtime->source);
}

static esp_err_t runtime_storage_commit(void *context)
{
    recorder_runtime_t *runtime = context;
    const esp_err_t error = recording_store_commit(&runtime->store, NULL);
    if (error != ESP_OK) {
        recorder_failure = RECORDER_FAILURE_STORAGE;
    }
    return error;
}

static void recorder_ui_refresh(lv_timer_t *timer)
{
    (void)timer;
    const recorder_snapshot_t snapshot = recorder_get_snapshot();
    const recorder_ui_view_t view = recorder_ui_model(&snapshot, recorder_failure);
    lv_label_set_text(status_label, view.status);
    lv_obj_set_style_text_color(status_label, lv_color_hex(view.color_rgb), LV_PART_MAIN);
    lv_label_set_text(detail_label, view.detail);
    lv_obj_set_style_text_color(detail_label, lv_color_hex(view.color_rgb), LV_PART_MAIN);
}

static void recorder_observer(const recorder_snapshot_t *snapshot, void *context)
{
    (void)context;
    ESP_LOGI(TAG, "recording state=%d elapsed=%" PRIu32 "ms bytes=%" PRIu32
             " error=%s id=%s", snapshot->state, snapshot->elapsed_ms,
             snapshot->pcm_bytes, esp_err_to_name(snapshot->last_error),
             snapshot->active_recording_id);
}

static void recorder_button(button_action_t action, void *context)
{
    (void)context;
    if (action == BUTTON_ACTION_LONG_PRESS) {
        ESP_LOGI(TAG, "PWR long press: shutdown is not enabled yet");
        return;
    }
    const recorder_snapshot_t snapshot = recorder_get_snapshot();
    esp_err_t error;
    if (snapshot.state == RECORDER_IDLE || snapshot.state == RECORDER_STORED) {
        error = recorder_request_start(RECORDING_MODE_MEETING);
    } else if (snapshot.state == RECORDER_RECORDING) {
        error = recorder_request_stop();
    } else {
        error = ESP_ERR_INVALID_STATE;
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "PWR recording command failed: %s state=%d last_error=%s",
                 esp_err_to_name(error), snapshot.state,
                 esp_err_to_name(snapshot.last_error));
    }
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

    if (diagnostic_requested()) {
        ESP_LOGI(TAG, "BOOT held: starting five-second microphone diagnostic");
        ESP_ERROR_CHECK(run_diagnostic_task());
    } else {
        ESP_ERROR_CHECK(recorder_board_init());
        ESP_ERROR_CHECK(recorder_board_mount_sd(&recorder_runtime.mount_path));
        recorder_runtime.sequence = esp_random();
        recovery_report_t recovery = {0};
        ESP_ERROR_CHECK(recording_store_recover_all(recorder_runtime.mount_path, &recovery));
        ESP_LOGI(TAG, "storage recovery repaired=%" PRIu32 " corrupt=%" PRIu32,
                 recovery.repaired, recovery.corrupt);
        const recorder_config_t recorder_config = {
            .ctx = &recorder_runtime,
            .storage_begin = runtime_storage_begin,
            .audio_open = runtime_audio_open,
            .audio_read = runtime_audio_read,
            .storage_append = runtime_storage_append,
            .audio_close = runtime_audio_close,
            .storage_commit = runtime_storage_commit,
            .observer = recorder_observer,
        };
        ESP_ERROR_CHECK(recorder_init(&recorder_config));
        ESP_ERROR_CHECK(recorder_board_register_button(recorder_button, NULL));
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

    status_label = lv_label_create(screen);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -18);

    detail_label = lv_label_create(screen);
    lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(detail_label, LV_ALIGN_CENTER, 0, 18);

    recorder_ui_refresh(NULL);
    lv_timer_create(recorder_ui_refresh, 250, NULL);

    bsp_display_unlock();
    ESP_ERROR_CHECK(bsp_display_backlight_on());
}
