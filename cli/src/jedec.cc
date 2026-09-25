// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
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

std::string Trim(const std::string& text) {
  size_t first = text.find_first_not_of(" \t\r\n");
  return first == std::string::npos
             ? ""
             : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
// Parses without signs or overflow; malformed input never reaches a fuse index.
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

// Extracts records without normalizing bytes used by the transmission checksum.
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
  uint8_t default_fuse = 0;
  unsigned int checksum = 0;
};

// Collects required metadata and rejects unsupported record types.
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
      if (have_fuse_count || count != kFuseCount) {
        return Status("Expected exactly one QF16808 (ATF1502AS)");
      }
      have_fuse_count = true;
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

// Applies sparse data records while rejecting ambiguous overlapping
// assignments.
Status ApplyFuseRecords(const std::vector<std::string>& records,
                        Fuses* output) {
  Status status;
  Fuses fuses = *output;
  Fuses assigned{};
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
        if ((r[i] != '0' && r[i] != '1') || index >= kFuseCount) {
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

// Checks the complete logical map before it can be converted or programmed.
Status ValidateFuses(const Fuses& fuses, unsigned int expected) {
  // JESD3 packs fuse zero into the least-significant bit of the first byte.
  unsigned int sum = 0;
  for (unsigned int i = 0; i < kFuseCount; ++i) {
    sum += static_cast<unsigned int>(fuses[i]) << (i % 8);
  }
  if ((sum & 0xffff) != expected) {
    return Status("JEDEC fuse checksum mismatch");
  }
  // Never disable JTAG or readback: both are required for subsequent access.
  if (!fuses[16782] || !fuses[16783] || !fuses[16784] || !fuses[16785]) {
    return Status("JTAG/security word must be 1111; locking is unsupported");
  }
  for (unsigned int i = 16802; i < kFuseCount; ++i) {
    if (fuses[i]) {
      return Status("Reserved JEDEC fuses must be zero");
    }
  }
  return Status();
}

}  // namespace

Status ParseJedec(const std::string& text, Fuses* output) {
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
  Fuses fuses;
  fuses.fill(options.default_fuse);
  status = ApplyFuseRecords(records, &fuses);
  if (!status.ok()) {
    return status;
  }
  status = ValidateFuses(fuses, options.checksum);
  if (!status.ok()) {
    return status;
  }
  *output = fuses;
  return Status();
}

Image PackFuses(const Fuses& fuses) {
  Image image;
  // Project Bureau maps the two logic banks, routing, configuration and UES.
  // The six trailing reserved JEDEC fuses have no physical coordinates.
  for (unsigned int i = 0; i < 16802; ++i) {
    unsigned int row;
    unsigned int col;
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
    auto& word = image[row];
    if (word.empty()) {
      // Padding cells remain erased, but unused wire bits must be zero.
      unsigned int bits = WordBits(row);
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

std::string EncodeHex(const Word& bytes) {
  static const char kHexDigits[] = "0123456789ABCDEF";
  std::string s;
  for (auto b : bytes) {
    s += kHexDigits[b >> 4];
    s += kHexDigits[b & 15];
  }
  return s;
}

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
