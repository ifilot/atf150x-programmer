// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "cli/src/jedec.h"
#include "firmware/atf1502_programmer/jtag.h"
#include "firmware/atf1502_programmer/protocol.h"
#include "tests/tap_model.h"
#include "tests/test_support.h"
namespace {

// Generate checksummed fixtures so rejected options are tested independently
// of checksum failures.
std::string BuildJedec(const atf::Fuses& f) {
  unsigned int sum = 0;
  std::ostringstream s;
  s << '\x02' << "test*QF16808*F0*L0 ";
  for (unsigned int i = 0; i < f.size(); ++i) {
    s << static_cast<unsigned int>(f[i]);
    sum += static_cast<unsigned int>(f[i]) << (i % 8);
  }
  s << "*C" << std::hex << std::setw(4) << std::setfill('0') << (sum & 65535)
    << "*\x03" << "0000";
  return s.str();
}

atf::Fuses ParseForTest(const std::string& text) {
  atf::Fuses fuses;
  ATF_CHECK(atf::ParseJedec(text, &fuses).ok());
  return fuses;
}

atf::Word DecodeForTest(const std::string& text) {
  atf::Word bytes;
  ATF_CHECK(atf::DecodeHex(text, &bytes).ok());
  return bytes;
}

void ExpectRejected(const std::string& text) {
  atf::Fuses fuses;
  ATF_CHECK(!atf::ParseJedec(text, &fuses).ok());
}

}  // namespace

