#pragma once

#include "recorder.h"

typedef struct {
    recorder_config_t config;
    recorder_snapshot_t snapshot;
    bool audio_open;
} recorder_engine_t;

esp_err_t recorder_engine_init(recorder_engine_t *engine, const recorder_config_t *config);
esp_err_t recorder_engine_start(recorder_engine_t *engine, recording_mode_t mode);
esp_err_t recorder_engine_capture_once(recorder_engine_t *engine);
esp_err_t recorder_engine_stop(recorder_engine_t *engine);
