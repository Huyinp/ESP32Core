#include <inttypes.h>

#include "bsp/esp-bsp.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"

static const char *TAG = "recorder";

void app_main(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_bytes = 0;

    esp_chip_info(&chip_info);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_bytes));

    const size_t psram_bytes = esp_psram_get_size();
    if (psram_bytes == 0) {
        ESP_LOGE(TAG, "required PSRAM was not detected");
        ESP_ERROR_CHECK(ESP_ERR_NOT_FOUND);
    }

    if (!BSP_CAPS_AUDIO || !BSP_CAPS_SDCARD || !BSP_CAPS_TOUCH) {
        ESP_LOGE(TAG, "required BSP capability is absent: audio=%d sdcard=%d touch=%d",
                 BSP_CAPS_AUDIO, BSP_CAPS_SDCARD, BSP_CAPS_TOUCH);
        ESP_ERROR_CHECK(ESP_ERR_NOT_SUPPORTED);
    }

    ESP_LOGI(TAG, "RECORDER_BOARD_READY");
    ESP_LOGI(TAG, "target=esp32s3 cores=%d", chip_info.cores);
    ESP_LOGI(TAG, "flash=%" PRIu32 " bytes psram=%u bytes",
             flash_bytes, (unsigned)psram_bytes);
    ESP_LOGI(TAG, "display=%dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    ESP_LOGI(TAG, "audio=%d sdcard=%d touch=%d",
             BSP_CAPS_AUDIO, BSP_CAPS_SDCARD, BSP_CAPS_TOUCH);
}
