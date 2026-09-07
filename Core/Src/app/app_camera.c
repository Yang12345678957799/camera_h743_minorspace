#include "app/app_camera.h"
#include "bsp/ov5640.h"
#include "services/camera_capture.h"
#include "services/lvds_tx.h"
#include "services/storage.h"
#include "services/avi_recorder.h"
#include "services/uart_video_tx.h"
#include "services/uart_image_tx.h"

#define PHOTO_CAPTURE_MAX_INVALID_FRAMES 20U

/* OV5640 芯片 ID；正常值应为 0x5640。 */
volatile uint16_t ov5640_id = 0U;
/* OV5640 的 I2C 探测和寄存器配置状态。 */
volatile HAL_StatusTypeDef ov5640_status = HAL_ERROR;

/* Counters used by Watch while the capture/send loop runs continuously. */
volatile uint32_t camera_frames_sent = 0U;
volatile uint32_t camera_frame_send_errors = 0U;
volatile uint8_t camera_sd_save_request = 0U;
volatile uint8_t camera_sd_save_status = FR_NOT_READY;
volatile uint32_t camera_images_saved = 0U;
volatile uint8_t camera_raw_save_request = 0U;
volatile uint8_t camera_raw_save_status = FR_NOT_READY;
volatile uint32_t camera_raw_images_saved = 0U;
volatile uint8_t camera_jpeg_save_request = 0U;
volatile uint8_t camera_jpeg_save_status = FR_NOT_READY;
volatile uint32_t camera_jpeg_images_saved = 0U;
volatile uint8_t photo_capture_active = 0U;
volatile uint16_t photo_capture_target = 0U;
volatile uint16_t photo_capture_saved = 0U;
volatile uint16_t photo_capture_failed = 0U;
volatile uint16_t photo_capture_invalid_frames = 0U;
volatile uint8_t photo_capture_status = PHOTO_CAPTURE_STATUS_IDLE;
volatile uint8_t photo_batch_valid = 0U;
volatile uint16_t photo_batch_completed_count = 0U;
volatile FRESULT photo_last_save_result = FR_NOT_READY;
volatile uint32_t photo_last_file_size = 0U;
volatile uint8_t photo_capture_done_pending = 0U;
volatile uint8_t photo_capture_error_pending = 0U;

static uint32_t photo_last_stall_recoveries = 0U;