int main() {
  ATF_CHECK(atf::Crc16("123456789", 9) == 0x29b1);
  atf::Fuses f{};
  for (unsigned int i = 16782; i < 16786; ++i) {
    f[i] = 1;
  }
  f[0] = 1;
  f[7679] = 1;
  f[7680] = 1;
  f[15359] = 1;
  f[15360] = 1;
  f[16319] = 1;
  f[16320] = 1;
  f[16719] = 1;
  f[16720] = 1;
  f[16749] = 1;
  f[16786] = 1;
  auto text = BuildJedec(f);
  ATF_CHECK(ParseForTest(text) == f);
  // Failed status-returning APIs must not publish partial output.
  atf::Fuses unchanged_fuses = f;
  ATF_CHECK(!atf::ParseJedec("invalid", &unchanged_fuses).ok());
  ATF_CHECK(unchanged_fuses == f);
  atf::Word unchanged_bytes{0xa5};
  ATF_CHECK(!atf::DecodeHex("01XZ", &unchanged_bytes).ok());
  ATF_CHECK(unchanged_bytes == atf::Word{0xa5});

  auto image = atf::PackFuses(f);
  ATF_CHECK(image.size() == 212);
  auto bit = [&](unsigned int r, unsigned int c) {
    return (image.at(r)[c / 8] >> (c % 8)) & 1;
  };
  ATF_CHECK(bit(12, 79));
  ATF_CHECK(bit(107, 0));
  ATF_CHECK(bit(128, 79));
  ATF_CHECK(bit(223, 0));
  ATF_CHECK(bit(0, 79));
  ATF_CHECK(bit(11, 0));
  ATF_CHECK(bit(224, 79));
  ATF_CHECK(bit(228, 0));
  ATF_CHECK(bit(224, 85));
  ATF_CHECK(bit(228, 80));
  ATF_CHECK(bit(768, 15));
  ATF_CHECK(!bit(768, 14));
  ATF_CHECK(image.at(512)[0] == 15);
  ATF_CHECK(image.at(0)[10] == 0x3f);
  ATF_CHECK(image.at(256).size() == 4);
  ATF_CHECK(!image.count(108) && !image.count(229));
  for (const auto& e : image) {
    ATF_CHECK(DecodeForTest(atf::EncodeHex(e.second)) == e.second);
  }
  // Every physical mapped cell has one distinct JEDEC index (including all
  // boundaries).
  atf::Fuses ones{};
  ones.fill(1);
  auto all = atf::PackFuses(ones);
  size_t cell_count = 0;
  for (const auto& e : all) {
    for (unsigned int col = 0; col < atf::WordBits(e.first); ++col) {
      unsigned int index = 16808, r = e.first;
      if (r < 12 && col < 80) {
        index = 15360 + r * 80 + 79 - col;
      } else if (r >= 12 && r < 108 && col < 80) {
        index = r - 12 + (79 - col) * 96;
      } else if (r >= 128 && r < 224 && col < 80) {
        index = 7680 + r - 128 + (79 - col) * 96;
      } else if (r >= 224 && r < 229) {
        index = col < 80 ? 16320 + r - 224 + (79 - col) * 5
                         : 16720 + r - 224 + (85 - col) * 5;
      } else if (r == 256) {
        index = 16750 + 31 - col;
      } else if (r == 512) {
        index = 16782 + 3 - col;
      } else if (r == 768) {
        index = 16786 + 15 - col;
      }
      if (index < 16802) {
        ATF_CHECK(static_cast<unsigned int>(
                      (image.at(r)[col / 8] >> (col % 8)) & 1) == f[index]);
        ++cell_count;
      } else {
        ATF_CHECK((image.at(r)[col / 8] >> (col % 8)) & 1);
      }
    }
  }
  ATF_CHECK(cell_count == 16802);
  auto replace = [&](const std::string& from, const std::string& to) {
    auto s = text;
    s.replace(s.find(from), from.size(), to);
    return s;
  };
  ExpectRejected(replace("QF16808", "QF34192"));
  ExpectRejected(replace("F0", "F2"));
  ExpectRejected(replace("L0 ", "L16808 "));
  ExpectRejected(replace("*C", "*L0 1*C"));
  ExpectRejected(replace("*C", "*G1*C"));
  ExpectRejected(replace("*C", "*X0*C"));
  ExpectRejected(replace("QF16808*", "QF16808*QF16808*"));
  ExpectRejected(replace("0000", "0001"));
  auto corrupt = text;
  corrupt[corrupt.find("L0 ") + 3] = '0';
  ExpectRejected(corrupt);
  for (unsigned int i : {16782u, 16783u, 16784u, 16785u, 16802u}) {
    auto bad = f;
    bad[i] ^= 1;
    ExpectRejected(BuildJedec(bad));
  }
  unsigned int sum = 0;
  auto etx = text.find('\x03');
  for (size_t i = 0; i <= etx; ++i) {
    sum += static_cast<uint8_t>(text[i]);
  }
  std::ostringstream checksum;
  checksum << std::hex << std::setw(4) << std::setfill('0') << (sum & 65535);
  ATF_CHECK(ParseForTest(text.substr(0, etx + 1) + checksum.str()) == f);
  // Exercise the actual firmware JTAG engine with independent TAP transitions.
  atf::testing::Tap tap;
  atf::Jtag<atf::testing::Tap> jtag(tap);
  ATF_CHECK(jtag.Identify() == atf::kDeviceId);
  ATF_CHECK(tap.state == atf::testing::Tap::kIdle);
  jtag.Enable();
  ATF_CHECK(tap.enabled);
  jtag.Erase();
  ATF_CHECK(tap.delays.back() == 210);
  for (unsigned int addr : {0u, 107u, 128u, 228u, 256u, 512u, 768u}) {
    auto word = image.at(addr);
    jtag.Program(addr, word.data());
    ATF_CHECK(tap.delays.back() == 30);
    atf::Word got(word.size());
    jtag.Read(addr, got.data());
    ATF_CHECK(tap.delays.back() == 20);
    ATF_CHECK(got == word);
  }
  jtag.Disable();
  ATF_CHECK(!tap.enabled);
  ATF_CHECK(tap.state == atf::testing::Tap::kIdle);
  std::cout << "Core tests passed\n";
  return 0;
}
