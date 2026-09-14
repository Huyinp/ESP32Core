#include "unity.h"

#include "recorder_board.h"

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

TEST_CASE("microphone rejects a null source", "[board]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, recorder_board_open_mic(NULL, 16000));
}

TEST_CASE("microphone rejects an unsupported rate", "[board]")
{
    recorder_audio_source_t source = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, recorder_board_open_mic(&source, 8000));
}
