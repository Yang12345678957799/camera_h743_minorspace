#ifndef OV5640_H
#define OV5640_H

#include "stm32h7xx_hal.h"

/* OV5640 使用 I2C 7 位地址 0x3C，HAL 接口传入左移一位后的地址。 */
#define OV5640_I2C_ADDRESS  (0x3CU << 1)
#define OV5640_CHIP_ID      0x5640U

/* 用于 Watch 检查相机输出格式与分辨率寄存器是否已经写入。 */
extern volatile uint8_t ov5640_reg_3008;
extern volatile uint8_t ov5640_reg_3808;
extern volatile uint8_t ov5640_reg_3809;
extern volatile uint8_t ov5640_reg_4300;
extern volatile uint8_t ov5640_reg_501f;
/* JPEG 配置诊断：1=通用表，2=QVGA 表，3=JPEG 输出表，4=寄存器回读完成。 */
extern volatile uint8_t ov5640_jpeg_config_step;
extern volatile uint8_t ov5640_reg_3821;
extern volatile uint8_t ov5640_reg_3002;
extern volatile uint8_t ov5640_reg_3006;

/* 复位相机后读取 0x300A / 0x300B，组合得到传感器 ID。 */
HAL_StatusTypeDef OV5640_Probe(volatile uint16_t *sensor_id);
/* 配置 OV5640 DVP 并行输出为 RGB565 + VGA (640 x 480)。 */
HAL_StatusTypeDef OV5640_ConfigRGB565VGA(void);
/* 配置 OV5640 DVP 并行输出为 RGB565 + QVGA (320 x 240)。 */
HAL_StatusTypeDef OV5640_ConfigRGB565QVGA(void);
HAL_StatusTypeDef OV5640_ConfigJPEGQVGA(void);

#endif /* OV5640_H */
