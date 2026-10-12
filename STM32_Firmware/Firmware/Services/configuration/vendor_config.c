#include "vendor_config.h"

#include "configuration_service.h"
#include "reader_settings.h"
#include "../../Application/Silion/sillion_application.h"
#include "../ethernet/ethernet_service.h"
#include "../ethernet/http_upload.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern SILION_ReaderConfig_t readerConfig;

#define VENDOR_CONFIG_STATUS_OK          0UL
#define VENDOR_CONFIG_STATUS_INVALID     1UL
#define VENDOR_CONFIG_STATUS_FAILED      2UL

#define VENDOR_CONFIG_FILTER_SIZE        1024U

/* ------------------------------------------------------------------
 * Minimal JSON navigation (read-only, no allocation).
 * ------------------------------------------------------------------ */

typedef struct
{
    const char *start;
    const char *end;     /* one past the last character of the value */
} VendorJsonSpan_t;

static const char *VendorJson_SkipWs(const char *p, const char *end)
{
    while((p < end) && ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')))
    {
        p++;
    }
    return p;
}

/* Returns the position after a string starting at p (p points at '"'). */
static const char *VendorJson_SkipString(const char *p, const char *end)
{
    p++;
    while(p < end)
    {
        if(*p == '\\')
        {
            p += 2;
            continue;
        }
        if(*p == '"')
        {
            return p + 1;
        }
        p++;
    }
    return NULL;
}

/* Returns the position after the value starting at p. */
static const char *VendorJson_SkipValue(const char *p, const char *end)
{
    uint16_t depth = 0U;

    p = VendorJson_SkipWs(p, end);
    if(p >= end)
    {
        return NULL;
    }

    if(*p == '"')
    {
        return VendorJson_SkipString(p, end);
    }

    if((*p != '{') && (*p != '['))
    {
        while((p < end) && (*p != ',') && (*p != '}') && (*p != ']'))
        {
            p++;
        }
        return p;
    }

    while(p < end)
    {
        if(*p == '"')
        {
            p = VendorJson_SkipString(p, end);
            if(p == NULL)
            {
                return NULL;
            }
            continue;
        }
        if((*p == '{') || (*p == '['))
        {
            depth++;
        }
        else if((*p == '}') || (*p == ']'))
        {
            depth--;
            if(depth == 0U)
            {
                return p + 1;
            }
        }
        p++;
    }

    return NULL;
}

/* Find "key" in the object span; returns 1 and the value span. */
static uint8_t VendorJson_FindKey(
    VendorJsonSpan_t object,
    const char *key,
    VendorJsonSpan_t *value
)
{
    const char *p = VendorJson_SkipWs(object.start, object.end);
    size_t keyLength = strlen(key);

    if((p >= object.end) || (*p != '{'))
    {
        return 0U;
    }
    p++;

    for(;;)
    {
        const char *keyStart;
        const char *keyEnd;
        const char *valueEnd;

        p = VendorJson_SkipWs(p, object.end);
        if((p >= object.end) || (*p != '"'))
        {
            return 0U;
        }

        keyStart = p + 1;
        keyEnd = VendorJson_SkipString(p, object.end);
        if(keyEnd == NULL)
        {
            return 0U;
        }

        p = VendorJson_SkipWs(keyEnd, object.end);
        if((p >= object.end) || (*p != ':'))
        {
            return 0U;
        }
        p = VendorJson_SkipWs(p + 1, object.end);

        valueEnd = VendorJson_SkipValue(p, object.end);
        if(valueEnd == NULL)
        {
            return 0U;
        }

        if(
            ((size_t)(keyEnd - 1 - keyStart) == keyLength) &&
            (memcmp(keyStart, key, keyLength) == 0)
        )
        {
            value->start = p;
            value->end = valueEnd;
            return 1U;
        }

        p = VendorJson_SkipWs(valueEnd, object.end);
        if((p < object.end) && (*p == ','))
        {
            p++;
            continue;
        }
        return 0U;
    }
}

/* Dotted path lookup, e.g. "upload.tcp.ser_ip". */
static uint8_t VendorJson_Find(
    VendorJsonSpan_t root,
    const char *path,
    VendorJsonSpan_t *value
)
{
    char segment[32];
    VendorJsonSpan_t current = root;

    while(*path != '\0')
    {
        size_t length = 0U;

        while((path[length] != '\0') && (path[length] != '.'))
        {
            length++;
        }
        if(length >= sizeof(segment))
        {
            return 0U;
        }

        memcpy(segment, path, length);
        segment[length] = '\0';

        if(VendorJson_FindKey(current, segment, &current) == 0U)
        {
            return 0U;
        }

        path += length;
        if(*path == '.')
        {
            path++;
        }
    }

    *value = current;
    return 1U;
}

static uint8_t VendorJson_IsNull(VendorJsonSpan_t value)
{
    return (
        ((value.end - value.start) >= 4) &&
        (memcmp(value.start, "null", 4U) == 0)
    ) ? 1U : 0U;
}

/* Number or numeric string ("9000" is how Reader Manager sends ports). */
static uint8_t VendorJson_GetUnsigned(VendorJsonSpan_t value, uint32_t *out)
{
    const char *p = value.start;
    uint32_t result = 0U;
    uint8_t digits = 0U;

    if((p < value.end) && (*p == '"'))
    {
        p++;
    }

    while((p < value.end) && (*p >= '0') && (*p <= '9'))
    {
        result = (result * 10U) + (uint32_t)(*p - '0');
        digits++;
        p++;
    }

    if(digits == 0U)
    {
        return 0U;
    }

    *out = result;
    return 1U;
}

static uint8_t VendorJson_GetString(VendorJsonSpan_t value, char *out, size_t size)
{
    size_t length;

    if((value.start >= value.end) || (*value.start != '"') || (size == 0U))
    {
        return 0U;
    }

    length = (size_t)(value.end - value.start - 2);
    if(length >= size)
    {
        length = size - 1U;
    }

    memcpy(out, value.start + 1, length);
    out[length] = '\0';
    return 1U;
}

static uint8_t VendorJson_GetIp(VendorJsonSpan_t value, uint8_t ip[4])
{
    char text[20];
    unsigned int a;
    unsigned int b;
    unsigned int c;
    unsigned int d;

    if(VendorJson_GetString(value, text, sizeof(text)) == 0U)
    {
        return 0U;
    }

    if(
        (sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) ||
        (a > 255U) || (b > 255U) || (c > 255U) || (d > 255U)
    )
    {
        return 0U;
    }

    ip[0] = (uint8_t)a;
    ip[1] = (uint8_t)b;
    ip[2] = (uint8_t)c;
    ip[3] = (uint8_t)d;
    return 1U;
}

/* ------------------------------------------------------------------
 * Filtered document builder for the existing strict parsers.
 * ------------------------------------------------------------------ */

typedef struct
{
    char buffer[VENDOR_CONFIG_FILTER_SIZE];
    size_t length;
    uint8_t overflow;
} VendorFilter_t;

static void VendorFilter_Append(VendorFilter_t *filter, const char *text, size_t length)
{
    if((filter->length + length + 1U) > sizeof(filter->buffer))
    {
        filter->overflow = 1U;
        return;
    }

    memcpy(&filter->buffer[filter->length], text, length);
    filter->length += length;
    filter->buffer[filter->length] = '\0';
}

static void VendorFilter_AppendText(VendorFilter_t *filter, const char *text)
{
    VendorFilter_Append(filter, text, strlen(text));
}

/* Copy object.key into the filter as "key":value when present and not
 * null. *first tracks comma placement. */
static void VendorFilter_CopyKey(
    VendorFilter_t *filter,
    VendorJsonSpan_t object,
    const char *key,
    uint8_t *first
)
{
    VendorJsonSpan_t value;

    if(
        (VendorJson_FindKey(object, key, &value) == 0U) ||
        (VendorJson_IsNull(value) != 0U)
    )
    {
        return;
    }

    if(*first == 0U)
    {
        VendorFilter_AppendText(filter, ",");
    }
    *first = 0U;

    VendorFilter_AppendText(filter, "\"");
    VendorFilter_AppendText(filter, key);
    VendorFilter_AppendText(filter, "\":");
    VendorFilter_Append(filter, value.start, (size_t)(value.end - value.start));
}

/* Copy object.key only when it equals the required value. */
static void VendorFilter_CopyKeyIfValue(
    VendorFilter_t *filter,
    VendorJsonSpan_t object,
    const char *key,
    uint32_t requiredValue,
    uint8_t *first
)
{
    VendorJsonSpan_t value;
    uint32_t number;

    if(
        (VendorJson_FindKey(object, key, &value) != 0U) &&
        (VendorJson_GetUnsigned(value, &number) != 0U) &&
        (number == requiredValue)
    )
    {
        VendorFilter_CopyKey(filter, object, key, first);
    }
}

/* ------------------------------------------------------------------
 * Local inventory pause helper (RF / session changes need the module).
 * ------------------------------------------------------------------ */

static uint8_t VendorConfig_PauseInventory(void)
{
    if(SILION_Application_IsInventoryRunning() != 0U)
    {
        (void)SILION_Application_StopInventory();
        return 1U;
    }
    return 0U;
}

static void VendorConfig_ResumeInventory(uint8_t wasRunning)
{
    if(wasRunning != 0U)
    {
        (void)SILION_Application_StartInventory();
    }
}

uint8_t VendorConfig_SetWorkState(uint8_t workState)
{
    ReaderSettings_t settings = *ReaderSettings_Get();

    if(
        (workState != READER_SETTINGS_WORK_PASSIVE) &&
        (workState != READER_SETTINGS_WORK_ACTIVE)
    )
    {
        return 0U;
    }

    if(settings.workState != workState)
    {
        settings.workState = workState;

        if(ReaderSettings_Save(&settings) == 0U)
        {
            return 0U;
        }
    }

    /* Passive: wait for tool commands, no autonomous reading/upload.
     * Active: resume the configured autonomous behaviour. */
    if(workState == READER_SETTINGS_WORK_PASSIVE)
    {
        (void)VendorConfig_PauseInventory();
    }
    else if(
        (Configuration_Service_GetStartInventoryAtBoot() != 0U) &&
        (SILION_Application_GetState() == SILION_APP_IDLE)
    )
    {
        (void)SILION_Application_StartInventory();
    }

    return 1U;
}

/* ------------------------------------------------------------------
 * 0x25 set ActiveModeJson
 * ------------------------------------------------------------------ */

/* upload.http.url, when upload.http is given, must be http:// with an
 * IPv4 host (the reader has no DNS) and fit the settings. Returns the
 * URL in text (if not NULL); 1 = no http section or a usable URL. */
static uint8_t VendorConfig_GetHttpUrl(
    VendorJsonSpan_t root,
    uint8_t *present,
    char *text,
    size_t textSize,
    uint32_t *timeout
)
{
    VendorJsonSpan_t upload;
    VendorJsonSpan_t http;
    VendorJsonSpan_t value;
    char url[READER_SETTINGS_URL_MAX + 1U];
    HttpUpload_Url_t parsed;

    *present = 0U;

    if(
        (VendorJson_FindKey(root, "upload", &upload) == 0U) ||
        (VendorJson_FindKey(upload, "http", &http) == 0U) ||
        (*http.start != '{')
    )
    {
        return 1U;
    }

    *present = 1U;

    if(
        (VendorJson_FindKey(http, "url", &value) == 0U) ||
        ((value.end - value.start) < 2) ||
        ((size_t)(value.end - value.start - 2) > READER_SETTINGS_URL_MAX) ||
        (VendorJson_GetString(value, url, sizeof(url)) == 0U) ||
        (HttpUpload_ParseUrl(url, &parsed) == 0U)
    )
    {
        return 0U;
    }

    if((text != NULL) && (textSize > strlen(url)))
    {
        memcpy(text, url, strlen(url) + 1U);
    }

    if(timeout != NULL)
    {
        *timeout = 0U;
        if(VendorJson_FindKey(http, "timeout", &value) != 0U)
        {
            (void)VendorJson_GetUnsigned(value, timeout);
            if(*timeout > 65535U)
            {
                *timeout = 65535U;
            }
        }
    }

    return 1U;
}

static uint8_t VendorConfig_CheckHttpUrl(VendorJsonSpan_t root)
{
    uint8_t present;

    return VendorConfig_GetHttpUrl(root, &present, NULL, 0U, NULL);
}

static int VendorConfig_HexValue(char value)
{
    if((value >= '0') && (value <= '9')) return value - '0';
    if((value >= 'a') && (value <= 'f')) return value - 'a' + 10;
    if((value >= 'A') && (value <= 'F')) return value - 'A' + 10;
    return -1;
}

static uint8_t VendorConfig_AppendUtf8(uint32_t codepoint, char *out,
                                       size_t capacity, size_t *used)
{
    uint8_t bytes[4];
    size_t count;
    if(codepoint <= 0x7FU)
    {
        bytes[0] = (uint8_t)codepoint; count = 1U;
    }
    else if(codepoint <= 0x7FFU)
    {
        bytes[0] = (uint8_t)(0xC0U | (codepoint >> 6U));
        bytes[1] = (uint8_t)(0x80U | (codepoint & 0x3FU)); count = 2U;
    }
    else if((codepoint >= 0xD800U) && (codepoint <= 0xDFFFU))
    {
        return 0U;
    }
    else if(codepoint <= 0xFFFFU)
    {
        bytes[0] = (uint8_t)(0xE0U | (codepoint >> 12U));
        bytes[1] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU));
        bytes[2] = (uint8_t)(0x80U | (codepoint & 0x3FU)); count = 3U;
    }
    else if(codepoint <= 0x10FFFFU)
    {
        bytes[0] = (uint8_t)(0xF0U | (codepoint >> 18U));
        bytes[1] = (uint8_t)(0x80U | ((codepoint >> 12U) & 0x3FU));
        bytes[2] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU));
        bytes[3] = (uint8_t)(0x80U | (codepoint & 0x3FU)); count = 4U;
    }
    else return 0U;

    if((*used + count) >= capacity) return 0U;
    memcpy(&out[*used], bytes, count);
    *used += count;
    return 1U;
}

