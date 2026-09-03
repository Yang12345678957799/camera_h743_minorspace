#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include "stm32h7xx_hal.h"

/*
 * 应用层：决定相机模块的执行顺序。
 * 当前流程为：识别 OV5640 -> 配置 QVGA RGB565 -> 启动 DCMI 采集
 * -> 一帧采集完成后，发送一个 LVDS 测试数据包。
 */
void App_CameraInit(void);
void App_CameraProcess(void);

/* 保留为全局变量，便于在 Keil 的 Watch 窗口观察传感器识别结果。 */
extern volatile uint16_t ov5640_id;
extern volatile HAL_StatusTypeDef ov5640_status;

/* Number of whole QVGA frames transmitted successfully through LVDS. */
extern volatile uint32_t camera_frames_sent;
/* Number of frame transmissions that stopped because a packet failed. */
extern volatile uint32_t camera_frame_send_errors;

/* 默认 0；在 Watch 中写 1 后保存下一张完整帧为 0:/CAM00001.BMP。 */
extern volatile uint8_t camera_sd_save_request;
/* FatFs FRESULT 的数值状态；避免应用层头文件依赖 FatFs 类型定义。 */
extern volatile uint8_t camera_sd_save_status;
extern volatile uint32_t camera_images_saved;

/* 按原 F429 拍照方式保存下一帧为 0:/img/pic.raw。 */
extern volatile uint8_t camera_raw_save_request;
extern volatile uint8_t camera_raw_save_status;
extern volatile uint32_t camera_raw_images_saved;

/* JPEG 验证阶段：Watch 中将 request 写为 1 后，下一帧有效 JPEG 保存为
 * 0:/img/test.jpg；2 表示请求已处理。 */
extern volatile uint8_t camera_jpeg_save_request;
extern volatile uint8_t camera_jpeg_save_status;
extern volatile uint32_t camera_jpeg_images_saved;

#endif /* APP_CAMERA_H */
