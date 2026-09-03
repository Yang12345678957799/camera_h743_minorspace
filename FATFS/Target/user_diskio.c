/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
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

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "sdmmc.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

volatile uint32_t sd_disk_last_hal_status = HAL_ERROR;
volatile uint32_t sd_disk_last_hal_error = 0U;
volatile uint32_t sd_disk_card_state = 0U;
volatile uint32_t sd_disk_last_result = RES_NOTRDY;

/* 等待 SD 卡完成上一项读写操作，回到可继续传输的状态。 */
static HAL_StatusTypeDef USER_WaitForTransfer(uint32_t timeout_ms)
{
  uint32_t tickstart = HAL_GetTick();

  sd_disk_card_state = HAL_SD_GetCardState(&hsd1);
  while (sd_disk_card_state != HAL_SD_CARD_TRANSFER)
  {
    if ((HAL_GetTick() - tickstart) >= timeout_ms)
    {
      sd_disk_last_hal_status = HAL_TIMEOUT;
      sd_disk_last_hal_error = HAL_SD_GetError(&hsd1);
      return HAL_TIMEOUT;
    }
    sd_disk_card_state = HAL_SD_GetCardState(&hsd1);
  }

  sd_disk_last_hal_status = HAL_OK;
  sd_disk_last_hal_error = HAL_SD_GetError(&hsd1);
  return HAL_OK;
}

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }

  /* MX_SDMMC1_SD_Init() 已在 main() 中完成卡和 4-bit 总线初始化。 */
  Stat = (USER_WaitForTransfer(1000U) == HAL_OK) ? 0U : STA_NOINIT;
  sd_disk_last_result = ((Stat == 0U) ? RES_OK : RES_NOTRDY);
  return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }

  return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
  if ((pdrv != 0U) || (buff == NULL) || (count == 0U) || ((Stat & STA_NOINIT) != 0U))
  {
    sd_disk_last_result = RES_PARERR;
    return RES_PARERR;
  }

  sd_disk_last_hal_status = HAL_SD_ReadBlocks(&hsd1, (uint8_t *)buff, sector, count, 5000U);
  sd_disk_last_hal_error = HAL_SD_GetError(&hsd1);
  sd_disk_card_state = HAL_SD_GetCardState(&hsd1);
  if (sd_disk_last_hal_status != HAL_OK)
  {
    sd_disk_last_result = RES_ERROR;
    return RES_ERROR;
  }

  sd_disk_last_result = (USER_WaitForTransfer(5000U) == HAL_OK) ? RES_OK : RES_ERROR;
  return (DRESULT)sd_disk_last_result;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
  if ((pdrv != 0U) || (buff == NULL) || (count == 0U) || ((Stat & STA_NOINIT) != 0U))
  {
    return RES_PARERR;
  }

  sd_disk_last_hal_status = HAL_SD_WriteBlocks(&hsd1, (uint8_t *)buff, sector, count, 5000U);
  sd_disk_last_hal_error = HAL_SD_GetError(&hsd1);
  sd_disk_card_state = HAL_SD_GetCardState(&hsd1);
  if (sd_disk_last_hal_status != HAL_OK)
  {
    sd_disk_last_result = RES_ERROR;
    return RES_ERROR;
  }

  sd_disk_last_result = (USER_WaitForTransfer(5000U) == HAL_OK) ? RES_OK : RES_ERROR;
  return (DRESULT)sd_disk_last_result;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
  HAL_SD_CardInfoTypeDef card_info;

  if ((pdrv != 0U) || ((Stat & STA_NOINIT) != 0U))
  {
    return RES_NOTRDY;
  }

  switch (cmd)
  {
    case CTRL_SYNC:
      return (USER_WaitForTransfer(5000U) == HAL_OK) ? RES_OK : RES_ERROR;

    case GET_SECTOR_COUNT:
      if ((buff == NULL) || (HAL_SD_GetCardInfo(&hsd1, &card_info) != HAL_OK))
      {
        return RES_ERROR;
      }
      *(DWORD *)buff = card_info.LogBlockNbr;
      return RES_OK;

    case GET_SECTOR_SIZE:
      if ((buff == NULL) || (HAL_SD_GetCardInfo(&hsd1, &card_info) != HAL_OK))
      {
        return RES_ERROR;
      }
      *(WORD *)buff = (WORD)card_info.LogBlockSize;
      return RES_OK;

    case GET_BLOCK_SIZE:
      if (buff == NULL)
      {
        return RES_PARERR;
      }
      /* 对本工程仅需文件读写；FatFs 格式化时按单逻辑扇区处理。 */
      *(DWORD *)buff = 1U;
      return RES_OK;

    default:
      return RES_PARERR;
  }
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

