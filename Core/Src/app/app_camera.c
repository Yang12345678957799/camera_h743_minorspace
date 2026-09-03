#include "app/app_camera.h"
#include "bsp/ov5640.h"
#include "services/camera_capture.h"
#include "services/lvds_tx.h"
#include "services/storage.h"
#include "services/avi_recorder.h"

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

/*
 * Send one complete RGB565 QVGA frame using the existing LVDS packet format.
 * 153600 bytes are split into 179 packets: 178 x 862 bytes plus 1 x 164 bytes.
 * Packet indexes remain zero-based, consistent with the initial single-packet test.
 */
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
  Camera_CapturePollDiagnostics();

  if (camera_frame_ready != 0U)
  {
    camera_frame_ready = 0U;

    /* JPEG 为可变长度数据。帧结束后停止 DMA，才可读取 SDRAM。 */
    (void)Camera_CaptureStop();

    if ((camera_jpeg_save_request == 1U) &&
        (camera_jpeg_soi_found != 0U) &&
        (camera_jpeg_eoi_found != 0U) &&
        (camera_jpeg_bytes != 0U))
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
        (camera_jpeg_eoi_found != 0U) &&
        (camera_jpeg_bytes != 0U))
    {
      avi_record_status = (uint8_t)AVI_RecorderAddJPEG(
          (const uint8_t *)CAMERA_FRAME_ADDRESS, camera_jpeg_bytes);
      if ((avi_record_status != FR_OK) ||
          (avi_record_request == 3U) ||
          (avi_record_frames >= AVI_TEST_FRAME_LIMIT))
      {
        if (avi_record_active != 0U)
        {
          avi_record_status = (uint8_t)AVI_RecorderStop();
        }
        avi_record_request = 4U;
      }
    }

    /* 连续采下一帧，供 Watch 检查 JPEG 的 SOI、EOI 和实际字节数。 */
    camera_capture_status = Camera_CaptureStart();
  }
}
