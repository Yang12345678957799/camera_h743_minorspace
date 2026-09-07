#include "services/uart_image_tx.h"
#include "app/app_camera.h"
#include "services/avi_recorder.h"
#include "services/storage.h"
#include "services/uart_video_tx.h"
#include "usart.h"

static FIL image_tx_file;
static uint8_t image_tx_file_open = 0U;
static ImagePacketTypeDef image_uart_packet;
static uint8_t image_tx_raw[IMAGE_PACKET_PAYLOAD_SIZE];

volatile uint8_t image_tx_active = 0U;
volatile uint8_t image_tx_done = 0U;
volatile uint8_t image_tx_status = IMAGE_TX_STATUS_IDLE;
volatile FRESULT image_tx_fresult = FR_NOT_READY;
volatile uint16_t image_tx_batch_total = 0U;
volatile uint16_t image_tx_current_image = 0U;
volatile uint32_t image_tx_file_size = 0U;
volatile uint32_t image_tx_packet_total = 0U;
volatile uint32_t image_tx_packet_index = 0U;
volatile uint32_t image_tx_bytes_sent = 0U;
volatile uint32_t image_tx_total_bytes_sent = 0U;
volatile uint32_t image_tx_current_length = 0U;
volatile uint8_t image_uart_tx_busy = 0U;
volatile uint8_t image_uart_packet_pending = 0U;
volatile uint8_t image_uart_tx_complete = 0U;
volatile uint32_t image_tx_error_count = 0U;
volatile uint32_t uart_image_error_code = 0U;
volatile uint8_t image_uart_error_pending = 0U;

static void UARTImageTx_MakePath(uint16_t image_index, TCHAR *path)
{
  uint16_t value = image_index;

  /* 0:/img/IMG00001.JPG: exactly legal 8.3, plus trailing NUL. */
  path[0] = '0'; path[1] = ':'; path[2] = '/'; path[3] = 'i';
  path[4] = 'm'; path[5] = 'g'; path[6] = '/'; path[7] = 'I';
  path[8] = 'M'; path[9] = 'G';
  path[15] = '.'; path[16] = 'J'; path[17] = 'P'; path[18] = 'G';
  path[19] = '\0';
  path[14] = (TCHAR)('0' + (value % 10U)); value /= 10U;
  path[13] = (TCHAR)('0' + (value % 10U)); value /= 10U;
  path[12] = (TCHAR)('0' + (value % 10U)); value /= 10U;
  path[11] = (TCHAR)('0' + (value % 10U)); value /= 10U;
  path[10] = (TCHAR)('0' + (value % 10U));
}

static FRESULT UARTImageTx_OpenCurrentFile(void)
{
  TCHAR path[20];
  FRESULT result;

  UARTImageTx_MakePath(image_tx_current_image, path);
  result = f_open(&image_tx_file, path, FA_READ);
  if (result != FR_OK)
  {
    return result;
  }
  image_tx_file_open = 1U;
  image_tx_file_size = (uint32_t)f_size(&image_tx_file);
  if (image_tx_file_size == 0U)
  {
    (void)f_close(&image_tx_file);
    image_tx_file_open = 0U;
    return FR_NO_FILE;
  }
  image_tx_packet_total = (image_tx_file_size + IMAGE_PACKET_PAYLOAD_SIZE - 1U) /
                          IMAGE_PACKET_PAYLOAD_SIZE;
  image_tx_packet_index = 0U;
  image_tx_bytes_sent = 0U;
  image_tx_current_length = 0U;
  return FR_OK;
}

static void UARTImageTx_Abort(void)
{
  if (image_tx_file_open != 0U)
  {
    (void)f_close(&image_tx_file);
    image_tx_file_open = 0U;
  }
  image_tx_active = 0U;
  image_uart_tx_busy = 0U;
  image_uart_packet_pending = 0U;
  image_tx_status = IMAGE_TX_STATUS_ERROR;
}

void UARTImageTx_Init(void)
{
  image_tx_active = 0U;
  image_tx_done = 0U;
  image_tx_status = IMAGE_TX_STATUS_IDLE;
  image_tx_fresult = FR_NOT_READY;
  image_uart_tx_busy = 0U;
  image_uart_packet_pending = 0U;
  image_uart_tx_complete = 0U;
  image_uart_error_pending = 0U;
  image_tx_file_open = 0U;
}

