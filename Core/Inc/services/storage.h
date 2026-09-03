#ifndef STORAGE_H
#define STORAGE_H

#include "fatfs.h"

/* 最近一次挂载结果：FR_OK 表示 SD 卡与 FatFs 文件系统均可访问。 */
extern volatile FRESULT storage_mount_status;
/* 读取到的逻辑扇区数量和每扇区字节数，便于在 Keil Watch 中确认卡容量。 */
extern volatile DWORD storage_sector_count;
extern volatile WORD storage_sector_size;
/* 默认为 0。仅在 Keil Watch 中手动写 1，才格式化焊载 SD 卡。 */
extern volatile uint8_t storage_format_request;
extern volatile FRESULT storage_format_status;
/* 默认为 0。手动写 1 后执行“写入、读回、比对、删除”的 SD 自检。 */
extern volatile uint8_t storage_test_request;
extern volatile FRESULT storage_test_status;
extern volatile UINT storage_test_bytes_written;
extern volatile UINT storage_test_bytes_read;

/* 只挂载并读取容量信息，不创建、修改或删除 SD 卡中的任何文件。 */
FRESULT Storage_Mount(void);
/* 将内存中的大端序 RGB565 图像编码为标准 24-bit BMP 文件。 */
FRESULT Storage_SaveRGB565BMP(const uint8_t *rgb565_be, uint16_t width,
                              uint16_t height, const TCHAR *path);
/* 创建目录（目录已存在也视为成功），并按固定块写入原始图像。 */
FRESULT Storage_SaveRawImage(const uint8_t *data, uint32_t length,
                              const TCHAR *directory, const TCHAR *path);
/* 轮询一次性格式化请求；格式化完成后自动重新挂载。 */
void Storage_Process(void);

#endif /* STORAGE_H */
