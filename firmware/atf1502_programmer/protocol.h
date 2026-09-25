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

/** @brief Devices understood by the host and firmware protocol. */
enum class Device : uint8_t {
    kUnknown,
    kAtf1502as,
    kAtf1504as,
};

constexpr uint32_t kAtf1502Id = 0x0150203f;
constexpr uint32_t kAtf1504Id = 0x0150403f;

/**
 * @brief Converts a JTAG IDCODE into a supported device selector.
 *
 * @param[in] idcode Raw 32-bit JTAG identification code.
 * @return Matching selector, or Device::kUnknown.
 */
inline Device DeviceFromId(uint32_t idcode) {
    if (idcode == kAtf1502Id) {
        return Device::kAtf1502as;
    }
    if (idcode == kAtf1504Id) {
        return Device::kAtf1504as;
    }
    return Device::kUnknown;
}

/**
 * @brief Returns the JTAG IDCODE for a supported device.
 *
 * @param[in] device Device selector.
 * @return IDCODE, or zero for Device::kUnknown.
 */
inline uint32_t DeviceId(Device device) {
    if (device == Device::kAtf1502as) {
        return kAtf1502Id;
    }
    if (device == Device::kAtf1504as) {
        return kAtf1504Id;
    }
    return 0;
}

/**
 * @brief Returns the printable device name.
 *
 * @param[in] device Device selector.
 * @return Static name string.
 */
inline const char* DeviceName(Device device) {
    if (device == Device::kAtf1502as) {
        return "ATF1502AS";
    }
    if (device == Device::kAtf1504as) {
        return "ATF1504AS";
    }
    return "unknown device";
}

/**
 * @brief Returns the JEDEC fuse count for a supported device.
 *
 * @param[in] device Device selector.
 * @return Fuse count, or zero for Device::kUnknown.
 */
inline unsigned int FuseCount(Device device) {
    if (device == Device::kAtf1502as) {
        return 16808;
    }
    if (device == Device::kAtf1504as) {
        return 34192;
    }
    return 0;
}

/**
 * @brief Selects a device from its JEDEC fuse count.
 *
 * @param[in] count QF value from the JEDEC file.
 * @return Matching selector, or Device::kUnknown.
 */
inline Device DeviceFromFuseCount(unsigned int count) {
    if (count == 16808) {
        return Device::kAtf1502as;
    }
    if (count == 34192) {
        return Device::kAtf1504as;
    }
    return Device::kUnknown;
}

/**
 * @brief Returns the first trailing reserved JEDEC fuse.
 *
 * @param[in] device Device selector.
 * @return First reserved fuse, or zero for Device::kUnknown.
 */
inline unsigned int ReservedFuseStart(Device device) {
    if (device == Device::kAtf1502as) {
        return 16802;
    }
    return device == Device::kAtf1504as ? 34186 : 0;
}

/**
 * @brief Returns the first fuse in the four-bit JTAG/security word.
 *
 * @param[in] device Device selector.
 * @return First JTAG fuse, or zero for Device::kUnknown.
 */
inline unsigned int JtagFuseStart(Device device) {
    if (device == Device::kAtf1502as) {
        return 16782;
    }
    return device == Device::kAtf1504as ? 34166 : 0;
}

/**
 * @brief Returns the arming-switch JEDEC fuse index.
 *
 * @param[in] device Device selector.
 * @return Arming fuse index, or zero for Device::kUnknown.
 */
inline unsigned int ArmingFuse(Device device) {
    if (device == Device::kAtf1502as) {
        return 16750;
    }
    return device == Device::kAtf1504as ? 34134 : 0;
}

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
 * @brief Looks up a device's data-register width for a physical word.
 *
 * @param[in] device Device selector.
 * @param[in] address Physical Flash row address.
 * @return Width in bits, or zero for an unmapped address.
 */
inline unsigned int WordBits(Device device, unsigned int address) {
    if (device == Device::kAtf1502as &&
        (address < 108 || (address >= 128 && address < 229))) {
        return 86;
    }
    if (device == Device::kAtf1504as &&
        (address < 108 || (address >= 128 && address < 233))) {
        return 166;
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
