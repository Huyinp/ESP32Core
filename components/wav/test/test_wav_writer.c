#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "wav_writer.h"

static const wav_format_t mono_16k = {
    .sample_rate_hz = 16000,
    .bits_per_sample = 16,
    .channels = 1,
};

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static FILE *open_memory_file(uint8_t *buffer, size_t size)
{
    memset(buffer, 0, size);
    FILE *file = fmemopen(buffer, size, "w+b");
    TEST_ASSERT_NOT_NULL(file);
    return file;
}

static void assert_pcm_header(const uint8_t *header, uint32_t pcm_bytes)
{
    TEST_ASSERT_EQUAL_MEMORY("RIFF", header, 4);
    TEST_ASSERT_EQUAL_UINT32(36 + pcm_bytes, read_le32(header + 4));
    TEST_ASSERT_EQUAL_MEMORY("WAVE", header + 8, 4);
    TEST_ASSERT_EQUAL_MEMORY("fmt ", header + 12, 4);
    TEST_ASSERT_EQUAL_UINT32(16, read_le32(header + 16));
    TEST_ASSERT_EQUAL_UINT16(1, read_le16(header + 20));
    TEST_ASSERT_EQUAL_UINT16(1, read_le16(header + 22));
    TEST_ASSERT_EQUAL_UINT32(16000, read_le32(header + 24));
    TEST_ASSERT_EQUAL_UINT32(32000, read_le32(header + 28));
    TEST_ASSERT_EQUAL_UINT16(2, read_le16(header + 32));
    TEST_ASSERT_EQUAL_UINT16(16, read_le16(header + 34));
    TEST_ASSERT_EQUAL_MEMORY("data", header + 36, 4);
    TEST_ASSERT_EQUAL_UINT32(pcm_bytes, read_le32(header + 40));
}

TEST_CASE("wav placeholder is canonical PCM", "[wav]")
{
    uint8_t buffer[44];
    FILE *file = open_memory_file(buffer, sizeof(buffer));

    TEST_ASSERT_EQUAL(ESP_OK, wav_write_placeholder(file, &mono_16k));
    TEST_ASSERT_EQUAL(0, fflush(file));
    assert_pcm_header(buffer, 0);

    TEST_ASSERT_EQUAL(0, fclose(file));
}

TEST_CASE("wav finalization writes data sizes", "[wav]")
{
    uint8_t buffer[48];
    FILE *file = open_memory_file(buffer, sizeof(buffer));

    TEST_ASSERT_EQUAL(ESP_OK, wav_write_placeholder(file, &mono_16k));
    TEST_ASSERT_EQUAL(4, fwrite("\0\0\0\0", 1, 4, file));
    TEST_ASSERT_EQUAL(ESP_OK, wav_finalize(file, 4, &mono_16k));
    TEST_ASSERT_EQUAL(0, fflush(file));
    assert_pcm_header(buffer, 4);

    TEST_ASSERT_EQUAL(0, fclose(file));
}

TEST_CASE("wav repair creates one second header", "[wav]")
{
    static uint8_t buffer[44 + 32000];
    static const uint8_t silence[1000] = {0};
    FILE *file = open_memory_file(buffer, sizeof(buffer));

    TEST_ASSERT_EQUAL(ESP_OK, wav_write_placeholder(file, &mono_16k));
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL(sizeof(silence), fwrite(silence, 1, sizeof(silence), file));
    }
    TEST_ASSERT_EQUAL(ESP_OK, wav_repair(file, sizeof(buffer), &mono_16k));
    TEST_ASSERT_EQUAL(0, fflush(file));
    assert_pcm_header(buffer, 32000);

    TEST_ASSERT_EQUAL(0, fclose(file));
}

TEST_CASE("wav writer rejects invalid inputs", "[wav]")
{
    uint8_t buffer[44];
    FILE *file = open_memory_file(buffer, sizeof(buffer));
    wav_format_t invalid = mono_16k;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wav_write_placeholder(NULL, &mono_16k));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wav_write_placeholder(file, NULL));
    invalid.sample_rate_hz = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wav_write_placeholder(file, &invalid));
    invalid = mono_16k;
    invalid.channels = 2;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wav_write_placeholder(file, &invalid));
    invalid = mono_16k;
    invalid.bits_per_sample = 8;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wav_write_placeholder(file, &invalid));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wav_repair(file, 43, &mono_16k));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wav_finalize(file, UINT32_MAX, &mono_16k));

    TEST_ASSERT_EQUAL(0, fclose(file));
}
