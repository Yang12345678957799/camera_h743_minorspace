#include "services/lvds_tx.h"
#include "main.h"
#include "spi.h"
#include <string.h>

/*
 * 与原 F429 工程兼容的 LVDS 数据包：
 * 帧头 + 包序号 + 类型 + 总包数 + 当前包号 + 长度 + 862字节数据 + 校验 + 帧尾。
 */
#pragma pack(push, 1)
typedef struct
{
  uint8_t head[4];
  uint8_t count[2];
  uint8_t type[2];
  uint8_t total[4];
  uint8_t current[4];
  uint8_t length[2];
  uint8_t data[LVDS_PAYLOAD_SIZE];
  uint8_t checksum[2];
  uint8_t tail[4];
} LVDS_PacketTypeDef;
#pragma pack(pop)

volatile HAL_StatusTypeDef lvds_tx_status = HAL_ERROR;
volatile uint32_t lvds_packets_sent = 0U;
volatile uint8_t lvds_tx_entered = 0U;
volatile uint8_t lvds_tx_returned = 0U;
/* 静态缓冲区避免在栈中分配 886 字节协议包。 */
static LVDS_PacketTypeDef lvds_packet;

static void LVDS_WriteU32BE(uint8_t *target, uint32_t value)
{
  target[0] = (uint8_t)(value >> 24);
  target[1] = (uint8_t)(value >> 16);
  target[2] = (uint8_t)(value >> 8);
  target[3] = (uint8_t)value;
}

/*
 * LVDS_PacketTypeDef is packed for wire-protocol compatibility. Its data[]
 * member begins at byte offset 18, which is not word aligned. Use byte stores
 * instead of memcpy so Cortex-M7 never performs an unaligned word access.
 */
static void LVDS_CopyPayload(const uint8_t *source, uint16_t length)
{
  uint16_t index;

  for (index = 0U; index < length; index++)
  {
    lvds_packet.data[index] = source[index];
  }
}

static uint16_t LVDS_Checksum(const LVDS_PacketTypeDef *packet)
{
  uint32_t sum = 0U;
  uint32_t index;
  const uint8_t *bytes = (const uint8_t *)packet;

  /* 校验范围与原协议一致：从 type 字段到 data 字段末尾。 */
  for (index = 6U; index < 880U; index++)
  {
    sum += bytes[index];
  }
  return (uint16_t)sum;
}

HAL_StatusTypeDef LVDS_TxInit(void)
{
  /*
   * SPI1 已由 CubeMX 的 MX_SPI1_Init() 初始化：Mode 1、8位、MSB、/8。
   * 此处绝不能再次 DeInit/Init，否则 H7 SPI 的内部状态与 CubeMX 生成
   * 的 TX-only 配置可能不一致，导致 HAL_SPI_Transmit 卡住。
   */
  HAL_GPIO_WritePin(LVDS_CS_GPIO_Port, LVDS_CS_Pin, GPIO_PIN_SET);
  lvds_tx_status = HAL_OK;
  return lvds_tx_status;
}

HAL_StatusTypeDef LVDS_SendBlock(const uint8_t *data, uint16_t length,
                                 uint32_t packet_index, uint32_t packet_total)
{
  uint16_t checksum;

  if ((data == NULL) || (length > LVDS_PAYLOAD_SIZE))
  {
    lvds_tx_status = HAL_ERROR;
    return lvds_tx_status;
  }

  (void)memset(&lvds_packet, 0xAA, sizeof(lvds_packet));
  lvds_packet.head[0] = 0xFCU;
  lvds_packet.head[1] = 0xFCU;
  lvds_packet.head[2] = 0xA1U;
  lvds_packet.head[3] = 0xA1U;
  lvds_packet.count[0] = (uint8_t)(packet_index >> 8);
  lvds_packet.count[1] = (uint8_t)packet_index;
  lvds_packet.type[0] = 0xA5U;
  lvds_packet.type[1] = 0xA5U;
  LVDS_WriteU32BE(lvds_packet.total, packet_total);
  LVDS_WriteU32BE(lvds_packet.current, packet_index);
  lvds_packet.length[0] = (uint8_t)(length >> 8);
  lvds_packet.length[1] = (uint8_t)length;
  LVDS_CopyPayload(data, length);
  checksum = LVDS_Checksum(&lvds_packet);
  lvds_packet.checksum[0] = (uint8_t)(checksum >> 8);
  lvds_packet.checksum[1] = (uint8_t)checksum;
  lvds_packet.tail[0] = 0x1EU;
  lvds_packet.tail[1] = 0x1BU;
  lvds_packet.tail[2] = 0x1EU;
  lvds_packet.tail[3] = 0x1BU;

  /* 软件片选：发送前拉低，发送完成或超时后立即拉高。 */
  lvds_tx_entered = 1U;
  lvds_tx_returned = 0U;
  HAL_GPIO_WritePin(LVDS_CS_GPIO_Port, LVDS_CS_Pin, GPIO_PIN_RESET);
  lvds_tx_status = HAL_SPI_Transmit(&hspi1, (uint8_t *)&lvds_packet,
                                    sizeof(lvds_packet), 100U);
  lvds_tx_returned = 1U;
  HAL_GPIO_WritePin(LVDS_CS_GPIO_Port, LVDS_CS_Pin, GPIO_PIN_SET);
  if (lvds_tx_status == HAL_OK)
  {
    lvds_packets_sent++;
  }
  return lvds_tx_status;
}
