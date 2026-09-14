idf_component_register(
    SRCS "app_main.c" "../components/board/recorder_board.c"
    INCLUDE_DIRS "." "../components/board/include"
    REQUIRES board recorder storage ui driver esp_timer spi_flash esp_psram
)
