#pragma once

#include "esp_err.h"

typedef int (*recorder_codec_set_gain_fn)(void *codec, float gain_db);

esp_err_t recorder_board_configure_mic_gain(void *codec,
                                            recorder_codec_set_gain_fn set_gain);
