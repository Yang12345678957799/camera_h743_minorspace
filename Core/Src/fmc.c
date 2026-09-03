/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : FMC.c
  * Description        : This file provides code for the configuration
  *                      of the FMC peripheral.
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
#include "fmc.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

SDRAM_HandleTypeDef hsdram1;

/* FMC initialization function */
void MX_FMC_Init(void)
{
  /* USER CODE BEGIN FMC_Init 0 */
#define SDRAM_REFRESH_COUNT 449U

#define SDRAM_MODEREG_BURST_LENGTH_1          ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   ((uint16_t)0x0000)
#define SDRAM_MODEREG_CAS_LATENCY_3           ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE  ((uint16_t)0x0200)
  /* USER CODE END FMC_Init 0 */

  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* USER CODE BEGIN FMC_Init 1 */

  /* USER CODE END FMC_Init 1 */

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 8;
  SdramTiming.SelfRefreshTime = 5;
  SdramTiming.RowCycleDelay = 7;
  SdramTiming.WriteRecoveryTime = 2;
  SdramTiming.RPDelay = 3;
  SdramTiming.RCDDelay = 3;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler( );
  }

  /* USER CODE BEGIN FMC_Init 2 */
FMC_SDRAM_CommandTypeDef command = {0};

/* 1. ʹ�� SDRAM ʱ�� */
command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
command.AutoRefreshNumber = 1;
command.ModeRegisterDefinition = 0;

if (HAL_SDRAM_SendCommand(&hsdram1, &command, HAL_MAX_DELAY) != HAL_OK)
{
    Error_Handler();
}

/* ʱ���ȶ��ȴ����� 100 us���˴��ȴ� 1 ms */
HAL_Delay(1);

/* 2. Ԥ���ȫ�� Bank */
command.CommandMode = FMC_SDRAM_CMD_PALL;
command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
command.AutoRefreshNumber = 1;
command.ModeRegisterDefinition = 0;

if (HAL_SDRAM_SendCommand(&hsdram1, &command, HAL_MAX_DELAY) != HAL_OK)
{
    Error_Handler();
}

/* 3. ִ�� 8 ���Զ�ˢ�� */
command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
command.AutoRefreshNumber = 8;
command.ModeRegisterDefinition = 0;

if (HAL_SDRAM_SendCommand(&hsdram1, &command, HAL_MAX_DELAY) != HAL_OK)
{
    Error_Handler();
}

/* 4. д��ģʽ�Ĵ�����Burst Length=1��˳��ͻ����CAS=3������дͻ�� */
command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
command.AutoRefreshNumber = 1;
command.ModeRegisterDefinition =
    SDRAM_MODEREG_BURST_LENGTH_1 |
    SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
    SDRAM_MODEREG_CAS_LATENCY_3 |
    SDRAM_MODEREG_OPERATING_MODE_STANDARD |
    SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

if (HAL_SDRAM_SendCommand(&hsdram1, &command, HAL_MAX_DELAY) != HAL_OK)
{
    Error_Handler();
}

/* 5. �����Զ�ˢ�£�SDRAM ʱ��Ϊ 112.5 MHz */
HAL_SDRAM_ProgramRefreshRate(&hsdram1, SDRAM_REFRESH_COUNT);
  /* USER CODE END FMC_Init 2 */
}

static uint32_t FMC_Initialized = 0;

static void HAL_FMC_MspInit(void){
  /* USER CODE BEGIN FMC_MspInit 0 */

  /* USER CODE END FMC_MspInit 0 */
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (FMC_Initialized) {
    return;
  }
  FMC_Initialized = 1;
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_FMC;
    PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

  /* Peripheral clock enable */
  __HAL_RCC_FMC_CLK_ENABLE();

  /** FMC GPIO Configuration
  PD1   ------> FMC_D3
  PE1   ------> FMC_NBL1
  PE0   ------> FMC_NBL0
  PD0   ------> FMC_D2
  PF1   ------> FMC_A1
  PF3   ------> FMC_A3
  PG15   ------> FMC_SDNCAS
  PF0   ------> FMC_A0
  PF2   ------> FMC_A2
  PF5   ------> FMC_A5
  PG4   ------> FMC_BA0
  PF4   ------> FMC_A4
  PF13   ------> FMC_A7
  PE7   ------> FMC_D4
  PG8   ------> FMC_SDCLK
  PF14   ------> FMC_A8
  PE8   ------> FMC_D5
  PG2   ------> FMC_A12
  PG5   ------> FMC_BA1
  PC0   ------> FMC_SDNWE
  PF15   ------> FMC_A9
  PE9   ------> FMC_D6
  PE14   ------> FMC_D11
  PD15   ------> FMC_D1
  PD14   ------> FMC_D0
  PC3_C   ------> FMC_SDCKE0
  PC2_C   ------> FMC_SDNE0
  PG0   ------> FMC_A10
  PE13   ------> FMC_D10
  PD9   ------> FMC_D14
  PD10   ------> FMC_D15
  PG1   ------> FMC_A11
  PE12   ------> FMC_D9
  PF11   ------> FMC_SDNRAS
  PE10   ------> FMC_D7
  PD8   ------> FMC_D13
  PF12   ------> FMC_A6
  PE11   ------> FMC_D8
  PE15   ------> FMC_D12
  */
  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_15|GPIO_PIN_14
                          |GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_7|GPIO_PIN_8
                          |GPIO_PIN_9|GPIO_PIN_14|GPIO_PIN_13|GPIO_PIN_12
                          |GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_3|GPIO_PIN_0|GPIO_PIN_2
                          |GPIO_PIN_5|GPIO_PIN_4|GPIO_PIN_13|GPIO_PIN_14
                          |GPIO_PIN_15|GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_15|GPIO_PIN_4|GPIO_PIN_8|GPIO_PIN_2
                          |GPIO_PIN_5|GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_3|GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN FMC_MspInit 1 */

  /* USER CODE END FMC_MspInit 1 */
}

