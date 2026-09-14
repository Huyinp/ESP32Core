#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../recorder_internal.h"

typedef struct {
    char calls[16];
    size_t call_count;
    size_t reads;
    bool fail_append;
    recorder_state_t states[8];
    size_t state_count;
} fake_ports_t;

static void called(fake_ports_t *fake, char value)
{
    fake->calls[fake->call_count++] = value;
}

static esp_err_t fake_begin(void *ctx, recording_mode_t mode, char *id, size_t id_size)
{
    fake_ports_t *fake = ctx;
    called(fake, 'B');
    snprintf(id, id_size, "test-0001");
    return mode == RECORDING_MODE_MEETING ? ESP_OK : ESP_FAIL;
}

static esp_err_t fake_open(void *ctx)
{
    called(ctx, 'O');
    return ESP_OK;
}

static esp_err_t fake_read(void *ctx, int16_t *samples, size_t capacity, size_t *read)
{
    fake_ports_t *fake = ctx;
    called(fake, 'R');
    samples[0] = 11;
    samples[1] = 22;
    *read = capacity >= 2 ? 2 : capacity;
    ++fake->reads;
    return ESP_OK;
}

static esp_err_t fake_append(void *ctx, const void *pcm, size_t bytes)
{
    fake_ports_t *fake = ctx;
    called(fake, 'A');
    TEST_ASSERT_EQUAL_UINT32(4, bytes);
    TEST_ASSERT_EQUAL_INT16(11, ((const int16_t *)pcm)[0]);
    return fake->fail_append ? ESP_FAIL : ESP_OK;
}

static esp_err_t fake_close(void *ctx)
{
    called(ctx, 'C');
    return ESP_OK;
}

static esp_err_t fake_commit(void *ctx)
{
    called(ctx, 'M');
    return ESP_OK;
}

static void fake_observer(const recorder_snapshot_t *snapshot, void *ctx)
{
    fake_ports_t *fake = ctx;
    fake->states[fake->state_count++] = snapshot->state;
}

static recorder_config_t fake_config(fake_ports_t *fake)
{
    return (recorder_config_t){
        .ctx = fake,
        .storage_begin = fake_begin,
        .audio_open = fake_open,
        .audio_read = fake_read,
        .storage_append = fake_append,
        .audio_close = fake_close,
        .storage_commit = fake_commit,
        .observer = fake_observer,
        .observer_ctx = fake,
    };
}

TEST_CASE("recorder opens storage before capture and commits after final PCM", "[coordinator]")
{
    fake_ports_t fake = {0};
    recorder_engine_t engine;
    recorder_config_t config = fake_config(&fake);

    TEST_ASSERT_EQUAL(ESP_OK, recorder_engine_init(&engine, &config));
    TEST_ASSERT_EQUAL(ESP_OK, recorder_engine_start(&engine, RECORDING_MODE_MEETING));
    TEST_ASSERT_EQUAL_STRING_LEN("BO", fake.calls, 2);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      recorder_engine_start(&engine, RECORDING_MODE_MEETING));
    TEST_ASSERT_EQUAL(ESP_OK, recorder_engine_capture_once(&engine));
    TEST_ASSERT_EQUAL_UINT32(4, engine.snapshot.pcm_bytes);
    TEST_ASSERT_EQUAL(ESP_OK, recorder_engine_stop(&engine));
    TEST_ASSERT_EQUAL_STRING("BORACM", fake.calls);
    TEST_ASSERT_EQUAL(RECORDER_STORED, engine.snapshot.state);
    const recorder_state_t expected[] = {
        RECORDER_PREPARING, RECORDER_RECORDING,
        RECORDER_FINALIZING, RECORDER_STORED,
    };
    TEST_ASSERT_EQUAL_UINT32(4, fake.state_count);
    TEST_ASSERT_EQUAL_MEMORY(expected, fake.states, sizeof(expected));
}

TEST_CASE("recorder append failure closes capture and requires recovery", "[coordinator]")
{
    fake_ports_t fake = {.fail_append = true};
    recorder_engine_t engine;
    recorder_config_t config = fake_config(&fake);

    TEST_ASSERT_EQUAL(ESP_OK, recorder_engine_init(&engine, &config));
    TEST_ASSERT_EQUAL(ESP_OK, recorder_engine_start(&engine, RECORDING_MODE_MEETING));
    TEST_ASSERT_EQUAL(ESP_FAIL, recorder_engine_capture_once(&engine));
    TEST_ASSERT_EQUAL_STRING("BORAC", fake.calls);
    TEST_ASSERT_EQUAL(RECORDER_RECOVERY_REQUIRED, engine.snapshot.state);
    TEST_ASSERT_EQUAL(ESP_FAIL, engine.snapshot.last_error);
}
