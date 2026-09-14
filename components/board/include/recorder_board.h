#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define RECORDER_BUTTON_DEBOUNCE_MS 50U
#define RECORDER_BUTTON_LONG_PRESS_MS 1000U

typedef enum {
    BUTTON_ACTION_IGNORED = 0,
    BUTTON_ACTION_SHORT_PRESS,
    BUTTON_ACTION_LONG_PRESS,
} button_action_t;

typedef struct {
    void *codec;
    bool open;
} recorder_audio_source_t;

button_action_t button_classify(uint32_t held_ms);
esp_err_t recorder_board_init(void);
esp_err_t recorder_board_mount_sd(const char **mount_path);
esp_err_t recorder_board_open_mic(recorder_audio_source_t *out, uint32_t rate_hz);
esp_err_t recorder_audio_read(recorder_audio_source_t *source, int16_t *samples,
                              size_t capacity, size_t *read);
esp_err_t recorder_audio_close(recorder_audio_source_t *source);