static uint8_t VendorConfig_DecodeJsonString(VendorJsonSpan_t value,
                                              char *out, size_t capacity)
{
    const char *p;
    const char *end;
    size_t used = 0U;
    if((value.start >= value.end) || (*value.start != '"') ||
       (value.end[-1] != '"') || (capacity == 0U)) return 0U;

    p = value.start + 1;
    end = value.end - 1;
    while(p < end)
    {
        uint32_t codepoint;
        if((unsigned char)*p < 0x20U) return 0U;
        if(*p != '\\')
        {
            if((used + 1U) >= capacity) return 0U;
            out[used++] = *p++;
            continue;
        }
        p++;
        if(p >= end) return 0U;
        switch(*p++)
        {
            case '"': codepoint = '"'; break;
            case '\\': codepoint = '\\'; break;
            case '/': codepoint = '/'; break;
            case 'b': codepoint = '\b'; break;
            case 'f': codepoint = '\f'; break;
            case 'n': codepoint = '\n'; break;
            case 'r': codepoint = '\r'; break;
            case 't': codepoint = '\t'; break;
            case 'u':
            {
                int h0, h1, h2, h3;
                if((end - p) < 4) return 0U;
                h0 = VendorConfig_HexValue(p[0]); h1 = VendorConfig_HexValue(p[1]);
                h2 = VendorConfig_HexValue(p[2]); h3 = VendorConfig_HexValue(p[3]);
                if((h0 < 0) || (h1 < 0) || (h2 < 0) || (h3 < 0)) return 0U;
                codepoint = ((uint32_t)h0 << 12U) | ((uint32_t)h1 << 8U) |
                            ((uint32_t)h2 << 4U) | (uint32_t)h3;
                p += 4;
                if((codepoint >= 0xD800U) && (codepoint <= 0xDBFFU))
                {
                    uint32_t low;
                    if(((end - p) < 6) || (p[0] != '\\') || (p[1] != 'u')) return 0U;
                    h0 = VendorConfig_HexValue(p[2]); h1 = VendorConfig_HexValue(p[3]);
                    h2 = VendorConfig_HexValue(p[4]); h3 = VendorConfig_HexValue(p[5]);
                    if((h0 < 0) || (h1 < 0) || (h2 < 0) || (h3 < 0)) return 0U;
                    low = ((uint32_t)h0 << 12U) | ((uint32_t)h1 << 8U) |
                          ((uint32_t)h2 << 4U) | (uint32_t)h3;
                    if((low < 0xDC00U) || (low > 0xDFFFU)) return 0U;
                    p += 6;
                    codepoint = 0x10000U + (((codepoint - 0xD800U) << 10U) | (low - 0xDC00U));
                }
                break;
            }
            default: return 0U;
        }
        if((codepoint < 0x20U) || (VendorConfig_AppendUtf8(codepoint, out, capacity, &used) == 0U))
            return 0U;
    }
    out[used] = '\0';
    return 1U;
}

