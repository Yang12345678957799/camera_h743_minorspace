#include "services/uart_video_tx.h"
#include "usart.h"
#include "services/storage.h"
#include "services/avi_recorder.h"
#include <string.h>

/*
 * AVI file transfer over USART1/RS422, non-blocking.
 *
 * Path: 0:/video/AVI00001.AVI -> f_read -> VideoPacket_Build (886-byte
 * legacy F429 video packet) -> HAL_UART_Transmit_IT -> RS422 -> PC.
 *
 * The UART is shared with the remote control: reception keeps running
 * (interrupt driven, RxState) while packets are transmitted (gState), so the
 * PC can still send commands during a transfer.
 *
 * State machine rules (see phase-2 spec):
 *  - the packet buffer is STATIC memory: it must stay valid until
 *    HAL_UART_TxCpltCallback fires;
 *  - a packet is only counted as sent after that callback;
 *  - if HAL_UART_Transmit_IT returns HAL_BUSY the packet stays pending and
 *    the SAME packet is retried on the next UARTVideoTx_Process call, so no
 *    868-byte block is ever skipped;
 *  - on a UART error the whole transfer stops (no retransmission for now).
 */

static FIL video_tx_file;
/* Long-lived TX buffer: valid from HAL_UART_Transmit_IT until TxCplt. */
static VideoPacketTypeDef video_uart_packet;
static uint8_t video_tx_raw[VIDEO_PACKET_PAYLOAD_SIZE];

volatile uint8_t video_tx_active = 0U;
volatile uint8_t video_tx_done = 0U;
volatile uint8_t video_tx_status = VIDEO_TX_STATUS_IDLE;
volatile FRESULT video_tx_fresult = FR_NOT_READY;
volatile uint32_t video_tx_file_size = 0U;
volatile uint32_t video_tx_packet_total = 0U;
volatile uint32_t video_tx_packet_index = 0U;
volatile uint32_t video_tx_bytes_sent = 0U;
volatile uint32_t video_tx_current_length = 0U;
volatile uint8_t video_uart_tx_busy = 0U;
volatile uint8_t video_uart_packet_pending = 0U;
volatile uint8_t video_uart_tx_complete = 0U;
volatile uint32_t video_tx_error_count = 0U;
volatile uint32_t uart_video_error_code = 0U;
volatile uint8_t uart_video_error_pending = 0U;

volatile uint32_t recorded_file_size = 0U;
volatile uint8_t recorded_file_valid = 0U;
volatile FRESULT recorded_file_check_result = FR_NOT_READY;

void UARTVideoTx_Init(void)
{
  video_tx_active = 0U;
  video_tx_done = 0U;
  video_tx_status = VIDEO_TX_STATUS_IDLE;
  video_uart_tx_busy = 0U;
  video_uart_packet_pending = 0U;
  video_uart_tx_complete = 0U;
  video_tx_fresult = FR_NOT_READY;
}

/* Main-loop context only: closes the file and leaves the error state. */
static void UARTVideoTx_Abort(void)
{
  (void)f_close(&video_tx_file);
  video_tx_active = 0U;
  video_uart_tx_busy = 0U;
  video_uart_packet_pending = 0U;
  video_tx_status = VIDEO_TX_STATUS_ERROR;
}

FRESULT UARTVideoTx_Start(void)
{
  FRESULT result;

  if (video_tx_active != 0U)
  {
    return FR_LOCKED;
  }
  /* Recording running or a start/stop still pending: do not touch the file.
   * (Reading it while the recorder rewrites it with FA_CREATE_ALWAYS would
   * destroy the transfer mid-file.) */
  if ((avi_record_active != 0U) || (avi_record_request == 1U) ||
      (avi_record_request == 2U) || (avi_record_request == 3U))
  {
    return FR_LOCKED;
  }
  if (storage_mount_status != FR_OK)
  {
    result = Storage_Mount();
    if (result != FR_OK)
    {
      return result;
    }
  }
  if (recorded_file_valid == 0U)
  {
    return FR_NOT_READY;
  }

  result = f_open(&video_tx_file, VIDEO_TX_PATH, FA_READ);
  if (result != FR_OK)
  {
    return result;
  }

  video_tx_file_size = (uint32_t)f_size(&video_tx_file);
  if (video_tx_file_size == 0U)
  {
    (void)f_close(&video_tx_file);
    return FR_NO_FILE;
  }

  video_tx_packet_total =
      (video_tx_file_size + VIDEO_PACKET_PAYLOAD_SIZE - 1U) /
      VIDEO_PACKET_PAYLOAD_SIZE;
  video_tx_packet_index = 0U;
  video_tx_bytes_sent = 0U;
  video_tx_current_length = 0U;
  video_tx_done = 0U;
  video_uart_tx_busy = 0U;
  video_uart_packet_pending = 0U;
  video_uart_tx_complete = 0U;
  video_tx_fresult = FR_OK;
  video_tx_active = 1U;
  video_tx_status = VIDEO_TX_STATUS_SENDING;
  return FR_OK;
}

