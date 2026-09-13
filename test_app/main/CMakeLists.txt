idf_component_register(
    SRCS
        "test_main.c"
        "../../components/recorder/test/test_recorder_state.c"
        "../../components/wav/test/test_wav_writer.c"
        "../../components/storage/test/test_recording_store.c"
    INCLUDE_DIRS "."
    REQUIRES unity recorder wav storage spiffs
    WHOLE_ARCHIVE
)