FRESULT UARTImageTx_Start(void)
{
  FRESULT result;

  if ((image_tx_active != 0U) || (video_tx_active != 0U) ||
      (photo_capture_active != 0U) || (avi_record_active != 0U) ||
      (avi_record_request == 1U) || (avi_record_request == 2U) ||
      (avi_record_request == 3U))
  {
    return FR_LOCKED;
  }
  if ((photo_batch_valid == 0U) || (photo_batch_completed_count == 0U))
  {
    return FR_NOT_READY;
  }
  if (storage_mount_status != FR_OK)
  {
    result = Storage_Mount();
    if (result != FR_OK)
    {
      return result;
    }
  }

  image_tx_batch_total = photo_batch_completed_count;
  image_tx_current_image = 1U;
  image_tx_total_bytes_sent = 0U;
  image_tx_done = 0U;
  image_uart_tx_busy = 0U;
  image_uart_packet_pending = 0U;
  image_uart_tx_complete = 0U;
  image_uart_error_pending = 0U;
  image_tx_fresult = UARTImageTx_OpenCurrentFile();
  if (image_tx_fresult != FR_OK)
  {
    return image_tx_fresult;
  }

  image_tx_active = 1U;
  image_tx_status = IMAGE_TX_STATUS_SENDING;
  return FR_OK;
}

void UARTImageTx_Stop(void)
{
  if (image_tx_active == 0U)
  {
    return;
  }
  if (image_tx_file_open != 0U)
  {
    (void)f_close(&image_tx_file);
    image_tx_file_open = 0U;
  }
  image_tx_active = 0U;
  image_uart_tx_busy = 0U;
  image_uart_packet_pending = 0U;
  image_tx_status = IMAGE_TX_STATUS_IDLE;
}

void UARTImageTx_Process(void)
{
  uint32_t chunk;
  UINT bytes_read = 0U;
  HAL_StatusTypeDef hal_status;

  if (image_tx_active == 0U)
  {
    return;
  }

  if (image_uart_error_pending != 0U)
  {
    image_uart_error_pending = 0U;
    image_tx_error_count++;
    UARTImageTx_Abort();
    return;
  }

  if (image_uart_tx_complete != 0U)
  {
    image_uart_tx_complete = 0U;
    image_uart_tx_busy = 0U;
    if (image_uart_packet_pending != 0U)
    {
      image_uart_packet_pending = 0U;
      image_tx_bytes_sent += image_tx_current_length;
      image_tx_total_bytes_sent += image_tx_current_length;
      image_tx_packet_index++;
    }
  }

  if ((image_uart_packet_pending == 0U) &&
      (image_tx_bytes_sent >= image_tx_file_size))
  {
    image_tx_fresult = f_close(&image_tx_file);
    image_tx_file_open = 0U;
    if (image_tx_fresult != FR_OK)
    {
      image_tx_error_count++;
      UARTImageTx_Abort();
      return;
    }
    if (image_tx_current_image >= image_tx_batch_total)
    {
      image_tx_active = 0U;
      image_tx_done = 1U;
      image_tx_status = IMAGE_TX_STATUS_COMPLETE;
      return;
    }
    image_tx_current_image++;
    image_tx_fresult = UARTImageTx_OpenCurrentFile();
    if (image_tx_fresult != FR_OK)
    {
      image_tx_error_count++;
      UARTImageTx_Abort();
      return;
    }
  }

  if (image_uart_packet_pending == 0U)
  {
    chunk = image_tx_file_size - image_tx_bytes_sent;
    if (chunk > IMAGE_PACKET_PAYLOAD_SIZE)
    {
      chunk = IMAGE_PACKET_PAYLOAD_SIZE;
    }
    image_tx_fresult = f_read(&image_tx_file, image_tx_raw, (UINT)chunk,
                              &bytes_read);
    if ((image_tx_fresult != FR_OK) || (bytes_read != (UINT)chunk))
    {
      image_tx_error_count++;
      UARTImageTx_Abort();
      return;
    }
    ImagePacket_Build(&image_uart_packet, image_tx_raw, (uint16_t)chunk,
                      image_tx_packet_index, image_tx_packet_total);
    image_tx_current_length = chunk;
    image_uart_packet_pending = 1U;
  }

  if (image_uart_tx_busy == 0U)
  {
    hal_status = HAL_UART_Transmit_IT(&huart1, (uint8_t *)&image_uart_packet,
                                      sizeof(image_uart_packet));
    if (hal_status == HAL_OK)
    {
      image_uart_tx_busy = 1U;
    }
    else if (hal_status != HAL_BUSY)
    {
      image_tx_error_count++;
      uart_image_error_code = (uint32_t)hal_status;
      UARTImageTx_Abort();
    }
    /* HAL_BUSY retains this exact static packet for retry next iteration. */
  }
}

void UARTImageTx_OnTxComplete(void)
{
  if (image_tx_active != 0U)
  {
    image_uart_tx_complete = 1U;
  }
}

void UARTImageTx_OnUartError(uint32_t error_code)
{
  if (image_tx_active != 0U)
  {
    uart_image_error_code = error_code;
    image_uart_error_pending = 1U;
  }
}
