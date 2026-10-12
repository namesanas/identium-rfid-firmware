set(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
idf_component_register(
    SRCS "main.c" "i2c_link.c" "wifi_manager.c" "socket_service.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "${REPOSITORY_ROOT}/STM32_Firmware/Firmware/Devices/esp32"
    REQUIRES driver esp_driver_gpio esp_wifi esp_event esp_netif nvs_flash lwip esp_timer esp_system esp_hw_support
)
