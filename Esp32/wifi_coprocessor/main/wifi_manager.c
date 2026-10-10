#include "wifi_manager.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_manager";

#define WIFI_MANAGER_RECONNECT_US (1000000LL)
#define WIFI_MANAGER_EVENT_IP     0x01U
#define WIFI_MANAGER_EVENT_LINK   0x02U

static esp_netif_t *g_staNetif;
static esp_netif_t *g_apNetif;
static uint8_t g_enabled;
static uint8_t g_mode;
static uint8_t g_dhcp;
static uint8_t g_apChannel;
static char g_staSsid[33];
static char g_staPassword[65];
static char g_apSsid[33];
static char g_apPassword[65];
static uint8_t g_staticIp[4];
static uint8_t g_netmask[4];
static uint8_t g_gateway[4];
static uint8_t g_dns[4];
static uint8_t g_state = ESP32_LINK_WIFI_DISABLED;
static uint8_t g_currentIp[4];
static int8_t g_rssi;
static uint8_t g_mac[6];
static uint8_t g_events;
static uint8_t g_wifiStarted;
static uint8_t g_scanRunning;
static uint8_t g_scanComplete;
static uint8_t g_scanCount;
static WifiManager_ScanResult_t g_scanResults[WIFI_MANAGER_SCAN_LIMIT];
static int64_t g_nextReconnectUs;
static uint8_t g_staAssociated;

static void WifiManager_EventHandler(void *argument, esp_event_base_t eventBase,
                                     int32_t eventId, void *eventData)
{
    (void)argument;
    (void)eventData;

    if(eventBase == WIFI_EVENT)
    {
        if(eventId == WIFI_EVENT_STA_START)
        {
            g_state = ESP32_LINK_WIFI_CONNECTING;
        }
        else if(eventId == WIFI_EVENT_STA_CONNECTED)
        {
            g_staAssociated = 1U;
            g_events |= WIFI_MANAGER_EVENT_LINK;
        }
        else if(eventId == WIFI_EVENT_STA_DISCONNECTED)
        {
            g_staAssociated = 0U;
            memset(g_currentIp, 0, sizeof(g_currentIp));
            g_rssi = 0;
            g_events |= WIFI_MANAGER_EVENT_LINK;
            if((g_enabled != 0U) && (g_mode == ESP32_LINK_MODE_STA))
            {
                g_state = ESP32_LINK_WIFI_CONNECTING;
                g_nextReconnectUs = esp_timer_get_time() + WIFI_MANAGER_RECONNECT_US;
            }
            else if(g_enabled == 0U)
            {
                g_state = ESP32_LINK_WIFI_DISABLED;
            }
        }
        else if(eventId == WIFI_EVENT_AP_START)
        {
            g_state = ESP32_LINK_WIFI_AP_STARTED;
            g_events |= WIFI_MANAGER_EVENT_LINK;
        }
        else if(eventId == WIFI_EVENT_AP_STOP)
        {
            if(g_enabled != 0U)
            {
                g_state = ESP32_LINK_WIFI_IDLE;
            }
            g_events |= WIFI_MANAGER_EVENT_LINK;
        }
        else if(eventId == WIFI_EVENT_SCAN_DONE)
        {
            uint16_t count = WIFI_MANAGER_SCAN_LIMIT;
            wifi_ap_record_t records[WIFI_MANAGER_SCAN_LIMIT];
            memset(records, 0, sizeof(records));
            g_scanRunning = 0U;
            g_scanComplete = 1U;
            if(esp_wifi_scan_get_ap_records(&count, records) == ESP_OK)
            {
                uint16_t i;
                if(count > WIFI_MANAGER_SCAN_LIMIT) count = WIFI_MANAGER_SCAN_LIMIT;
                g_scanCount = (uint8_t)count;
                for(i = 0U; i < count; i++)
                {
                    memcpy(g_scanResults[i].ssid, records[i].ssid,
                           sizeof(records[i].ssid));
                    g_scanResults[i].ssid[sizeof(g_scanResults[i].ssid) - 1U] = '\0';
                    g_scanResults[i].rssi = records[i].rssi;
                    g_scanResults[i].authMode = (uint8_t)records[i].authmode;
                    g_scanResults[i].channel = records[i].primary;
                }
            }
            else
            {
                g_scanCount = 0U;
            }
        }
    }
    else if((eventBase == IP_EVENT) && (eventId == IP_EVENT_STA_GOT_IP))
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)eventData;
        g_currentIp[0] = (uint8_t)(event->ip_info.ip.addr & 0xFFU);
        g_currentIp[1] = (uint8_t)((event->ip_info.ip.addr >> 8U) & 0xFFU);
        g_currentIp[2] = (uint8_t)((event->ip_info.ip.addr >> 16U) & 0xFFU);
        g_currentIp[3] = (uint8_t)((event->ip_info.ip.addr >> 24U) & 0xFFU);
        g_state = ESP32_LINK_WIFI_CONNECTED;
        g_events |= WIFI_MANAGER_EVENT_IP;
        g_events |= WIFI_MANAGER_EVENT_LINK;
    }
}

