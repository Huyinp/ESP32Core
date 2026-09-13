#include "unity.h"

#include "recorder_state.h"

TEST_CASE("idle start reaches preparing", "[recorder]")
{
    TEST_ASSERT_EQUAL(RECORDER_PREPARING,
                      recorder_state_next(RECORDER_IDLE, RECORDER_EVENT_START));
}

TEST_CASE("preparing capture ready reaches recording", "[recorder]")
{
    TEST_ASSERT_EQUAL(RECORDER_RECORDING,
                      recorder_state_next(RECORDER_PREPARING, RECORDER_EVENT_CAPTURE_READY));
}

TEST_CASE("recording stop reaches finalizing", "[recorder]")
{
    TEST_ASSERT_EQUAL(RECORDER_FINALIZING,
                      recorder_state_next(RECORDER_RECORDING, RECORDER_EVENT_STOP));
}

TEST_CASE("finalizing finalized reaches stored", "[recorder]")
{
    TEST_ASSERT_EQUAL(RECORDER_STORED,
                      recorder_state_next(RECORDER_FINALIZING, RECORDER_EVENT_FINALIZED));
}

TEST_CASE("failure requires recovery", "[recorder]")
{
    TEST_ASSERT_EQUAL(RECORDER_RECOVERY_REQUIRED,
                      recorder_state_next(RECORDER_PREPARING, RECORDER_EVENT_FAIL));
    TEST_ASSERT_EQUAL(RECORDER_RECOVERY_REQUIRED,
                      recorder_state_next(RECORDER_RECORDING, RECORDER_EVENT_FAIL));
    TEST_ASSERT_EQUAL(RECORDER_RECOVERY_REQUIRED,
                      recorder_state_next(RECORDER_FINALIZING, RECORDER_EVENT_FAIL));
}

TEST_CASE("recovery reaches stored", "[recorder]")
{
    TEST_ASSERT_EQUAL(RECORDER_STORED,
                      recorder_state_next(RECORDER_RECOVERY_REQUIRED, RECORDER_EVENT_RECOVERED));
}

TEST_CASE("invalid duplicate start is rejected", "[recorder]")
{
    TEST_ASSERT_EQUAL(RECORDER_INVALID,
                      recorder_state_next(RECORDER_RECORDING, RECORDER_EVENT_START));
}

TEST_CASE("only recording state reports recording", "[recorder]")
{
    TEST_ASSERT_TRUE(recorder_state_is_recording(RECORDER_RECORDING));
    TEST_ASSERT_FALSE(recorder_state_is_recording(RECORDER_IDLE));
    TEST_ASSERT_FALSE(recorder_state_is_recording(RECORDER_PREPARING));
    TEST_ASSERT_FALSE(recorder_state_is_recording(RECORDER_FINALIZING));
    TEST_ASSERT_FALSE(recorder_state_is_recording(RECORDER_STORED));
    TEST_ASSERT_FALSE(recorder_state_is_recording(RECORDER_RECOVERY_REQUIRED));
    TEST_ASSERT_FALSE(recorder_state_is_recording(RECORDER_INVALID));
}
