#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "recorder_internal.h"

static recorder_engine_t global_engine;
static TaskHandle_t capture_task_handle;
static volatile bool stop_requested;
static bool initialized;

static void publish(recorder_engine_t *engine)
{
    if (engine->config.observer != NULL) {
        engine->config.observer(&engine->snapshot, engine->config.observer_ctx);
    }
}

static esp_err_t fail(recorder_engine_t *engine, esp_err_t error)
{
    if (engine->audio_open) {
        engine->config.audio_close(engine->config.ctx);
        engine->audio_open = false;
    }
    engine->snapshot.last_error = error;
    engine->snapshot.state = RECORDER_RECOVERY_REQUIRED;
    publish(engine);
    return error;
}

esp_err_t recorder_engine_init(recorder_engine_t *engine, const recorder_config_t *config)
{
    if (engine == NULL || config == NULL || config->storage_begin == NULL ||
        config->audio_open == NULL || config->audio_read == NULL ||
        config->storage_append == NULL || config->audio_close == NULL ||
        config->storage_commit == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(engine, 0, sizeof(*engine));
    engine->config = *config;
    engine->snapshot.state = RECORDER_IDLE;
    return ESP_OK;
}

esp_err_t recorder_engine_start(recorder_engine_t *engine, recording_mode_t mode)
{
    if (engine == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (engine->snapshot.state != RECORDER_IDLE && engine->snapshot.state != RECORDER_STORED) {
        return ESP_ERR_INVALID_STATE;
    }
    engine->snapshot = (recorder_snapshot_t){
        .state = RECORDER_PREPARING,
        .mode = mode,
        .last_error = ESP_OK,
    };
    publish(engine);
    esp_err_t error = engine->config.storage_begin(
        engine->config.ctx, mode, engine->snapshot.active_recording_id,
        sizeof(engine->snapshot.active_recording_id));
    if (error != ESP_OK) {
        return fail(engine, error);
    }
    error = engine->config.audio_open(engine->config.ctx);
    if (error != ESP_OK) {
        return fail(engine, error);
    }
    engine->audio_open = true;
    engine->snapshot.state = RECORDER_RECORDING;
    publish(engine);
    return ESP_OK;
}

esp_err_t recorder_engine_capture_once(recorder_engine_t *engine)
{
    if (engine == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (engine->snapshot.state != RECORDER_RECORDING) {
        return ESP_ERR_INVALID_STATE;
    }
    int16_t samples[512];
    size_t count = 0;
    esp_err_t error = engine->config.audio_read(engine->config.ctx, samples, 512, &count);
    if (error != ESP_OK) {
        return fail(engine, error);
    }
    if (count > 512) {
        return fail(engine, ESP_ERR_INVALID_SIZE);
    }
    error = engine->config.storage_append(engine->config.ctx, samples,
                                          count * sizeof(samples[0]));
    if (error != ESP_OK) {
        return fail(engine, error);
    }
    engine->snapshot.pcm_bytes += (uint32_t)(count * sizeof(samples[0]));
    engine->snapshot.elapsed_ms = engine->snapshot.pcm_bytes / 32;
    return ESP_OK;
}

esp_err_t recorder_engine_stop(recorder_engine_t *engine)
{
    if (engine == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (engine->snapshot.state != RECORDER_RECORDING) {
        return ESP_ERR_INVALID_STATE;
    }
    engine->snapshot.state = RECORDER_FINALIZING;
    publish(engine);
    esp_err_t error = engine->config.audio_close(engine->config.ctx);
    engine->audio_open = false;
    if (error != ESP_OK) {
        return fail(engine, error);
    }
    error = engine->config.storage_commit(engine->config.ctx);
    if (error != ESP_OK) {
        return fail(engine, error);
    }
    engine->snapshot.state = RECORDER_STORED;
    publish(engine);
    return ESP_OK;
}

static void capture_task(void *argument)
{
    (void)argument;
    while (!stop_requested && global_engine.snapshot.state == RECORDER_RECORDING) {
        if (recorder_engine_capture_once(&global_engine) != ESP_OK) {
            break;
        }
    }
    if (global_engine.snapshot.state == RECORDER_RECORDING) {
        recorder_engine_stop(&global_engine);
    }
    capture_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t recorder_init(const recorder_config_t *config)
{
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = recorder_engine_init(&global_engine, config);
    if (error == ESP_OK) {
        initialized = true;
    }
    return error;
}

esp_err_t recorder_request_start(recording_mode_t mode)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = recorder_engine_start(&global_engine, mode);
    if (error != ESP_OK) {
        return error;
    }
    stop_requested = false;
    if (xTaskCreate(capture_task, "audio_capture", 4096, NULL, 6,
                    &capture_task_handle) != pdPASS) {
        return fail(&global_engine, ESP_ERR_NO_MEM);
    }
    return ESP_OK;
}

esp_err_t recorder_request_stop(void)
{
    if (!initialized || global_engine.snapshot.state != RECORDER_RECORDING) {
        return ESP_ERR_INVALID_STATE;
    }
    stop_requested = true;
    return ESP_OK;
}

recorder_snapshot_t recorder_get_snapshot(void)
{
    return global_engine.snapshot;
}