static esp_err_t WifiManager_SetStaticNetwork(void)
{
    esp_netif_ip_info_t ipInfo;
    esp_netif_dns_info_t dnsInfo;
    esp_err_t result;

    if(g_staNetif == NULL) return ESP_ERR_INVALID_STATE;

    result = esp_netif_dhcpc_stop(g_staNetif);
    if((result != ESP_OK) && (result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED))
        return result;

    memset(&ipInfo, 0, sizeof(ipInfo));
    IP4_ADDR(&ipInfo.ip, g_staticIp[0], g_staticIp[1], g_staticIp[2], g_staticIp[3]);
    IP4_ADDR(&ipInfo.netmask, g_netmask[0], g_netmask[1], g_netmask[2], g_netmask[3]);
    IP4_ADDR(&ipInfo.gw, g_gateway[0], g_gateway[1], g_gateway[2], g_gateway[3]);
    result = esp_netif_set_ip_info(g_staNetif, &ipInfo);
    if(result != ESP_OK) return result;

    memset(&dnsInfo, 0, sizeof(dnsInfo));
    dnsInfo.ip.type = ESP_IPADDR_TYPE_V4;
    IP4_ADDR(&dnsInfo.ip.u_addr.ip4, g_dns[0], g_dns[1], g_dns[2], g_dns[3]);
    return esp_netif_set_dns_info(g_staNetif, ESP_NETIF_DNS_MAIN, &dnsInfo);
}

esp_err_t WifiManager_Init(void)
{
    wifi_init_config_t wifiInit = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t result;

    result = esp_netif_init();
    if((result != ESP_OK) && (result != ESP_ERR_INVALID_STATE)) return result;
    result = esp_event_loop_create_default();
    if((result != ESP_OK) && (result != ESP_ERR_INVALID_STATE)) return result;

    g_staNetif = esp_netif_create_default_wifi_sta();
    g_apNetif = esp_netif_create_default_wifi_ap();
    if((g_staNetif == NULL) || (g_apNetif == NULL)) return ESP_FAIL;

    result = esp_wifi_init(&wifiInit);
    if(result != ESP_OK) return result;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if(result != ESP_OK) return result;
    result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &WifiManager_EventHandler, NULL);
    if(result != ESP_OK) return result;
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &WifiManager_EventHandler, NULL);
    if(result != ESP_OK) return result;
    result = esp_wifi_set_mode(WIFI_MODE_NULL);
    if(result != ESP_OK) return result;
    (void)esp_read_mac(g_mac, ESP_MAC_WIFI_STA);
    g_state = ESP32_LINK_WIFI_DISABLED;
    g_enabled = 0U;
    return ESP_OK;
}

