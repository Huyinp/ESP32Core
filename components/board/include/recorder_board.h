#pragma once

#include <stdint.h>

#define RECORDER_BUTTON_DEBOUNCE_MS 50U
#define RECORDER_BUTTON_LONG_PRESS_MS 1000U

typedef enum {
    BUTTON_ACTION_IGNORED = 0,
    BUTTON_ACTION_SHORT_PRESS,
    BUTTON_ACTION_LONG_PRESS,
} button_action_t;

button_action_t button_classify(uint32_t held_ms);
