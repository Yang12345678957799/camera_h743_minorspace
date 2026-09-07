#include "services/camera_capture.h"
#include "dcmi.h"

/* 由 HAL_DCMI_FrameEventCallback 在 DMA 完成时置位。 */
volatile uint8_t camera_frame_ready = 0U;
/* 1 = capture intentionally paused (AVI file transfer): no restart and
 * no stall-recovery while set. Set/cleared by app_camera. */
volatile uint8_t camera_capture_paused = 0U;
/* 供应用层和 Keil Watch 检查 DCMI 启动、停止或错误状态。 */
volatile HAL_StatusTypeDef camera_capture_status = HAL_ERROR;
volatile uint32_t camera_jpeg_bytes = 0U;
volatile uint8_t camera_jpeg_soi_found = 0U;
volatile uint8_t camera_jpeg_eoi_found = 0U;
volatile uint32_t camera_jpeg_last_bytes = 0U;
volatile uint8_t camera_jpeg_last_soi_found = 0U;
volatile uint8_t camera_jpeg_last_eoi_found = 0U;
volatile uint32_t camera_jpeg_frame_events = 0U;
volatile uint32_t camera_dcmi_vsync_events = 0U;
volatile uint32_t camera_dcmi_line_events = 0U;
volatile uint32_t camera_dcmi_error_events = 0U;
volatile uint32_t camera_dcmi_error_code = 0U;
volatile uint32_t camera_dma_error_code = 0U;
volatile uint32_t camera_dma_remaining_words = 0U;
volatile uint32_t camera_dcmi_status_reg = 0U;
volatile uint32_t camera_capture_stall_recoveries = 0U;
volatile uint32_t camera_jpeg_received_bytes = 0U;
volatile uint8_t camera_jpeg_first_byte_0 = 0U;
volatile uint8_t camera_jpeg_first_byte_1 = 0U;
volatile uint8_t camera_jpeg_first_byte_2 = 0U;
volatile uint8_t camera_jpeg_first_byte_3 = 0U;

static uint32_t camera_last_dma_remaining = CAMERA_FRAME_WORDS;
static uint32_t camera_last_dma_progress_tick = 0U;

static void Camera_FindJPEGMarkers(uint32_t received_bytes)
{
  const uint8_t *data = (const uint8_t *)CAMERA_FRAME_ADDRESS;
  uint32_t index;
  camera_jpeg_bytes = 0U;
  camera_jpeg_soi_found = 0U;
  camera_jpeg_eoi_found = 0U;
  for (index = 0U; (index + 1U) < received_bytes; index++)
  {
    if ((camera_jpeg_soi_found == 0U) && (data[index] == 0xFFU) && (data[index + 1U] == 0xD8U)) camera_jpeg_soi_found = 1U;
    if ((camera_jpeg_soi_found != 0U) && (data[index] == 0xFFU) && (data[index + 1U] == 0xD9U)) { camera_jpeg_eoi_found = 1U; camera_jpeg_bytes = index + 2U; return; }
  }
}

/* 启动一次单帧采集，图像数据目标地址为 SDRAM 的 0xC0000000。 */
HAL_StatusTypeDef Camera_CaptureStart(void)
{
  camera_frame_ready = 0U;
  camera_jpeg_bytes = 0U;
  camera_jpeg_soi_found = 0U;
  camera_jpeg_eoi_found = 0U;
  camera_capture_status = HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT,
                                             CAMERA_FRAME_ADDRESS,
                                             CAMERA_FRAME_WORDS);
  if (camera_capture_status == HAL_OK)
  {
    camera_last_dma_remaining = CAMERA_FRAME_WORDS;
    camera_last_dma_progress_tick = HAL_GetTick();
    /* 临时启用同步与行事件，供 JPEG 无帧时定位信号链路。 */
    __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_VSYNC);
  }
  return camera_capture_status;
}

HAL_StatusTypeDef Camera_CaptureStop(void)
{
  camera_capture_status = HAL_DCMI_Stop(&hdcmi);
  return camera_capture_status;
}

