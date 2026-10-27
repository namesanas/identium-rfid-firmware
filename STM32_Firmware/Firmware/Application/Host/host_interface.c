#include "host_interface.h"
#include "sillion_application.h"
#include "../../Services/host/host_transport.h"
#include "../../Services/USB/usb_cdc_transport.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../Services/ethernet/ethernet_service.h"
#include "../../Services/ethernet/host_tcp_server.h"

#include "../../Services/system/system_fault.h"
#include "../../Services/system/diagnostics.h"
#include "../../Services/GPIO/gpio_service.h"
#include "../../Services/configuration/configuration_port.h"
#include "../../Services/configuration/reader_settings.h"
#include "../../Services/configuration/configuration_service.h"
#include "../../Services/configuration/vendor_config.h"
#include "../../Services/wifi/wifi_service.h"
#include "../../Services/storage/tag_log.h"
#include "../../Services/update/firmware_update.h"




static uint8_t HOST_HexNibble(
    char c
)
{
    if(c >= '0' && c <= '9')
        return (uint8_t)(c - '0');

    if(c >= 'A' && c <= 'F')
        return (uint8_t)(c - 'A' + 10U);

    if(c >= 'a' && c <= 'f')
        return (uint8_t)(c - 'a' + 10U);

    return 0xFFU;
}

static const char *HOST_WifiStateName(uint8_t state)
{
    switch(state)
    {
        case ESP32_LINK_WIFI_IDLE:       return "IDLE";
        case ESP32_LINK_WIFI_CONNECTING: return "CONNECTING";
        case ESP32_LINK_WIFI_CONNECTED:  return "CONNECTED";
        case ESP32_LINK_WIFI_AP_STARTED: return "AP";
        case ESP32_LINK_WIFI_ERROR:      return "ERROR";
        default:                         return "DISABLED";
    }
}


/* GET_WIFI reply: link and connection state plus the stored settings
 * (passwords are never returned). */
static void HOST_SendWifiStatus(void)
{
    static char line[200];
    const ReaderSettings_t *settings = ReaderSettings_Get();
    WiFi_Status_t status;
    const char *uploadIf;

    WiFi_Service_GetStatus(&status);

    if(settings->uploadInterface == READER_SETTINGS_UPLOAD_IF_ETH)
        uploadIf = "ETH";
    else if(settings->uploadInterface == READER_SETTINGS_UPLOAD_IF_WIFI)
        uploadIf = "WIFI";
    else
        uploadIf = "AUTO";

    (void)snprintf(
        line,
        sizeof(line),
        "WIFI,LINK=%s,STATE=%s,IP=%u.%u.%u.%u,RSSI=%d,HOST=%u,UPLOAD=%u,"
        "ENABLE=%u,MODE=%s,SSID=%s,IF=%s,ERRORS=%lu,REJECTED=%02X/%u\r\n",
        (status.linkOnline != 0U) ? "ONLINE" : "OFFLINE",
        HOST_WifiStateName(status.wifiState),
        status.ip[0], status.ip[1], status.ip[2], status.ip[3],
        (int)status.rssi,
        (unsigned int)status.hostConnected,
        (unsigned int)status.uploadConnected,
        (unsigned int)settings->wifiEnabled,
        (settings->wifiMode == READER_SETTINGS_WIFI_AP) ? "AP" : "STA",
        (settings->wifiMode == READER_SETTINGS_WIFI_AP)
            ? settings->wifiApSsid : settings->wifiSsid,
        uploadIf,
        (unsigned long)status.linkErrors,
        (unsigned int)status.rejectedCommand,
        (unsigned int)status.rejectedResult
    );

    HOST_SendString(line);
}


/*
 * SET_WIFI,<0|1>,<STA|AP>,<ssid>,<password>
 * The SSID may not contain a comma; the password is the rest of the line
 * (empty = open network / open AP). "****" keeps the stored password.
 * SET_WIFI,0,STA,, disables WiFi and clears the station SSID and password.
 * In AP mode the SSID and password are those of the reader's own AP.
 */
static uint8_t HOST_SetWifi(char *args)
{
    ReaderSettings_t settings = *ReaderSettings_Get();
    char *enable = args;
    char *mode;
    char *ssid;
    char *password;
    char *ssidTarget;
    char *passwordTarget;

    mode = strchr(enable, ',');
    if(mode == NULL) return 0U;
    *mode++ = '\0';

    ssid = strchr(mode, ',');
    if(ssid == NULL) return 0U;
    *ssid++ = '\0';

    password = strchr(ssid, ',');
    if(password == NULL) return 0U;
    *password++ = '\0';

    if((strcmp(enable, "0") != 0) && (strcmp(enable, "1") != 0)) return 0U;
    settings.wifiEnabled = (uint8_t)(enable[0] - '0');

    if(strcmp(mode, "STA") == 0)
    {
        settings.wifiMode = READER_SETTINGS_WIFI_STA;
        ssidTarget = settings.wifiSsid;
        passwordTarget = settings.wifiPassword;
    }
    else if(strcmp(mode, "AP") == 0)
    {
        settings.wifiMode = READER_SETTINGS_WIFI_AP;
        ssidTarget = settings.wifiApSsid;
        passwordTarget = settings.wifiApPassword;
    }
    else
    {
        return 0U;
    }

    if(
        /* An empty SSID is only accepted when WiFi is being disabled. */
        ((strlen(ssid) == 0U) && (settings.wifiEnabled != 0U)) ||
        (strlen(ssid) > READER_SETTINGS_WIFI_SSID_MAX) ||
        (strlen(password) > READER_SETTINGS_WIFI_PASSWORD_MAX) ||
        /* WPA2 keys are 8..63 characters; the ESP rejects shorter AP keys. */
        ((strlen(password) > 0U) && (strlen(password) < 8U) &&
         (strcmp(password, "****") != 0))
    )
    {
        return 0U;
    }

    memset(ssidTarget, 0, READER_SETTINGS_WIFI_SSID_MAX + 1U);
    memcpy(ssidTarget, ssid, strlen(ssid));

    if(strcmp(password, "****") != 0)
    {
        memset(passwordTarget, 0, READER_SETTINGS_WIFI_PASSWORD_MAX + 1U);
        memcpy(passwordTarget, password, strlen(password));
    }

    return ReaderSettings_Save(&settings);
}


/* SET_UPLOAD_IF,ETH|WIFI|AUTO -- interface for active-mode uploads. */
static uint8_t HOST_SetUploadInterface(const char *name)
{
    ReaderSettings_t settings = *ReaderSettings_Get();

    if(strcmp(name, "ETH") == 0)
        settings.uploadInterface = READER_SETTINGS_UPLOAD_IF_ETH;
    else if(strcmp(name, "WIFI") == 0)
        settings.uploadInterface = READER_SETTINGS_UPLOAD_IF_WIFI;
    else if(strcmp(name, "AUTO") == 0)
        settings.uploadInterface = READER_SETTINGS_UPLOAD_IF_AUTO;
    else
        return 0U;

    return ReaderSettings_Save(&settings);
}


/* Records per TAGLOG_READ, so one reply stays well inside the
 * transport buffers; the host pages with the returned NEXT. */
#define HOST_TAGLOG_READ_MAX 20U

static void HOST_SendTagLogStatus(void)
{
    TagLog_Status_t status;
    char line[160];
    const char *state;

    TagLog_GetStatus(&status);

    if(status.state == TAG_LOG_STATE_READY)
        state = "READY";
    else if(status.state == TAG_LOG_STATE_CLEARING)
        state = "CLEARING";
    else
        state = "ABSENT";

    (void)snprintf(
        line,
        sizeof(line),
        "TAGLOG,STATUS,%s,OLDEST=%lu,NEXT=%lu,COUNT=%lu,CAPACITY=%lu,"
        "PENDING=%lu,DROPPED=%lu\r\n",
        state,
        (unsigned long)status.oldestSeq,
        (unsigned long)status.nextSeq,
        (unsigned long)(status.nextSeq - status.oldestSeq),
        (unsigned long)status.capacity,
        (unsigned long)status.pending,
        (unsigned long)status.dropped
    );

    HOST_SendString(line);
}