static uint8_t VendorConfig_ApplyWifiString(VendorJsonSpan_t wifi,
                                            const char *key,
                                            char *destination,
                                            size_t destinationSize,
                                            uint8_t isPassword,
                                            uint8_t *settingsChanged)
{
    VendorJsonSpan_t value;
    char text[READER_SETTINGS_WIFI_PASSWORD_MAX + 1U];

    if(VendorJson_FindKey(wifi, key, &value) == 0U)
    {
        return 1U;
    }
    if((VendorConfig_DecodeJsonString(value, text, sizeof(text)) == 0U) ||
       (strlen(text) >= destinationSize))
    {
        return 0U;
    }

    /* Readback masks stored secrets. A masked round-trip must not overwrite
     * the real credential with four asterisks. */
    if((isPassword != 0U) && (strcmp(text, "****") == 0))
    {
        return 1U;
    }
    if(strcmp(destination, text) != 0)
    {
        memset(destination, 0, destinationSize);
        memcpy(destination, text, strlen(text));
        *settingsChanged = 1U;
    }
    return 1U;
}

static uint8_t VendorConfig_ApplyWifiNumber(VendorJsonSpan_t wifi,
                                            const char *key,
                                            uint8_t *destination,
                                            uint32_t maximum,
                                            uint8_t *settingsChanged)
{
    VendorJsonSpan_t value;
    uint32_t number;

    if(VendorJson_FindKey(wifi, key, &value) == 0U)
    {
        return 1U;
    }
    if((VendorJson_GetUnsigned(value, &number) == 0U) || (number > maximum))
    {
        return 0U;
    }
    if(*destination != (uint8_t)number)
    {
        *destination = (uint8_t)number;
        *settingsChanged = 1U;
    }
    return 1U;
}

