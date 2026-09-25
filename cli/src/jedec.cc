// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief JEDEC validation, fuse permutation and byte encoding implementations.
 */
// Fuse permutation derived from Project Bureau; see docs/THIRD_PARTY.md.
#include "cli/src/jedec.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cli/src/status.h"
#include "firmware/atf1502_programmer/protocol.h"
namespace atf {
namespace {

/**
 * @brief Removes surrounding JEDEC whitespace.
 *
 * @param[in] text Text to trim; interior whitespace is preserved.
 * @return Trimmed text, or an empty string if it contains only whitespace.
 */
std::string Trim(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? ""
               : text.substr(first,
                             text.find_last_not_of(" \t\r\n") - first + 1);
}

/**
 * @brief Parses an unsigned number without signs or overflow.
 *
 * @param[in] text Nonempty digit sequence without surrounding whitespace.
 * @param[in] base Radix, between 2 and 16.
 * @param[out] value Non-null destination; unchanged on failure.
 * @return Success or an invalid-number error.
 */
Status ParseNumber(const std::string& text, unsigned int base,
                   unsigned int* value) {
    if (text.empty()) {
        return Status("Missing JEDEC number");
    }
    unsigned int result = 0;
    for (char c : text) {
        int digit = HexDigit(c);
        if (digit < 0 || static_cast<unsigned int>(digit) >= base ||
            result > (std::numeric_limits<unsigned int>::max() -
                      static_cast<unsigned int>(digit)) /
                         base) {
            return Status("Invalid JEDEC number: " + text);
        }
        result = result * base + static_cast<unsigned int>(digit);
    }
    *value = result;
    return Status();
}

/**
 * @brief Validates framing and extracts star-terminated JEDEC records.
 *
 * A zero transmission checksum disables that check. The header is skipped;
 * record whitespace is trimmed only after the original bytes are checked.
 *
 * @param[in] text Unmodified file bytes, including framing and checksum.
 * @param[out] output Non-null record list; unchanged on failure.
 * @return Success or a framing, termination or transmission-checksum error.
 */
Status ReadRecords(const std::string& text, std::vector<std::string>* output) {
    size_t stx = text.find('\x02');
    size_t etx = text.find('\x03');
    if (stx == std::string::npos || etx == std::string::npos || stx >= etx ||
        text.find('\x02', stx + 1) != std::string::npos ||
        text.find('\x03', etx + 1) != std::string::npos) {
        return Status("Expected one JEDEC STX/ETX frame");
    }
    auto checksum = Trim(text.substr(etx + 1));
    if (checksum.size() != 4) {
        return Status("Missing JEDEC transmission checksum");
    }
    unsigned int transmission = 0;
    Status status = ParseNumber(checksum, 16, &transmission);
    if (!status.ok()) {
        return status;
    }
    unsigned int sum = 0;
    for (size_t i = stx; i <= etx; ++i) {
        sum += static_cast<uint8_t>(text[i]);
    }
    if (transmission && (sum & 0xffff) != transmission) {
        return Status("JEDEC transmission checksum mismatch");
    }
    auto first = text.find('*', stx + 1);
    if (first == std::string::npos || first >= etx) {
        return Status("Missing JEDEC header terminator");
    }
    std::vector<std::string> records;
    for (size_t pos = first + 1; pos < etx;) {
        auto stop = text.find('*', pos);
        if (stop == std::string::npos || stop >= etx) {
            if (!Trim(text.substr(pos, etx - pos)).empty()) {
                return Status("Unterminated JEDEC record");
            }
            break;
        }
        records.push_back(Trim(text.substr(pos, stop - pos)));
        pos = stop + 1;
    }
    *output = std::move(records);
    return Status();
}

struct JedecOptions {
    Device device = Device::kUnknown;
    uint8_t default_fuse = 0;
    unsigned int checksum = 0;
};

/**
 * @brief Collects required metadata and rejects unsupported record types.
 *
 * @param[in] records Trimmed records from ReadRecords().
 * @param[out] output Non-null metadata destination; unchanged on failure.
 * @return Success or an invalid/missing metadata or unsupported-record error.
 */
Status ReadOptions(const std::vector<std::string>& records,
                   JedecOptions* output) {
    Status status;
    // Read metadata first: sparse L records may appear before the default.
    bool have_fuse_count = false;
    bool have_default = false;
    bool have_checksum = false;
    bool have_security = false;
    unsigned int default_fuse = 0;
    unsigned int expected = 0;
    for (const auto& r : records) {
        if (r.rfind("QF", 0) == 0) {
            unsigned int count = 0;
            status = ParseNumber(Trim(r.substr(2)), 10, &count);
            if (!status.ok()) {
                return status;
            }
            Device device = DeviceFromFuseCount(count);
            if (have_fuse_count || device == Device::kUnknown) {
                return Status(
                    "Expected exactly one supported QF value (16808 or 34192)");
            }
            have_fuse_count = true;
            output->device = device;
        } else if (r.rfind("F", 0) == 0) {
            if (have_default || (r != "F0" && r != "F1")) {
                return Status("Invalid JEDEC default fuse");
            }
            have_default = true;
            default_fuse = static_cast<unsigned int>(r[1] - '0');
        } else if (r.rfind("C", 0) == 0) {
            if (have_checksum || r.size() != 5) {
                return Status("Invalid JEDEC fuse checksum");
            }
            have_checksum = true;
            status = ParseNumber(r.substr(1), 16, &expected);
            if (!status.ok()) {
                return status;
            }
        } else if (r.rfind("G", 0) == 0) {
            if (have_security || r != "G0") {
                return Status("Security requests are unsupported");
            }
            have_security = true;
        } else if (!r.empty() && r[0] != 'L' && r[0] != 'N' &&
                   r.rfind("QP", 0) != 0 && r.rfind("QV", 0) != 0 &&
                   r.rfind("J", 0) != 0) {
            return Status("Unsupported JEDEC record: " + r.substr(0, 32));
        }
    }
    if (!have_fuse_count || !have_default || !have_checksum) {
        return Status("JEDEC requires QF, F and C records");
    }
    output->default_fuse = static_cast<uint8_t>(default_fuse);
    output->checksum = expected;
    return Status();
}

/**
 * @brief Applies sparse data records without ambiguous assignments.
 *
 * @param[in] records Trimmed records; only L records write fuse values.
 * @param[in,out] output Non-null map initialized to defaults; unchanged on
 * failure.
 * @return Success or a malformed, overlapping or out-of-range fuse error.
 */
Status ApplyFuseRecords(const std::vector<std::string>& records,
                        Fuses* output) {
    Status status;
    Fuses fuses = *output;
    Fuses assigned(output->size(), 0);
    for (const auto& r : records) {
        if (!r.empty() && r[0] == 'L') {
            auto space = r.find_first_of(" \t\r\n", 1);
            if (space == std::string::npos) {
                return Status("Missing L record bits");
            }
            unsigned int index = 0;
            status = ParseNumber(r.substr(1, space - 1), 10, &index);
            if (!status.ok()) {
                return status;
            }
            unsigned int count = 0;
            for (size_t i = space; i < r.size(); ++i) {
                if (std::isspace(static_cast<unsigned char>(r[i]))) {
                    continue;
                }
                if ((r[i] != '0' && r[i] != '1') || index >= output->size()) {
                    return Status("Invalid or out-of-range L record");
                }
                if (assigned[index]) {
                    return Status("Overlapping L records");
                }
                assigned[index] = 1;
                fuses[index++] = static_cast<uint8_t>(r[i] - '0');
                ++count;
            }
            if (!count) {
                return Status("Empty L record");
            }
        }
    }
    *output = fuses;
    return Status();
}

/**
 * @brief Checks the complete map before conversion or programming.
 *
 * @param[in] device Device selected from the JEDEC fuse count.
 * @param[in] fuses Complete binary fuse map, including defaults.
 * @param[in] expected Expected 16-bit sum of the packed fuse bytes.
 * @return Success or a checksum, JTAG/security or reserved-fuse error.
 */
Status ValidateFuses(Device device, const Fuses& fuses, unsigned int expected) {
    if (fuses.size() != FuseCount(device)) {
        return Status("JEDEC fuse count does not match device");
    }
    // JESD3 packs fuse zero into the least-significant bit of the first byte.
    unsigned int sum = 0;
    for (unsigned int i = 0; i < fuses.size(); ++i) {
        sum += static_cast<unsigned int>(fuses[i]) << (i % 8);
    }
    if ((sum & 0xffff) != expected) {
        return Status("JEDEC fuse checksum mismatch");
    }
    // Never disable JTAG or readback: both are required for subsequent access.
    unsigned int jtag = JtagFuseStart(device);
    if (!fuses[jtag] || !fuses[jtag + 1] || !fuses[jtag + 2] ||
        !fuses[jtag + 3]) {
        return Status(
            "JTAG/security word must be 1111; locking is unsupported");
    }
    for (unsigned int i = ReservedFuseStart(device); i < fuses.size(); ++i) {
        if (fuses[i]) {
            return Status("Reserved JEDEC fuses must be zero");
        }
    }
    return Status();
}

}  // namespace

/**
 * @brief Validates the complete document before publishing its fuse map.
 */
Status ParseJedec(const std::string& text, JedecFile* output) {
    std::vector<std::string> records;
    Status status = ReadRecords(text, &records);
    if (!status.ok()) {
        return status;
    }
    JedecOptions options;
    status = ReadOptions(records, &options);
    if (!status.ok()) {
        return status;
    }
    // Build locally: failure must never expose a partially validated image.
    Fuses fuses(FuseCount(options.device), options.default_fuse);
    status = ApplyFuseRecords(records, &fuses);
    if (!status.ok()) {
        return status;
    }
    status = ValidateFuses(options.device, fuses, options.checksum);
    if (!status.ok()) {
        return status;
    }
    JedecFile result;
    result.device = options.device;
    result.fuses = std::move(fuses);
    *output = std::move(result);
    return Status();
}

/**
 * @brief Applies the Project Bureau permutation to the physical word map.
 */
Image PackFuses(Device device, const Fuses& fuses) {
    Image image;
    // Project Bureau maps the two logic banks, routing, configuration and UES.
    // The six trailing reserved JEDEC fuses have no physical coordinates.
    for (unsigned int i = 0; i < ReservedFuseStart(device); ++i) {
        unsigned int row;
        unsigned int col;
        if (device == Device::kAtf1502as) {
            if (i < 7680) {
                row = 12 + i % 96;
                col = 79 - i / 96;
            } else if (i < 15360) {
                row = 128 + (i - 7680) % 96;
                col = 79 - (i - 7680) / 96;
            } else if (i < 16320) {
                row = (i - 15360) / 80;
                col = 79 - (i - 15360) % 80;
            } else if (i < 16720) {
                row = 224 + (i - 16320) % 5;
                col = 79 - (i - 16320) / 5;
            } else if (i < 16750) {
                row = 224 + (i - 16720) % 5;
                col = 85 - (i - 16720) / 5;
            } else if (i < 16782) {
                row = 256;
                col = 31 - (i - 16750);
            } else if (i < 16786) {
                row = 512;
                col = 3 - (i - 16782);
            } else {
                row = 768;
                col = 15 - (i - 16786);
            }
        } else {
            if (i < 15360) {
                row = 12 + i % 96;
                col = 165 - i / 96;
            } else if (i < 30720) {
                row = 128 + (i - 15360) % 96;
                col = 165 - (i - 15360) / 96;
            } else if (i < 32640) {
                row = (i - 30720) / 160;
                col = 165 - (i - 30720) % 160;
            } else if (i < 34134) {
                row = 224 + (i - 32640) % 9;
                col = 165 - (i - 32640) / 9;
            } else if (i < 34166) {
                row = 256;
                col = 31 - (i - 34134);
            } else if (i < 34170) {
                row = 512;
                col = 3 - (i - 34166);
            } else {
                row = 768;
                col = 15 - (i - 34170);
            }
        }
        auto& word = image[row];
        if (word.empty()) {
            // Padding cells remain erased, but unused wire bits must be zero.
            unsigned int bits = WordBits(device, row);
            word.assign((bits + 7) / 8, 0xff);
            if (bits % 8) {
                word.back() = static_cast<uint8_t>((1u << (bits % 8)) - 1);
            }
        }
        if (!fuses[i]) {
            word[col / 8] &= static_cast<uint8_t>(~(1u << (col % 8)));
        }
    }
    return image;
}

/**
 * @brief Converts each byte to two hexadecimal characters in wire order.
 */
std::string EncodeHex(const Word& bytes) {
    static const char kHexDigits[] = "0123456789ABCDEF";
    std::string s;
    for (auto b : bytes) {
        s += kHexDigits[b >> 4];
        s += kHexDigits[b & 15];
    }
    return s;
}

/**
 * @brief Validates all byte pairs before replacing the output.
 */
Status DecodeHex(const std::string& text, Word* output) {
    if (text.size() % 2) {
        return Status("Odd hex length");
    }
    Word bytes;
    for (size_t i = 0; i < text.size(); i += 2) {
        int high_nibble = HexDigit(text[i]);
        int low_nibble = HexDigit(text[i + 1]);
        if (high_nibble < 0 || low_nibble < 0) {
            return Status("Invalid hex data");
        }
        bytes.push_back(static_cast<uint8_t>((high_nibble << 4) | low_nibble));
    }
    *output = std::move(bytes);
    return Status();
}

/**
 * @brief Loads file bytes without text-mode newline conversion.
 */
Status ReadFile(const std::string& path, std::string* output) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Status("Cannot open " + path);
    }
    std::ostringstream s;
    s << file.rdbuf();
    if (file.bad()) {
        return Status("Cannot read " + path);
    }
    *output = s.str();
    return Status();
}
}  // namespace atf