void Camera_CapturePollDiagnostics(void)
{
  const uint8_t *data = (const uint8_t *)CAMERA_FRAME_ADDRESS;
  static uint32_t last_scan_tick = 0U;

  camera_dcmi_status_reg = hdcmi.Instance->SR;
  camera_dcmi_error_code = hdcmi.ErrorCode;
  if (hdcmi.DMA_Handle != NULL)
  {
    camera_dma_remaining_words = __HAL_DMA_GET_COUNTER(hdcmi.DMA_Handle);
    camera_dma_error_code = hdcmi.DMA_Handle->ErrorCode;
    camera_jpeg_received_bytes =
        (CAMERA_FRAME_WORDS - camera_dma_remaining_words) * 4U;

    if (camera_dma_remaining_words != camera_last_dma_remaining)
    {
      camera_last_dma_remaining = camera_dma_remaining_words;
      camera_last_dma_progress_tick = HAL_GetTick();
    }

    if (camera_jpeg_received_bytes >= 4U)
    {
      camera_jpeg_first_byte_0 = data[0];
      camera_jpeg_first_byte_1 = data[1];
      camera_jpeg_first_byte_2 = data[2];
      camera_jpeg_first_byte_3 = data[3];
    }

    /* Frame 回调未触发时仍可每 10 ms 检查一次当前 DMA 数据。 */
    if ((camera_jpeg_received_bytes >= 2U) &&
        ((HAL_GetTick() - last_scan_tick) >= 10U))
    {
      last_scan_tick = HAL_GetTick();
      Camera_FindJPEGMarkers(camera_jpeg_received_bytes);
      camera_jpeg_last_bytes = camera_jpeg_bytes;
      camera_jpeg_last_soi_found = camera_jpeg_soi_found;
      camera_jpeg_last_eoi_found = camera_jpeg_eoi_found;

      /* STM32H7 DCMI JPEG 模式下本板未产生 Frame 回调，因此以 JPEG 的
       * EOI(FF D9) 作为可靠帧结束条件，交给应用层停止 DMA 并处理该帧。 */
      if ((camera_jpeg_soi_found != 0U) &&
          (camera_jpeg_eoi_found != 0U) &&
          (camera_frame_ready == 0U))
      {
        camera_jpeg_frame_events++;
        camera_frame_ready = 1U;
      }
    }

    /* 某帧收到部分数据后长时间不再前进且没有 EOI，说明本帧被截断。
     * 丢弃它并重新对齐下一次 VSYNC，防止整个录像永久卡住。 */
    if ((camera_jpeg_received_bytes != 0U) &&
        (camera_jpeg_eoi_found == 0U) &&
        (camera_frame_ready == 0U) &&
        (camera_capture_paused == 0U) &&
        ((HAL_GetTick() - camera_last_dma_progress_tick) >= 250U))
    {
      camera_capture_stall_recoveries++;
      (void)Camera_CaptureStop();
      (void)Camera_CaptureStart();
    }
  }
}

void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *phdcmi)
{
  if (phdcmi->Instance == DCMI)
  {
    camera_dcmi_vsync_events++;
  }
}

void HAL_DCMI_LineEventCallback(DCMI_HandleTypeDef *phdcmi)
{
  if (phdcmi->Instance == DCMI)
  {
    camera_dcmi_line_events++;
  }
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *phdcmi)
{
  const uint8_t *frame = (const uint8_t *)CAMERA_FRAME_ADDRESS;

  if (phdcmi->Instance == DCMI)
  {
    camera_jpeg_received_bytes =
        (CAMERA_FRAME_WORDS - __HAL_DMA_GET_COUNTER(phdcmi->DMA_Handle)) * 4U;
    camera_jpeg_first_byte_0 = frame[0];
    camera_jpeg_first_byte_1 = frame[1];
    camera_jpeg_first_byte_2 = frame[2];
    camera_jpeg_first_byte_3 = frame[3];
    Camera_FindJPEGMarkers(camera_jpeg_received_bytes);
    camera_jpeg_last_bytes = camera_jpeg_bytes;
    camera_jpeg_last_soi_found = camera_jpeg_soi_found;
    camera_jpeg_last_eoi_found = camera_jpeg_eoi_found;
    camera_jpeg_frame_events++;
    camera_frame_ready = 1U;  /* 通知 App_CameraProcess 图像已准备好。 */
  }
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *phdcmi)
{
  if (phdcmi->Instance == DCMI)
  {
    camera_dcmi_error_events++;
    camera_dcmi_error_code = phdcmi->ErrorCode;
    if (phdcmi->DMA_Handle != NULL)
    {
      camera_dma_error_code = phdcmi->DMA_Handle->ErrorCode;
    }
    camera_capture_status = HAL_ERROR;
  }
}