static uint8_t VendorConfig_ApplyWifiIp(VendorJsonSpan_t wifi,
                                        const char *key,
                                        uint8_t destination[4],
                                        uint8_t *settingsChanged)
{
    VendorJsonSpan_t value;
    uint8_t address[4];

    if(VendorJson_FindKey(wifi, key, &value) == 0U)
    {
        return 1U;
    }
    if(VendorJson_GetIp(value, address) == 0U)
    {
        return 0U;
    }
    if(memcmp(destination, address, sizeof(address)) != 0)
    {
        memcpy(destination, address, sizeof(address));
        *settingsChanged = 1U;
    }
    return 1U;
}

static uint8_t VendorConfig_ApplyWifiSettings(VendorJsonSpan_t upload,
                                              ReaderSettings_t *settings,
                                              uint8_t *settingsChanged,
                                              uint8_t *uploadTargetChanged)
{
    VendorJsonSpan_t value;
    VendorJsonSpan_t wifi;
    char interfaceName[8];
    uint8_t interfaceValue;

    if(VendorJson_FindKey(upload, "interface", &value) != 0U)
    {
        if(VendorJson_GetString(value, interfaceName, sizeof(interfaceName)) == 0U)
        {
            return 0U;
        }
        if(strcmp(interfaceName, "eth") == 0)
            interfaceValue = READER_SETTINGS_UPLOAD_IF_ETH;
        else if(strcmp(interfaceName, "wifi") == 0)
            interfaceValue = READER_SETTINGS_UPLOAD_IF_WIFI;
        else if(strcmp(interfaceName, "auto") == 0)
            interfaceValue = READER_SETTINGS_UPLOAD_IF_AUTO;
        else
            return 0U;
        if(settings->uploadInterface != interfaceValue)
        {
            settings->uploadInterface = interfaceValue;
            *settingsChanged = 1U;
            *uploadTargetChanged = 1U;
        }
    }

    if(VendorJson_FindKey(upload, "wifi", &wifi) == 0U)
    {
        return 1U;
    }
    if(*wifi.start != '{')
    {
        return 0U;
    }

    if(!VendorConfig_ApplyWifiNumber(wifi, "enable", &settings->wifiEnabled, 1U, settingsChanged) ||
       !VendorConfig_ApplyWifiNumber(wifi, "dhcp", &settings->wifiDhcp, 1U, settingsChanged) ||
       !VendorConfig_ApplyWifiNumber(wifi, "ap_channel", &settings->wifiApChannel, 13U, settingsChanged) ||
       !VendorConfig_ApplyWifiString(wifi, "ssid", settings->wifiSsid, sizeof(settings->wifiSsid), 0U, settingsChanged) ||
       !VendorConfig_ApplyWifiString(wifi, "password", settings->wifiPassword, sizeof(settings->wifiPassword), 1U, settingsChanged) ||
       !VendorConfig_ApplyWifiString(wifi, "ap_ssid", settings->wifiApSsid, sizeof(settings->wifiApSsid), 0U, settingsChanged) ||
       !VendorConfig_ApplyWifiString(wifi, "ap_password", settings->wifiApPassword, sizeof(settings->wifiApPassword), 1U, settingsChanged) ||
       !VendorConfig_ApplyWifiIp(wifi, "ip", settings->wifiStaticIp, settingsChanged) ||
       !VendorConfig_ApplyWifiIp(wifi, "nm", settings->wifiNetmask, settingsChanged) ||
       !VendorConfig_ApplyWifiIp(wifi, "gw", settings->wifiGateway, settingsChanged) ||
       !VendorConfig_ApplyWifiIp(wifi, "dns", settings->wifiDns, settingsChanged))
    {
        return 0U;
    }

    if(VendorJson_FindKey(wifi, "mode", &value) != 0U)
    {
        char mode[8];
        uint8_t modeValue;
        if(VendorJson_GetString(value, mode, sizeof(mode)) == 0U)
            return 0U;
        if(strcmp(mode, "sta") == 0)
            modeValue = READER_SETTINGS_WIFI_STA;
        else if(strcmp(mode, "ap") == 0)
            modeValue = READER_SETTINGS_WIFI_AP;
        else
            return 0U;
        if(settings->wifiMode != modeValue)
        {
            settings->wifiMode = modeValue;
            *settingsChanged = 1U;
        }
    }

    return (settings->wifiApChannel != 0U) ? 1U : 0U;
}

