#include "../../Services/configuration/reader_settings.h"
#include "firmware_configuration.h"

#include "../Run_time/firmware_runtime.h"

#include "../../Services/USB/usb_cdc_transport.h"
#include "../../USB_DEVICE/App/usb_device.h"

#include "../../Config/firmware_config.h"
#if (TARGET_BOARD == BOARD_F411_CUSTOM)
#include "../../BSP/f411_custom/board.h"
#else
#include "../../BSP/f429_discovery/board.h"
#endif

#include "../../Common/uart_dispatch.h"

#include "../Silion/sillion_application.h"
#include "../Silion/sillion_application_port.h"

#include "../Host/host_interface.h"

#include "../../Services/serial/serial_port.h"
#include "../../Services/Time/time_service.h"

#include "../../Devices/w5500/w5500.h"

#include "../../Services/ethernet/ethernet_service.h"
#include "../../Services/configuration/configuration_service.h"
#include "../../Services/configuration/configuration_port.h"
#include "../../Services/ethernet/host_tcp_server.h"
#include "../../Services/wifi/wifi_service.h"
#include "../../Services/storage/tag_log.h"
#include "../../Services/host/host_transport.h"
#include "../../Protocol/identium_protocol.h"
#include "../../BSP/industrial_io.h"
#include "../../Services/GPIO/gpio_service.h"

#include "../../Services/system/system_fault.h"

#include "../../Common/status.h"

#include "../../Services/system/reset_cause.h"
#include "../../Services/system/brownout.h"

#include "../../Services/watchdog/watchdog.h"
#include "../../Config/product_config.h"




/*
 * ============================================================
 * GLOBAL HARDWARE / APPLICATION OBJECTS
 *
 * These objects belong to the modular firmware configuration
 * layer. They replace the old monolithic config.c ownership.
 * ============================================================
 */

USART_Handle_t rfidUart;
USART_Handle_t hostUart;

SPI_Handle_t w5500Spi;

Silion_Handle_t silion;

extern SILION_ReaderConfig_t readerConfig;

/* Interval between bring-up retries of an RFID module that did not
 * start. Each retry blocks the main loop for about a second. */
#define FIRMWARE_RFID_RETRY_MS   10000U

static uint32_t g_rfidRetryAtMs = 0U;


/*
 * ============================================================
 * READER DEFAULT CONFIGURATION
 * ============================================================
 */

static void Firmware_Configuration_ReaderDefaults(void)
{
    readerConfig.region =
    		PRODUCT_RFID_DEFAULT_REGION;

    readerConfig.txAntenna =
        1U;

    readerConfig.rxAntenna =
        1U;

    readerConfig.readPower =
        2500U;

    readerConfig.writePower =
        2500U;

    readerConfig.tagProtocol =
        SILION_TAG_PROTOCOL_GEN2;

    readerConfig.session =
        SILION_SESSION_0;
}


/*
 * ============================================================
 * UART INTERRUPT CONFIGURATION
 * ============================================================
 */

static void Firmware_Configuration_UART_IRQ_Init(void)
{
    USART_IRQPriorityConfig(
        BOARD_RFID_USART_IRQ,
        5U
    );

    USART_IRQInterruptConfig(
        BOARD_RFID_USART_IRQ,
        ENABLE
    );

    USART_IRQPriorityConfig(
        BOARD_HOST_USART_IRQ,
        5U
    );

    USART_IRQInterruptConfig(
        BOARD_HOST_USART_IRQ,
        ENABLE
    );
}


/*
 * ============================================================
 * START INTERRUPT-DRIVEN UART RX
 * ============================================================
 */

static void Firmware_Configuration_UART_Start(void)
{
    USART_ReceiveByteIT(
        &rfidUart
    );

    USART_ReceiveByteIT(
        &hostUart
    );
}


/*
 * ============================================================
 * RFID MODULE BRING-UP
 *
 * The proven application-level sequence: configure, then apply
 * the persisted session, Gen2 settings and boot-time inventory.
 * Used at start-up and for the retries of a degraded module.
 * ============================================================
 */

