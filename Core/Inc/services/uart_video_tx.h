#ifndef UART_VIDEO_TX_H
#define UART_VIDEO_TX_H

#include "stm32h7xx_hal.h"
#include "fatfs.h"
#include "services/video_packet.h"

/* Source file: the AVI written by avi_recorder (8.3 short name, LFN is off). */
#define VIDEO_TX_PATH "0:/video/AVI00001.AVI"

/* video_tx_status values (plain numbers for Keil Watch). */
#define VIDEO_TX_STATUS_IDLE      0U
#define VIDEO_TX_STATUS_SENDING   1U
#define VIDEO_TX_STATUS_COMPLETE  2U
#define VIDEO_TX_STATUS_ERROR    3U

/* ---- transfer state, all observable in Keil Watch ---- */
/* 1 while the file transfer is running. */
extern volatile uint8_t video_tx_active;
/* 1 after the last packet has been acknowledged by HAL_UART_TxCpltCallback. */
extern volatile uint8_t video_tx_done;
/* VIDEO_TX_STATUS_xxx. */
extern volatile uint8_t video_tx_status;
/* Last FatFs result inside the transfer (open/read/close). */
extern volatile FRESULT video_tx_fresult;

extern volatile uint32_t video_tx_file_size;
/* Internal progress variable: the legacy video packet has NO total field. */
extern volatile uint32_t video_tx_packet_total;
extern volatile uint32_t video_tx_packet_index;
extern volatile uint32_t video_tx_bytes_sent;
/* Payload length of the packet currently in flight (<= 868). */
extern volatile uint32_t video_tx_current_length;

/* 1 while HAL_UART_Transmit_IT owns the UART. */
extern volatile uint8_t video_uart_tx_busy;
/* 1 when a packet is built but not yet handed to the UART. */
extern volatile uint8_t video_uart_packet_pending;
/* Set by HAL_UART_TxCpltCallback, consumed by UARTVideoTx_Process. */
extern volatile uint8_t video_uart_tx_complete;

/* Number of UART transfer errors; last HAL error code seen. */
extern volatile uint32_t video_tx_error_count;
extern volatile uint32_t uart_video_error_code;
/* Set by HAL_UART_ErrorCallback, handled in the main loop. */
extern volatile uint8_t uart_video_error_pending;

/* ---- recorded file verification (filled after AVI_RecorderStop) ---- */
/* f_stat size of 0:/video/AVI00001.AVI. */
extern volatile uint32_t recorded_file_size;
/* 1 when f_stat succeeded, size > 0 and the header is "RIFF"..."AVI ". */
extern volatile uint8_t recorded_file_valid;
/* FRESULT of the verification (FR_OK == all checks passed). */
extern volatile FRESULT recorded_file_check_result;

/* Reset the transfer state. */
void UARTVideoTx_Init(void);
/* Open the AVI and enter SENDING. Refused while recording or already active;
 * requires recorded_file_valid == 1. */
FRESULT UARTVideoTx_Start(void);
/* Abort the transfer and close the file. */
void UARTVideoTx_Stop(void);
/* State machine: finishes a packet, reads+builds the next one, kicks
 * HAL_UART_Transmit_IT. Call from the main loop. */
void UARTVideoTx_Process(void);

/* Basic integrity check of the recorded AVI (f_stat + first 12 bytes).
 * Read-only; fills recorded_file_size / recorded_file_valid /
 * recorded_file_check_result. Call after AVI_RecorderStop succeeded. */
FRESULT UARTVideoTx_VerifyRecordedFile(void);

#endif /* UART_VIDEO_TX_H */