static uint32_t VendorConfig_SetActive(const char *json, uint16_t length, uint8_t switchToActive)
{
    static VendorFilter_t filter;
    VendorJsonSpan_t root = { json, json + length };
    VendorJsonSpan_t section;
    VendorJsonSpan_t value;
    ReaderSettings_t settings = *ReaderSettings_Get();
    uint8_t settingsChanged = 0U;
    uint8_t serverChanged = 0U;
    uint8_t anyRuntime = 0U;
    uint8_t first;

    memset(&filter, 0, sizeof(filter));
    VendorFilter_AppendText(&filter, "{\"command_data\":{");

    /* glob_params: supported numeric fields; name goes to settings. */
    if(VendorJson_FindKey(root, "glob_params", &section) != 0U)
    {
        static const char *const keys[] =
        {
            "hb_cylce", "s_buf_size", "thwdt_net_timeout",
            "thwdt_inv_timeout", "start_inv_atbeg", "remote_cmd_reply"
        };
        uint8_t i;
        size_t mark = filter.length;

        first = 1U;
        VendorFilter_AppendText(&filter, "\"glob_params\":{");
        for(i = 0U; i < (sizeof(keys) / sizeof(keys[0])); i++)
        {
            VendorFilter_CopyKey(&filter, section, keys[i], &first);
        }
        VendorFilter_AppendText(&filter, "}");

        if(first != 0U)
        {
            filter.length = mark;
            filter.buffer[mark] = '\0';
        }
        else
        {
            anyRuntime = 1U;
        }

        if(VendorJson_FindKey(section, "name", &value) != 0U)
        {
            char name[READER_SETTINGS_NAME_MAX + 1U];

            if(
                (VendorJson_GetString(value, name, sizeof(name)) != 0U) &&
                (name[0] != '\0') &&
                (strcmp(name, settings.name) != 0)
            )
            {
                memset(settings.name, 0, sizeof(settings.name));
                memcpy(settings.name, name, strlen(name));
                settingsChanged = 1U;
            }
        }
    }

    /* upload: supported fields; tcp target goes to settings. */
    if(VendorJson_FindKey(root, "upload", &section) != 0U)
    {
        size_t mark = filter.length;
        VendorJsonSpan_t aggr;

        first = 1U;
        VendorFilter_AppendText(&filter, (anyRuntime != 0U) ? ",\"upload\":{" : "\"upload\":{");
        VendorFilter_CopyKeyIfValue(&filter, section, "client_ack", 0U, &first);
        VendorFilter_CopyKeyIfValue(&filter, section, "crc_enable", 0U, &first);
        VendorFilter_CopyKey(&filter, section, "recv_timeout", &first);
        VendorFilter_CopyKey(&filter, section, "clr_r_buf_time", &first);
        VendorFilter_CopyKeyIfValue(&filter, section, "hw_inf", 1U, &first);

        if(
            (VendorJson_FindKey(section, "data_aggr", &aggr) != 0U) &&
            (VendorJson_IsNull(aggr) == 0U)
        )
        {
            uint8_t firstAggr = 1U;

            if(first == 0U)
            {
                VendorFilter_AppendText(&filter, ",");
            }
            first = 0U;
            VendorFilter_AppendText(&filter, "\"data_aggr\":{");
            VendorFilter_CopyKey(&filter, aggr, "mode", &firstAggr);
            VendorFilter_CopyKey(&filter, aggr, "timeval", &firstAggr);
            VendorFilter_AppendText(&filter, "}");
        }
        VendorFilter_AppendText(&filter, "}");

        if(first != 0U)
        {
            filter.length = mark;
            filter.buffer[mark] = '\0';
        }
        else
        {
            anyRuntime = 1U;
        }

        if(VendorJson_Find(section, "tcp.ser_ip", &value) != 0U)
        {
            uint8_t ip[4];

            if(
                (VendorJson_GetIp(value, ip) != 0U) &&
                (memcmp(ip, settings.serverIp, 4U) != 0)
            )
            {
                memcpy(settings.serverIp, ip, 4U);
                settingsChanged = 1U;
                serverChanged = 1U;
            }
        }

        if(VendorJson_Find(section, "tcp.ser_port", &value) != 0U)
        {
            uint32_t port;

            if(
                (VendorJson_GetUnsigned(value, &port) != 0U) &&
                (port > 0U) && (port <= 65535U) &&
                (port != settings.serverPort)
            )
            {
                settings.serverPort = (uint16_t)port;
                settingsChanged = 1U;
                serverChanged = 1U;
            }
        }

        if(VendorConfig_ApplyWifiSettings(section, &settings, &settingsChanged,
                                          &serverChanged) == 0U)
        {
            return VENDOR_CONFIG_STATUS_INVALID;
        }

        /* Upload transport: the tools send exactly one of tcp / http /
         * mqtt as an object and the others as null. */
        uint8_t httpPresent = 0U;
        char text[READER_SETTINGS_URL_MAX + 1U];
        uint32_t timeout = 0U;

        if(VendorConfig_GetHttpUrl(root, &httpPresent, text, sizeof(text), &timeout) == 0U)
        {
            /* https://, a host name (no DNS) or a malformed URL. */
            return VENDOR_CONFIG_STATUS_INVALID;
        }

        if(httpPresent != 0U)
        {
            if(
                (settings.uploadMode != READER_SETTINGS_UPLOAD_HTTP) ||
                (strcmp(settings.httpUrl, text) != 0) ||
                (settings.httpTimeout != (uint16_t)timeout)
            )
            {
                settings.uploadMode = READER_SETTINGS_UPLOAD_HTTP;
                memset(settings.httpUrl, 0, sizeof(settings.httpUrl));
                memcpy(settings.httpUrl, text, strlen(text));
                settings.httpTimeout = (uint16_t)timeout;
                settingsChanged = 1U;
                serverChanged = 1U;
            }
        }
        else if(
            (VendorJson_FindKey(section, "tcp", &value) != 0U) &&
            (*value.start == '{') &&
            (settings.uploadMode != READER_SETTINGS_UPLOAD_TCP)
        )
        {
            settings.uploadMode = READER_SETTINGS_UPLOAD_TCP;
            settingsChanged = 1U;
            serverChanged = 1U;
        }
    }

    /* events: drop codes the F429 does not implement (4, 5). */
    if(
        (VendorJson_FindKey(root, "events", &section) != 0U) &&
        (*section.start == '[')
    )
    {
        const char *p = section.start + 1;
        char number[8];
        uint8_t firstEvent = 1U;
        size_t mark = filter.length;

        VendorFilter_AppendText(&filter, (anyRuntime != 0U) ? ",\"events\":[" : "\"events\":[");

        while(p < section.end)
        {
            uint32_t code = 0U;
            uint8_t digits = 0U;

            while((p < section.end) && ((*p < '0') || (*p > '9')))
            {
                p++;
            }
            while((p < section.end) && (*p >= '0') && (*p <= '9'))
            {
                code = (code * 10U) + (uint32_t)(*p - '0');
                digits++;
                p++;
            }

            if((digits != 0U) && (code >= 1U) && (code <= 6U) && (code != 4U) && (code != 5U))
            {
                (void)snprintf(number, sizeof(number), "%s%lu", (firstEvent != 0U) ? "" : ",", (unsigned long)code);
                VendorFilter_AppendText(&filter, number);
                firstEvent = 0U;
            }
        }
        VendorFilter_AppendText(&filter, "]");

        if(firstEvent != 0U)
        {
            filter.length = mark;
            filter.buffer[mark] = '\0';
        }
        else
        {
            anyRuntime = 1U;
        }
    }

    if(
        (VendorJson_FindKey(root, "cus_param", &value) != 0U) &&
        (*value.start == '"')
    )
    {
        VendorFilter_AppendText(&filter, (anyRuntime != 0U) ? ",\"cus_param\":" : "\"cus_param\":");
        VendorFilter_Append(&filter, value.start, (size_t)(value.end - value.start));
        anyRuntime = 1U;
    }

    VendorFilter_AppendText(&filter, "}}");

    if(filter.overflow != 0U)
    {
        return VENDOR_CONFIG_STATUS_INVALID;
    }

    if(anyRuntime != 0U)
    {
        uint32_t errorCode = 0U;
        const char *errorString = NULL;

        if(
            Configuration_Service_ApplyRuntimeConfig(
                filter.buffer,
                &errorCode,
                &errorString
            ) == 0U
        )
        {
            return (errorCode != 0U) ? errorCode : VENDOR_CONFIG_STATUS_INVALID;
        }
    }

    /* Setting the active-mode document from the vendor tools switches the
     * reader to active mode (that is what both tools expect after "Set");
     * the host SET_HTTP command only stores the settings. */
    if(switchToActive != 0U)
    {
        settings.workState = READER_SETTINGS_WORK_ACTIVE;
    }
    if(
        ((settingsChanged != 0U) || (ReaderSettings_Get()->workState != settings.workState)) &&
        (ReaderSettings_Save(&settings) == 0U)
    )
    {
        return VENDOR_CONFIG_STATUS_FAILED;
    }

    if(switchToActive != 0U)
    {
        (void)VendorConfig_SetWorkState(READER_SETTINGS_WORK_ACTIVE);
    }

    if(serverChanged != 0U)
    {
        Ethernet_Service_Restart();
    }

    return VENDOR_CONFIG_STATUS_OK;
}

