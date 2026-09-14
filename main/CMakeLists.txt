idf_component_register(
    SRCS "app_main.c" "../components/board/recorder_board.c"
    INCLUDE_DIRS "." "../components/board/include"
    REQUIRES board storage driver esp_timer spi_flash esp_psram
)
