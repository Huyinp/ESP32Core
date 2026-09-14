#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "recorder_state.h"

#define RECORDER_ID_MAX 64

typedef enum {
    RECORDING_MODE_MEETING = 0,
    RECORDING_MODE_SCRATCH_NOTE,
} recording_mode_t;

typedef struct {
    recorder_state_t state;
    recording_mode_t mode;
    uint32_t elapsed_ms;
    uint32_t pcm_bytes;
    uint32_t dropped_buffers;
    esp_err_t last_error;
    char active_recording_id[RECORDER_ID_MAX];
} recorder_snapshot_t;

typedef void (*recorder_observer_t)(const recorder_snapshot_t *snapshot, void *ctx);

typedef struct {
    void *ctx;
    esp_err_t (*storage_begin)(void *ctx, recording_mode_t mode, char *id, size_t id_size);
    esp_err_t (*audio_open)(void *ctx);
    esp_err_t (*audio_read)(void *ctx, int16_t *samples, size_t capacity, size_t *read);
    esp_err_t (*storage_append)(void *ctx, const void *pcm, size_t bytes);
    esp_err_t (*audio_close)(void *ctx);
    esp_err_t (*storage_commit)(void *ctx);
    recorder_observer_t observer;
    void *observer_ctx;
} recorder_config_t;

esp_err_t recorder_init(const recorder_config_t *config);
esp_err_t recorder_request_start(recording_mode_t mode);
esp_err_t recorder_request_stop(void);
recorder_snapshot_t recorder_get_snapshot(void);
