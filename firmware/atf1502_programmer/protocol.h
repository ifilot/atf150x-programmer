// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Shared CRC, device identity and physical word geometry helpers.
 */
#ifndef ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_PROTOCOL_H_
#define ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

namespace atf {

constexpr uint32_t kDeviceId = 0x0150203f;
constexpr unsigned int kFuseCount = 16808;

/**
 * @brief Computes CRC-16/CCITT-FALSE for the supplied bytes.
 *
 * @param[in] data Buffer of at least length bytes; may be null only for zero
 * length.
 * @param[in] length Number of bytes, excluding any protocol star delimiter.
 * @return 16-bit CRC with polynomial 0x1021, seed 0xFFFF and no final XOR.
 */
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

/**
 * @brief Converts one hexadecimal character to its nibble value.
 *
 * @param[in] c Character to decode; either letter case is accepted.
 * @return A value from zero through 15, or -1 for an invalid character.
 */
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

/**
 * @brief Looks up the ATF1502AS data-register width for a physical word.
 *
 * @param[in] address Physical Flash row address.
 * @return Width in bits, or zero for an unmapped address.
 */
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
