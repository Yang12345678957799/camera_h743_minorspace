#ifndef IMAGE_PACKET_H
#define IMAGE_PACKET_H

#include <stdint.h>

/* F429 legacy still-image packet.  This module only formats the wire packet;
 * the physical transport is selected by its caller. */
#define IMAGE_PACKET_PAYLOAD_SIZE 862U
#define IMAGE_PACKET_SIZE         886U

#pragma pack(push, 1)
typedef struct
{
  uint8_t head[4];
  uint8_t count[2];
  uint8_t type[2];
  uint8_t total[4];
  uint8_t current[4];
  uint8_t length[2];
  uint8_t data[IMAGE_PACKET_PAYLOAD_SIZE];
  uint8_t checksum[2];
  uint8_t tail[4];
} ImagePacketTypeDef;
#pragma pack(pop)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(ImagePacketTypeDef) == IMAGE_PACKET_SIZE,
               "image packet size must be 886 bytes");
#else
typedef char image_packet_size_check[
    (sizeof(ImagePacketTypeDef) == IMAGE_PACKET_SIZE) ? 1 : -1];
#endif

/* Checksum is the 16-bit sum of bytes from type through the complete 862-byte
 * data area.  It deliberately includes 0xAA padding in the final packet. */
uint16_t ImagePacket_Checksum(const ImagePacketTypeDef *packet);

/* Build one zero-based packet of a single JPEG file.  `packet_total` is the
 * number of packets for that JPEG, not for an entire photo batch. */
void ImagePacket_Build(ImagePacketTypeDef *packet, const uint8_t *data,
                       uint16_t length, uint32_t packet_index,
                       uint32_t packet_total);

#endif /* IMAGE_PACKET_H */
