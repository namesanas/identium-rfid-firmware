/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include "../Firmware/Application/Run_time/firmware_runtime.h"

#include "../Firmware/Application/Silion/sillion_application.h"
#include "../Firmware/Application/Host/host_interface.h"

#include "../Firmware/Services/ethernet/ethernet_service.h"
#include "../Firmware/Services/wifi/wifi_service.h"
#include "../Firmware/Services/configuration/configuration_port.h"
#include "../Firmware/Services/host/host_transport.h"
#include "../Firmware/Common/status.h"
#include "../Firmware/Services/watchdog/watchdog.h"
#include "../Firmware/Protocol/identium_protocol.h"
#include "../Firmware/Services/GPIO/gpio_service.h"
#include "../Firmware/Services/system/fault_record.h"
#include "../Firmware/Services/system/loop_monitor.h"
#include "../Firmware/Services/system/stack_monitor.h"
  //#include "../Firmware/Services/cloud/cloud_service.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */



  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */

  Status_t firmwareStatus;

  /* First: take over the previous run's fault record, then paint the
   * stack and arm its overflow guard while the stack is still shallow. */
  Fault_Record_Init();
  Stack_Monitor_Init();

  firmwareStatus =
      Firmware_Runtime_Init();

  if(
      firmwareStatus != STATUS_OK
  )
  {
      Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  Loop_Monitor_Init();

  while(1)
  {
	//  Cloud_Service_Task();
      /* Company protocol Ethernet session is independent of legacy USB ASCII. */
      LOOP_MONITOR_RUN(LOOP_TASK_ETHERNET, Ethernet_Service_Task());
      LOOP_MONITOR_RUN(LOOP_TASK_WIFI, WiFi_Service_Task());
      LOOP_MONITOR_RUN(LOOP_TASK_CONFIG_PORT, Configuration_Port_Task());
      LOOP_MONITOR_RUN(LOOP_TASK_PROTOCOL, Identium_Protocol_Task());
      LOOP_MONITOR_RUN(LOOP_TASK_GPIO, GPIO_Service_Task());

      LOOP_MONITOR_RUN(LOOP_TASK_RFID, SILION_Application_Task());

      /* Legacy ASCII parser remains on native USB during migration. */
      LOOP_MONITOR_RUN(LOOP_TASK_HOST, HOST_Interface_Task());

      Loop_Monitor_PassEnd();


      /*
       * Feed the watchdog only after the complete
       * foreground cycle has successfully completed.
       */
     Watchdog_Refresh();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
#if defined(STM32F411xE)
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /*
   * F411 custom board, 8 MHz HSE (X1). Values from the STM32CubeMX
   * project Boards/f411/F411_custom.ioc.
   *
   * PLL: 8 MHz / 4 * 96 = 192 MHz VCO
   * SYSCLK = 192 / 2 = 96 MHz (the F411 maximum is 100 MHz)
   * USB CLK = 192 / 4 = 48 MHz, exact, as OTG_FS requires
   * APB1 = 48 MHz (limit 50 MHz), APB2 = 96 MHz
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4U;
  RCC_OscInitStruct.PLL.PLLN = 96U;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4U;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                 RCC_CLOCKTYPE_SYSCLK |
                                 RCC_CLOCKTYPE_PCLK1 |
                                 RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}
#else
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /*
   * STM32F429I-DISCOVERY uses an 8 MHz HSE.
   *
   * PLL: 8 MHz / 8 * 336 = 336 MHz VCO
   * SYSCLK = 336 / 2 = 168 MHz
   * USB CLK = 336 / 7 = 48 MHz
   *
   * The USB OTG_HS block is used with its internal FS PHY, so the exact
   * 48 MHz clock is mandatory for USB full-speed operation.
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8U;
  RCC_OscInitStruct.PLL.PLLN = 336U;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7U;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                 RCC_CLOCKTYPE_SYSCLK |
                                 RCC_CLOCKTYPE_PCLK1 |
                                 RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}
#endif


/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Recorded for GET_DIAG after the next reset; the reader stays halted
   * (a start-up failure must not turn into a reset loop). */
  Fault_Record_SaveErrorHandler((uint32_t)__builtin_return_address(0));
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