/* TAGLOG_READ,<seq>,<count>: one line per stored record, then
 * OK,TAGLOG_READ,NEXT=<seq to ask for next>. Records that were
 * overwritten or never written are skipped.
 * Line: TAGLOG,<seq>,<sec>.<ms>,<E|U>,<ant>,<rssi>,<reads>,<EPC>[,TRUNC]
 * E = Unix time, U = uptime of the boot that stored it. */
static uint8_t HOST_SendTagLogRecords(const char *args)
{
    TagLog_Record_t record;
    char line[128];
    char *end;
    uint32_t seq;
    uint32_t count;
    uint32_t i;
    int pos;
    uint8_t b;

    seq = (uint32_t)strtoul(args, &end, 10);

    if((end == args) || (*end != ','))
        return 0U;

    args = end + 1;
    count = (uint32_t)strtoul(args, &end, 10);

    if((end == args) || (*end != '\0') || (count == 0U))
        return 0U;

    if(count > HOST_TAGLOG_READ_MAX)
        count = HOST_TAGLOG_READ_MAX;

    for(i = 0U; i < count; i++, seq++)
    {
        if(TagLog_Read(seq, &record) == 0U)
            continue;

        pos = snprintf(
            line,
            sizeof(line),
            "TAGLOG,%lu,%lu.%03lu,%c,%u,%d,%u,",
            (unsigned long)record.seq,
            (unsigned long)(record.timeMs / 1000U),
            (unsigned long)(record.timeMs % 1000U),
            ((record.flags & TAG_LOG_FLAG_EPOCH_TIME) != 0U) ? 'E' : 'U',
            (unsigned int)record.antenna,
            (int)record.rssi,
            (unsigned int)record.readCount
        );

        for(b = 0U; (b < record.epcLength) && (b < TAG_LOG_EPC_MAX_BYTES); b++)
        {
            pos += snprintf(
                &line[pos],
                sizeof(line) - (size_t)pos,
                "%02X",
                record.epc[b]
            );
        }

        (void)snprintf(
            &line[pos],
            sizeof(line) - (size_t)pos,
            "%s\r\n",
            ((record.flags & TAG_LOG_FLAG_EPC_TRUNCATED) != 0U) ? ",TRUNC" : ""
        );

        HOST_SendString(line);
    }

    (void)snprintf(
        line,
        sizeof(line),
        "OK,TAGLOG_READ,NEXT=%lu\r\n",
        (unsigned long)seq
    );

    HOST_SendString(line);

    return 1U;
}


/* Longest command: SET_HTTP with a 127-character URL. */
#define HOST_RX_BUFFER_SIZE 320U


static char hostRxBuffer[HOST_RX_BUFFER_SIZE];
static uint16_t hostRxIndex = 0U;

static uint32_t hostRxOverflowReported = 0U;

void HOST_Interface_Init(void)
{
	hostRxIndex = 0U;
	hostRxOverflowReported = 0U;

	memset(
	    hostRxBuffer,
	    0,
	    sizeof(hostRxBuffer)
	);
}


/*
 * Persist RF settings changed with the text commands, so they survive a
 * reset. Region and power share the record the vendor tools write.
 */
extern SILION_ReaderConfig_t readerConfig;

static uint8_t HOST_PersistRegionPower(void)
{
    ReaderSettings_t settings = *ReaderSettings_Get();

    if(
        (settings.rfValid != 0U) &&
        (settings.region == readerConfig.region) &&
        (settings.readPower == readerConfig.readPower) &&
        (settings.writePower == readerConfig.writePower)
    )
    {
        return 1U;
    }

    settings.rfValid = 1U;
    settings.region = readerConfig.region;
    settings.readPower = readerConfig.readPower;
    settings.writePower = readerConfig.writePower;

    return ReaderSettings_Save(&settings);
}

static uint8_t HOST_PersistAntenna(uint8_t txAntenna, uint8_t rxAntenna)
{
    ReaderSettings_t settings = *ReaderSettings_Get();

    if(
        (settings.rfAntennaValid != 0U) &&
        (settings.txAntenna == txAntenna) &&
        (settings.rxAntenna == rxAntenna)
    )
    {
        return 1U;
    }

    settings.rfAntennaValid = 1U;
    settings.txAntenna = txAntenna;
    settings.rxAntenna = rxAntenna;

    return ReaderSettings_Save(&settings);
}

static uint8_t HOST_PersistGen2(uint8_t target, uint8_t q, uint8_t profile)
{
    ReaderSettings_t settings = *ReaderSettings_Get();

    if(
        (settings.gen2Valid != 0U) &&
        (settings.gen2Target == target) &&
        (settings.gen2Q == q) &&
        (settings.gen2Profile == profile)
    )
    {
        return 1U;
    }

    settings.gen2Valid = 1U;
    settings.gen2Target = target;
    settings.gen2Q = q;
    settings.gen2Profile = profile;

    return ReaderSettings_Save(&settings);
}

/*
 * SET_GEN2,<target>,<q>[,<profile>,<fast>,<hop>]
 *
 * target: A, B or A/B (AB); q: -1 = dynamic, 0..15 = static.
 * profile: 0 = module default (not sent), otherwise a Gen2 RF mode
 * (gen2tagEncoding, e.g. 113). fast: 1 = Reader Manager "Fast Mode",
 * which forces Session 0 and Target A/B. hop: only 0 is accepted (the
 * hopping setting behind it is not implemented).
 */
static uint8_t HOST_ParseGen2(
    const char *args,
    uint8_t *target,
    uint8_t *q,
    uint8_t *profile,
    uint8_t *fast)
{
    char targetText[8];
    int qValue;
    int extra[3] = {0, 0, 0};
    int fields;

    fields = sscanf(args, "%7[^,],%d,%d,%d,%d", targetText, &qValue, &extra[0], &extra[1], &extra[2]);

    if((fields != 2) && (fields != 5))
    {
        return 0U;
    }

    if(strcmp(targetText, "A") == 0)
    {
        *target = SILION_GEN2_TARGET_A;
    }
    else if(strcmp(targetText, "B") == 0)
    {
        *target = SILION_GEN2_TARGET_B;
    }
    else if((strcmp(targetText, "A/B") == 0) || (strcmp(targetText, "AB") == 0))
    {
        *target = SILION_GEN2_TARGET_AB;
    }
    else
    {
        return 0U;
    }

    if(qValue == -1)
    {
        *q = SILION_GEN2_Q_DYNAMIC;
    }
    else if((qValue >= 0) && (qValue <= 15))
    {
        *q = (uint8_t)qValue;
    }
    else
    {
        return 0U;
    }

    if((extra[0] != 0) &&
       ((extra[0] < 0) || (extra[0] > 255) ||
        (SILION_Application_IsGen2Profile((uint8_t)extra[0]) == 0U)))
    {
        return 0U;
    }

    if(((extra[1] != 0) && (extra[1] != 1)) || (extra[2] != 0))
    {
        return 0U;
    }

    *profile = (uint8_t)extra[0];
    *fast = (uint8_t)extra[1];

    if(*fast != 0U)
    {
        *target = SILION_GEN2_TARGET_AB;
    }

    return 1U;
}

/*
 * SET_ANTENNAS,<port>[,<port>...]
 *
 * The reader inventories on one antenna port, so the list must name a
 * single port (repeating it is allowed). Multi-port switching is not
 * implemented.
 */
