#ifndef LVDS_TX_H
#define LVDS_TX_H

#include "stm32h7xx_hal.h"

/* 单个 LVDS 协议包承载的图像数据长度，源自原 F429 工程协议。 */
#define LVDS_PAYLOAD_SIZE 862U

/* SPI/LVDS 发送的最近一次 HAL 返回状态。 */
extern volatile HAL_StatusTypeDef lvds_tx_status;
/* 仅在一个完整协议包发送成功后加一。 */
extern volatile uint32_t lvds_packets_sent;
/* 发送诊断标记：分别在调用 HAL_SPI_Transmit 前、返回后置 1。 */
extern volatile uint8_t lvds_tx_entered;
extern volatile uint8_t lvds_tx_returned;

/* 初始化 SPI1 及其软件片选引脚的空闲状态。 */
HAL_StatusTypeDef LVDS_TxInit(void);
/* 将一段图像数据封装为 LVDS 协议包并通过 SPI1 发送。 */
HAL_StatusTypeDef LVDS_SendBlock(const uint8_t *data, uint16_t length,
                                 uint32_t packet_index, uint32_t packet_total);

#endif /* LVDS_TX_H */
