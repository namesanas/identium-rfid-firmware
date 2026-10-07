#include "discovery_service.h"

#include "reader_settings.h"
#include "../../Config/product_config.h"
#include "../../Devices/w5500/w5500.h"

#include <stdint.h>
#include <string.h>

#define DISCOVERY_PORT              15000U
#define DISCOVERY_CMD_SEARCH        0x50U
#define DISCOVERY_REPLY_LENGTH      47U
#define DISCOVERY_BOARD_HC32F46X    1U
#define DISCOVERY_W5500_SR_UDP      0x22U

/*
 * Reply layout, parsed identically by Reader Manager (needs >= 47 bytes)
 * and ReaderSense:
 *   0..5   header FF 06 00 1F 50 00 (not checked by the tools)
 *   6..9   status (BE32)
 *   10..15 MAC
 *   16     DHCP flag
 *   17..32 IP, netmask, gateway, DNS
 *   33..34 configuration port (BE16)
 *   35     board type (1 = Hc32f46x)
 *   36     module type
 *   37     reserved
 *   38..41 bootloader version a.b.c.d
 *   42..45 module firmware version
 *   46     work mode (0 boot, 1 passive, other = active)
 */
static void Discovery_BuildReply(uint8_t reply[DISCOVERY_REPLY_LENGTH])
{
    const ReaderSettings_t *settings = ReaderSettings_Get();

    memset(reply, 0, DISCOVERY_REPLY_LENGTH);

    reply[0] = 0xFFU;
    reply[1] = 0x06U;
    reply[2] = 0x00U;
    reply[3] = (uint8_t)(DISCOVERY_REPLY_LENGTH - 16U);
    reply[4] = DISCOVERY_CMD_SEARCH;
    reply[5] = 0x00U;

    memcpy(&reply[10], settings->mac, 6U);
    reply[16] = 0U;
    memcpy(&reply[17], settings->ip, 4U);
    memcpy(&reply[21], settings->netmask, 4U);
    memcpy(&reply[25], settings->gateway, 4U);
    memcpy(&reply[29], settings->dns, 4U);
    reply[33] = (uint8_t)(settings->configPort >> 8U);
    reply[34] = (uint8_t)(settings->configPort & 0xFFU);
    reply[35] = DISCOVERY_BOARD_HC32F46X;

    reply[38] = PRODUCT_MAINBOARD_VERSION_0;
    reply[39] = PRODUCT_MAINBOARD_VERSION_1;
    reply[40] = PRODUCT_MAINBOARD_VERSION_2;
    reply[41] = PRODUCT_MAINBOARD_VERSION_3;

    reply[46] = settings->workState;
}

void Discovery_Service_Task(void)
{
    uint8_t request[32];
    uint8_t reply[DISCOVERY_REPLY_LENGTH];
    uint8_t sourceIp[4];
    uint16_t sourcePort = 0U;
    uint16_t length;
    static const uint8_t anyMac[6] = { 0U, 0U, 0U, 0U, 0U, 0U };

    if(W5500_Socket2_GetStatus() != DISCOVERY_W5500_SR_UDP)
    {
        (void)W5500_Socket2_OpenUdp(DISCOVERY_PORT);
        return;
    }

    length = W5500_Socket2_ReceiveFrom(request, sizeof(request), sourceIp, &sourcePort);

    if(
        (length < 12U) ||
        (request[0] != 0xEEU) ||
        (request[1] != 0x06U) ||
        (request[4] != DISCOVERY_CMD_SEARCH)
    )
    {
        return;
    }

    /* Target MAC: all zero = any reader, otherwise it must be ours. */
    if(
        (memcmp(&request[6], anyMac, 6U) != 0) &&
        (memcmp(&request[6], ReaderSettings_Get()->mac, 6U) != 0)
    )
    {
        return;
    }

    Discovery_BuildReply(reply);
    (void)W5500_Socket2_SendTo(reply, sizeof(reply), sourceIp, sourcePort);
}
