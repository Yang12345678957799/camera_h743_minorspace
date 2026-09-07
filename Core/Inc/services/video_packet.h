#ifndef VIDEO_PACKET_H
#define VIDEO_PACKET_H

#include <stdint.h>

/*
 * F429 legacy video packet, transport-agnostic.
 *
 * Recovered from commented-out code in the F429 project; it is NOT the
 * protocol F429 currently executes for images. This module only builds and
 * inspects packets; the physical link (USART1/RS422 today) lives elsewhere.
 *
 * Layout, 886 bytes packed:
 *   head[4]     5A 65 DF 2F
 *   count[4]    packet index, big endian, first packet is 0
 *   type[2]     5A 5A
 *   length[2]   valid bytes this packet, big endian (last packet may be short)
 *   data[868]   file bytes; unused tail of the LAST packet stays 0xAA
 *   checksum[2] big endian 16-bit sum of the FULL 868-byte data area
 *               (0xAA padding of the last packet included)
 *   tail[4]     2E E9 C8 FD
 *
 * There is deliberately NO total-packets field: the legacy protocol does not
 * have one. Do not add one for "convenience".
 */

#define VIDEO_PACKET_PAYLOAD_SIZE 868U
#define VIDEO_PACKET_SIZE        886U

#pragma pack(push, 1)
typedef struct
{
  uint8_t head[4];
  uint8_t count[4];
  uint8_t type[2];
  uint8_t length[2];
  uint8_t data[VIDEO_PACKET_PAYLOAD_SIZE];
  uint8_t checksum[2];
  uint8_t tail[4];
} VideoPacketTypeDef;
#pragma pack(pop)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(VideoPacketTypeDef) == VIDEO_PACKET_SIZE,
               "video packet size must be 886 bytes");
#else
typedef char video_packet_size_check[
    (sizeof(VideoPacketTypeDef) == VIDEO_PACKET_SIZE) ? 1 : -1];
#endif

/* Fill `packet` with `length` bytes from `data` as packet number
 * `packet_index` (0-based). The whole packet is first set to 0xAA, so the
 * unused area of a short last packet stays 0xAA, exactly like the F429
 * original. The checksum covers the complete 868-byte data area. */
void VideoPacket_Build(VideoPacketTypeDef *packet, const uint8_t *data,
                       uint16_t length, uint32_t packet_index);

/* 16-bit sum of the complete 868-byte data area (padding included). */
uint16_t VideoPacket_Checksum(const VideoPacketTypeDef *packet);

#endif /* VIDEO_PACKET_H */