uint8_t WifiManager_ApplyConfig(const uint8_t *payload, size_t length)
{
    uint8_t ssidLen;
    uint8_t passLen;
    uint8_t apSsidLen;
    uint8_t apPassLen;

    if((payload == NULL) || (length != ESP32_LINK_CFG_PAYLOAD_SIZE)) return ESP32_LINK_RESULT_INVALID;
    if((payload[ESP32_LINK_CFG_ENABLED_OFFSET] > 1U) ||
       (payload[ESP32_LINK_CFG_MODE_OFFSET] > ESP32_LINK_MODE_AP) ||
       (payload[ESP32_LINK_CFG_DHCP_OFFSET] > 1U) ||
       (payload[ESP32_LINK_CFG_AP_CHANNEL_OFFSET] == 0U) ||
       (payload[ESP32_LINK_CFG_AP_CHANNEL_OFFSET] > 13U))
        return ESP32_LINK_RESULT_INVALID;

    ssidLen = payload[ESP32_LINK_CFG_STA_SSID_LEN];
    passLen = payload[ESP32_LINK_CFG_STA_PASS_LEN];
    apSsidLen = payload[ESP32_LINK_CFG_AP_SSID_LEN];
    apPassLen = payload[ESP32_LINK_CFG_AP_PASS_LEN];
    if((ssidLen > 32U) || (passLen > 64U) || (apSsidLen > 32U) || (apPassLen > 64U))
        return ESP32_LINK_RESULT_INVALID;

    if((payload[ESP32_LINK_CFG_MODE_OFFSET] == ESP32_LINK_MODE_AP) &&
       (payload[ESP32_LINK_CFG_ENABLED_OFFSET] != 0U) &&
       (apPassLen != 0U) && (apPassLen < 8U))
        return ESP32_LINK_RESULT_INVALID;

    if(g_wifiStarted != 0U)
    {
        (void)esp_wifi_stop();
        g_wifiStarted = 0U;
    }

    g_enabled = payload[ESP32_LINK_CFG_ENABLED_OFFSET];
    g_mode = payload[ESP32_LINK_CFG_MODE_OFFSET];
    g_dhcp = payload[ESP32_LINK_CFG_DHCP_OFFSET];
    g_apChannel = payload[ESP32_LINK_CFG_AP_CHANNEL_OFFSET];
    memset(g_staSsid, 0, sizeof(g_staSsid));
    memset(g_staPassword, 0, sizeof(g_staPassword));
    memset(g_apSsid, 0, sizeof(g_apSsid));
    memset(g_apPassword, 0, sizeof(g_apPassword));
    memcpy(g_staSsid, &payload[ESP32_LINK_CFG_STA_SSID], ssidLen);
    memcpy(g_staPassword, &payload[ESP32_LINK_CFG_STA_PASS], passLen);
    memcpy(g_apSsid, &payload[ESP32_LINK_CFG_AP_SSID], apSsidLen);
    memcpy(g_apPassword, &payload[ESP32_LINK_CFG_AP_PASS], apPassLen);
    memcpy(g_staticIp, &payload[ESP32_LINK_CFG_STATIC_IP], 4U);
    memcpy(g_netmask, &payload[ESP32_LINK_CFG_NETMASK], 4U);
    memcpy(g_gateway, &payload[ESP32_LINK_CFG_GATEWAY], 4U);
    memcpy(g_dns, &payload[ESP32_LINK_CFG_DNS], 4U);
    memset(g_currentIp, 0, sizeof(g_currentIp));
    g_rssi = 0;
    g_scanRunning = 0U;
    g_scanComplete = 0U;
    g_scanCount = 0U;
    g_state = (g_enabled != 0U) ? ESP32_LINK_WIFI_IDLE : ESP32_LINK_WIFI_DISABLED;
    return ESP32_LINK_RESULT_OK;
}

