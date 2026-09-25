// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Validated ATF1502AS/ATF1504AS JEDEC and fuse-map interfaces.
 */
#ifndef ATF150X_PROGRAMMER_CLI_SRC_JEDEC_H_
#define ATF150X_PROGRAMMER_CLI_SRC_JEDEC_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cli/src/status.h"
#include "firmware/atf1502_programmer/protocol.h"

namespace atf {

using Fuses = std::vector<uint8_t>;
// Packed bytes in JTAG shift order: least-significant byte and bit first.
using Word = std::vector<uint8_t>;
using Image = std::map<unsigned int, Word>;

/** @brief Fully validated JEDEC data and its inferred target device. */
struct JedecFile {
    Device device = Device::kUnknown;
    Fuses fuses;
};

/**
 * @brief Parses and validates an ATF1502AS or ATF1504AS JEDEC file.
 *
 * Accepts sparse fuse records and checks the complete resulting map. Rejects
 * unsupported fuse counts, locking options and nonzero reserved fuses.
 *
 * @param[in] text Complete file contents, preserving original line endings.
 * @param[out] output Non-null destination; unchanged on failure.
 * @return Success or a framing, checksum, record or configuration error.
 */
Status ParseJedec(const std::string& text, JedecFile* output);

/**
 * @brief Maps JEDEC fuse indices to physical Flash words.
 *
 * The caller supplies validated binary values. Reserved JEDEC fuses are
 * omitted. Byte and bit zero shift first; unused wire bits are zero.
 *
 * @param[in] device Target device matching the fuse vector.
 * @param[in] fuses Complete binary fuse vector.
 * @return Mapped physical words, with unused physical cells set to one.
 */
Image PackFuses(Device device, const Fuses& fuses);

/**
 * @brief Encodes bytes as uppercase hexadecimal pairs.
 *
 * @param[in] bytes Bytes already arranged in wire order.
 * @return Two hexadecimal characters per byte; byte order is preserved.
 */
std::string EncodeHex(const Word& bytes);

/**
 * @brief Decodes hexadecimal byte pairs without reordering them.
 *
 * @param[in] text Even-length hexadecimal string; letter case is ignored.
 * @param[out] bytes Non-null destination; unchanged on failure.
 * @return Success, or an error for odd length or invalid characters.
 */
Status DecodeHex(const std::string& text, Word* bytes);

/**
 * @brief Reads a complete file without newline translation.
 *
 * Binary mode preserves CRLF bytes used by the transmission checksum.
 *
 * @param[in] path File path in the host operating system.
 * @param[out] text Non-null destination; unchanged on failure.
 * @return Success or a file-open/read error.
 */
Status ReadFile(const std::string& path, std::string* text);

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_CLI_SRC_JEDEC_H_