static uint8_t HOST_ParseAntennaList(const char *args, uint8_t *port)
{
    char list[48];
    char *token;
    char *save = NULL;
    unsigned long value;
    char *end;
    uint8_t found = 0U;

    if(strlen(args) >= sizeof(list))
    {
        return 0U;
    }

    (void)strcpy(list, args);

    for(token = strtok_r(list, ",", &save); token != NULL; token = strtok_r(NULL, ",", &save))
    {
        value = strtoul(token, &end, 10);

        if((end == token) || (*end != '\0') || (value < 1UL) || (value > 255UL))
        {
            return 0U;
        }

        if((found != 0U) && (*port != (uint8_t)value))
        {
            return 0U;
        }

        *port = (uint8_t)value;
        found = 1U;
    }

    return found;
}

static uint8_t HOST_ParseHexByte(
    const char *text,
    uint8_t *value
)
{
    unsigned int temp;

    if(text == NULL || value == NULL)
        return 0U;

    if(sscanf(text, "%x", &temp) != 1)
        return 0U;

    if(temp > 0xFFU)
        return 0U;

    *value = (uint8_t)temp;

    return 1U;
}


static uint8_t HOST_ParseUint16(
    const char *text,
    uint16_t *value
)
{
    unsigned int temp;

    if(text == NULL || value == NULL)
        return 0U;

    if(sscanf(text, "%u", &temp) != 1)
        return 0U;

    if(temp > 65535U)
        return 0U;

    *value = (uint16_t)temp;

    return 1U;
}

static uint8_t HOST_ParseHexBytes(
    const char *text,
    uint8_t *data,
    uint8_t maxBytes,
    uint8_t *dataLength
)
{
    uint16_t textLength;
    uint8_t length;
    uint8_t i;
    unsigned int value;

    if(
        text == NULL ||
        data == NULL ||
        dataLength == NULL
    )
    {
        return 0U;
    }

    textLength = (uint16_t)strlen(text);

    if(
        textLength == 0U ||
        (textLength % 2U) != 0U
    )
    {
        return 0U;
    }

    length = (uint8_t)(textLength / 2U);

    if(length > maxBytes)
        return 0U;

    for(i = 0U; i < length; i++)
    {
        if(
            sscanf(
                &text[i * 2U],
                "%2x",
                &value
            ) != 1
        )
        {
            return 0U;
        }

        if(value > 0xFFU)
            return 0U;

        data[i] = (uint8_t)value;
    }

    *dataLength = length;

    return 1U;
}

void HOST_SendString(const char *text)
{
    HOST_Transport_SendString(
        HOST_Transport_GetActive(),
        text
    );
}


/* Legacy ASCII tag line, sent on the transport selected for tags. */
void HOST_SendTag(const SILION_Tag_t *tag)
{
    char buffer[256];
    uint16_t pos = 0U;
    uint16_t i;
    int written;

    if(tag == NULL)
    {
        return;
    }

    written = snprintf(
        &buffer[pos],
        sizeof(buffer) - pos,
        "TAG,EPC="
    );

    if(written < 0)
    {
        return;
    }

    if((size_t)written >= (sizeof(buffer) - pos))
    {
        return;
    }

    pos += (uint16_t)written;

    for(i = 0U; i < tag->epcLengthBytes; i++)
    {
        written = snprintf(
            &buffer[pos],
            sizeof(buffer) - pos,
            "%02X",
            tag->epc[i]
        );

        if(written < 0)
        {
            return;
        }

        if((size_t)written >= (sizeof(buffer) - pos))
        {
            return;
        }

        pos += (uint16_t)written;
    }

    written = snprintf(
        &buffer[pos],
        sizeof(buffer) - pos,
        ",RSSI=%d,ANT=%u,FREQ=%lu,TIME=%lu\r\n",
        tag->rssi,
        tag->antenna,
        (unsigned long)tag->frequencyKHz,
        (unsigned long)tag->timestampMs
    );

    if(written < 0)
    {
        return;
    }

    if((size_t)written >= (sizeof(buffer) - pos))
    {
        return;
    }

    pos += (uint16_t)written;

    HOST_Transport_SendString(
        HOST_Transport_GetTagTransport(),
        buffer
    );
}


uint8_t HOST_IsTagTransportEthernet(void)
{
    return (
        HOST_Transport_GetTagTransport()
        == HOST_TRANSPORT_ETHERNET
    );
}


/* Stop async inventory, retrying a STOP whose reply was lost in a fast
 * tag stream. Returns 1 when the reader is idle afterwards (also when it
 * was idle already). */
static uint8_t HOST_EnsureInventoryStopped(void)
{
    uint8_t attempt;

    for(
        attempt = 0U;
        (attempt < 3U) && (SILION_Application_IsInventoryRunning() != 0U);
        attempt++
    )
    {
        (void)SILION_Application_StopInventory();
    }

    return (SILION_Application_IsInventoryRunning() == 0U) ? 1U : 0U;
}

/* Bytes per FW_DATA line: 256 hex characters fit HOST_RX_BUFFER_SIZE. */
#define HOST_FW_CHUNK_MAX 128U

/* Firmware update over any host link (docs/design/0020):
 *
 *   FW_BEGIN,<bytes>        -> OK,FW_BEGIN,CHUNK=128
 *   FW_DATA,<offset>,<hex>  -> OK,FW_DATA,<bytes received>
 *   FW_END                  -> OK,FW_END,<version>   (SHA-256 checked)
 *   FW_APPLY                -> OK,FW_APPLY, then a reset into the
 *                              bootloader, which checks the signature
 *   FW_ABORT                -> OK,FW_ABORT
 *   FW_STATUS               -> FW,STATE=..,RECEIVED=..,BOOT=..,LAST=..,VERSION=..
 *
 * Errors: ERROR,<command>,<reason>; FW_DATA adds the byte count the
 * reader expects next. Returns 0 for a command that is not one of these. */
static uint8_t HOST_FirmwareUpdateCommand(const char *command)
{
    static uint8_t data[HOST_FW_CHUNK_MAX];
    char line[112];
    char version[32];
    FirmwareUpdate_Result_t result;
    uint8_t length;
    char *end;
    uint32_t value;

    if(strncmp(command, "FW_DATA,", 8U) == 0)
    {
        value = (uint32_t)strtoul(&command[8], &end, 10);

        if(
            (end == &command[8]) || (*end != ',') ||
            (HOST_ParseHexBytes(end + 1, data, HOST_FW_CHUNK_MAX, &length) == 0U)
        )
        {
            (void)snprintf(line, sizeof(line), "ERROR,FW_DATA,FORMAT,%lu\r\n",
                           (unsigned long)FirmwareUpdate_GetReceived());
        }
        else if((result = FirmwareUpdate_Write(value, data, length)) != FW_UPDATE_OK)
        {
            (void)snprintf(line, sizeof(line), "ERROR,FW_DATA,%s,%lu\r\n",
                           FirmwareUpdate_ResultName(result),
                           (unsigned long)FirmwareUpdate_GetReceived());
        }
        else
        {
            (void)snprintf(line, sizeof(line), "OK,FW_DATA,%lu\r\n",
                           (unsigned long)FirmwareUpdate_GetReceived());
        }
    }
    else if(strncmp(command, "FW_BEGIN,", 9U) == 0)
    {
        value = (uint32_t)strtoul(&command[9], &end, 10);

        if((end == &command[9]) || (*end != '\0'))
            (void)snprintf(line, sizeof(line), "ERROR,FW_BEGIN,FORMAT\r\n");
        else if(HOST_EnsureInventoryStopped() == 0U)
            (void)snprintf(line, sizeof(line), "ERROR,FW_BEGIN,INVENTORY\r\n");
        else if((result = FirmwareUpdate_Begin(value)) != FW_UPDATE_OK)
            (void)snprintf(line, sizeof(line), "ERROR,FW_BEGIN,%s\r\n",
                           FirmwareUpdate_ResultName(result));
        else
            (void)snprintf(line, sizeof(line), "OK,FW_BEGIN,CHUNK=%u\r\n",
                           (unsigned int)HOST_FW_CHUNK_MAX);
    }
    else if(strcmp(command, "FW_END") == 0)
    {
        if((result = FirmwareUpdate_Finish()) != FW_UPDATE_OK)
            (void)snprintf(line, sizeof(line), "ERROR,FW_END,%s\r\n",
                           FirmwareUpdate_ResultName(result));
        else
            (void)snprintf(line, sizeof(line), "OK,FW_END,%s\r\n",
                           FirmwareUpdate_GetVersion());
    }
    else if(strcmp(command, "FW_APPLY") == 0)
    {
        if((result = FirmwareUpdate_Apply()) != FW_UPDATE_OK)
        {
            (void)snprintf(line, sizeof(line), "ERROR,FW_APPLY,%s\r\n",
                           FirmwareUpdate_ResultName(result));
        }
        else
        {
            (void)snprintf(line, sizeof(line), "OK,FW_APPLY\r\n");

            /* Delayed so the reply still reaches the host. */
            ReaderSettings_RequestReboot(500U);
        }
    }
    else if(strcmp(command, "FW_ABORT") == 0)
    {
        FirmwareUpdate_Abort();
        (void)snprintf(line, sizeof(line), "OK,FW_ABORT\r\n");
    }
    else if(strcmp(command, "FW_STATUS") == 0)
    {
        static const char *const stateNames[] = { "IDLE", "RECEIVING", "READY" };
        const char *last = FirmwareUpdate_GetLastOutcome(version, sizeof(version));

        (void)snprintf(
            line,
            sizeof(line),
            "FW,STATE=%s,RECEIVED=%lu,BOOT=%s,LAST=%s,VERSION=%s\r\n",
            stateNames[FirmwareUpdate_GetState()],
            (unsigned long)FirmwareUpdate_GetReceived(),
            (FirmwareUpdate_BootloaderPresent() != 0U) ? "1" : "NONE",
            last,
            version
        );
    }
    else
    {
        return 0U;
    }

    HOST_SendString(line);

    return 1U;
}


