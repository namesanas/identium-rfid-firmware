#include "../../Services/configuration/reader_settings.h"
#include "firmware_configuration.h"

#include "../Run_time/firmware_runtime.h"

#include "../../Services/USB/usb_cdc_transport.h"
#include "../../USB_DEVICE/App/usb_device.h"

#include "../../BSP/f429_discovery/board.h"

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

USART_Handle_t usart3;
USART_Handle_t usart1;

SPI_Handle_t w5500Spi;

Silion_Handle_t silion;

extern SILION_ReaderConfig_t readerConfig;


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
        &usart3
    );

    USART_ReceiveByteIT(
        &usart1
    );
}



/*
 * ============================================================
 * MAIN CONFIGURATION
 * ============================================================
 */

Status_t  Firmware_Configuration_Init(void)
{
    uint8_t rfidAttempt;

    /*
     * --------------------------------------------------------
     * TIME
     * --------------------------------------------------------
     */
    Time_Service_Init();
    Reset_Cause_Init();
    (void)Brownout_Init();
    System_Fault_Init();
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
    Board_F429_Init(
        &usart3,
        &usart1,
        &w5500Spi
    );

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
    USART_Dispatch_Init(&usart3, &usart1);


    /*
     * --------------------------------------------------------
     * HOST SERIAL PORT (RS232 / RS485)
     *
     * On the F429 Discovery this USART is wired to the ST-LINK
     * virtual COM port.
     * --------------------------------------------------------
     */
    Serial_Port_Init(
        &usart1,
        PRODUCT_SERIAL_DEFAULT_MODE
    );


    /*
     * --------------------------------------------------------
     * SILION PROTOCOL
     * --------------------------------------------------------
     */
    SILION_Init(
        &silion,
        &usart3
    );


    /*
     * --------------------------------------------------------
     * SILION APPLICATION PORT
     * --------------------------------------------------------
     */
    SILION_Application_Port_Init(
        &silion,
        &usart3
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

    /*
     * --------------------------------------------------------
     * W5500 BASIC STARTUP
     *
     * Existing W5500 functions are reused. We are not
     * rewriting its protocol implementation in this step.
     * --------------------------------------------------------
     */
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

        return STATUS_ERROR;
    }


    if(
        ReaderSettings_ApplyNetwork() == 0U
    )
    {
        System_Fault_Set(
            ERROR_W5500_INIT
        );

        return STATUS_ERROR;
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

        return STATUS_ERROR;
    }

    /* WiFi is optional: a missing ESP32 never prevents normal reader boot. */
    WiFi_Service_Init(Identium_Protocol_PushTcpByte);



    /*
     * --------------------------------------------------------
     * SILION READER STARTUP / CONFIGURATION
     *
     * This is the proven application-level sequence.
     * --------------------------------------------------------
     */

    /* A failed start-up halts the reader for good (Error_Handler), so
     * a module that answered garbage once gets another chance. */
    for(
        rfidAttempt = 0U;
        (rfidAttempt < 3U) && (SILION_Application_ConfigureReader() == 0U);
        rfidAttempt++
    )
    {
    }

    if(rfidAttempt >= 3U)
    {
        System_Fault_Set(
            ERROR_RFID_INIT
        );

        return STATUS_ERROR;
    }


    /*
     * The existing ConfigureReader sequence deliberately keeps the
     * Gen2 session command disabled. Apply the persisted session here
     * after the module is known to be alive and in application phase.
     */
    if(
        SILION_Application_SetSession(readerConfig.session)
        == 0U
    )
    {
        System_Fault_Set(
            ERROR_RFID_INIT
        );

        return STATUS_ERROR;
    }


    /* Passive work state (vendor tools): no autonomous reading. */
    if(
        (Configuration_Service_GetStartInventoryAtBoot() != 0U) &&
        (ReaderSettings_GetWorkState() == READER_SETTINGS_WORK_ACTIVE)
    )
    {
        if(SILION_Application_StartInventory() == 0U)
        {
            System_Fault_Set(
                ERROR_RFID_INIT
            );

            return STATUS_ERROR;
        }
    }


    /*
     * --------------------------------------------------------
     * WATCHDOG
     *
     * Start the independent watchdog only after the complete
     * firmware startup sequence has succeeded.
     * --------------------------------------------------------
     */
    if(
        Watchdog_Init()
        == 0U
    )
    {
        System_Fault_Set(
            ERROR_WATCHDOG_INIT
        );

        return STATUS_ERROR;
    }


    System_Fault_Clear();

    return STATUS_OK;
}
