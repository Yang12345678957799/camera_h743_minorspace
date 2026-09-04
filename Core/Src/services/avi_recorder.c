#include "services/avi_recorder.h"
#include "services/storage.h"

#define AVIIF_KEYFRAME         0x00000010U

volatile uint8_t avi_record_request = 0U;
volatile uint8_t avi_record_active = 0U;
volatile uint8_t avi_record_status = FR_NOT_READY;
volatile uint32_t avi_record_frames = 0U;
volatile uint32_t avi_record_bytes = 0U;

static FIL avi_file;
/* 与原 F429 相同：录像过程中把每帧长度写入临时参数文件，结束时读回。 */
static FIL avi_parameter_file;
/* FatFs 关闭了 LFN（_USE_LFN=0），路径必须符合 8.3 短文件名格式。 */
static const TCHAR avi_parameter_path[] = "0:/video/AVI00001.PRM";
static uint32_t avi_riff_size_pos;
static uint32_t avi_avih_frames_pos;
static uint32_t avi_strh_frames_pos;
static uint32_t avi_movi_size_pos;
static uint32_t avi_movi_fourcc_pos;

static FRESULT AVI_Write(const void *data, UINT length)
{
  UINT written = 0U;
  FRESULT result = f_write(&avi_file, data, length, &written);
  if ((result == FR_OK) && (written != length))
  {
    result = FR_DISK_ERR;
  }
  return result;
}

static FRESULT AVI_WriteU16(uint16_t value)
{
  uint8_t bytes[2];
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  return AVI_Write(bytes, sizeof(bytes));
}

static FRESULT AVI_WriteU32(uint32_t value)
{
  uint8_t bytes[4];
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
  return AVI_Write(bytes, sizeof(bytes));
}

static FRESULT AVI_WriteFourCC(const char text[4])
{
  return AVI_Write(text, 4U);
}

static FRESULT AVI_PatchU32(uint32_t position, uint32_t value)
{
  FRESULT result = f_lseek(&avi_file, position);
  if (result == FR_OK)
  {
    result = AVI_WriteU32(value);
  }
  return result;
}

/* 写入标准的单视频流 MJPEG AVI 文件头，并记录结束时需要回填的位置。 */
FRESULT AVI_RecorderStart(const TCHAR *path, uint16_t width,
                          uint16_t height, uint16_t fps)
{
  uint32_t hdrl_size_pos;
  uint32_t hdrl_start;
  uint32_t strl_size_pos;
  uint32_t strl_start;
  uint32_t index;
  FRESULT result;

  if ((path == NULL) || (width == 0U) || (height == 0U) || (fps == 0U))
  {
    return FR_INVALID_PARAMETER;
  }
  if (avi_record_active != 0U)
  {
    return FR_LOCKED;
  }
  if (storage_mount_status != FR_OK)
  {
    result = Storage_Mount();
    if (result != FR_OK) return result;
  }
  result = f_mkdir("0:/video");
  if ((result != FR_OK) && (result != FR_EXIST)) return result;
  result = f_open(&avi_file, path, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK) return result;

  avi_record_frames = 0U;
  avi_record_bytes = 0U;
  result = f_open(&avi_parameter_file, avi_parameter_path,
                  FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
  if (result != FR_OK)
  {
    (void)f_close(&avi_file);
    return result;
  }

#define AVI_CHECK(call) do { result = (call); if (result != FR_OK) goto fail; } while (0)
  AVI_CHECK(AVI_WriteFourCC("RIFF"));
  avi_riff_size_pos = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteFourCC("AVI "));

  AVI_CHECK(AVI_WriteFourCC("LIST"));
  hdrl_size_pos = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteU32(0U));
  hdrl_start = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteFourCC("hdrl"));

  AVI_CHECK(AVI_WriteFourCC("avih"));
  AVI_CHECK(AVI_WriteU32(56U));
  AVI_CHECK(AVI_WriteU32(1000000U / fps));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(AVIIF_KEYFRAME));
  avi_avih_frames_pos = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(1U));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(width));
  AVI_CHECK(AVI_WriteU32(height));
  for (index = 0U; index < 4U; index++) AVI_CHECK(AVI_WriteU32(0U));

  AVI_CHECK(AVI_WriteFourCC("LIST"));
  strl_size_pos = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteU32(0U));
  strl_start = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteFourCC("strl"));
  AVI_CHECK(AVI_WriteFourCC("strh"));
  AVI_CHECK(AVI_WriteU32(56U));
  AVI_CHECK(AVI_WriteFourCC("vids"));
  AVI_CHECK(AVI_WriteFourCC("MJPG"));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU16(0U));
  AVI_CHECK(AVI_WriteU16(0U));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(1U));
  AVI_CHECK(AVI_WriteU32(fps));
  AVI_CHECK(AVI_WriteU32(0U));
  avi_strh_frames_pos = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(0xFFFFFFFFU));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU16(0U));
  AVI_CHECK(AVI_WriteU16(0U));
  AVI_CHECK(AVI_WriteU16(width));
  AVI_CHECK(AVI_WriteU16(height));

  AVI_CHECK(AVI_WriteFourCC("strf"));
  AVI_CHECK(AVI_WriteU32(40U));
  AVI_CHECK(AVI_WriteU32(40U));
  AVI_CHECK(AVI_WriteU32(width));
  AVI_CHECK(AVI_WriteU32(height));
  AVI_CHECK(AVI_WriteU16(1U));
  AVI_CHECK(AVI_WriteU16(24U));
  AVI_CHECK(AVI_WriteFourCC("MJPG"));
  AVI_CHECK(AVI_WriteU32((uint32_t)width * height * 3U));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(0U));
  AVI_CHECK(AVI_WriteU32(0U));

  AVI_CHECK(AVI_PatchU32(strl_size_pos, (uint32_t)f_tell(&avi_file) - strl_start));
  AVI_CHECK(f_lseek(&avi_file, strl_start + ((uint32_t)f_tell(&avi_file) - strl_start)));
  /* Patch 后文件指针不在末尾，因此直接回到当前固定文件头末端。 */
  AVI_CHECK(f_lseek(&avi_file, 212U));
  AVI_CHECK(AVI_PatchU32(hdrl_size_pos, 192U));
  AVI_CHECK(f_lseek(&avi_file, 212U));

  AVI_CHECK(AVI_WriteFourCC("LIST"));
  avi_movi_size_pos = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteU32(0U));
  avi_movi_fourcc_pos = (uint32_t)f_tell(&avi_file);
  AVI_CHECK(AVI_WriteFourCC("movi"));
  AVI_CHECK(f_sync(&avi_file));

  avi_record_active = 1U;
  avi_record_status = FR_OK;
  return FR_OK;