uint8_t WifiManager_Connect(void)
{
    esp_err_t result;

    if(g_enabled == 0U) return ESP32_LINK_RESULT_NOT_CONNECTED;
    if(g_mode == ESP32_LINK_MODE_STA)
    {
        wifi_config_t config;
        memset(&config, 0, sizeof(config));
        memcpy(config.sta.ssid, g_staSsid, strnlen(g_staSsid, sizeof(g_staSsid)));
        memcpy(config.sta.password, g_staPassword, strnlen(g_staPassword, sizeof(g_staPassword)));
        config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        config.sta.pmf_cfg.capable = true;
        config.sta.pmf_cfg.required = false;
        /* A connect attempt still in progress makes set_config fail;
         * cancel it first (an error just means none was running). */
        if(g_wifiStarted != 0U) (void)esp_wifi_disconnect();
        if(g_dhcp == 0U)
        {
            result = WifiManager_SetStaticNetwork();
            if(result != ESP_OK) return ESP32_LINK_RESULT_FAILED;
        }
        else
        {
            result = esp_netif_dhcpc_start(g_staNetif);
            if((result != ESP_OK) && (result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED))
                ESP_LOGW(TAG, "DHCP client start: %s", esp_err_to_name(result));
        }
        result = esp_wifi_set_mode(WIFI_MODE_STA);
        if(result != ESP_OK) return ESP32_LINK_RESULT_FAILED;
        result = esp_wifi_set_config(WIFI_IF_STA, &config);
        if(result != ESP_OK) return ESP32_LINK_RESULT_FAILED;
    }
    else
    {
        wifi_config_t config;
        memset(&config, 0, sizeof(config));
        memcpy(config.ap.ssid, g_apSsid, strnlen(g_apSsid, sizeof(g_apSsid)));
        config.ap.ssid_len = (uint8_t)strnlen(g_apSsid, sizeof(g_apSsid));
        memcpy(config.ap.password, g_apPassword, strnlen(g_apPassword, sizeof(g_apPassword)));
        config.ap.channel = g_apChannel;
        config.ap.max_connection = 1U;
        config.ap.authmode = (g_apPassword[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        result = esp_wifi_set_mode(WIFI_MODE_AP);
        if(result != ESP_OK) return ESP32_LINK_RESULT_FAILED;
        result = esp_wifi_set_config(WIFI_IF_AP, &config);
        if(result != ESP_OK) return ESP32_LINK_RESULT_FAILED;
        (void)esp_netif_dhcps_start(g_apNetif);
    }

    if(g_wifiStarted == 0U)
    {
        result = esp_wifi_start();
        if((result != ESP_OK) && (result != ESP_ERR_WIFI_CONN)) return ESP32_LINK_RESULT_FAILED;
        g_wifiStarted = 1U;
    }
    if(g_mode == ESP32_LINK_MODE_STA)
    {
        g_state = ESP32_LINK_WIFI_CONNECTING;
        g_staAssociated = 0U;
        /* An attempt (scan + auth) takes a few seconds; the retry below
         * only runs after a disconnect event re-arms it. */
        g_nextReconnectUs = INT64_MAX;
        result = esp_wifi_connect();
        if((result != ESP_OK) && (result != ESP_ERR_WIFI_CONN))
            ESP_LOGW(TAG, "Initial connect: %s", esp_err_to_name(result));
    }
    return ESP32_LINK_RESULT_OK;
}

uint8_t WifiManager_Disconnect(void)
{
    if(g_wifiStarted != 0U)
    {
        if(g_mode == ESP32_LINK_MODE_STA) (void)esp_wifi_disconnect();
        (void)esp_wifi_stop();
        g_wifiStarted = 0U;
    }
    g_state = ESP32_LINK_WIFI_DISABLED;
    memset(g_currentIp, 0, sizeof(g_currentIp));
    g_rssi = 0;
    return ESP32_LINK_RESULT_OK;
}

void WifiManager_Task(void)
{
    wifi_ap_record_t ap;
    if((g_wifiStarted != 0U) && (g_mode == ESP32_LINK_MODE_STA) &&
       (g_state == ESP32_LINK_WIFI_CONNECTING) && (g_staAssociated == 0U) &&
       (esp_timer_get_time() >= g_nextReconnectUs))
    {
        g_nextReconnectUs = INT64_MAX;
        (void)esp_wifi_connect();
    }
    if((g_state == ESP32_LINK_WIFI_CONNECTED) && (esp_wifi_sta_get_ap_info(&ap) == ESP_OK))
    {
        g_rssi = ap.rssi;
    }
}

void WifiManager_GetStatus(WifiManager_Status_t *status)
{
    if(status == NULL) return;
    memset(status, 0, sizeof(*status));
    status->state = g_state;
    memcpy(status->ip, g_currentIp, sizeof(status->ip));
    status->rssi = g_rssi;
    memcpy(status->mac, g_mac, sizeof(status->mac));
    status->events = g_events;
    if(g_state == ESP32_LINK_WIFI_AP_STARTED)
    {
        esp_netif_ip_info_t ipInfo;
        if((g_apNetif != NULL) && (esp_netif_get_ip_info(g_apNetif, &ipInfo) == ESP_OK))
        {
            status->ip[0] = (uint8_t)(ipInfo.ip.addr & 0xFFU);
            status->ip[1] = (uint8_t)((ipInfo.ip.addr >> 8U) & 0xFFU);
            status->ip[2] = (uint8_t)((ipInfo.ip.addr >> 16U) & 0xFFU);
            status->ip[3] = (uint8_t)((ipInfo.ip.addr >> 24U) & 0xFFU);
        }
    }
}

uint8_t WifiManager_TakeEvents(void)
{
    uint8_t events = g_events;
    g_events = 0U;
    return events;
}

uint8_t WifiManager_ScanStart(void)
{
    esp_err_t result;
    if((g_wifiStarted == 0U) || (g_mode != ESP32_LINK_MODE_STA))
        return ESP32_LINK_RESULT_NOT_CONNECTED;
    if(g_scanRunning != 0U) return ESP32_LINK_RESULT_BUSY;
    g_scanComplete = 0U;
    g_scanCount = 0U;
    result = esp_wifi_scan_start(NULL, false);
    if(result != ESP_OK) return ESP32_LINK_RESULT_FAILED;
    g_scanRunning = 1U;
    return ESP32_LINK_RESULT_OK;
}

uint8_t WifiManager_ScanGetResult(uint8_t index, WifiManager_ScanResult_t *result,
                                  uint8_t *count, uint8_t *complete)
{
    if((count == NULL) || (complete == NULL)) return ESP32_LINK_RESULT_INVALID;
    *count = g_scanCount;
    *complete = g_scanComplete;
    if(g_scanComplete == 0U) return ESP32_LINK_RESULT_BUSY;
    if(index >= g_scanCount) return ESP32_LINK_RESULT_INVALID;
    if(result != NULL) *result = g_scanResults[index];
    return ESP32_LINK_RESULT_OK;
}

uint8_t WifiManager_IsNetworkReady(void)
{
    return ((g_state == ESP32_LINK_WIFI_CONNECTED) ||
            (g_state == ESP32_LINK_WIFI_AP_STARTED)) ? 1U : 0U;
}
