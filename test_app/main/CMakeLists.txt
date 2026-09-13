idf_component_register(
    SRCS
        "test_main.c"
        "../../components/recorder/test/test_recorder_state.c"
        "../../components/wav/test/test_wav_writer.c"
    INCLUDE_DIRS "."
    REQUIRES unity recorder wav
    WHOLE_ARCHIVE
)
