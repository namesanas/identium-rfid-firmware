#include "i2c_link.h"
#include "socket_service.h"
#include "wifi_manager.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_coprocessor";

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if((result == ESP_ERR_NVS_NO_FREE_PAGES) || (result == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        /* Credentials are deliberately not stored in NVS. This erase is only
         * for recovery from an incompatible NVS layout, not configuration. */
        result = nvs_flash_erase();
        if(result == ESP_OK) result = nvs_flash_init();
    }
    if(result != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(result));
        return;
    }

    result = WifiManager_Init();
    if(result != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(result));
        return;
    }
    SocketService_Init();
    result = I2cLink_Init();
    if(result != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C link init failed: %s", esp_err_to_name(result));
        return;
    }

    for(;;)
    {
        I2cLink_Task();
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}
