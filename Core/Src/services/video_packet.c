#include "services/video_packet.h"
#include <string.h>

/* Wire constants of the legacy F429 video protocol. */
#define VIDEO_PACKET_HEAD0 0x5AU
#define VIDEO_PACKET_HEAD1 0x65U
#define VIDEO_PACKET_HEAD2 0xDFU
#define VIDEO_PACKET_HEAD3 0x2FU
#define VIDEO_PACKET_TYPE0 0x5AU
#define VIDEO_PACKET_TYPE1 0x5AU
#define VIDEO_PACKET_TAIL0 0x2EU
#define VIDEO_PACKET_TAIL1 0xE9U
#define VIDEO_PACKET_TAIL2 0xC8U
#define VIDEO_PACKET_TAIL3 0xFDU

uint16_t VideoPacket_Checksum(const VideoPacketTypeDef *packet)
{
  uint32_t sum = 0U;
  uint16_t index;
  const uint8_t *data = packet->data;

  for (index = 0U; index < VIDEO_PACKET_PAYLOAD_SIZE; index++)
  {
    sum += data[index];
  }
  return (uint16_t)sum;
}

void VideoPacket_Build(VideoPacketTypeDef *packet, const uint8_t *data,
                       uint16_t length, uint32_t packet_index)
{
  uint16_t checksum;
  uint16_t index;

  /* Whole packet to 0xAA first: the unused area of a short last packet must
   * stay 0xAA, and the checksum must cover it. */
  (void)memset(packet, 0xAA, sizeof(*packet));

  packet->head[0] = VIDEO_PACKET_HEAD0;
  packet->head[1] = VIDEO_PACKET_HEAD1;
  packet->head[2] = VIDEO_PACKET_HEAD2;
  packet->head[3] = VIDEO_PACKET_HEAD3;
  packet->count[0] = (uint8_t)(packet_index >> 24);
  packet->count[1] = (uint8_t)(packet_index >> 16);
  packet->count[2] = (uint8_t)(packet_index >> 8);
  packet->count[3] = (uint8_t)packet_index;
  packet->type[0] = VIDEO_PACKET_TYPE0;
  packet->type[1] = VIDEO_PACKET_TYPE1;
  packet->length[0] = (uint8_t)(length >> 8);
  packet->length[1] = (uint8_t)length;

  /* data[] sits at byte offset 12 of the packed struct: copy byte-wise so
   * Cortex-M7 never performs an unaligned word access. */
  for (index = 0U; index < length; index++)
  {
    packet->data[index] = data[index];
  }

  checksum = VideoPacket_Checksum(packet);
  packet->checksum[0] = (uint8_t)(checksum >> 8);
  packet->checksum[1] = (uint8_t)checksum;

  packet->tail[0] = VIDEO_PACKET_TAIL0;
  packet->tail[1] = VIDEO_PACKET_TAIL1;
  packet->tail[2] = VIDEO_PACKET_TAIL2;
  packet->tail[3] = VIDEO_PACKET_TAIL3;
}
