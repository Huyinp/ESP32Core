#include <inttypes.h>
#include <stdio.h>

#include "recorder_ui_model.h"

recorder_ui_view_t recorder_ui_model(const recorder_snapshot_t *snapshot,
                                     recorder_failure_t failure)
{
    recorder_ui_view_t view = {
        .status = "READY",
        .color_rgb = 0x4ade80,
    };
    if (snapshot == NULL) {
        return view;
    }

    switch (snapshot->state) {
    case RECORDER_PREPARING:
        view.status = "PREPARING";
        view.color_rgb = 0xfacc15;
        break;
    case RECORDER_RECORDING: {
        view.status = "REC";
        view.color_rgb = 0xff3b30;
        const uint32_t seconds = snapshot->elapsed_ms / 1000;
        if (seconds < 6000) {
            snprintf(view.detail, sizeof(view.detail), "%02" PRIu32 ":%02" PRIu32,
                     seconds / 60, seconds % 60);
        } else {
            snprintf(view.detail, sizeof(view.detail), "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
                     seconds / 3600, (seconds / 60) % 60, seconds % 60);
        }
        break;
    }
    case RECORDER_FINALIZING:
        view.status = "SAVING";
        view.color_rgb = 0xfacc15;
        break;
    case RECORDER_STORED:
        view.status = "SAVED";
        break;
    case RECORDER_RECOVERY_REQUIRED:
        view.status = failure == RECORDER_FAILURE_STORAGE ? "SD ERROR" : "RECORD ERROR";
        view.color_rgb = 0xff3b30;
        break;
    case RECORDER_IDLE:
    default:
        break;
    }
    return view;
}
