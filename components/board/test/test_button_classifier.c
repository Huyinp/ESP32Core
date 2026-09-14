#include "unity.h"

#include "recorder_board.h"
#include "../recorder_board_internal.h"

static float requested_gain_db;

static int capture_gain(void *codec, float gain_db)
{
    TEST_ASSERT_NOT_NULL(codec);
    requested_gain_db = gain_db;
    return 0;
}

TEST_CASE("button ignores 49 milliseconds", "[button]")
{
    TEST_ASSERT_EQUAL(BUTTON_ACTION_IGNORED, button_classify(49));
}

TEST_CASE("button accepts 50 milliseconds as short", "[button]")
{
    TEST_ASSERT_EQUAL(BUTTON_ACTION_SHORT_PRESS, button_classify(50));
}

TEST_CASE("button accepts 999 milliseconds as short", "[button]")
{
    TEST_ASSERT_EQUAL(BUTTON_ACTION_SHORT_PRESS, button_classify(999));
}

TEST_CASE("button accepts 1000 milliseconds as long", "[button]")
{
    TEST_ASSERT_EQUAL(BUTTON_ACTION_LONG_PRESS, button_classify(1000));
}

TEST_CASE("button edge tracker reports action only on release", "[button]")
{
    recorder_button_tracker_t tracker = {0};
    button_action_t action = BUTTON_ACTION_IGNORED;

    TEST_ASSERT_FALSE(recorder_button_track_edge(&tracker, true, 100, &action));
    TEST_ASSERT_TRUE(recorder_button_track_edge(&tracker, false, 1099, &action));
    TEST_ASSERT_EQUAL(BUTTON_ACTION_SHORT_PRESS, action);
}

TEST_CASE("button edge tracker handles millisecond wraparound", "[button]")
{
    recorder_button_tracker_t tracker = {0};
    button_action_t action = BUTTON_ACTION_IGNORED;

    TEST_ASSERT_FALSE(recorder_button_track_edge(&tracker, true, UINT32_MAX - 20, &action));
    TEST_ASSERT_TRUE(recorder_button_track_edge(&tracker, false, 40, &action));
    TEST_ASSERT_EQUAL(BUTTON_ACTION_SHORT_PRESS, action);
}

TEST_CASE("button edge tracker rejects release without press", "[button]")
{
    recorder_button_tracker_t tracker = {0};
    button_action_t action = BUTTON_ACTION_LONG_PRESS;

    TEST_ASSERT_FALSE(recorder_button_track_edge(&tracker, false, 100, &action));
    TEST_ASSERT_EQUAL(BUTTON_ACTION_LONG_PRESS, action);
}

TEST_CASE("microphone rejects a null source", "[board]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, recorder_board_open_mic(NULL, 16000));
}

TEST_CASE("microphone rejects an unsupported rate", "[board]")
{
    recorder_audio_source_t source = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, recorder_board_open_mic(&source, 8000));
}

TEST_CASE("microphone configuration requests 36 dB input gain", "[board]")
{
    requested_gain_db = 0.0f;

    TEST_ASSERT_EQUAL(ESP_OK,
                      recorder_board_configure_mic_gain((void *)1, capture_gain));
    TEST_ASSERT_EQUAL_FLOAT(36.0f, requested_gain_db);
}
