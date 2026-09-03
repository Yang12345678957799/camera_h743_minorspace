#ifndef AVI_RECORDER_H
#define AVI_RECORDER_H

#include "fatfs.h"

#define AVI_RECORD_WIDTH       320U
#define AVI_RECORD_HEIGHT      240U
#define AVI_RECORD_FPS         15U
#define AVI_TEST_FRAME_LIMIT   30U

/* Watch 控制：0=空闲，写1开始，2=录像中，写3可提前停止，4=已完成。 */
extern volatile uint8_t avi_record_request;
extern volatile uint8_t avi_record_active;
extern volatile uint8_t avi_record_status;
extern volatile uint32_t avi_record_frames;
extern volatile uint32_t avi_record_bytes;

FRESULT AVI_RecorderStart(const TCHAR *path, uint16_t width,
                          uint16_t height, uint16_t fps);
FRESULT AVI_RecorderAddJPEG(const uint8_t *jpeg, uint32_t length);
FRESULT AVI_RecorderStop(void);

#endif /* AVI_RECORDER_H */
