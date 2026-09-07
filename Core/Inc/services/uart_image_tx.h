#ifndef UART_IMAGE_TX_H
#define UART_IMAGE_TX_H

#include "stm32h7xx_hal.h"
#include "fatfs.h"
#include "services/image_packet.h"

#define IMAGE_TX_STATUS_IDLE      0U
#define IMAGE_TX_STATUS_SENDING   1U
#define IMAGE_TX_STATUS_COMPLETE  2U
#define IMAGE_TX_STATUS_ERROR     3U

extern volatile uint8_t image_tx_active;
extern volatile uint8_t image_tx_done;
extern volatile uint8_t image_tx_status;
extern volatile FRESULT image_tx_fresult;
extern volatile uint16_t image_tx_batch_total;
/* One-based current JPEG index within the last photo batch. */
extern volatile uint16_t image_tx_current_image;
extern volatile uint32_t image_tx_file_size;
extern volatile uint32_t image_tx_packet_total;
extern volatile uint32_t image_tx_packet_index;
extern volatile uint32_t image_tx_bytes_sent;
extern volatile uint32_t image_tx_total_bytes_sent;
extern volatile uint32_t image_tx_current_length;
extern volatile uint8_t image_uart_tx_busy;
extern volatile uint8_t image_uart_packet_pending;
extern volatile uint8_t image_uart_tx_complete;
extern volatile uint32_t image_tx_error_count;
extern volatile uint32_t uart_image_error_code;
extern volatile uint8_t image_uart_error_pending;

void UARTImageTx_Init(void);
FRESULT UARTImageTx_Start(void);
void UARTImageTx_Stop(void);
void UARTImageTx_Process(void);

/* ISR notification hooks.  They never access FatFs. */
void UARTImageTx_OnTxComplete(void);
void UARTImageTx_OnUartError(uint32_t error_code);

#endif /* UART_IMAGE_TX_H */
