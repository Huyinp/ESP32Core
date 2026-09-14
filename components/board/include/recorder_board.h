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
    bool pressed;
    uint32_t pressed_at_ms;
} recorder_button_tracker_t;

typedef void (*recorder_button_cb_t)(button_action_t action, void *ctx);

typedef struct {
    void *codec;
    bool open;
} recorder_audio_source_t;

button_action_t button_classify(uint32_t held_ms);
bool recorder_button_track_edge(recorder_button_tracker_t *tracker, bool pressed,
                                uint32_t now_ms, button_action_t *action);
esp_err_t recorder_board_init(void);
esp_err_t recorder_board_register_button(recorder_button_cb_t cb, void *ctx);
esp_err_t recorder_board_mount_sd(const char **mount_path);
esp_err_t recorder_board_open_mic(recorder_audio_source_t *out, uint32_t rate_hz);
esp_err_t recorder_audio_read(recorder_audio_source_t *source, int16_t *samples,
                              size_t capacity, size_t *read);
esp_err_t recorder_audio_close(recorder_audio_source_t *source);
