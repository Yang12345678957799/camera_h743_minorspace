#ifndef REMOTE_CONTROL_H
#define REMOTE_CONTROL_H

#include "stm32h7xx_hal.h"

/* Remote-control frame on USART1 (115200 8N1), variable length:
 *
 *   EB 90 | class | payload_length | payload... | checksum
 *
 * total length = 5 + payload_length; checksum is the low byte of the sum of
 * every preceding byte.  Existing one-byte payload commands remain 6 bytes:
 *   EB 90 02 01 53 D1  ->  start recording
 *   EB 90 02 01 54 D2  ->  stop recording
 *   EB 90 14 01 11 A1  ->  start AVI file transfer to the PC ("视频传输LVDS"
 *                          in the legacy host software; the physical link is
 *                          USART1/RS422, no LVDS hardware involved)
 *   EB 90 02 01 45 C3  ->  capture one JPEG (F429-compatible)
 *   EB 90 02 02 45 NN CS -> capture NN JPEGs; CS sums EB through NN
 *   EB 90 14 01 33 C3  ->  transfer the latest completed JPEG batch
 *
 * EB 90 14 01 22 B2 (abort transfer) is defined on the F429 side but is NOT
 * accepted in this phase; UARTVideoTx_Stop() stays available internally so
 * the command can be wired up later without further changes. */
#define RC_HEADER0        0xEBU
#define RC_HEADER1        0x90U
#define RC_MAX_PAYLOAD    8U
#define RC_MIN_FRAME_SIZE 5U

/* Command codes carried in frame byte 4. */
#define REMOTE_CMD_NONE                  0U
#define REMOTE_CMD_REC_START             0x53U
#define REMOTE_CMD_REC_STOP              0x54U
#define REMOTE_CMD_VIDEO_TRANSFER_START  0x11U
#define REMOTE_CMD_PHOTO_CAPTURE          0x45U
#define REMOTE_CMD_IMAGE_TRANSFER_START   0x33U
#define REMOTE_CMD_UNKNOWN               0xFFU

/* Last accepted command code, for Keil Watch. */
extern volatile uint8_t remote_last_command;
/* Total bytes received / valid frames / unknown commands / checksum
 * failures / reception errors / ring-buffer overruns. */
extern volatile uint32_t remote_rx_bytes;
extern volatile uint32_t remote_valid_frames;
extern volatile uint32_t remote_unknown_frames;
extern volatile uint32_t remote_checksum_errors;
extern volatile uint32_t remote_rx_errors;
extern volatile uint32_t remote_rx_overruns;

/* Start interrupt reception of command bytes on USART1. */
void RemoteControl_Init(void);

/* Scan the receive ring buffer for frames and execute commands.
 * Call from the main loop. */
void RemoteControl_Process(void);

#endif /* REMOTE_CONTROL_H */