/* ------------------------------------------------------------------
 * 0x1F set StaticModelJson
 * ------------------------------------------------------------------ */

static uint32_t VendorConfig_SetStatic(const char *json, uint16_t length)
{
    VendorJsonSpan_t root = { json, json + length };
    VendorJsonSpan_t section;
    VendorJsonSpan_t value;
    ReaderSettings_t settings = *ReaderSettings_Get();
    uint8_t settingsChanged = 0U;
    uint8_t networkChanged = 0U;
    uint8_t rfChanged = 0U;
    uint8_t reboot = 0U;
    uint8_t wasInventory;
    uint32_t number;

    if(
        (VendorJson_FindKey(root, "reboot", &value) != 0U) &&
        (VendorJson_GetUnsigned(value, &number) != 0U) &&
        (number != 0U)
    )
    {
        reboot = 1U;
    }

    /* ethernet: static addressing only ("0.0.0.0" = DHCP, not supported
     * by this firmware, so the current address is kept). */
    if(VendorJson_FindKey(root, "ethernet", &section) != 0U)
    {
        uint8_t ip[4];

        if(
            (VendorJson_FindKey(section, "ip", &value) != 0U) &&
            (VendorJson_GetIp(value, ip) != 0U) &&
            ((ip[0] | ip[1] | ip[2] | ip[3]) != 0U)
        )
        {
            if(VendorJson_FindKey(section, "ip", &value) != 0U && memcmp(ip, settings.ip, 4U) != 0)
            {
                memcpy(settings.ip, ip, 4U);
                networkChanged = 1U;
            }

            if(
                (VendorJson_FindKey(section, "nm", &value) != 0U) &&
                (VendorJson_GetIp(value, ip) != 0U) &&
                (memcmp(ip, settings.netmask, 4U) != 0)
            )
            {
                memcpy(settings.netmask, ip, 4U);
                networkChanged = 1U;
            }

            if(
                (VendorJson_FindKey(section, "gw", &value) != 0U) &&
                (VendorJson_GetIp(value, ip) != 0U) &&
                (memcmp(ip, settings.gateway, 4U) != 0)
            )
            {
                memcpy(settings.gateway, ip, 4U);
                networkChanged = 1U;
            }
        }

        if(
            (VendorJson_FindKey(section, "dns", &value) != 0U) &&
            (VendorJson_GetIp(value, ip) != 0U) &&
            (memcmp(ip, settings.dns, 4U) != 0)
        )
        {
            memcpy(settings.dns, ip, 4U);
            settingsChanged = 1U;
        }
    }

    /* rf: region and the first tx_powers entry drive the F429's own
     * inventory configuration. */
    if(VendorJson_FindKey(root, "rf", &section) != 0U)
    {
        uint8_t region = settings.rfValid ? settings.region : readerConfig.region;
        uint16_t readPower = settings.rfValid ? settings.readPower : readerConfig.readPower;
        uint16_t writePower = settings.rfValid ? settings.writePower : readerConfig.writePower;

        if(
            (VendorJson_FindKey(section, "region", &value) != 0U) &&
            (VendorJson_GetUnsigned(value, &number) != 0U) &&
            (number <= 255U)
        )
        {
            region = (uint8_t)number;
        }

        if(
            (VendorJson_FindKey(section, "tx_powers", &value) != 0U) &&
            (*value.start == '[')
        )
        {
            const char *p = value.start + 1;

            while((p < value.end) && (*p != '{'))
            {
                p++;
            }

            if(p < value.end)
            {
                VendorJsonSpan_t entry;

                entry.start = p;
                entry.end = VendorJson_SkipValue(p, value.end);

                if(entry.end != NULL)
                {
                    VendorJsonSpan_t field;

                    if(
                        (VendorJson_FindKey(entry, "rp", &field) != 0U) &&
                        (VendorJson_GetUnsigned(field, &number) != 0U) &&
                        (number <= 3300U)
                    )
                    {
                        readPower = (uint16_t)number;
                    }

                    if(
                        (VendorJson_FindKey(entry, "wp", &field) != 0U) &&
                        (VendorJson_GetUnsigned(field, &number) != 0U) &&
                        (number <= 3300U)
                    )
                    {
                        writePower = (uint16_t)number;
                    }
                }
            }
        }

        if(
            (settings.rfValid == 0U) ||
            (region != settings.region) ||
            (readPower != settings.readPower) ||
            (writePower != settings.writePower)
        )
        {
            settings.rfValid = 1U;
            settings.region = region;
            settings.readPower = readPower;
            settings.writePower = writePower;
            rfChanged = 1U;
        }
    }

    if((settingsChanged != 0U) || (networkChanged != 0U) || (rfChanged != 0U))
    {
        if(ReaderSettings_Save(&settings) == 0U)
        {
            return VENDOR_CONFIG_STATUS_FAILED;
        }
    }

    wasInventory = VendorConfig_PauseInventory();

    /* RF changes applied live when the module is free (it is owned by
     * the module bridge while a tool session is open: then they are
     * applied at the next boot). */
    if(
        (rfChanged != 0U) &&
        (SILION_Application_GetState() == SILION_APP_IDLE)
    )
    {
        (void)SILION_Application_SetRegion(settings.region);
        (void)SILION_Application_SetPower(settings.readPower, settings.writePower);
    }

    /* protocol.gen2.session goes through the existing Identium static
     * configuration path (persistent Configuration record). */
    if(
        (VendorJson_Find(root, "protocol.gen2.session", &value) != 0U) &&
        (VendorJson_GetUnsigned(value, &number) != 0U) &&
        (number <= 3U)
    )
    {
        char document[96];
        uint32_t errorCode = 0U;
        const char *errorString = NULL;
        uint8_t rebootRequested = 0U;

        (void)snprintf(
            document,
            sizeof(document),
            "{\"command_data\":{\"protocol\":{\"gen2\":{\"session\":%lu}}}}",
            (unsigned long)number
        );

        if(SILION_Application_GetState() == SILION_APP_IDLE)
        {
            (void)Configuration_Service_ApplyStaticConfig(
                document,
                &errorCode,
                &errorString,
                &rebootRequested
            );
        }
    }

    VendorConfig_ResumeInventory(wasInventory);

    if(reboot != 0U)
    {
        ReaderSettings_RequestReboot(500U);
    }
    else if(networkChanged != 0U)
    {
        /* Give the reply time to reach the tool before the address moves. */
        ReaderSettings_RequestNetworkApply(1000U);
    }

    return VENDOR_CONFIG_STATUS_OK;
}