static uint8_t Firmware_Configuration_StartReader(uint8_t attempts)
{
    uint8_t attempt;

    /* A module that answered garbage once gets another chance. */
    for(attempt = 0U; attempt < attempts; attempt++)
    {
        Watchdog_Refresh();

        if(SILION_Application_ConfigureReader() != 0U)
        {
            break;
        }
    }

    if(attempt >= attempts)
    {
        return 0U;
    }

    /*
     * The existing ConfigureReader sequence deliberately keeps the
     * Gen2 session command disabled. Apply the persisted session here
     * after the module is known to be alive and in application phase.
     */
    if(SILION_Application_SetSession(readerConfig.session) == 0U)
    {
        return 0U;
    }

    /* Gen2 target and Q saved with SET_GEN2. Not fatal: if the module
     * rejects them it keeps its own defaults and reading still works. */
    if(ReaderSettings_Get()->gen2Valid != 0U)
    {
        (void)SILION_Application_SetGen2(
            ReaderSettings_Get()->gen2Target,
            ReaderSettings_Get()->gen2Q
        );

        if(ReaderSettings_Get()->gen2Profile != 0U)
        {
            (void)SILION_Application_SetGen2Profile(
                ReaderSettings_Get()->gen2Profile
            );
        }
    }

    /* Passive work state (vendor tools): no autonomous reading. */
    if(
        (Configuration_Service_GetStartInventoryAtBoot() != 0U) &&
        (ReaderSettings_GetWorkState() == READER_SETTINGS_WORK_ACTIVE)
    )
    {
        if(SILION_Application_StartInventory() == 0U)
        {
            return 0U;
        }
    }

    return 1U;
}


/*
 * ============================================================
 * DEGRADED RFID RETRY
 *
 * Only while the module is idle: a vendor tool may be talking to
 * it through the module bridge, or a host may have started an
 * inventory by hand.
 * ============================================================
 */

void Firmware_Configuration_Task(void)
{
    if((System_Fault_GetDegraded() & SYSTEM_DEGRADED_RFID) == 0U)
    {
        return;
    }

    if((int32_t)(Time_GetTickMs() - g_rfidRetryAtMs) < 0)
    {
        return;
    }

    if(SILION_Application_GetState() != SILION_APP_IDLE)
    {
        g_rfidRetryAtMs = Time_GetTickMs() + FIRMWARE_RFID_RETRY_MS;
        return;
    }

    if(Firmware_Configuration_StartReader(1U) != 0U)
    {
        System_Fault_ClearDegraded(SYSTEM_DEGRADED_RFID);

        if(System_Fault_Get() == ERROR_RFID_INIT)
        {
            System_Fault_Clear();
        }
    }

    g_rfidRetryAtMs = Time_GetTickMs() + FIRMWARE_RFID_RETRY_MS;
}



/*
 * ============================================================
 * MAIN CONFIGURATION
 * ============================================================
 */