void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef* sdramHandle){
  /* USER CODE BEGIN SDRAM_MspInit 0 */

  /* USER CODE END SDRAM_MspInit 0 */
  HAL_FMC_MspInit();
  /* USER CODE BEGIN SDRAM_MspInit 1 */

  /* USER CODE END SDRAM_MspInit 1 */
}

static uint32_t FMC_DeInitialized = 0;

static void HAL_FMC_MspDeInit(void){
  /* USER CODE BEGIN FMC_MspDeInit 0 */

  /* USER CODE END FMC_MspDeInit 0 */
  if (FMC_DeInitialized) {
    return;
  }
  FMC_DeInitialized = 1;
  /* Peripheral clock enable */
  __HAL_RCC_FMC_CLK_DISABLE();

  /** FMC GPIO Configuration
  PD1   ------> FMC_D3
  PE1   ------> FMC_NBL1
  PE0   ------> FMC_NBL0
  PD0   ------> FMC_D2
  PF1   ------> FMC_A1
  PF3   ------> FMC_A3
  PG15   ------> FMC_SDNCAS
  PF0   ------> FMC_A0
  PF2   ------> FMC_A2
  PF5   ------> FMC_A5
  PG4   ------> FMC_BA0
  PF4   ------> FMC_A4
  PF13   ------> FMC_A7
  PE7   ------> FMC_D4
  PG8   ------> FMC_SDCLK
  PF14   ------> FMC_A8
  PE8   ------> FMC_D5
  PG2   ------> FMC_A12
  PG5   ------> FMC_BA1
  PC0   ------> FMC_SDNWE
  PF15   ------> FMC_A9
  PE9   ------> FMC_D6
  PE14   ------> FMC_D11
  PD15   ------> FMC_D1
  PD14   ------> FMC_D0
  PC3_C   ------> FMC_SDCKE0
  PC2_C   ------> FMC_SDNE0
  PG0   ------> FMC_A10
  PE13   ------> FMC_D10
  PD9   ------> FMC_D14
  PD10   ------> FMC_D15
  PG1   ------> FMC_A11
  PE12   ------> FMC_D9
  PF11   ------> FMC_SDNRAS
  PE10   ------> FMC_D7
  PD8   ------> FMC_D13
  PF12   ------> FMC_A6
  PE11   ------> FMC_D8
  PE15   ------> FMC_D12
  */

  HAL_GPIO_DeInit(GPIOD, GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_15|GPIO_PIN_14
                          |GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_8);

  HAL_GPIO_DeInit(GPIOE, GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_7|GPIO_PIN_8
                          |GPIO_PIN_9|GPIO_PIN_14|GPIO_PIN_13|GPIO_PIN_12
                          |GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_15);

  HAL_GPIO_DeInit(GPIOF, GPIO_PIN_1|GPIO_PIN_3|GPIO_PIN_0|GPIO_PIN_2
                          |GPIO_PIN_5|GPIO_PIN_4|GPIO_PIN_13|GPIO_PIN_14
                          |GPIO_PIN_15|GPIO_PIN_11|GPIO_PIN_12);

  HAL_GPIO_DeInit(GPIOG, GPIO_PIN_15|GPIO_PIN_4|GPIO_PIN_8|GPIO_PIN_2
                          |GPIO_PIN_5|GPIO_PIN_0|GPIO_PIN_1);

  HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0|GPIO_PIN_3|GPIO_PIN_2);

  /* USER CODE BEGIN FMC_MspDeInit 1 */

  /* USER CODE END FMC_MspDeInit 1 */
}

void HAL_SDRAM_MspDeInit(SDRAM_HandleTypeDef* sdramHandle){
  /* USER CODE BEGIN SDRAM_MspDeInit 0 */

  /* USER CODE END SDRAM_MspDeInit 0 */
  HAL_FMC_MspDeInit();
  /* USER CODE BEGIN SDRAM_MspDeInit 1 */

  /* USER CODE END SDRAM_MspDeInit 1 */
}
/**
  * @}
  */

/**
  * @}
  */