static void App_CameraPhotoMakePath(uint16_t image_index, TCHAR *path)
{
  uint16_t value = image_index;

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

static void App_CameraPhotoFail(FRESULT result)
{
  photo_capture_active = 0U;
  photo_capture_failed++;
  photo_capture_status = PHOTO_CAPTURE_STATUS_ERROR;
  photo_batch_valid = 0U;
  photo_last_save_result = result;
  photo_capture_error_pending = 1U;
}

FRESULT App_CameraStartPhotoCapture(uint16_t count)
{
  if ((count == 0U) || (count > 255U))
  {
    return FR_INVALID_PARAMETER;
  }
  if ((avi_record_active != 0U) || (avi_record_request == 1U) ||
      (avi_record_request == 2U) || (avi_record_request == 3U) ||
      (video_tx_active != 0U) ||
      (image_tx_active != 0U) || (photo_capture_active != 0U))
  {
    return FR_LOCKED;
  }

  photo_capture_target = count;
  photo_capture_saved = 0U;
  photo_capture_failed = 0U;
  photo_capture_invalid_frames = 0U;
  photo_capture_status = PHOTO_CAPTURE_STATUS_CAPTURING;
  photo_batch_valid = 0U;
  photo_batch_completed_count = 0U;
  photo_last_save_result = FR_OK;
  photo_last_file_size = 0U;
  photo_capture_done_pending = 0U;
  photo_capture_error_pending = 0U;
  photo_last_stall_recoveries = camera_capture_stall_recoveries;
  photo_capture_active = 1U;
  return FR_OK;
}

/* Legacy SPI/LVDS RGB565 diagnostic path retained for hardware tests.  Normal
 * JPEG capture, AVI recording and the new photo batch feature do not call it. */
static HAL_StatusTypeDef App_CameraSendFrame(void)
{
  const uint8_t *frame = (const uint8_t *)CAMERA_FRAME_ADDRESS;
  uint32_t packet_total;
  uint32_t packet_index;
  uint32_t byte_offset = 0U;
  uint16_t packet_length;

  packet_total = (CAMERA_FRAME_BYTES + LVDS_PAYLOAD_SIZE - 1U) / LVDS_PAYLOAD_SIZE;

  for (packet_index = 0U; packet_index < packet_total; packet_index++)
  {
    if ((CAMERA_FRAME_BYTES - byte_offset) > LVDS_PAYLOAD_SIZE)
    {
      packet_length = LVDS_PAYLOAD_SIZE;
    }
    else
    {
      packet_length = (uint16_t)(CAMERA_FRAME_BYTES - byte_offset);
    }

    lvds_tx_status = LVDS_SendBlock(&frame[byte_offset], packet_length,
                                    packet_index, packet_total);
    if (lvds_tx_status != HAL_OK)
    {
      return lvds_tx_status;
    }

    byte_offset += packet_length;
  }

  return HAL_OK;
}

/*
 * 完成相机链路初始化。
 * 任一步失败都会立刻返回，具体状态可通过 Watch 查看：
 * ov5640_status、lvds_tx_status、camera_capture_status。
 */
void App_CameraInit(void)
{
  camera_frames_sent = 0U;
  camera_frame_send_errors = 0U;
  camera_images_saved = 0U;
  camera_raw_images_saved = 0U;
  camera_jpeg_images_saved = 0U;
  photo_capture_active = 0U;
  photo_capture_status = PHOTO_CAPTURE_STATUS_IDLE;
  photo_batch_valid = 0U;

  ov5640_status = OV5640_Probe(&ov5640_id);
  if (ov5640_status != HAL_OK)
  {
    return;
  }

  ov5640_status = OV5640_ConfigJPEGQVGA();
  if (ov5640_status != HAL_OK)
  {
    return;
  }

  lvds_tx_status = LVDS_TxInit();
  if (lvds_tx_status != HAL_OK)
  {
    return;
  }

  camera_capture_status = Camera_CaptureStart();
}

/* JPEG 首帧验证流程：不复用 RGB565 的固定长度 LVDS/BMP 保存路径。 */
void App_CameraProcess(void)
{
  uint8_t jpeg_valid;

  Camera_CapturePollDiagnostics();

  /* A stalled (truncated) JPEG is restarted inside camera_capture.c.  Count
   * that discarded frame when a remote photo batch owns the capture loop. */
  if ((photo_capture_active != 0U) &&
      (camera_capture_stall_recoveries != photo_last_stall_recoveries))
  {
    photo_capture_invalid_frames += (uint16_t)(camera_capture_stall_recoveries -
                                                photo_last_stall_recoveries);
    photo_last_stall_recoveries = camera_capture_stall_recoveries;
    if (photo_capture_invalid_frames >= PHOTO_CAPTURE_MAX_INVALID_FRAMES)
    {
      (void)Camera_CaptureStop();
      App_CameraPhotoFail(FR_INT_ERR);
    }
  }

  /* File transfer owns the SD/UART path.  Stop the current camera snapshot
   * immediately and keep it paused until BOTH transfer state machines end. */
  if ((video_tx_active != 0U) || (image_tx_active != 0U))
  {
    if (camera_capture_paused == 0U)
    {
      (void)Camera_CaptureStop();
      camera_capture_paused = 1U;
    }
  }
  else if (camera_capture_paused != 0U)
  {
    camera_capture_paused = 0U;
    camera_capture_status = Camera_CaptureStart();
  }

  if (camera_frame_ready != 0U)
  {
    camera_frame_ready = 0U;

    /* JPEG 为可变长度数据。帧结束后停止 DMA，才可读取 SDRAM。 */
    (void)Camera_CaptureStop();

    jpeg_valid = (uint8_t)((camera_jpeg_soi_found != 0U) &&
                            (camera_jpeg_eoi_found != 0U) &&
                            (camera_jpeg_bytes != 0U));

    if ((camera_jpeg_save_request == 1U) &&
        (camera_jpeg_soi_found != 0U) &&
        (jpeg_valid != 0U))
    {
      camera_jpeg_save_request = 2U;
      camera_jpeg_save_status = Storage_SaveRawImage(
          (const uint8_t *)CAMERA_FRAME_ADDRESS, camera_jpeg_bytes,
          "0:/img", "0:/img/test.jpg");
      if (camera_jpeg_save_status == FR_OK)
      {
        camera_jpeg_images_saved++;
      }
    }

    /* A batch consumes exactly one successfully saved JPEG per completed
     * DCMI frame.  It never loops or waits in this main-loop iteration. */
    if (photo_capture_active != 0U)
    {
      if (jpeg_valid == 0U)
      {
        photo_capture_invalid_frames++;
        if (photo_capture_invalid_frames >= PHOTO_CAPTURE_MAX_INVALID_FRAMES)
        {
          App_CameraPhotoFail(FR_INT_ERR);
        }
      }
      else
      {
        TCHAR photo_path[20];
        App_CameraPhotoMakePath((uint16_t)(photo_capture_saved + 1U), photo_path);
        photo_last_save_result = Storage_SaveRawImage(
            (const uint8_t *)CAMERA_FRAME_ADDRESS, camera_jpeg_bytes,
            "0:/img", photo_path);
        if (photo_last_save_result != FR_OK)
        {
          App_CameraPhotoFail(photo_last_save_result);
        }
        else
        {
          photo_capture_saved++;
          photo_last_file_size = camera_jpeg_bytes;
          if (photo_capture_saved >= photo_capture_target)
          {
            photo_capture_active = 0U;
            photo_batch_valid = 1U;
            photo_batch_completed_count = photo_capture_saved;
            photo_capture_status = PHOTO_CAPTURE_STATUS_COMPLETE;
            photo_capture_done_pending = 1U;
          }
        }
      }
    }

    /* Watch 写 1 开始测试录像；本版自动录制 30 帧后关闭并回填 AVI。 */
    if (avi_record_request == 1U)
    {
      avi_record_status = (uint8_t)AVI_RecorderStart(
          "0:/video/AVI00001.AVI", AVI_RECORD_WIDTH,
          AVI_RECORD_HEIGHT, AVI_RECORD_FPS);
      avi_record_request = (avi_record_status == FR_OK) ? 2U : 4U;
    }

    if ((avi_record_active != 0U) &&
        (camera_jpeg_soi_found != 0U) &&
        (jpeg_valid != 0U))
    {
      avi_record_status = (uint8_t)AVI_RecorderAddJPEG(
          (const uint8_t *)CAMERA_FRAME_ADDRESS, camera_jpeg_bytes);
      if ((avi_record_status != FR_OK) ||
          (avi_record_request == 3U))
      {
        if (avi_record_active != 0U)
        {
          avi_record_status = (uint8_t)AVI_RecorderStop();
          /* Recording only by explicit stop: verify the file on-board. */
          if (avi_record_status == FR_OK)
          {
            recorded_file_check_result = UARTVideoTx_VerifyRecordedFile();
          }
        }
        avi_record_request = 4U;
      }
    }

    /* Keep continuous JPEG sampling only while neither file transfer owns
     * the camera.  Photo capture simply uses this one-frame-at-a-time loop. */
    if ((video_tx_active != 0U) || (image_tx_active != 0U))
    {
      camera_capture_paused = 1U;
    }
    else
    {
      camera_capture_status = Camera_CaptureStart();
    }
  }
}