void HOST_Interface_Task(void)
	{
	    USB_CDC_Transport_Task();
	    Host_Tcp_Server_Task();
	    uint8_t byte;
	    uint8_t transport;

	    /*
	     * --------------------------------------------------------
	     * Report host RX queue overflow.
	     *
	     * Overflow is counted by the transport service.
	     * Fault reporting is performed here in task context,
	     * never from the RX interrupt.
	     * --------------------------------------------------------
	     */
	    if(
	        HOST_Transport_GetRxOverflowCount()
	        !=
	        hostRxOverflowReported
	    )
	    {
	        System_Fault_Set(
	            ERROR_HOST_RX_OVERFLOW
	        );

	        hostRxOverflowReported =
	            HOST_Transport_GetRxOverflowCount();
	    }

	    while(
	        HOST_Transport_ReadByte(
	            &transport,
	            &byte
	        )
	    )


	    {
	        /*
	         * ----------------------------------------------------
	         * Transport selected by the manager.
	         * ----------------------------------------------------
	         */

        /*
         * ----------------------------------------------------
         * LF handling.
         *
         * LF is ignored completely.
         *
         * Therefore:
         *
         *     COMMAND\r
         *     COMMAND\r\n
         *
         * both result in exactly one command.
         * ----------------------------------------------------
         */
        if(byte == '\n')
        {
            continue;
        }


        /*
         * ----------------------------------------------------
         * CR = command terminator.
         * ----------------------------------------------------
         */
        if(byte == '\r')
        {
            /*
             * Ignore empty commands.
             *
             * This prevents extra CR characters from
             * generating ERROR,INVALID_COMMAND.
             */
            if(hostRxIndex == 0U)
            {
                continue;
            }

            /*
             * Null terminate command.
             */
            hostRxBuffer[hostRxIndex] = '\0';

            /* =================================================
             * RELEASE TRANSPORT
             * =================================================
             */
            if(strcmp(hostRxBuffer, "RELEASE") == 0)
            {
                /*
                 * Stop asynchronous inventory before releasing
                 * the current host transport.
                 */
                (void)SILION_Application_StopInventory();

                /*
                 * Send the acknowledgement using the transport
                 * that issued RELEASE.
                 */
                HOST_SendString(
                    "OK,RELEASE\r\n"
                );

                /*
                 * Discard any remaining host input already queued.
                 * This prevents bytes from the old transport from
                 * becoming part of the next command.
                 */
                /*
                  * Reset the command parser.
                  */
                 hostRxIndex = 0U;
                 hostRxBuffer[0] = '\0';

                 /*
                  * Release the host transport.
                  *
                  * The transport manager flushes stale RX
                  * bytes and prevents the released transport
                  * from immediately reclaiming ownership.
                  */
                 HOST_Transport_Release();
            }
            else


            /*
             * =================================================
             * START
             * =================================================
             */
            if(strcmp(hostRxBuffer, "START") == 0)
            {
            	HOST_Transport_SetTagTransport(
            	    transport
            	);
                if(SILION_Application_StartInventory() == 1U)
                {
                    HOST_SendString(
                        "OK,START\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,START\r\n"
                    );
                }
            }

            /*
             * =================================================
             * SINGLE POLL
             *
             * Format:
             *     SINGLE_POLL
             *     SINGLE_POLL,1000
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "SINGLE_POLL",
                    11U
                ) == 0
            )
            {
                uint16_t timeoutMs = 1000U;

                /*
                 * Optional timeout:
                 *
                 * SINGLE_POLL,1000
                 */
                if(hostRxBuffer[11] == ',')
                {
                    int value =
                        atoi(&hostRxBuffer[12]);

                    if(
                        value >= 1 &&
                        value <= 65535
                    )
                    {
                        timeoutMs =
                            (uint16_t)value;
                    }
                    else
                    {
                        HOST_SendString(
                            "ERROR,SINGLE_POLL\r\n"
                        );

                        goto host_command_done;
                    }
                }
                HOST_Transport_SetTagTransport(
                    transport
                );


                if(
                    SILION_Application_SingleInventory(
                        timeoutMs
                    ) == 1U
                )
                {
                    /*
                     * Tag/no-tag response has already
                     * been emitted by the application.
                     *
                     * Only return the command ACK here.
                     */
                    HOST_SendString(
                        "OK,SINGLE_POLL\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,SINGLE_POLL\r\n"
                    );
                }
            }


            /*
             * =================================================
             * STOP
             * =================================================
             */
            else if(strcmp(hostRxBuffer, "STOP") == 0)
            {
                /* Idempotent: an idle reader answers OK, so a host that
                 * lost track of the state (e.g. active mode stopped by
                 * SET_WORK_MODE) can always recover with STOP. */
            	if(
            	    HOST_EnsureInventoryStopped()
            	    == 1U
            	)
            	{
            	    HOST_SendString(
            	        "OK,STOP\r\n"
            	    );
            	}
            	else
            	{
            	    HOST_SendString(
            	        "ERROR,STOP\r\n"
            	    );
            	}
            }

            /*
             * =================================================
             * SYNC POLL
             *
             * Examples:
             *
             *     SYNC_POLL
             *     SYNC_POLL,1000
             *
             * Uses native:
             *
             *     0x22 Synchronous Inventory
             *     0x29 Get Tag Buffer
             *
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "SYNC_POLL",
                    9U
                ) == 0
            )
            {
                uint16_t timeoutMs = 1000U;

                /*
                 * Optional timeout:
                 *
                 * SYNC_POLL,1000
                 */
                if(hostRxBuffer[9] == ',')
                {
                    unsigned int timeout;

                    if(
                        sscanf(
                            &hostRxBuffer[10],
                            "%u",
                            &timeout
                        ) != 1
                        ||
                        timeout == 0U
                        ||
                        timeout > 65535U
                    )
                    {
                        HOST_SendString(
                            "ERROR,SYNC_POLL\r\n"
                        );

                        goto host_command_done;
                    }

                    timeoutMs =
                        (uint16_t)timeout;
                }
                else if(hostRxBuffer[9] != '\0')
                {
                    HOST_SendString(
                        "ERROR,SYNC_POLL\r\n"
                    );

                    goto host_command_done;
                }

                HOST_Transport_SetTagTransport(
                    transport
                );

                if(
                    SILION_Application_SynchronousInventory(
                        timeoutMs
                    ) == 1U
                )
                {
                    HOST_SendString(
                        "OK,SYNC_POLL\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,SYNC_POLL\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET STATUS
             * =================================================
             */
            else if(strcmp(hostRxBuffer, "GET_STATUS") == 0)
            {
                if(
                    SILION_Application_GetState()
                    ==
                    SILION_APP_INVENTORY
                )
                {
                    HOST_SendString(
                        "STATUS,INVENTORY\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "STATUS,IDLE\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET DIAG / CLEAR DIAG
             * =================================================
             */
            else if(strcmp(hostRxBuffer, "GET_DIAG") == 0)
            {
                static char diagLine[DIAGNOSTICS_LINE_MAX];

                (void)Diagnostics_Format(
                    diagLine,
                    sizeof(diagLine)
                );

                HOST_SendString(diagLine);
            }

            else if(strcmp(hostRxBuffer, "CLEAR_DIAG") == 0)
            {
                Diagnostics_Clear();

                HOST_SendString(
                    "OK,CLEAR_DIAG\r\n"
                );
            }


            /*
             * =================================================
             * REBOOT
             * =================================================
             * The reset is delayed so OK,REBOOT still reaches the
             * host, including over the polled ESP32 WiFi link.
             */
            else if(strcmp(hostRxBuffer, "REBOOT") == 0)
            {
                HOST_SendString(
                    "OK,REBOOT\r\n"
                );

                ReaderSettings_RequestReboot(500U);
            }


            /*
             * =================================================
             * TAG BACKUP LOG (external flash, F411 board)
             * =================================================
             */
            else if(strcmp(hostRxBuffer, "TAGLOG_STATUS") == 0)
            {
                HOST_SendTagLogStatus();
            }

            else if(strncmp(hostRxBuffer, "TAGLOG_READ,", 12U) == 0)
            {
                if(HOST_SendTagLogRecords(&hostRxBuffer[12]) == 0U)
                {
                    HOST_SendString(
                        "ERROR,TAGLOG_READ\r\n"
                    );
                }
            }

            else if(strcmp(hostRxBuffer, "TAGLOG_CLEAR") == 0)
            {
                if(TagLog_Clear() != 0U)
                {
                    HOST_SendString(
                        "OK,TAGLOG_CLEAR\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,TAGLOG_CLEAR\r\n"
                    );
                }
            }

            /*
             * =================================================
             * FIRMWARE UPDATE (FW_BEGIN, FW_DATA, FW_END, ...)
             * =================================================
             */
            else if(
                (strncmp(hostRxBuffer, "FW_", 3U) == 0) &&
                (HOST_FirmwareUpdateCommand(hostRxBuffer) != 0U)
            )
            {
            }

#ifdef DEBUG
            else if(strncmp(hostRxBuffer, "DEBUG_FAULT,", 12U) == 0)
            {
                if(Diagnostics_TriggerFault(&hostRxBuffer[12]) == 0U)
                {
                    HOST_SendString(
                        "ERROR,DEBUG_FAULT\r\n"
                    );
                }
            }
#endif


            /*
             * =================================================
             * GET VERSION
             * =================================================
             */
            else if(strcmp(hostRxBuffer, "GET_VERSION") == 0)
            {
                if(
                    SILION_Application_GetVersion()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_VERSION\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET SERIAL
             * =================================================
             */
            else if(strcmp(hostRxBuffer, "GET_SERIAL") == 0)
            {
                if(
                    SILION_Application_GetSerial()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_SERIAL\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET TEMPERATURE
             * =================================================
             */
            else if(
                strcmp(
                    hostRxBuffer,
                    "GET_TEMPERATURE"
                ) == 0
            )
            {
                if(
                    SILION_Application_GetTemperature()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_TEMPERATURE\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET REGION
             * =================================================
             */
            else if(
                strcmp(
                    hostRxBuffer,
                    "GET_REGION"
                ) == 0
            )
            {
                if(
                    SILION_Application_GetRegion()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_REGION\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET ANTENNA
             * =================================================
             */
            else if(
                strcmp(
                    hostRxBuffer,
                    "GET_ANTENNA"
                ) == 0
            )
            {
                if(
                    SILION_Application_GetAntenna()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_ANTENNA\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET POWER
             * =================================================
             */
            else if(
                strcmp(
                    hostRxBuffer,
                    "GET_POWER"
                ) == 0
            )
            {
                if(
                    SILION_Application_GetPower()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_POWER\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET PROTOCOL
             * =================================================
             */
            else if(
                strcmp(
                    hostRxBuffer,
                    "GET_PROTOCOL"
                ) == 0
            )
            {
                if(
                    SILION_Application_GetProtocol()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_PROTOCOL\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET SESSION
             * =================================================
             */
            else if(
                strcmp(
                    hostRxBuffer,
                    "GET_SESSION"
                ) == 0
            )
            {
                if(
                    SILION_Application_GetSession()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_SESSION\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET FREQUENCY
             * =================================================
             */
            else if(
                strcmp(
                    hostRxBuffer,
                    "GET_FREQUENCY"
                ) == 0
            )
            {
                if(
                    SILION_Application_GetFrequency()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_FREQUENCY\r\n"
                    );
                }
            }


            /*
             * =================================================
             * GET REGIONS
             * =================================================
             */
            else if(
                strcmp(
                    hostRxBuffer,
                    "GET_REGIONS"
                ) == 0
            )
            {
                if(
                    SILION_Application_GetRegions()
                    ==
                    0U
                )
                {
                    HOST_SendString(
                        "ERROR,GET_REGIONS\r\n"
                    );
                }
            }


            /*
             * =================================================
             * SET POWER
             *
             * Example:
             *
             *     SET_POWER,3000
             *
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "SET_POWER,",
                    10U
                ) == 0
            )
            {
                uint16_t power;

                if(
                    HOST_ParseUint16(
                        &hostRxBuffer[10U],
                        &power
                    ) == 0U
                )
                {
                    HOST_SendString(
                        "ERROR,SET_POWER\r\n"
                    );
                }
                else if(
                    (SILION_Application_SetPower(
                        power,
                        power
                    ) == 1U) &&
                    (HOST_PersistRegionPower() == 1U)
                )
                {
                    HOST_SendString(
                        "OK,SET_POWER\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,SET_POWER\r\n"
                    );
                }
            }


            /*
             * =================================================
             * SET ANTENNA
             *
             * Example:
             *
             *     SET_ANTENNA,1,1
             *
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "SET_ANTENNA,",
                    12U
                ) == 0
            )
            {
                unsigned int tx;
                unsigned int rx;

                if(
                    sscanf(
                        &hostRxBuffer[12U],
                        "%u,%u",
                        &tx,
                        &rx
                    ) == 2
                    &&
                    tx <= 255U
                    &&
                    rx <= 255U
                )
                {
                    if(
                        (SILION_Application_SetAntenna(
                            (uint8_t)tx,
                            (uint8_t)rx
                        ) == 1U) &&
                        (HOST_PersistAntenna((uint8_t)tx, (uint8_t)rx) == 1U)
                    )
                    {
                        HOST_SendString(
                            "OK,SET_ANTENNA\r\n"
                        );
                    }
                    else
                    {
                        HOST_SendString(
                            "ERROR,SET_ANTENNA\r\n"
                        );
                    }
                }
                else
                {
                    HOST_SendString(
                        "ERROR,SET_ANTENNA\r\n"
                    );
                }
            }


            /*
             * =================================================
             * SET REGION
             *
             * Example:
             *
             *     SET_REGION,FF
             *
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "SET_REGION,",
                    11U
                ) == 0
            )
            {
                uint8_t region;

                if(
                    HOST_ParseHexByte(
                        &hostRxBuffer[11U],
                        &region
                    ) == 0U
                )
                {
                    HOST_SendString(
                        "ERROR,SET_REGION\r\n"
                    );
                }
                else if(
                    (SILION_Application_SetRegion(
                        region
                    ) == 1U) &&
                    (HOST_PersistRegionPower() == 1U)
                )
                {
                    HOST_SendString(
                        "OK,SET_REGION\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,SET_REGION\r\n"
                    );
                }
            }


            /*
             * =================================================
             * SET SESSION
             *
             * Example:
             *
             *     SET_SESSION,0
             *
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "SET_SESSION,",
                    12U
                ) == 0
            )
            {
                unsigned int session;

                if(
                    sscanf(
                        &hostRxBuffer[12U],
                        "%u",
                        &session
                    ) == 1
                    &&
                    session <= 3U
                )
                {
                    if(
                        (SILION_Application_SetSession(
                            (uint8_t)session
                        ) == 1U) &&
                        (Configuration_Service_SaveSession((uint8_t)session) == 1U)
                    )
                    {
                        HOST_SendString(
                            "OK,SET_SESSION\r\n"
                        );
                    }
                    else
                    {
                        HOST_SendString(
                            "ERROR,SET_SESSION\r\n"
                        );
                    }
                }
                else
                {
                    HOST_SendString(
                        "ERROR,SET_SESSION\r\n"
                    );
                }
            }

            /*
             * =================================================
             * READ TAG BY EPC
             *
             * Format:
             *
             *     READ_TAG,EPC,BANK,ADDRESS,WORDS
             *
             * Example:
             *
             *     READ_TAG,E2000017221101441890A123,2,0,2
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "READ_TAG,",
                    9U
                ) == 0
            )
            {
                char epcString[127];
                uint8_t epcData[62];
                uint8_t epcLengthBytes;

                unsigned int bank;
                unsigned long address;
                unsigned int words;

                /*
                 * Parse the EPC string plus numeric parameters.
                 */
                if(
                    sscanf(
                        &hostRxBuffer[9],
                        "%126[^,],%u,%lu,%u",
                        epcString,
                        &bank,
                        &address,
                        &words
                    ) == 4
                    &&
                    bank <= 255U
                    &&
                    address <= 0xFFFFFFFFUL
                    &&
                    words >= 1U
                    &&
                    words <= 96U
                )
                {
                    size_t epcStringLength =
                        strlen(epcString);

                    /*
                     * EPC must contain an even number
                     * of hexadecimal characters.
                     */
                    if(
                        epcStringLength == 0U ||
                        epcStringLength > 124U ||
                        (epcStringLength % 2U) != 0U
                    )
                    {
                        HOST_SendString(
                            "ERROR,READ_TAG\r\n"
                        );
                    }
                    else
                    {
                        uint8_t parseOk = 1U;

                        epcLengthBytes =
                            (uint8_t)(epcStringLength / 2U);

                        for(
                            uint8_t i = 0U;
                            i < epcLengthBytes;
                            i++
                        )
                        {
                            uint8_t high =
                                HOST_HexNibble(
                                    epcString[i * 2U]
                                );

                            uint8_t low =
                                HOST_HexNibble(
                                    epcString[(i * 2U) + 1U]
                                );

                            if(
                                high == 0xFFU ||
                                low == 0xFFU
                            )
                            {
                                parseOk = 0U;
                                break;
                            }

                            epcData[i] =
                                (uint8_t)(
                                    (high << 4) |
                                    low
                                );
                        }

                        if(parseOk == 0U)
                        {
                            HOST_SendString(
                                "ERROR,READ_TAG\r\n"
                            );
                        }
                        else if(
                            SILION_Application_ReadTagData(
                                1000U,
                                (uint8_t)bank,
                                (uint32_t)address,
                                (uint8_t)words,
                                epcData,
                                epcLengthBytes
                            ) == 1U
                        )
                        {
                            HOST_SendString(
                                "OK,READ_TAG\r\n"
                            );
                        }
                        else
                        {
                            HOST_SendString(
                                "ERROR,READ_TAG\r\n"
                            );
                        }
                    }
                }
                else
                {
                    HOST_SendString(
                        "ERROR,READ_TAG\r\n"
                    );
                }
            }

            /*
             * =================================================
             * WRITE EPC
             *
             * Format:
             *
             *     WRITE_EPC,OLD_EPC,NEW_EPC
             *
             * Example:
             *
             *     WRITE_EPC,E2806894000050328410E5CD,E2000017221101441890A123
             *
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "WRITE_EPC,",
                    10U
                ) == 0
            )
            {
                char oldEpcText[63];
                char newEpcText[63];

                uint8_t oldEpc[31];
                uint8_t newEpc[62];

                uint8_t oldEpcLengthBytes;
                uint8_t newEpcLengthBytes;

                int parsed;

                oldEpcText[0] = '\0';
                newEpcText[0] = '\0';

                parsed =
                    sscanf(
                        hostRxBuffer,
                        "WRITE_EPC,%62[^,],%62s",
                        oldEpcText,
                        newEpcText
                    );

                if(parsed != 2)
                {
                    HOST_SendString(
                        "ERROR,WRITE_EPC\r\n"
                    );
                }
                else if(
                    HOST_ParseHexBytes(
                        oldEpcText,
                        oldEpc,
                        sizeof(oldEpc),
                        &oldEpcLengthBytes
                    ) == 0U
                )
                {
                    HOST_SendString(
                        "ERROR,WRITE_EPC\r\n"
                    );
                }
                else if(
                    HOST_ParseHexBytes(
                        newEpcText,
                        newEpc,
                        sizeof(newEpc),
                        &newEpcLengthBytes
                    ) == 0U
                )
                {
                    HOST_SendString(
                        "ERROR,WRITE_EPC\r\n"
                    );
                }
                else if(
                    SILION_Application_WriteTagEPC(
                        oldEpc,
                        oldEpcLengthBytes,
                        newEpc,
                        newEpcLengthBytes
                    ) == 1U
                )
                {
                    HOST_SendString(
                        "OK,WRITE_EPC\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,WRITE_EPC\r\n"
                    );
                }
            }

            /*
             * =================================================
             * LOCK TAG
             *
             * Format:
             *
             * LOCK_TAG,EPC,ACCESS_PASSWORD,MASK_BITS,ACTION_BITS
             *
             * Example:
             *
             * LOCK_TAG,E2806894000050328410E5CD,00000000,0001,0001
             *
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "LOCK_TAG,",
                    9U
                ) == 0
            )
            {
                char epcText[63];

                uint8_t epc[62];
                uint8_t epcLengthBytes;

                unsigned long accessPassword;
                unsigned int maskBits;
                unsigned int actionBits;

                int parsed;

                epcText[0] = '\0';

                parsed =
                    sscanf(
                        hostRxBuffer,
                        "LOCK_TAG,%62[^,],%lx,%x,%x",
                        epcText,
                        &accessPassword,
                        &maskBits,
                        &actionBits
                    );

                if(
                    parsed != 4 ||
                    accessPassword > 0xFFFFFFFFUL ||
                    maskBits > 0x03FFU ||
                    actionBits > 0x03FFU
                )
                {
                    HOST_SendString(
                        "ERROR,LOCK_TAG\r\n"
                    );
                }
                else if(
                    HOST_ParseHexBytes(
                        epcText,
                        epc,
                        sizeof(epc),
                        &epcLengthBytes
                    ) == 0U
                )
                {
                    HOST_SendString(
                        "ERROR,LOCK_TAG\r\n"
                    );
                }
                else if(
                    SILION_Application_LockTag(
                        epc,
                        epcLengthBytes,
                        (uint32_t)accessPassword,
                        (uint16_t)maskBits,
                        (uint16_t)actionBits
                    ) == 1U
                )
                {
                    HOST_SendString(
                        "OK,LOCK_TAG\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,LOCK_TAG\r\n"
                    );
                }
            }

            /*
             * =================================================
             * KILL TAG
             *
             * Format:
             *
             * KILL_TAG,EPC,KILL_PASSWORD
             *
             * Example:
             *
             * KILL_TAG,E2806894000050328410E5CD,00000000
             *
             * =================================================
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "KILL_TAG,",
                    9U
                ) == 0
            )
            {
                char epcText[63];

                uint8_t epc[62];
                uint8_t epcLengthBytes;

                unsigned long killPassword;

                int parsed;

                epcText[0] = '\0';

                parsed =
                    sscanf(
                        hostRxBuffer,
                        "KILL_TAG,%62[^,],%lx",
                        epcText,
                        &killPassword
                    );

                if(
                    parsed != 2 ||
                    killPassword > 0xFFFFFFFFUL
                )
                {
                    HOST_SendString(
                        "ERROR,KILL_TAG\r\n"
                    );
                }
                else if(
                    HOST_ParseHexBytes(
                        epcText,
                        epc,
                        sizeof(epc),
                        &epcLengthBytes
                    ) == 0U
                )
                {
                    HOST_SendString(
                        "ERROR,KILL_TAG\r\n"
                    );
                }
                else if(
                    SILION_Application_KillTag(
                        epc,
                        epcLengthBytes,
                        (uint32_t)killPassword
                    ) == 1U
                )
                {
                    HOST_SendString(
                        "OK,KILL_TAG\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,KILL_TAG\r\n"
                    );
                }
            }



            /*
             * Format:
             *
             * WRITE_TAG,EPC,BANK,ADDRESS,ACCESS_PASSWORD,HEX_DATA
             *
             * Example: now write
             *
             * WRITE_TAG,E2806894000050328410E5CD,3,2,00000000,11223344
             */
            else if(
                strncmp(
                    hostRxBuffer,
                    "WRITE_TAG,",
                    10U
                ) == 0
            )
            {
            	char epcText[63];
            	char dataText[65];

            	uint8_t epc[62];
            	uint8_t writeData[64];

            	uint8_t epcLengthBytes;
            	uint8_t writeDataLength;

            	unsigned int bankTemp;
            	unsigned long addressTemp;
            	unsigned long accessPasswordTemp;

            	int parsed;

                epcText[0] = '\0';
                dataText[0] = '\0';

                parsed =
                    sscanf(
                        hostRxBuffer,
						"WRITE_TAG,%62[^,],%u,%lu,%8lx,%64s",
                        epcText,
                        &bankTemp,
                        &addressTemp,
						&accessPasswordTemp,
                        dataText
                    );

                if(
                    parsed != 5 ||
                    bankTemp > 3U ||
                    addressTemp > 0xFFFFFFFFUL ||
                    accessPasswordTemp > 0xFFFFFFFFUL
                )
                {
                    HOST_SendString(
                        "ERROR,WRITE_TAG\r\n"
                    );
                }
                else if(
                    HOST_ParseHexBytes(
                        epcText,
                        epc,
                        sizeof(epc),
                        &epcLengthBytes
                    ) == 0U
                )
                {
                    HOST_SendString(
                        "ERROR,WRITE_TAG\r\n"
                    );
                }
                else if(
                    HOST_ParseHexBytes(
                        dataText,
                        writeData,
                        sizeof(writeData),
                        &writeDataLength
                    ) == 0U
                )
                {
                    HOST_SendString(
                        "ERROR,WRITE_TAG\r\n"
                    );
                }
                else if(
				SILION_Application_WriteTagData(
					epc,
					epcLengthBytes,
					(uint8_t)bankTemp,
					(uint32_t)addressTemp,
					(uint32_t)accessPasswordTemp,
					writeData,
					writeDataLength) == 1U
                )
                {
                    HOST_SendString(
                        "OK,WRITE_TAG\r\n"
                    );
                }
                else
                {
                    HOST_SendString(
                        "ERROR,WRITE_TAG\r\n"
                    );
                }
            }

            /*
             * =================================================
             * BOARD COMMANDS (desktop GUI)
             * =================================================
             */
            else if(strcmp(hostRxBuffer, "GET_CAPABILITIES") == 0)
            {
                char response[144];

                (void)snprintf(
                    response,
                    sizeof(response),
                    "CAPABILITIES,GPI=%u,GPO=%u,ANTENNAS=1,HTTP=1,VSWR=1,SET_GPO=1,SET_HTTP=1,SET_GEN2=1,BUZZER=%u\r\n",
                    (unsigned int)INDUSTRIAL_IO_COUNT,
                    (unsigned int)Industrial_IO_GetOutputCount(),
                    (unsigned int)Industrial_IO_HasBuzzer()
                );
                HOST_SendString(response);
            }
            else if(strcmp(hostRxBuffer, "GET_GPI") == 0)
            {
                uint8_t states[INDUSTRIAL_IO_COUNT];
                char response[64];

                GPIO_Service_GetAllInputs(states);
                (void)snprintf(
                    response,
                    sizeof(response),
                    "GPI,GPI1=%u,GPI2=%u,GPI3=%u,GPI4=%u\r\n",
                    states[0], states[1], states[2], states[3]
                );
                HOST_SendString(response);
            }
            else if(strcmp(hostRxBuffer, "GET_NETWORK") == 0)
            {
                const ReaderSettings_t *settings = ReaderSettings_Get();
                char response[160];

                (void)snprintf(
                    response,
                    sizeof(response),
                    "NETWORK,IP=%u.%u.%u.%u,MASK=%u.%u.%u.%u,GATEWAY=%u.%u.%u.%u,"
                    "DNS=%u.%u.%u.%u,PORT=%u,MAC=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                    settings->ip[0], settings->ip[1], settings->ip[2], settings->ip[3],
                    settings->netmask[0], settings->netmask[1], settings->netmask[2], settings->netmask[3],
                    settings->gateway[0], settings->gateway[1], settings->gateway[2], settings->gateway[3],
                    settings->dns[0], settings->dns[1], settings->dns[2], settings->dns[3],
                    (unsigned int)CONFIGURATION_PORT_TCP_LISTEN_PORT,
                    settings->mac[0], settings->mac[1], settings->mac[2],
                    settings->mac[3], settings->mac[4], settings->mac[5]
                );
                HOST_SendString(response);
            }
            /* SET_GPO,number,state,durationSeconds */
            else if(strncmp(hostRxBuffer, "SET_GPO,", 8U) == 0)
            {
                unsigned int number;
                unsigned int state;
                unsigned long duration;
                uint8_t outputNumber;
                uint8_t outputState;
                uint32_t outputDuration;
                uint32_t errorCode = 0U;
                const char *errorString = "ok";

                if(
                    (sscanf(&hostRxBuffer[8U], "%u,%u,%lu", &number, &state, &duration) == 3) &&
                    (number >= 1U) && (number <= INDUSTRIAL_IO_COUNT) &&
                    (state <= 1U)
                )
                {
                    outputNumber = (uint8_t)number;
                    outputState = (uint8_t)state;
                    outputDuration = (uint32_t)duration;

                    if(
                        GPIO_Service_SetOutputs(
                            &outputNumber,
                            &outputState,
                            &outputDuration,
                            1U,
                            &errorCode,
                            &errorString
                        ) == GPIO_SERVICE_OK
                    )
                    {
                        HOST_SendString("OK,SET_GPO\r\n");
                    }
                    else
                    {
                        HOST_SendString("ERROR,SET_GPO\r\n");
                    }
                }
                else
                {
                    HOST_SendString("ERROR,SET_GPO\r\n");
                }
            }
            /* Work state: PASSIVE = answer host commands only, ACTIVE = the
             * configured autonomous inventory and upload. */
            else if(strcmp(hostRxBuffer, "GET_WORK_MODE") == 0)
            {
                HOST_SendString(
                    (ReaderSettings_GetWorkState() == READER_SETTINGS_WORK_ACTIVE)
                        ? "WORK_MODE,ACTIVE\r\n"
                        : "WORK_MODE,PASSIVE\r\n"
                );
            }
            else if(strcmp(hostRxBuffer, "GET_WIFI") == 0)
            {
                HOST_SendWifiStatus();
            }
            else if(strncmp(hostRxBuffer, "SET_WIFI,", 9U) == 0)
            {
                HOST_SendString(
                    (HOST_SetWifi(&hostRxBuffer[9U]) != 0U)
                        ? "OK,SET_WIFI\r\n"
                        : "ERROR,SET_WIFI\r\n"
                );
            }
            else if(strncmp(hostRxBuffer, "SET_UPLOAD_IF,", 14U) == 0)
            {
                HOST_SendString(
                    (HOST_SetUploadInterface(&hostRxBuffer[14U]) != 0U)
                        ? "OK,SET_UPLOAD_IF\r\n"
                        : "ERROR,SET_UPLOAD_IF\r\n"
                );
            }
            /* SET_WORK_MODE,PASSIVE|ACTIVE -- persisted; changes nothing else.
             * PASSIVE answers OK only once inventory has really stopped. */
            else if(strncmp(hostRxBuffer, "SET_WORK_MODE,", 14U) == 0)
            {
                const char *mode = &hostRxBuffer[14U];
                uint8_t workState = 0U;

                if(strcmp(mode, "PASSIVE") == 0)
                {
                    workState = READER_SETTINGS_WORK_PASSIVE;
                }
                else if(strcmp(mode, "ACTIVE") == 0)
                {
                    workState = READER_SETTINGS_WORK_ACTIVE;
                }

                if(
                    (workState != 0U) &&
                    (VendorConfig_SetWorkState(workState) != 0U) &&
                    ((workState != READER_SETTINGS_WORK_PASSIVE) ||
                     (HOST_EnsureInventoryStopped() != 0U))
                )
                {
                    HOST_SendString("OK,SET_WORK_MODE\r\n");
                }
                else
                {
                    HOST_SendString("ERROR,SET_WORK_MODE\r\n");
                }
            }
            /* SET_HTTP,url,timeoutSeconds,startAtBoot -- HTTP upload settings
             * only, stored for the next active session; the work state is
             * left alone (use SET_WORK_MODE).
             * The older SET_HTTP,url,timeoutSeconds,active,startAtBoot form
             * also sets the work state. Both are applied like a Reader
             * Manager set-active request. */
            else if(strncmp(hostRxBuffer, "SET_HTTP,", 9U) == 0)
            {
                char url[READER_SETTINGS_URL_MAX + 1U];
                char json[256];
                unsigned int timeout = 0U;
                unsigned int field3 = 0U;
                unsigned int field4 = 0U;
                unsigned int active = 0U;
                unsigned int startAtBoot;
                uint8_t setWorkState;
                int fields;
                int jsonLength;
                uint8_t ok = 0U;

                fields = sscanf(&hostRxBuffer[9U], "%127[^,],%u,%u,%u", url, &timeout, &field3, &field4);
                setWorkState = (fields == 4) ? 1U : 0U;
                if(setWorkState != 0U)
                {
                    active = field3;
                    startAtBoot = field4;
                }
                else
                {
                    startAtBoot = field3;
                }

                if(
                    ((fields == 3) || (fields == 4)) &&
                    (timeout <= 65535U) && (active <= 1U) && (startAtBoot <= 1U) &&
                    (strchr(url, '"') == NULL) && (strchr(url, '\\') == NULL)
                )
                {
                    jsonLength = snprintf(
                        json,
                        sizeof(json),
                        "{\"glob_params\":{\"start_inv_atbeg\":%u},"
                        "\"upload\":{\"http\":{\"url\":\"%s\",\"timeout\":%u}}}",
                        startAtBoot,
                        url,
                        timeout
                    );

                    if(
                        (jsonLength > 0) &&
                        (jsonLength < (int)sizeof(json)) &&
                        (VendorConfig_HandleActiveSettings(
                            json,
                            (uint16_t)jsonLength
                        ) == 0U)
                    )
                    {
                        /* Apply the new settings before changing the work
                         * state, so active mode starts with them. */
                        VendorConfig_Task();

                        ok = (setWorkState == 0U)
                            ? 1U
                            : VendorConfig_SetWorkState(
                                  (active != 0U)
                                      ? READER_SETTINGS_WORK_ACTIVE
                                      : READER_SETTINGS_WORK_PASSIVE
                              );
                    }
                }

                HOST_SendString((ok != 0U) ? "OK,SET_HTTP\r\n" : "ERROR,SET_HTTP\r\n");
            }
            /*
             * SET_BUZZER,<ms>[,<tone>]
             *
             * The board buzzer has a fixed tone, so <tone> is accepted and
             * ignored. Boards without a buzzer (F429 Discovery) answer
             * ERROR; GET_CAPABILITIES reports BUZZER=0/1.
             */
            else if(strncmp(hostRxBuffer, "SET_BUZZER,", 11U) == 0)
            {
                unsigned long durationMs;

                if(
                    (sscanf(&hostRxBuffer[11U], "%lu", &durationMs) == 1) &&
                    (GPIO_Service_Beep((uint32_t)durationMs) == GPIO_SERVICE_OK)
                )
                {
                    HOST_SendString("OK,SET_BUZZER\r\n");
                }
                else
                {
                    HOST_SendString("ERROR,SET_BUZZER\r\n");
                }
            }
            else if(strncmp(hostRxBuffer, "SET_GEN2,", 9U) == 0)
            {
                uint8_t target;
                uint8_t q;
                uint8_t profile;
                uint8_t fast;

                if(
                    (HOST_ParseGen2(&hostRxBuffer[9U], &target, &q, &profile, &fast) == 1U) &&
                    ((fast == 0U) || (SILION_Application_SetSession(0U) == 1U)) &&
                    ((fast == 0U) || (Configuration_Service_SaveSession(0U) == 1U)) &&
                    (SILION_Application_SetGen2(target, q) == 1U) &&
                    ((profile == 0U) || (SILION_Application_SetGen2Profile(profile) == 1U)) &&
                    (HOST_PersistGen2(target, q, profile) == 1U)
                )
                {
                    HOST_SendString("OK,SET_GEN2\r\n");
                }
                else
                {
                    HOST_SendString("ERROR,SET_GEN2\r\n");
                }
            }
            else if(strcmp(hostRxBuffer, "GET_GEN2") == 0)
            {
                if(SILION_Application_GetGen2() == 0U)
                {
                    HOST_SendString("ERROR,GET_GEN2\r\n");
                }
            }
            else if(strncmp(hostRxBuffer, "SET_ANTENNAS,", 13U) == 0)
            {
                uint8_t port = 0U;

                if(
                    (HOST_ParseAntennaList(&hostRxBuffer[13U], &port) == 1U) &&
                    (SILION_Application_SetAntenna(port, port) == 1U) &&
                    (HOST_PersistAntenna(port, port) == 1U)
                )
                {
                    HOST_SendString("OK,SET_ANTENNAS\r\n");
                }
                else
                {
                    HOST_SendString("ERROR,SET_ANTENNAS\r\n");
                }
            }
            /*
             * =================================================
             * UNKNOWN COMMAND
             * =================================================
             */
            else
            {
                HOST_SendString(
                    "ERROR,INVALID_COMMAND\r\n"
                );
            }

            host_command_done:
            /*
             * ------------------------------------------------
             * Reset command buffer.
             * ------------------------------------------------
             */
            hostRxIndex = 0U;
            hostRxBuffer[0] = '\0';

            continue;
        }


        /*
         * ----------------------------------------------------
         * Ignore other non-printable/control characters.
         * ----------------------------------------------------
         */
        if(
            byte < 0x20U ||
            byte > 0x7EU
        )
        {
            continue;
        }


        /*
         * ----------------------------------------------------
         * Store normal printable character.
         * ----------------------------------------------------
         */
        if(
            hostRxIndex <
            (HOST_RX_BUFFER_SIZE - 1U)
        )
        {
            hostRxBuffer[hostRxIndex] = (char)byte;
            hostRxIndex++;
        }
    }
}
