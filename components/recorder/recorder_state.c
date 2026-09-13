#include <stddef.h>

#include "recorder_state.h"

typedef struct {
    recorder_state_t from;
    recorder_event_t event;
    recorder_state_t to;
} recorder_transition_t;

static const recorder_transition_t transitions[] = {
    {RECORDER_IDLE, RECORDER_EVENT_START, RECORDER_PREPARING},
    {RECORDER_PREPARING, RECORDER_EVENT_CAPTURE_READY, RECORDER_RECORDING},
    {RECORDER_RECORDING, RECORDER_EVENT_STOP, RECORDER_FINALIZING},
    {RECORDER_FINALIZING, RECORDER_EVENT_FINALIZED, RECORDER_STORED},
    {RECORDER_PREPARING, RECORDER_EVENT_FAIL, RECORDER_RECOVERY_REQUIRED},
    {RECORDER_RECORDING, RECORDER_EVENT_FAIL, RECORDER_RECOVERY_REQUIRED},
    {RECORDER_FINALIZING, RECORDER_EVENT_FAIL, RECORDER_RECOVERY_REQUIRED},
    {RECORDER_RECOVERY_REQUIRED, RECORDER_EVENT_RECOVERED, RECORDER_STORED},
};

recorder_state_t recorder_state_next(recorder_state_t current, recorder_event_t event)
{
    for (size_t i = 0; i < sizeof(transitions) / sizeof(transitions[0]); ++i) {
        if (transitions[i].from == current && transitions[i].event == event) {
            return transitions[i].to;
        }
    }

    return RECORDER_INVALID;
}

bool recorder_state_is_recording(recorder_state_t state)
{
    return state == RECORDER_RECORDING;
}
