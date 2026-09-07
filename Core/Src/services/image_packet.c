#include "services/image_packet.h"
#include <string.h>

#define IMAGE_PACKET_HEAD0 0xFCU
#define IMAGE_PACKET_HEAD1 0xFCU
#define IMAGE_PACKET_HEAD2 0xA1U
#define IMAGE_PACKET_HEAD3 0xA1U
#define IMAGE_PACKET_TYPE0 0xA5U
#define IMAGE_PACKET_TYPE1 0xA5U
#define IMAGE_PACKET_TAIL0 0x1EU
#define IMAGE_PACKET_TAIL1 0x1BU
#define IMAGE_PACKET_TAIL2 0x1EU
#define IMAGE_PACKET_TAIL3 0x1BU

static void ImagePacket_WriteU32BE(uint8_t *target, uint32_t value)
{
  target[0] = (uint8_t)(value >> 24);
  target[1] = (uint8_t)(value >> 16);
  target[2] = (uint8_t)(value >> 8);
  target[3] = (uint8_t)value;
}

uint16_t ImagePacket_Checksum(const ImagePacketTypeDef *packet)
{
  const uint8_t *bytes = (const uint8_t *)packet;
  uint32_t sum = 0U;
  uint16_t index;

  /* Offsets 6..879: type, total, current, length and all data bytes. */
  for (index = 6U; index < 880U; index++)
  {
    sum += bytes[index];
  }
  return (uint16_t)sum;
}

void ImagePacket_Build(ImagePacketTypeDef *packet, const uint8_t *data,
                       uint16_t length, uint32_t packet_index,
                       uint32_t packet_total)
{
  uint16_t checksum;
  uint16_t index;

  if ((packet == NULL) || (data == NULL) ||
      (length == 0U) || (length > IMAGE_PACKET_PAYLOAD_SIZE))
  {
    return;
  }

  (void)memset(packet, 0xAA, sizeof(*packet));
  packet->head[0] = IMAGE_PACKET_HEAD0;
  packet->head[1] = IMAGE_PACKET_HEAD1;
  packet->head[2] = IMAGE_PACKET_HEAD2;
  packet->head[3] = IMAGE_PACKET_HEAD3;
  packet->count[0] = (uint8_t)(packet_index >> 8);
  packet->count[1] = (uint8_t)packet_index;
  packet->type[0] = IMAGE_PACKET_TYPE0;
  packet->type[1] = IMAGE_PACKET_TYPE1;
  ImagePacket_WriteU32BE(packet->total, packet_total);
  ImagePacket_WriteU32BE(packet->current, packet_index);
  packet->length[0] = (uint8_t)(length >> 8);
  packet->length[1] = (uint8_t)length;

  /* data starts at a packed, unaligned offset.  Use byte stores for M7. */
  for (index = 0U; index < length; index++)
  {
    packet->data[index] = data[index];
  }

  checksum = ImagePacket_Checksum(packet);
  packet->checksum[0] = (uint8_t)(checksum >> 8);
  packet->checksum[1] = (uint8_t)checksum;
  packet->tail[0] = IMAGE_PACKET_TAIL0;
  packet->tail[1] = IMAGE_PACKET_TAIL1;
  packet->tail[2] = IMAGE_PACKET_TAIL2;
  packet->tail[3] = IMAGE_PACKET_TAIL3;
}
