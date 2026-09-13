#include "esp_err.h"
#include "esp_spiffs.h"
#include "unity.h"

void app_main(void)
{
    const esp_vfs_spiffs_conf_t storage = {
        .base_path = "/test",
        .partition_label = "testdata",
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&storage));
    unity_run_menu();
}
