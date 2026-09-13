#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "recording_store.h"

enum {
    WAV_HEADER_BYTES = 44,
};

static const wav_format_t recovery_format = {
    .sample_rate_hz = 16000,
    .bits_per_sample = 16,
    .channels = 1,
};

static bool path_exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static bool has_suffix(const char *text, const char *suffix)
{
    const size_t text_length = strlen(text);
    const size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length &&
           strcmp(text + text_length - suffix_length, suffix) == 0;
}

static esp_err_t make_paths(recording_store_t *store, const char *root,
                            const recording_clock_t *clock)
{
    char id[64];
    int length;

    if (clock->valid) {
        const time_t timestamp = (time_t)clock->unix_seconds;
        struct tm utc;
        if (gmtime_r(&timestamp, &utc) == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        length = snprintf(id, sizeof(id), "%04d%02d%02dT%02d%02d%02dZ-%04lu",
                          utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                          utc.tm_hour, utc.tm_min, utc.tm_sec,
                          (unsigned long)clock->sequence);
    } else {
        length = snprintf(id, sizeof(id), "boot-%08lu-%04lu",
                          (unsigned long)clock->boot_count,
                          (unsigned long)clock->sequence);
    }
    if (length < 0 || (size_t)length >= sizeof(id)) {
        return ESP_ERR_INVALID_SIZE;
    }

    length = snprintf(store->temp_path, sizeof(store->temp_path), "%s/%s.wav.part", root, id);
    if (length < 0 || (size_t)length >= sizeof(store->temp_path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    length = snprintf(store->final_path, sizeof(store->final_path), "%s/%s.wav", root, id);
    if (length < 0 || (size_t)length >= sizeof(store->final_path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t recording_store_begin(recording_store_t *store, const char *root,
                                const recording_clock_t *clock, wav_format_t format)
{
    if (store == NULL || root == NULL || root[0] == '\0' || clock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(store, 0, sizeof(*store));
    esp_err_t error = make_paths(store, root, clock);
    if (error != ESP_OK) {
        return error;
    }
    if (path_exists(store->temp_path) || path_exists(store->final_path)) {
        return ESP_ERR_INVALID_STATE;
    }

    store->file = fopen(store->temp_path, "wb");
    if (store->file == NULL) {
        return ESP_FAIL;
    }
    store->format = format;
    error = wav_write_placeholder(store->file, &store->format);
    if (error == ESP_OK && fflush(store->file) != 0) {
        error = ESP_FAIL;
    }
    if (error != ESP_OK) {
        fclose(store->file);
        store->file = NULL;
        remove(store->temp_path);
    }
    return error;
}

esp_err_t recording_store_append(recording_store_t *store, const void *pcm, size_t bytes)
{
    if (store == NULL || store->file == NULL || (pcm == NULL && bytes > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bytes > UINT32_MAX - 36 - store->pcm_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (bytes > 0 && fwrite(pcm, 1, bytes, store->file) != bytes) {
        return ESP_FAIL;
    }

    store->pcm_bytes += (uint32_t)bytes;
    store->bytes_since_flush += (uint32_t)bytes;
    const uint32_t one_second = store->format.sample_rate_hz *
                                store->format.channels *
                                store->format.bits_per_sample / 8;
    if (one_second > 0 && store->bytes_since_flush >= one_second) {
        if (fflush(store->file) != 0) {
            return ESP_FAIL;
        }
        store->bytes_since_flush %= one_second;
    }
    return ESP_OK;
}

esp_err_t recording_store_commit(recording_store_t *store, recording_info_t *out)
{
    if (store == NULL || store->file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = wav_finalize(store->file, store->pcm_bytes, &store->format);
    if (error == ESP_OK && fflush(store->file) != 0) {
        error = ESP_FAIL;
    }
    if (fclose(store->file) != 0 && error == ESP_OK) {
        error = ESP_FAIL;
    }
    store->file = NULL;
    if (error != ESP_OK) {
        return error;
    }
    if (rename(store->temp_path, store->final_path) != 0) {
        return ESP_FAIL;
    }

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        snprintf(out->path, sizeof(out->path), "%s", store->final_path);
        out->pcm_bytes = store->pcm_bytes;
        out->file_bytes = WAV_HEADER_BYTES + store->pcm_bytes;
    }
    return ESP_OK;
}

esp_err_t recording_store_recover_all(const char *root, recovery_report_t *out)
{
    if (root == NULL || root[0] == '\0' || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    DIR *directory = opendir(root);
    if (directory == NULL) {
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (!has_suffix(entry->d_name, ".wav.part")) {
            continue;
        }

        char part_path[RECORDING_PATH_MAX];
        char target_path[RECORDING_PATH_MAX];
        const int part_length = snprintf(part_path, sizeof(part_path), "%s/%s", root, entry->d_name);
        if (part_length < 0 || (size_t)part_length >= sizeof(part_path)) {
            ++out->failed;
            continue;
        }
        struct stat info;
        if (stat(part_path, &info) != 0 || info.st_size < 0 || (uint64_t)info.st_size > UINT32_MAX) {
            ++out->failed;
            continue;
        }

        const size_t base_length = strlen(part_path) - strlen(".part");
        if ((uint32_t)info.st_size < WAV_HEADER_BYTES) {
            if (base_length + strlen(".corrupt") + 1 > sizeof(target_path)) {
                ++out->failed;
                continue;
            }
            memcpy(target_path, part_path, base_length);
            strcpy(target_path + base_length, ".corrupt");
            if (rename(part_path, target_path) == 0) {
                ++out->corrupt;
            } else {
                ++out->failed;
            }
            continue;
        }

        FILE *file = fopen(part_path, "r+b");
        esp_err_t error = file == NULL ? ESP_FAIL :
                          wav_repair(file, (uint32_t)info.st_size, &recovery_format);
        if (file != NULL) {
            if (error == ESP_OK && fflush(file) != 0) {
                error = ESP_FAIL;
            }
            if (fclose(file) != 0 && error == ESP_OK) {
                error = ESP_FAIL;
            }
        }
        if (error != ESP_OK) {
            ++out->failed;
            continue;
        }

        memcpy(target_path, part_path, base_length);
        target_path[base_length] = '\0';
        if (rename(part_path, target_path) == 0) {
            ++out->repaired;
        } else {
            ++out->failed;
        }
    }

    closedir(directory);
    return out->failed == 0 ? ESP_OK : ESP_FAIL;
}
