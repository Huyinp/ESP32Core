#include "recorder_ui_model.h"
#include "unity.h"

TEST_CASE("recording view shows REC and elapsed time", "[ui]")
{
    recorder_ui_view_t view = recorder_ui_model(
        &(recorder_snapshot_t){
            .state = RECORDER_RECORDING,
            .elapsed_ms = 75400,
        }, RECORDER_FAILURE_NONE);

    TEST_ASSERT_EQUAL_STRING("REC", view.status);
    TEST_ASSERT_EQUAL_STRING("01:15", view.detail);
    TEST_ASSERT_EQUAL_HEX32(0xff3b30, view.color_rgb);
}

TEST_CASE("storage failure is distinct from capture failure", "[ui]")
{
    recorder_snapshot_t snapshot = {
        .state = RECORDER_RECOVERY_REQUIRED,
        .last_error = ESP_FAIL,
    };

    recorder_ui_view_t storage = recorder_ui_model(&snapshot, RECORDER_FAILURE_STORAGE);
    recorder_ui_view_t capture = recorder_ui_model(&snapshot, RECORDER_FAILURE_AUDIO);

    TEST_ASSERT_EQUAL_STRING("SD ERROR", storage.status);
    TEST_ASSERT_EQUAL_STRING("RECORD ERROR", capture.status);
}
