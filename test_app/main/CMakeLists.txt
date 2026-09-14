idf_component_register(
    SRCS
        "test_main.c"
        "../../components/recorder/test/test_recorder_state.c"
        "../../components/recorder/test/test_recorder.c"
        "../../components/ui/test/test_recorder_ui_model.c"
        "../../components/wav/test/test_wav_writer.c"
        "../../components/storage/test/test_recording_store.c"
        "../../components/board/test/test_button_classifier.c"
    INCLUDE_DIRS "."
    REQUIRES unity recorder ui wav storage board spiffs
    WHOLE_ARCHIVE
)
