#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "unity.h"

#include "recording_store.h"

#define TEST_ROOT "/test"

static const wav_format_t mono_16k = {
    .sample_rate_hz = 16000,
    .bits_per_sample = 16,
    .channels = 1,
};

static void remove_if_present(const char *path)
{
    remove(path);
}

static bool file_exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

TEST_CASE("storage uses RTC recording name", "[storage]")
{
    const char *part = TEST_ROOT "/20260912T093000Z-0001.wav.part";
    const char *final = TEST_ROOT "/20260912T093000Z-0001.wav";
    remove_if_present(part);
    remove_if_present(final);
    recording_clock_t clock = {
        .valid = true,
        .unix_seconds = 1789205400,
        .boot_count = 7,
        .sequence = 1,
    };
    recording_store_t store = {0};
    recording_info_t info = {0};

    TEST_ASSERT_EQUAL(ESP_OK, recording_store_begin(&store, TEST_ROOT, &clock, mono_16k));
    TEST_ASSERT_TRUE(file_exists(part));
    TEST_ASSERT_EQUAL(ESP_OK, recording_store_commit(&store, &info));
    TEST_ASSERT_EQUAL_STRING(final, info.path);
    TEST_ASSERT_FALSE(file_exists(part));
    TEST_ASSERT_TRUE(file_exists(final));

    remove_if_present(final);
}

TEST_CASE("storage uses boot fallback name", "[storage]")
{
    const char *part = TEST_ROOT "/boot-00000007-0001.wav.part";
    const char *final = TEST_ROOT "/boot-00000007-0001.wav";
    remove_if_present(part);
    remove_if_present(final);
    recording_clock_t clock = {
        .valid = false,
        .boot_count = 7,
        .sequence = 1,
    };
    recording_store_t store = {0};

    TEST_ASSERT_EQUAL(ESP_OK, recording_store_begin(&store, TEST_ROOT, &clock, mono_16k));
    TEST_ASSERT_TRUE(file_exists(part));
    TEST_ASSERT_EQUAL(ESP_OK, recording_store_commit(&store, NULL));
    TEST_ASSERT_TRUE(file_exists(final));

    remove_if_present(final);
}

TEST_CASE("storage accounts appended PCM bytes", "[storage]")
{
    const char *final = TEST_ROOT "/boot-00000008-0002.wav";
    remove_if_present(TEST_ROOT "/boot-00000008-0002.wav.part");
    remove_if_present(final);
    recording_clock_t clock = {.boot_count = 8, .sequence = 2};
    recording_store_t store = {0};
    recording_info_t info = {0};
    const uint8_t pcm[6] = {0};

    TEST_ASSERT_EQUAL(ESP_OK, recording_store_begin(&store, TEST_ROOT, &clock, mono_16k));
    TEST_ASSERT_EQUAL(ESP_OK, recording_store_append(&store, pcm, sizeof(pcm)));
    TEST_ASSERT_EQUAL(ESP_OK, recording_store_commit(&store, &info));
    TEST_ASSERT_EQUAL_UINT32(sizeof(pcm), info.pcm_bytes);
    TEST_ASSERT_EQUAL_UINT32(44 + sizeof(pcm), info.file_bytes);

    remove_if_present(final);
}

TEST_CASE("storage repairs partial WAV once", "[storage]")
{
    const char *part = TEST_ROOT "/recover.wav.part";
    const char *final = TEST_ROOT "/recover.wav";
    remove_if_present(part);
    remove_if_present(final);
    FILE *file = fopen(part, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL(ESP_OK, wav_write_placeholder(file, &mono_16k));
    const uint8_t pcm[8] = {0};
    TEST_ASSERT_EQUAL(sizeof(pcm), fwrite(pcm, 1, sizeof(pcm), file));
    TEST_ASSERT_EQUAL(0, fclose(file));
    recovery_report_t report = {0};

    TEST_ASSERT_EQUAL(ESP_OK, recording_store_recover_all(TEST_ROOT, &report));
    TEST_ASSERT_EQUAL_UINT32(1, report.repaired);
    TEST_ASSERT_TRUE(file_exists(final));
    TEST_ASSERT_FALSE(file_exists(part));

    report = (recovery_report_t){0};
    TEST_ASSERT_EQUAL(ESP_OK, recording_store_recover_all(TEST_ROOT, &report));
    TEST_ASSERT_EQUAL_UINT32(0, report.repaired);
    TEST_ASSERT_EQUAL_UINT32(0, report.corrupt);

    remove_if_present(final);
}

TEST_CASE("storage preserves short partial as corrupt", "[storage]")
{
    const char *part = TEST_ROOT "/short.wav.part";
    const char *corrupt = TEST_ROOT "/short.wav.corrupt";
    remove_if_present(part);
    remove_if_present(corrupt);
    FILE *file = fopen(part, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL(10, fwrite("0123456789", 1, 10, file));
    TEST_ASSERT_EQUAL(0, fclose(file));
    recovery_report_t report = {0};

    TEST_ASSERT_EQUAL(ESP_OK, recording_store_recover_all(TEST_ROOT, &report));
    TEST_ASSERT_EQUAL_UINT32(1, report.corrupt);
    TEST_ASSERT_FALSE(file_exists(part));
    TEST_ASSERT_TRUE(file_exists(corrupt));

    remove_if_present(corrupt);
}