fail:
  (void)f_close(&avi_file);
  (void)f_close(&avi_parameter_file);
  avi_record_active = 0U;
  avi_record_status = (uint8_t)result;
  return result;
#undef AVI_CHECK
}

FRESULT AVI_RecorderAddJPEG(const uint8_t *jpeg, uint32_t length)
{
  uint8_t pad = 0U;
  UINT written = 0U;
  FRESULT result;
  if ((avi_record_active == 0U) || (jpeg == NULL) || (length < 4U))
  {
    return FR_INVALID_OBJECT;
  }
  /* 原 F429 的 .param 文件逻辑：每帧先记录未填充的 JPEG 实际长度。 */
  result = f_write(&avi_parameter_file, &length, sizeof(length), &written);
  if ((result == FR_OK) && (written != sizeof(length))) result = FR_DISK_ERR;
  if (result == FR_OK) result = AVI_WriteFourCC("00dc");
  if (result == FR_OK) result = AVI_WriteU32(length);
  if (result == FR_OK) result = AVI_Write(jpeg, (UINT)length);
  if ((result == FR_OK) && ((length & 1U) != 0U)) result = AVI_Write(&pad, 1U);
  if (result != FR_OK)
  {
    avi_record_status = (uint8_t)result;
    return result;
  }

  avi_record_frames++;
  avi_record_bytes += length;
  avi_record_status = FR_OK;
  return FR_OK;
}

FRESULT AVI_RecorderStop(void)
{
  uint32_t end_movi;
  uint32_t final_size;
  uint32_t index;
  uint32_t frame_length;
  uint32_t frame_offset = 4U;
  UINT bytes_read;
  FRESULT result = FR_OK;

  if (avi_record_active == 0U) return FR_INVALID_OBJECT;
  result = f_sync(&avi_file);
  if (result == FR_OK) result = f_sync(&avi_parameter_file);
  if (result == FR_OK) result = f_lseek(&avi_parameter_file, 0U);
  if (result != FR_OK) goto close_files;

  end_movi = (uint32_t)f_tell(&avi_file);
  result = AVI_WriteFourCC("idx1");
  if (result == FR_OK) result = AVI_WriteU32(avi_record_frames * 16U);
  for (index = 0U; (index < avi_record_frames) && (result == FR_OK); index++)
  {
    bytes_read = 0U;
    result = f_read(&avi_parameter_file, &frame_length,
                    sizeof(frame_length), &bytes_read);
    if ((result == FR_OK) && (bytes_read != sizeof(frame_length)))
    {
      result = FR_DISK_ERR;
    }
    if (result != FR_OK) break;
    result = AVI_WriteFourCC("00dc");
    if (result == FR_OK) result = AVI_WriteU32(AVIIF_KEYFRAME);
    if (result == FR_OK) result = AVI_WriteU32(frame_offset);
    if (result == FR_OK) result = AVI_WriteU32(frame_length);
    frame_offset += 8U + frame_length + (frame_length & 1U);
  }
  final_size = (uint32_t)f_tell(&avi_file);
  if (result == FR_OK) result = AVI_PatchU32(avi_movi_size_pos, end_movi - (avi_movi_size_pos + 4U));
  if (result == FR_OK) result = AVI_PatchU32(avi_avih_frames_pos, avi_record_frames);
  if (result == FR_OK) result = AVI_PatchU32(avi_strh_frames_pos, avi_record_frames);
  if (result == FR_OK) result = AVI_PatchU32(avi_riff_size_pos, final_size - 8U);
  if (result == FR_OK) result = f_lseek(&avi_file, final_size);
  if (result == FR_OK) result = f_sync(&avi_file);

close_files:
  if (f_close(&avi_file) != FR_OK) result = FR_DISK_ERR;
  if (f_close(&avi_parameter_file) != FR_OK) result = FR_DISK_ERR;
  if ((result == FR_OK) && (f_unlink(avi_parameter_path) != FR_OK))
  {
    result = FR_DISK_ERR;
  }

  avi_record_active = 0U;
  avi_record_status = (uint8_t)result;
  return result;
}
