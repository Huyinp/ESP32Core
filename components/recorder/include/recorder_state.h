#pragma once

#include <stdbool.h>

typedef enum {
    RECORDER_IDLE,
    RECORDER_PREPARING,
    RECORDER_RECORDING,
    RECORDER_FINALIZING,
    RECORDER_STORED,
    RECORDER_RECOVERY_REQUIRED,
    RECORDER_INVALID,
} recorder_state_t;

typedef enum {
    RECORDER_EVENT_START,
    RECORDER_EVENT_CAPTURE_READY,
    RECORDER_EVENT_STOP,
    RECORDER_EVENT_FINALIZED,
    RECORDER_EVENT_FAIL,
    RECORDER_EVENT_RECOVERED,
} recorder_event_t;

recorder_state_t recorder_state_next(recorder_state_t current, recorder_event_t event);
bool recorder_state_is_recording(recorder_state_t state);
