#include "recorder_board.h"

button_action_t button_classify(uint32_t held_ms)
{
    if (held_ms < RECORDER_BUTTON_DEBOUNCE_MS) {
        return BUTTON_ACTION_IGNORED;
    }
    if (held_ms < RECORDER_BUTTON_LONG_PRESS_MS) {
        return BUTTON_ACTION_SHORT_PRESS;
    }
    return BUTTON_ACTION_LONG_PRESS;
}
