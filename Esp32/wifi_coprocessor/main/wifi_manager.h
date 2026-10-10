#ifndef WIFI_COPROCESSOR_WIFI_MANAGER_H_
#define WIFI_COPROCESSOR_WIFI_MANAGER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp32_link_protocol.h"

#define WIFI_MANAGER_SCAN_LIMIT 16U

typedef struct
{
    char ssid[33];
    int8_t rssi;
    uint8_t authMode;
    uint8_t channel;
} WifiManager_ScanResult_t;

typedef struct
{
    uint8_t state;
    uint8_t ip[4];
    int8_t rssi;
    uint8_t mac[6];
    uint8_t events;
} WifiManager_Status_t;

esp_err_t WifiManager_Init(void);
uint8_t WifiManager_ApplyConfig(const uint8_t *payload, size_t length);
uint8_t WifiManager_Connect(void);
uint8_t WifiManager_Disconnect(void);
void WifiManager_Task(void);
void WifiManager_GetStatus(WifiManager_Status_t *status);
uint8_t WifiManager_TakeEvents(void);
uint8_t WifiManager_ScanStart(void);
uint8_t WifiManager_ScanGetResult(uint8_t index, WifiManager_ScanResult_t *result,
                                  uint8_t *count, uint8_t *complete);
uint8_t WifiManager_IsNetworkReady(void);

#endif
