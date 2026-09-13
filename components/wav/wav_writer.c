#include <limits.h>
#include <stdbool.h>

#include "wav_writer.h"

enum {
    WAV_HEADER_BYTES = 44,
    WAV_BITS_PER_BYTE = 8,
};

static bool format_is_valid(const wav_format_t *format)
{
    return format != NULL &&
           format->sample_rate_hz > 0 &&
           format->sample_rate_hz <= UINT32_MAX / 2 &&
           format->bits_per_sample == 16 &&
           format->channels == 1;
}

static esp_err_t write_bytes(FILE *file, const void *bytes, size_t size)
{
    return fwrite(bytes, 1, size, file) == size ? ESP_OK : ESP_FAIL;
}

static esp_err_t write_le16(FILE *file, uint16_t value)
{
    const uint8_t bytes[] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

static esp_err_t write_le32(FILE *file, uint32_t value)
{
    const uint8_t bytes[] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

static esp_err_t write_header(FILE *file, uint32_t pcm_bytes, const wav_format_t *format)
{
    if (file == NULL || !format_is_valid(format)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pcm_bytes > UINT32_MAX - 36) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        return ESP_FAIL;
    }

    const uint16_t block_align = format->channels * format->bits_per_sample / WAV_BITS_PER_BYTE;
    const uint32_t byte_rate = format->sample_rate_hz * block_align;

    if (write_bytes(file, "RIFF", 4) != ESP_OK ||
        write_le32(file, 36 + pcm_bytes) != ESP_OK ||
        write_bytes(file, "WAVEfmt ", 8) != ESP_OK ||
        write_le32(file, 16) != ESP_OK ||
        write_le16(file, 1) != ESP_OK ||
        write_le16(file, format->channels) != ESP_OK ||
        write_le32(file, format->sample_rate_hz) != ESP_OK ||
        write_le32(file, byte_rate) != ESP_OK ||
        write_le16(file, block_align) != ESP_OK ||
        write_le16(file, format->bits_per_sample) != ESP_OK ||
        write_bytes(file, "data", 4) != ESP_OK ||
        write_le32(file, pcm_bytes) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t wav_write_placeholder(FILE *file, const wav_format_t *format)
{
    return write_header(file, 0, format);
}

esp_err_t wav_finalize(FILE *file, uint32_t pcm_bytes, const wav_format_t *format)
{
    return write_header(file, pcm_bytes, format);
}

esp_err_t wav_repair(FILE *file, uint32_t actual_file_bytes, const wav_format_t *format)
{
    if (actual_file_bytes < WAV_HEADER_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    return write_header(file, actual_file_bytes - WAV_HEADER_BYTES, format);
}
