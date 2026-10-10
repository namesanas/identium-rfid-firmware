#ifndef RFID_CONTROLLER_READER_SETTINGS_H_
#define RFID_CONTROLLER_READER_SETTINGS_H_

#include <stdint.h>

/*
 * Reader identity / network settings that the vendor tools (Reader
 * Manager, ReaderSense) can change: Ethernet address, upload server,
 * reader name and work state. Stored separately from the Identium
 * ActiveMode record (configuration_storage) in flash sector 21.
 */

#define READER_SETTINGS_NAME_MAX         32U

#define READER_SETTINGS_WORK_PASSIVE     1U
#define READER_SETTINGS_WORK_ACTIVE      3U

#define READER_SETTINGS_URL_MAX          127U
#define READER_SETTINGS_WIFI_SSID_MAX    32U
#define READER_SETTINGS_WIFI_PASSWORD_MAX 64U

#define READER_SETTINGS_WIFI_STA         0U
#define READER_SETTINGS_WIFI_AP          1U
#define READER_SETTINGS_UPLOAD_IF_ETH    0U
#define READER_SETTINGS_UPLOAD_IF_WIFI   1U
#define READER_SETTINGS_UPLOAD_IF_AUTO   2U

/* Active-mode upload transport. */
#define READER_SETTINGS_UPLOAD_TCP       0U  /* JSON lines to serverIp:serverPort */
#define READER_SETTINGS_UPLOAD_HTTP      1U  /* POST to httpUrl */

typedef struct
{
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint8_t mac[6];
    uint16_t configPort;

    uint8_t serverIp[4];
    uint16_t serverPort;

    uint8_t workState;

    /* RF settings from the static configuration (rf.region / tx_powers).
     * Only used when rfValid != 0; otherwise product defaults apply. */
    uint8_t rfValid;
    uint8_t region;
    uint8_t reserved;
    uint16_t readPower;
    uint16_t writePower;

    char name[READER_SETTINGS_NAME_MAX + 1U];

    /* Added in record version 2. */
    uint8_t uploadMode;
    uint16_t httpTimeout;   /* upload.http.timeout, seconds (0 = recv_timeout) */
    char httpUrl[READER_SETTINGS_URL_MAX + 1U];

    /* Added in record version 3. Passwords are never returned in readback. */
    uint8_t wifiEnabled;
    uint8_t wifiMode;       /* READER_SETTINGS_WIFI_STA / _AP */
    uint8_t wifiDhcp;       /* 1 = DHCP, 0 = static addresses below */
    uint8_t wifiApChannel;
    char wifiSsid[READER_SETTINGS_WIFI_SSID_MAX + 1U];
    char wifiPassword[READER_SETTINGS_WIFI_PASSWORD_MAX + 1U];
    uint8_t wifiStaticIp[4];
    uint8_t wifiNetmask[4];
    uint8_t wifiGateway[4];
    uint8_t wifiDns[4];
    char wifiApSsid[READER_SETTINGS_WIFI_SSID_MAX + 1U];
    char wifiApPassword[READER_SETTINGS_WIFI_PASSWORD_MAX + 1U];
    uint8_t uploadInterface; /* eth / wifi / auto (physical Ethernet first) */
} ReaderSettings_t;

/* Load from flash (or defaults). Call before the W5500 is configured. */
void ReaderSettings_Init(void);

const ReaderSettings_t *ReaderSettings_Get(void);

/* Persist a full settings set. Returns 1 on success. The new values
 * become the current values; network changes take effect when
 * ReaderSettings_ApplyNetwork() is called. */
uint8_t ReaderSettings_Save(const ReaderSettings_t *settings);

/* Write IP / mask / gateway / MAC of the current settings to the W5500. */
uint8_t ReaderSettings_ApplyNetwork(void);

/* Reader name used in events / readback (falls back to the product name). */
const char *ReaderSettings_GetName(void);

uint8_t ReaderSettings_GetWorkState(void);

uint8_t ReaderSettings_GetUploadMode(void);
uint8_t ReaderSettings_GetUploadInterface(void);

/* Apply the stored network settings after delayMs (lets a reply reach the
 * PC first). Handled by ReaderSettings_Task(). */
void ReaderSettings_RequestNetworkApply(uint32_t delayMs);

/* Reset the MCU after delayMs (lets a reply reach the PC first). */
void ReaderSettings_RequestReboot(uint32_t delayMs);

void ReaderSettings_Task(void);

#endif /* RFID_CONTROLLER_READER_SETTINGS_H_ */
