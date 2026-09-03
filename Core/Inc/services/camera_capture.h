#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include "stm32h7xx_hal.h"

/* 当前测试分辨率：RGB565 的 QVGA 图像，每个像素 2 字节。 */
#define CAMERA_FRAME_WIDTH       320U
#define CAMERA_FRAME_HEIGHT      240U
#define CAMERA_FRAME_BYTES       (CAMERA_FRAME_WIDTH * CAMERA_FRAME_HEIGHT * 2U)
#define CAMERA_FRAME_WORDS       (CAMERA_FRAME_BYTES / 4U)
#define CAMERA_FRAME_ADDRESS     0xC0000000U

/* DCMI 帧完成回调置 1；应用层处理完该帧后再启动下一次采集。 */
extern volatile uint8_t camera_frame_ready;
/* HAL_DCMI_Start_DMA / Stop 的最后一次返回状态。 */
extern volatile HAL_StatusTypeDef camera_capture_status;
extern volatile uint32_t camera_jpeg_bytes;
extern volatile uint8_t camera_jpeg_soi_found;
extern volatile uint8_t camera_jpeg_eoi_found;
/* 上一帧 JPEG 的保留诊断值；不会在下一次采集开始时清零，适合 Keil Watch。 */
extern volatile uint32_t camera_jpeg_last_bytes;
extern volatile uint8_t camera_jpeg_last_soi_found;
extern volatile uint8_t camera_jpeg_last_eoi_found;
extern volatile uint32_t camera_jpeg_frame_events;
extern volatile uint32_t camera_dcmi_vsync_events;
extern volatile uint32_t camera_dcmi_line_events;
extern volatile uint32_t camera_dcmi_error_events;
extern volatile uint32_t camera_dcmi_error_code;
extern volatile uint32_t camera_dma_error_code;
extern volatile uint32_t camera_dma_remaining_words;
extern volatile uint32_t camera_dcmi_status_reg;
/* DMA 长时间不再接收且没有 EOI 时，丢弃坏帧并重新启动的次数。 */
extern volatile uint32_t camera_capture_stall_recoveries;
extern volatile uint32_t camera_jpeg_received_bytes;
extern volatile uint8_t camera_jpeg_first_byte_0;
extern volatile uint8_t camera_jpeg_first_byte_1;
extern volatile uint8_t camera_jpeg_first_byte_2;
extern volatile uint8_t camera_jpeg_first_byte_3;

/* 以 Snapshot 模式将一帧图像 DMA 到外部 SDRAM。 */
HAL_StatusTypeDef Camera_CaptureStart(void);
/* 停止 DCMI 采集。 */
HAL_StatusTypeDef Camera_CaptureStop(void);
/* 主循环调用，仅更新 Watch 诊断值，不改变采集状态。 */
void Camera_CapturePollDiagnostics(void);

#endif /* CAMERA_CAPTURE_H */
