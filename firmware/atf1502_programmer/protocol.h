// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#ifndef ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_PROTOCOL_H_
#define ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

namespace atf {

constexpr uint32_t kDeviceId = 0x0150203f;
constexpr unsigned int kFuseCount = 16808;

// CRC-16/CCITT-FALSE over exactly length bytes, excluding the '*' delimiter.
// data must address at least length bytes; it need not be null-terminated.
inline uint16_t Crc16(const char* data, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(static_cast<uint8_t>(data[i])) << 8;
    for (unsigned int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

// Returns the nibble value, or -1 for a non-hexadecimal character.
inline int HexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Returns the physical data-register width, or zero for an unmapped address.
inline unsigned int WordBits(unsigned int address) {
  if (address < 108 || (address >= 128 && address < 229)) {
    return 86;
  }
  if (address == 256) {
    return 32;
  }
  if (address == 512) {
    return 4;
  }
  if (address == 768) {
    return 16;
  }
  return 0;
}

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_PROTOCOL_H_
