#include "host_interface.h"
#include "sillion_application.h"
#include "../../Services/host/host_transport.h"
#include "../../Services/USB/usb_cdc_transport.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../Services/ethernet/ethernet_service.h"

#include "../../Services/system/system_fault.h"




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

#define HOST_RX_BUFFER_SIZE 128U


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


void HOST_Interface_Task(void)
	{
	    USB_CDC_Transport_Task();
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
            	if(
            	    SILION_Application_StopInventory()
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
                    SILION_Application_SetPower(
                        power,
                        power
                    ) == 1U
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
                        SILION_Application_SetAntenna(
                            (uint8_t)tx,
                            (uint8_t)rx
                        ) == 1U
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
                    SILION_Application_SetRegion(
                        region
                    ) == 1U
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
                        SILION_Application_SetSession(
                            (uint8_t)session
                        ) == 1U
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
