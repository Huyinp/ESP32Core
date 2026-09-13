#pragma once

#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

typedef struct {
    uint32_t sample_rate_hz;
    uint16_t bits_per_sample;
    uint16_t channels;
} wav_format_t;

esp_err_t wav_write_placeholder(FILE *file, const wav_format_t *format);
esp_err_t wav_finalize(FILE *file, uint32_t pcm_bytes, const wav_format_t *format);
esp_err_t wav_repair(FILE *file, uint32_t actual_file_bytes, const wav_format_t *format);