void UARTVideoTx_Stop(void)
{
  if (video_tx_active == 0U)
  {
    return;
  }
  (void)f_close(&video_tx_file);
  video_tx_active = 0U;
  video_uart_tx_busy = 0U;
  video_uart_packet_pending = 0U;
  video_tx_status = VIDEO_TX_STATUS_IDLE;
}

void UARTVideoTx_Process(void)
{
  uint32_t chunk;
  UINT bytes_read = 0U;
  HAL_StatusTypeDef hal_status;

  if (video_tx_active == 0U)
  {
    return;
  }

  /* 1) UART error flagged by HAL_UART_ErrorCallback (ISR context). */
  if (uart_video_error_pending != 0U)
  {
    uart_video_error_pending = 0U;
    video_tx_error_count++;
    UARTVideoTx_Abort();
    return;
  }

  /* 2) Previous packet fully shifted out: only now does it count as sent. */
  if (video_uart_tx_complete != 0U)
  {
    video_uart_tx_complete = 0U;
    video_uart_tx_busy = 0U;
    if (video_uart_packet_pending != 0U)
    {
      video_uart_packet_pending = 0U;
      video_tx_bytes_sent += video_tx_current_length;
      video_tx_packet_index++;
    }
  }

  /* 3) All packets sent? */
  if ((video_uart_packet_pending == 0U) &&
      (video_tx_bytes_sent >= video_tx_file_size))
  {
    video_tx_fresult = f_close(&video_tx_file);
    video_tx_active = 0U;
    video_tx_done = 1U;
    video_tx_status = (video_tx_fresult == FR_OK) ? VIDEO_TX_STATUS_COMPLETE
                                                 : VIDEO_TX_STATUS_ERROR;
    return;
  }

  /* 4) Read + build the next packet if none is pending. */
  if (video_uart_packet_pending == 0U)
  {
    chunk = video_tx_file_size - video_tx_bytes_sent;
    if (chunk > VIDEO_PACKET_PAYLOAD_SIZE)
    {
      chunk = VIDEO_PACKET_PAYLOAD_SIZE;
    }

    video_tx_fresult = f_read(&video_tx_file, video_tx_raw, (UINT)chunk,
                              &bytes_read);
    if ((video_tx_fresult != FR_OK) || (bytes_read != (UINT)chunk))
    {
      video_tx_error_count++;
      UARTVideoTx_Abort();
      return;
    }

    VideoPacket_Build(&video_uart_packet, video_tx_raw, (uint16_t)chunk,
                      video_tx_packet_index);
    video_tx_current_length = chunk;
    video_uart_packet_pending = 1U;
  }

  /* 5) Hand the pending packet to the UART. HAL_BUSY keeps the packet
   * pending: the SAME packet is retried next call, nothing is skipped. */
  if (video_uart_tx_busy == 0U)
  {
    hal_status = HAL_UART_Transmit_IT(&huart1,
                                      (uint8_t *)&video_uart_packet,
                                      sizeof(video_uart_packet));
    if (hal_status == HAL_OK)
    {
      video_uart_tx_busy = 1U;
    }
    else if (hal_status != HAL_BUSY)
    {
      video_tx_error_count++;
      uart_video_error_code = (uint32_t)hal_status;
      UARTVideoTx_Abort();
    }
    else
    {
      /* HAL_BUSY: retry on the next call. */
    }
  }
}

FRESULT UARTVideoTx_VerifyRecordedFile(void)
{
  FIL file;
  FILINFO info;
  UINT bytes_read = 0U;
  uint8_t header[12];
  FRESULT result;

  recorded_file_size = 0U;
  recorded_file_valid = 0U;
  recorded_file_check_result = FR_NOT_READY;

  if (storage_mount_status != FR_OK)
  {
    result = Storage_Mount();
    if (result != FR_OK)
    {
      recorded_file_check_result = result;
      return result;
    }
  }

  /* Read-only checks: f_stat + first 12 bytes, the file is not modified. */
  result = f_stat(VIDEO_TX_PATH, &info);
  if (result != FR_OK)
  {
    recorded_file_check_result = result;
    return result;
  }
  recorded_file_size = (uint32_t)info.fsize;
  if (recorded_file_size == 0U)
  {
    recorded_file_check_result = FR_NO_FILE;
    return FR_NO_FILE;
  }

  result = f_open(&file, VIDEO_TX_PATH, FA_READ);
  if (result != FR_OK)
  {
    recorded_file_check_result = result;
    return result;
  }
  result = f_read(&file, header, sizeof(header), &bytes_read);
  (void)f_close(&file);
  if (result != FR_OK)
  {
    recorded_file_check_result = result;
    return result;
  }

  /* AVI identity: "RIFF" at 0..3, "AVI " at 8..11. */
  if ((bytes_read == sizeof(header)) &&
      (memcmp(header, "RIFF", 4U) == 0) &&
      (memcmp(&header[8], "AVI ", 4U) == 0))
  {
    recorded_file_valid = 1U;
    recorded_file_check_result = FR_OK;
    return FR_OK;
  }

  recorded_file_check_result = FR_NO_FILE;
  return FR_NO_FILE;
}

/* ISR context: keep this as light as the phase-2 spec demands. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    video_uart_tx_complete = 1U;
  }
}