Status_t  Firmware_Configuration_Init(void)
{
    /*
     * --------------------------------------------------------
     * TIME
     * --------------------------------------------------------
     */
    Time_Service_Init();
    Reset_Cause_Init();
    (void)Brownout_Init();
    System_Fault_Init();

    /*
     * --------------------------------------------------------
     * WATCHDOG
     *
     * Started before any peripheral or external chip, so a hang
     * anywhere in the start-up below resets the reader instead of
     * leaving it dead. Parts that fail to start leave the reader
     * running degraded (System_Fault_GetDegraded) rather than
     * halting it. A watchdog that does not start is degraded too.
     * --------------------------------------------------------
     */
    if(Watchdog_Init() == 0U)
    {
        System_Fault_Set(ERROR_WATCHDOG_INIT);
        System_Fault_SetDegraded(SYSTEM_DEGRADED_WATCHDOG);
    }

    HOST_Transport_Init();
    Identium_Protocol_Init();
    Configuration_Service_Init();

    /*
     * --------------------------------------------------------
     * BOARD / MCU HARDWARE
     *
     * Board-specific GPIO, UART and SPI configuration remains
     * inside the BSP.
     * --------------------------------------------------------
     */
#if (TARGET_BOARD == BOARD_F411_CUSTOM)
    Board_F411_Init(
        &rfidUart,
        &hostUart,
        &w5500Spi
    );
#else
    Board_F429_Init(
        &rfidUart,
        &hostUart,
        &w5500Spi
    );
#endif

    if(Industrial_IO_Init() == 0U)
    {
        System_Fault_Set(ERROR_GPIO_INIT);
        return STATUS_ERROR;
    }

    GPIO_Service_Init(
        Identium_Protocol_ReportGpiChange
    );


    /*
     * --------------------------------------------------------
     * NATIVE USB CDC
     *
     * Existing firmware remains the owner of system clocks,
     * SysTick, UART, SPI and application initialization.
     * --------------------------------------------------------
     */
    USB_CDC_Transport_Init();
    MX_USB_DEVICE_Init();


    /*
     * --------------------------------------------------------
     * UART DISPATCH
     * --------------------------------------------------------
     */
    USART_Dispatch_Init(&rfidUart, &hostUart);


    /*
     * --------------------------------------------------------
     * HOST SERIAL PORT (RS232 / RS485)
     *
     * On the F429 Discovery this USART is wired to the ST-LINK
     * virtual COM port; on the F411 board it is USART2 behind the
     * RS232/RS485 mux.
     * --------------------------------------------------------
     */
    Serial_Port_Init(
        &hostUart,
        PRODUCT_SERIAL_DEFAULT_MODE
    );


    /*
     * --------------------------------------------------------
     * SILION PROTOCOL
     * --------------------------------------------------------
     */
    SILION_Init(
        &silion,
        &rfidUart
    );


    /*
     * --------------------------------------------------------
     * SILION APPLICATION PORT
     * --------------------------------------------------------
     */
    SILION_Application_Port_Init(
        &silion,
        &rfidUart
    );


    /*
     * --------------------------------------------------------
     * UART IRQ
     * --------------------------------------------------------
     */
    Firmware_Configuration_UART_IRQ_Init();


    /*
     * --------------------------------------------------------
     * START UART RX
     * --------------------------------------------------------
     */
    Firmware_Configuration_UART_Start();



    /*
     * --------------------------------------------------------
     * READER DEFAULTS + PERSISTENT IDENTIUM CONFIGURATION
     * --------------------------------------------------------
     * Defaults establish a safe baseline first. A valid stored
     * configuration then overrides only the fields implemented by
     * the current configuration service.
     */
    Firmware_Configuration_ReaderDefaults();
    (void)Configuration_Service_LoadPersistent();


    /*
     * --------------------------------------------------------
     * APPLICATION INITIALIZATION
     * --------------------------------------------------------
     */
    SILION_Application_Init(
        &silion
    );


    /*
     * --------------------------------------------------------
     * HOST INTERFACE
     * --------------------------------------------------------
     */
    HOST_Interface_Init();


    /* Persistent network / identity settings (vendor-tool configurable). */
    ReaderSettings_Init();

    /* RF values saved from the vendor tools' static configuration. */
    if(ReaderSettings_Get()->rfValid != 0U)
    {
        readerConfig.region = ReaderSettings_Get()->region;
        readerConfig.readPower = ReaderSettings_Get()->readPower;
        readerConfig.writePower = ReaderSettings_Get()->writePower;
    }

    /* Antenna saved with SET_ANTENNA / SET_ANTENNAS. */
    if(ReaderSettings_Get()->rfAntennaValid != 0U)
    {
        readerConfig.txAntenna = ReaderSettings_Get()->txAntenna;
        readerConfig.rxAntenna = ReaderSettings_Get()->rxAntenna;
    }

    /*
     * --------------------------------------------------------
     * W5500 BASIC STARTUP
     *
     * Existing W5500 functions are reused. We are not
     * rewriting its protocol implementation in this step.
     * --------------------------------------------------------
     */
    /* A W5500 that does not start leaves Ethernet down; USB, serial
     * and WiFi still work, so the reader keeps running. */
    if(
        W5500_Init(
            &w5500Spi,
            BOARD_W5500_CS_PORT,
            BOARD_W5500_CS_PIN,
            BOARD_W5500_RESET_PORT,
            BOARD_W5500_RESET_PIN
        ) == 0U
    )
    {
        System_Fault_Set(
            ERROR_W5500_INIT
        );
        System_Fault_SetDegraded(SYSTEM_DEGRADED_ETHERNET);
    }

    Watchdog_Refresh();


    /* Tag backup flash shares the W5500 SPI bus (F411 only). A missing
     * or dead chip only disables the backup; the reader still runs. */
    (void)TagLog_Init(&w5500Spi);

    Watchdog_Refresh();


    if(
        ReaderSettings_ApplyNetwork() == 0U
    )
    {
        System_Fault_Set(
            ERROR_W5500_INIT
        );
        System_Fault_SetDegraded(SYSTEM_DEGRADED_ETHERNET);
    }


    Configuration_Port_Init();
    Host_Tcp_Server_Init();

    if(
        Ethernet_Service_Init(
            Identium_Protocol_PushTcpByte
        ) == 0U
    )
    {
        System_Fault_Set(
            ERROR_ETHERNET_INIT
        );
        System_Fault_SetDegraded(SYSTEM_DEGRADED_ETHERNET);
    }

    /* WiFi is optional: a missing ESP32 never prevents normal reader boot. */
    WiFi_Service_Init(Identium_Protocol_PushTcpByte);



    /*
     * --------------------------------------------------------
     * SILION READER STARTUP / CONFIGURATION
     *
     * A module that does not come up leaves the reader running
     * without RFID, reachable over the network to diagnose
     * (GET_DIAG); Firmware_Configuration_Task keeps retrying.
     * --------------------------------------------------------
     */
    if(Firmware_Configuration_StartReader(3U) == 0U)
    {
        System_Fault_Set(
            ERROR_RFID_INIT
        );
        System_Fault_SetDegraded(SYSTEM_DEGRADED_RFID);

        g_rfidRetryAtMs = Time_GetTickMs() + FIRMWARE_RFID_RETRY_MS;
    }

    Watchdog_Refresh();


    if(System_Fault_GetDegraded() == 0U)
    {
        System_Fault_Clear();
    }

    return STATUS_OK;
}