/* ------------------------------------------------------------------
 * 0x20 clear: default Identium parameters, passive mode.
 * ------------------------------------------------------------------ */

static uint32_t VendorConfig_Clear(void)
{
    uint32_t errorCode = 0U;
    const char *errorString = NULL;
    uint8_t rebootRequested = 0U;

    (void)VendorConfig_PauseInventory();

    /* The Identium static "reset" restores every stored default. */
    if(SILION_Application_GetState() == SILION_APP_IDLE)
    {
        (void)Configuration_Service_ApplyStaticConfig(
            "{\"reset\":1}",
            &errorCode,
            &errorString,
            &rebootRequested
        );
    }

    return (VendorConfig_SetWorkState(READER_SETTINGS_WORK_PASSIVE) != 0U)
        ? VENDOR_CONFIG_STATUS_OK
        : VENDOR_CONFIG_STATUS_FAILED;
}

/* ------------------------------------------------------------------
 * Deferred execution.
 *
 * Reader Manager waits only 1000 ms for the reply, while saving the
 * Identium configuration erases a 128 KB flash sector (1-2 s). The
 * request is therefore acknowledged first and applied right after the
 * reply has been sent (VendorConfig_Task).
 * ------------------------------------------------------------------ */

#define VENDOR_CONFIG_MAX_REQUEST  2048U

