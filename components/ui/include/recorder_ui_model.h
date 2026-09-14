#pragma once

#include <stdint.h>

#include "recorder.h"

typedef enum {
    RECORDER_FAILURE_NONE,
    RECORDER_FAILURE_STORAGE,
    RECORDER_FAILURE_AUDIO,
} recorder_failure_t;

typedef struct {
    const char *status;
    char detail[12];
    uint32_t color_rgb;
} recorder_ui_view_t;

recorder_ui_view_t recorder_ui_model(const recorder_snapshot_t *snapshot,
                                     recorder_failure_t failure);
