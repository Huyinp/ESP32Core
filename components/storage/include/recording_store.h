#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "wav_writer.h"

#define RECORDING_PATH_MAX 192

typedef struct {
    bool valid;
    int64_t unix_seconds;
    uint32_t boot_count;
    uint32_t sequence;
} recording_clock_t;

typedef struct {
    FILE *file;
    wav_format_t format;
    uint32_t pcm_bytes;
    uint32_t bytes_since_flush;
    char temp_path[RECORDING_PATH_MAX];
    char final_path[RECORDING_PATH_MAX];
} recording_store_t;

typedef struct {
    char path[RECORDING_PATH_MAX];
    uint32_t pcm_bytes;
    uint32_t file_bytes;
} recording_info_t;

typedef struct {
    uint32_t repaired;
    uint32_t corrupt;
    uint32_t failed;
} recovery_report_t;

esp_err_t recording_store_begin(recording_store_t *store, const char *root,
                                const recording_clock_t *clock, wav_format_t format);
esp_err_t recording_store_append(recording_store_t *store, const void *pcm, size_t bytes);
esp_err_t recording_store_commit(recording_store_t *store, recording_info_t *out);
esp_err_t recording_store_recover_all(const char *root, recovery_report_t *out);
