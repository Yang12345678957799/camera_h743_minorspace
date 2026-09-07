#include "services/remote_control.h"
#include "usart.h"
#include "services/avi_recorder.h"
#include "services/uart_video_tx.h"
#include <string.h>

/*
 * USART1 remote control.
 *
 * Reception is interrupt-driven (one byte per HAL_UART_Receive_IT) so no
 * command bytes are lost while the main loop is blocked in SD-card writes
 * during AVI recording. Bytes land in a ring buffer; RemoteControl_Process()
 * scans it for 6-byte frames, verifies the header and the checksum, and
 * executes the command. Reception stays alive while the AVI transfer is
 * running (HAL gState/RxState are independent).
 *
 * The ISR only stores bytes; commands are executed in the main loop:
 *   REC_START -> avi_record_request = 1 (same mechanism as Keil Watch)
 *   REC_STOP  -> avi_record_request = 3 (stops at the next frame boundary)
 *   VIDEO_TRANSFER_START -> UARTVideoTx_Start() (opens the AVI, packets are
 *                           sent by UARTVideoTx_Process)
 *
 * Short ASCII responses ("OK ..." / "ERR ...") are sent back for bench
 * debugging with a plain serial terminal; they silently fail (HAL_BUSY)
 * while a video packet transmission is in flight.
 */

#define RC_RING_SIZE 64U   /* power of two */
#define RC_RING_MASK (RC_RING_SIZE - 1U)

volatile uint8_t remote_last_command = REMOTE_CMD_NONE;
volatile uint32_t remote_rx_bytes = 0U;
volatile uint32_t remote_valid_frames = 0U;
volatile uint32_t remote_unknown_frames = 0U;
volatile uint32_t remote_checksum_errors = 0U;
volatile uint32_t remote_rx_errors = 0U;
volatile uint32_t remote_rx_overruns = 0U;

/* rc_head is written by the ISR only, rc_tail by the main loop only:
 * single-producer / single-consumer, safe without a critical section. */
static volatile uint16_t rc_head = 0U;
static volatile uint16_t rc_tail = 0U;
static volatile uint8_t rc_ring[RC_RING_SIZE];
static uint8_t rc_rx_byte;

static void RC_Respond(const char *text)
{
  (void)HAL_UART_Transmit(&huart1, (const uint8_t *)text,
                          (uint16_t)strlen(text), 100U);
}

void RemoteControl_Init(void)
{
  rc_head = 0U;
  rc_tail = 0U;
  if (HAL_UART_Receive_IT(&huart1, &rc_rx_byte, 1U) != HAL_OK)
  {
    remote_rx_errors++;
  }
}

/* Execute one verified frame (class = byte 2, cmd = byte 4). */
static void RC_Execute(uint8_t frame_class, uint8_t cmd)
{
  if ((frame_class == 0x02U) && (cmd == REMOTE_CMD_REC_START))
  {
    remote_last_command = REMOTE_CMD_REC_START;
    if (video_tx_active != 0U)
    {
      RC_Respond("ERR BUSY\r\n");           /* file transfer in progress */
    }
    else if (avi_record_active != 0U)
    {
      RC_Respond("OK REC_ALREADY\r\n");
    }
    else
    {
      avi_record_request = 1U;
      RC_Respond("OK REC_START\r\n");
    }
  }
  else if ((frame_class == 0x02U) && (cmd == REMOTE_CMD_REC_STOP))
  {
    remote_last_command = REMOTE_CMD_REC_STOP;
    if (avi_record_active != 0U)
    {
      avi_record_request = 3U;               /* stops at the next frame */
      RC_Respond("OK REC_STOP\r\n");
    }
    else
    {
      RC_Respond("ERR NOT_RECORDING\r\n");
    }
  }
  else if ((frame_class == 0x14U) && (cmd == REMOTE_CMD_VIDEO_TRANSFER_START))
  {
    remote_last_command = REMOTE_CMD_VIDEO_TRANSFER_START;
    if (UARTVideoTx_Start() == FR_OK)
    {
      RC_Respond("OK VIDEO_TX\r\n");
    }
    else
    {
      RC_Respond("ERR VIDEO_TX\r\n");
    }
  }
  else
  {
    remote_last_command = REMOTE_CMD_UNKNOWN;
    remote_unknown_frames++;
    RC_Respond("ERR CMD\r\n");
  }
}

void RemoteControl_Process(void)
{
  uint8_t frame[RC_FRAME_SIZE];
  uint32_t sum;
  uint8_t index;

  while ((uint16_t)(rc_head - rc_tail) >= RC_FRAME_SIZE)
  {
    for (index = 0U; index < RC_FRAME_SIZE; index++)
    {
      frame[index] = rc_ring[(uint16_t)(rc_tail + index) & RC_RING_MASK];
    }

    if ((frame[0] == RC_HEADER0) && (frame[1] == RC_HEADER1))
    {
      sum = (uint32_t)frame[0] + frame[1] + frame[2] + frame[3] + frame[4];
      if ((uint8_t)sum == frame[5])
      {
        remote_valid_frames++;
        rc_tail = (uint16_t)(rc_tail + RC_FRAME_SIZE);
        RC_Execute(frame[2], frame[4]);
        continue;
      }
      remote_checksum_errors++;
    }

    /* No header or bad checksum: resynchronise one byte at a time. */
    rc_tail = (uint16_t)(rc_tail + 1U);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART1)
  {
    return;
  }

  remote_rx_bytes++;

  if ((uint16_t)(rc_head - rc_tail) < RC_RING_SIZE)
  {
    rc_ring[rc_head & RC_RING_MASK] = rc_rx_byte;
    rc_head = (uint16_t)(rc_head + 1U);
  }
  else
  {
    remote_rx_overruns++;                    /* main loop stalled: drop byte */
  }

  if (HAL_UART_Receive_IT(&huart1, &rc_rx_byte, 1U) != HAL_OK)
  {
    remote_rx_errors++;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART1)
  {
    return;
  }
  /* Overrun or noise: restart byte reception so the link recovers. If a
   * video transfer is running, only flag the error here; the main loop
   * (UARTVideoTx_Process) does the f_close. */
  remote_rx_errors++;
  if (video_tx_active != 0U)
  {
    uart_video_error_code = huart->ErrorCode;
    uart_video_error_pending = 1U;
  }
  if (HAL_UART_Receive_IT(&huart1, &rc_rx_byte, 1U) != HAL_OK)
  {
    remote_rx_errors++;
  }
}