static char g_pendingJson[VENDOR_CONFIG_MAX_REQUEST + 1U];
static uint16_t g_pendingLength = 0U;
static uint8_t g_pendingCommand = 0U;
static uint8_t g_pendingValid = 0U;
static uint8_t g_pendingKeepWorkState = 0U;

static uint32_t VendorConfig_Execute(uint8_t commandId, const char *json, uint16_t length)
{
    switch(commandId)
    {
        case VENDOR_CONFIG_CMD_SET_ACTIVE:
            return VendorConfig_SetActive(json, length, (g_pendingKeepWorkState == 0U) ? 1U : 0U);

        case VENDOR_CONFIG_CMD_SET_STATIC:
            return VendorConfig_SetStatic(json, length);

        case VENDOR_CONFIG_CMD_CLEAR:
            return VendorConfig_Clear();

        case VENDOR_CONFIG_CMD_SWITCH:
            return VendorConfig_SetWorkState(
                (ReaderSettings_GetWorkState() == READER_SETTINGS_WORK_ACTIVE)
                    ? READER_SETTINGS_WORK_PASSIVE
                    : READER_SETTINGS_WORK_ACTIVE
            ) ? VENDOR_CONFIG_STATUS_OK : VENDOR_CONFIG_STATUS_FAILED;

        default:
            return VENDOR_CONFIG_STATUS_INVALID;
    }
}

uint32_t VendorConfig_Handle(
    uint8_t commandId,
    const char *json,
    uint16_t length,
    uint8_t *replyByte
)
{
    VendorJsonSpan_t root;

    *replyByte = ReaderSettings_GetWorkState();

    if(g_pendingValid != 0U)
    {
        return VENDOR_CONFIG_STATUS_FAILED;   /* previous request still pending */
    }

    if(
        (commandId == VENDOR_CONFIG_CMD_SET_ACTIVE) ||
        (commandId == VENDOR_CONFIG_CMD_SET_STATIC)
    )
    {
        /* Cheap structural validation before acknowledging. */
        root.start = json;
        root.end = json + length;

        if(
            (length == 0U) ||
            (length > VENDOR_CONFIG_MAX_REQUEST) ||
            (VendorJson_SkipValue(VendorJson_SkipWs(root.start, root.end), root.end) == NULL) ||
            (*VendorJson_SkipWs(root.start, root.end) != '{')
        )
        {
            return VENDOR_CONFIG_STATUS_INVALID;
        }

        /* An unusable upload URL must be reported now: the change itself
         * is applied after the reply. */
        if(
            (commandId == VENDOR_CONFIG_CMD_SET_ACTIVE) &&
            (VendorConfig_CheckHttpUrl(root) == 0U)
        )
        {
            return VENDOR_CONFIG_STATUS_INVALID;
        }

        memcpy(g_pendingJson, json, length);
        g_pendingJson[length] = '\0';
        g_pendingLength = length;
        *replyByte = READER_SETTINGS_WORK_ACTIVE;
    }
    else if(commandId == VENDOR_CONFIG_CMD_CLEAR)
    {
        g_pendingLength = 0U;
        *replyByte = READER_SETTINGS_WORK_PASSIVE;
    }
    else if(commandId == VENDOR_CONFIG_CMD_SWITCH)
    {
        g_pendingLength = 0U;
        *replyByte = (ReaderSettings_GetWorkState() == READER_SETTINGS_WORK_ACTIVE)
            ? READER_SETTINGS_WORK_PASSIVE
            : READER_SETTINGS_WORK_ACTIVE;
    }
    else
    {
        return VENDOR_CONFIG_STATUS_INVALID;
    }

    if(commandId == VENDOR_CONFIG_CMD_SET_STATIC)
    {
        *replyByte = ReaderSettings_GetWorkState();
    }

    g_pendingCommand = commandId;
    g_pendingKeepWorkState = 0U;
    g_pendingValid = 1U;
    return VENDOR_CONFIG_STATUS_OK;
}

uint32_t VendorConfig_HandleActiveSettings(const char *json, uint16_t length)
{
    uint8_t replyByte;
    uint32_t status;

    status = VendorConfig_Handle(VENDOR_CONFIG_CMD_SET_ACTIVE, json, length, &replyByte);
    if(status == VENDOR_CONFIG_STATUS_OK)
    {
        g_pendingKeepWorkState = 1U;
    }

    return status;
}

void VendorConfig_Task(void)
{
    if(g_pendingValid == 0U)
    {
        return;
    }

    (void)VendorConfig_Execute(g_pendingCommand, g_pendingJson, g_pendingLength);
    g_pendingValid = 0U;
}
