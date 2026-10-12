#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../Firmware/Protocol/identium_protocol.h"
#include "../Firmware/Protocol/identium_command.h"

static uint32_t g_tick = 0U;
static uint8_t g_connected = 1U;
static uint32_t g_heartbeatSeconds = 10U;
static uint8_t g_inventoryRunning = 1U;

static char g_tx[64][1600];
static uint16_t g_txLen[64];
static uint16_t g_txCount = 0U;
static uint32_t g_commandCount = 0U;
static char g_lastCommand[128];

void Identium_Command_Init(void) {}
void Identium_Command_Task(uint32_t nowMs) { (void)nowMs; }

void Identium_Command_ProcessJson(
    const char *json,
    Identium_CommandReplyCallback_t replyCallback
)
{
    (void)replyCallback;
    g_commandCount++;
    strncpy(g_lastCommand, json, sizeof(g_lastCommand)-1U);
    g_lastCommand[sizeof(g_lastCommand)-1U] = '\0';
}

const char *ReaderSettings_GetName(void) { return "RFID-F429-DEV"; }
uint32_t Time_GetTickMs(void) { return g_tick; }
uint64_t Time_GetEpochMs(void) { return 1789981401488ULL + g_tick; }

uint32_t Configuration_Service_GetHeartbeatSeconds(void) { return g_heartbeatSeconds; }
uint8_t Configuration_Service_GetRemoteCmdReply(void) { return 1U; }
uint32_t Configuration_Service_GetSBufSize(void) { return 1580U; }
uint32_t Configuration_Service_GetRecvTimeoutSeconds(void) { return 10U; }
uint32_t Configuration_Service_GetClearBufferTimeSeconds(void) { return 8U; }
uint8_t Configuration_Service_GetDataAggregationMode(void) { return 0U; }
uint32_t Configuration_Service_GetDataAggregationTimeMs(void) { return 1000U; }
uint32_t Configuration_Service_GetNetworkWatchdogMinutes(void) { return 0U; }
uint32_t Configuration_Service_GetInventoryWatchdogMinutes(void) { return 0U; }
uint8_t Configuration_Service_IsEventEnabled(uint8_t eventCode) {
    return ((eventCode == 1U) || (eventCode == 2U) || (eventCode == 3U)) ? 1U : 0U;
}
const char *Configuration_Service_GetCusParam(void) { return ""; }
const uint8_t *Configuration_Service_GetTagJsonFormat(uint8_t *count) {
    static const uint8_t format[] = {1U, 2U, 5U, 8U};
    if(count != NULL) *count = (uint8_t)sizeof(format);
    return format;
}
uint8_t SILION_Application_IsInventoryRunning(void) { return g_inventoryRunning; }

void NVIC_SystemReset(void) { assert(0 && "unexpected reset"); }

uint8_t ActiveUpload_Transport_IsConnected(void) { return g_connected; }
uint16_t ActiveUpload_Transport_SendComplete(const uint8_t *buffer, uint16_t length)
{
    assert(g_txCount < 64U);
    assert(length < sizeof(g_tx[0]));
    memcpy(g_tx[g_txCount], buffer, length);
    g_tx[g_txCount][length] = '\0';
    g_txLen[g_txCount] = length;
    g_txCount++;
    return length;
}

static void reset_tx(void)
{
    g_txCount = 0U;
    memset(g_txLen, 0, sizeof(g_txLen));
}

static SILION_Tag_t make_tag(uint8_t value)
{
    SILION_Tag_t tag;
    memset(&tag, 0, sizeof(tag));
    tag.epcLengthBytes = 12U;
    for(uint16_t i = 0U; i < tag.epcLengthBytes; i++)
        tag.epc[i] = (uint8_t)(value + i);
    tag.antenna = 1U;
    tag.readCount = 2U;
    tag.rssi = -59;
    tag.frequencyKHz = 865700U;
    return tag;
}

static void test_framing(void)
{
    const char *fragment1 = "garbage-prefix ";
    const char *fragment2 = "{\"command_type\":\"get_gpi\",\"command_data\":{}}\n";

    g_commandCount = 0U;
    memset(g_lastCommand, 0, sizeof(g_lastCommand));

    for(const char *p = fragment1; *p; ++p) Identium_Protocol_PushTcpByte((uint8_t)*p);
    for(size_t i = 0U; i < 17U; i++) Identium_Protocol_PushTcpByte((uint8_t)fragment2[i]);
    for(size_t i = 17U; fragment2[i]; i++) Identium_Protocol_PushTcpByte((uint8_t)fragment2[i]);

    assert(g_commandCount == 1U);
    assert(strcmp(g_lastCommand, "{\"command_type\":\"get_gpi\",\"command_data\":{}}") == 0);

    /* Oversized line is discarded, and the next line still works. */
    g_commandCount = 0U;
    for(size_t i = 0U; i < 2201U; i++) Identium_Protocol_PushTcpByte((uint8_t)'A');
    Identium_Protocol_PushTcpByte((uint8_t)'\n');
    for(const char *p = fragment2; *p; ++p) Identium_Protocol_PushTcpByte((uint8_t)*p);
    assert(g_commandCount == 1U);
}

static void test_tag_queue(void)
{
    SILION_Tag_t tag = make_tag(0x10U);
    reset_tx();
    g_tick = 100U;

    assert(Identium_Protocol_ReportTag(&tag) == 1U);
    assert(g_txCount == 0U);

    Identium_Protocol_Task();

    assert(g_txCount >= 1U);
    assert(strstr(g_tx[0], "\"event_type\":\"heart_beat\"") != NULL);
    assert(g_txCount >= 2U);
    assert(strstr(g_tx[1], "\"event_type\":\"tag_read\"") != NULL);
    assert(strstr(g_tx[1], "\"ep\":\"101112131415161718191A1B\"") != NULL);
    assert(g_txLen[1] <= 1580U);
}

static void test_disconnect_buffer_and_stale(void)
{
    SILION_Tag_t oldTag = make_tag(0x20U);
    SILION_Tag_t newTag = make_tag(0x40U);

    Identium_Protocol_Init();
    reset_tx();
    g_connected = 0U;
    g_tick = 1000U;

    assert(Identium_Protocol_ReportTag(&oldTag) == 1U);
    Identium_Protocol_Task();
    assert(g_txCount == 0U);

    /* Older than the configured 8 second stale-data window. */
    g_tick += 9000U;
    Identium_Protocol_Task();

    g_connected = 1U;
    g_tick += 100U;
    assert(Identium_Protocol_ReportTag(&newTag) == 1U);
    reset_tx();
    Identium_Protocol_Task();

    for(uint16_t i = 0U; i < g_txCount; i++)
    {
        if(strstr(g_tx[i], "\"event_type\":\"tag_read\"") != NULL)
        {
            assert(strstr(g_tx[i], "404142434445464748494A4B") != NULL);
            assert(strstr(g_tx[i], "202122232425262728292A2B") == NULL);
        }
    }
}

int main(void)
{
    Identium_Protocol_Init();
    test_framing();
    test_tag_queue();
    test_disconnect_buffer_and_stale();
    printf("HOST PROTOCOL TEST: PASS\n");
    return 0;
}
