// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#ifndef ATF150X_PROGRAMMER_CLI_SRC_JEDEC_H_
#define ATF150X_PROGRAMMER_CLI_SRC_JEDEC_H_

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cli/src/status.h"
#include "firmware/atf1502_programmer/protocol.h"

namespace atf {

using Fuses = std::array<uint8_t, kFuseCount>;
// Packed bytes in JTAG shift order: least-significant byte and bit first.
using Word = std::vector<uint8_t>;
using Image = std::map<unsigned int, Word>;

// Validates framing, checksums, fuse count and supported configuration options.
// On success, replaces *fuses with the complete map, including default fuses.
// The output pointer must not be null and is unchanged on failure.
Status ParseJedec(const std::string& text, Fuses* fuses);

// Maps binary fuse values to the 212 physical words, with unused cells erased.
// The caller must supply 16,808 binary values; reserved fuses are not mapped.
Image PackFuses(const Fuses& fuses);

// Encodes bytes in wire order, without reversing the byte sequence.
std::string EncodeHex(const Word& bytes);

// Decodes hexadecimal byte pairs. Output must be non-null and is unchanged on
// failure. Odd-length strings and non-hexadecimal characters are rejected.
Status DecodeHex(const std::string& text, Word* bytes);

// Reads binary data so CRLF bytes remain intact for checksum validation.
// Output must be non-null and is unchanged if the file cannot be read.
Status ReadFile(const std::string& path, std::string* text);

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_CLI_SRC_JEDEC_H_
