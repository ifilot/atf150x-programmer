// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#ifndef ATF150X_PROGRAMMER_TESTS_TAP_MODEL_H_
#define ATF150X_PROGRAMMER_TESTS_TAP_MODEL_H_

#include <map>
#include <vector>

#include "cli/src/jedec.h"
#include "firmware/atf1502_programmer/protocol.h"
#include "tests/test_support.h"

namespace atf {
namespace testing {
// IEEE 1149.1 TAP model, independent state transition table.
struct Tap {
  enum {
    kReset,
    kIdle,
    kSelectDr,
    kCaptureDr,
    kShiftDr,
    kExit1Dr,
    kPauseDr,
    kExit2Dr,
    kUpdateDr,
    kSelectIr,
    kCaptureIr,
    kShiftIr,
    kExit1Ir,
    kPauseIr,
    kExit2Ir,
    kUpdateIr
  };
  int state = kReset;
  unsigned int ir = 0;
  unsigned int address = 0;
  unsigned int count = 0;
  std::vector<bool> shifted;
  std::vector<unsigned int> instructions;
  std::vector<unsigned int> delays;
  std::map<unsigned int, atf::Word> memory;
  atf::Word shift_data;
  bool enabled = false;
  bool erase_latched = false;

  // Interprets the bits shifted since Capture as a little-endian integer.
  unsigned int ShiftedInteger() {
    unsigned int n = 0;
    for (unsigned int i = 0; i < shifted.size() && i < 32; ++i) {
      if (shifted[i]) {
        n |= 1u << i;
      }
    }
    return n;
  }

  // Models Capture/Shift/Update transitions independently of the production
  // JTAG engine; observations are public so tests can assert protocol details.
  bool Clock(bool tms, bool tdi) {
    static const int kNextState[16][2] = {
        {kIdle, kReset},       {kIdle, kSelectDr},    {kCaptureDr, kSelectIr},
        {kShiftDr, kExit1Dr},  {kShiftDr, kExit1Dr},  {kPauseDr, kUpdateDr},
        {kPauseDr, kExit2Dr},  {kShiftDr, kUpdateDr}, {kIdle, kSelectDr},
        {kCaptureIr, kReset},  {kShiftIr, kExit1Ir},  {kShiftIr, kExit1Ir},
        {kPauseIr, kUpdateIr}, {kPauseIr, kExit2Ir},  {kShiftIr, kUpdateIr},
        {kIdle, kSelectDr}};
    bool out = false;
    if (state == kShiftDr || state == kShiftIr) {
      if (state == kShiftDr && ir == 0x059) {
        out = (atf::kDeviceId >> count) & 1;
      } else if (state == kShiftDr && (ir & ~3u) == 0x290 &&
                 memory.count(address)) {
        out = (memory[address][count / 8] >> (count % 8)) & 1;
      }
      shifted.push_back(tdi);
      ++count;
    }
    int entered = kNextState[state][tms];
    if (entered == kCaptureDr || entered == kCaptureIr) {
      count = 0;
      shifted.clear();
    }
    if (entered == kUpdateIr) {
      ATF_CHECK(count == 10);
      ir = ShiftedInteger();
      instructions.push_back(ir);
      if (ir == 0x2b3) {
        ATF_CHECK(enabled);
        erase_latched = true;
      }
      if (ir == 0x29e) {
        ATF_CHECK(enabled);
        if (erase_latched) {
          memory.clear();
          erase_latched = false;
        } else {
          memory[address] = shift_data;
        }
      }
    }
    if (entered == kUpdateDr) {
      if (ir == 0x059) {
        ATF_CHECK(count == 32);
      }
      if (ir == 0x280) {
        ATF_CHECK(count == 10);
        enabled = ShiftedInteger() == 0x1b9;
      }
      if (ir == 0x2a1) {
        ATF_CHECK(count == 11);
        address = ShiftedInteger();
      }
      if ((ir & ~3u) == 0x290) {
        ATF_CHECK(count == atf::WordBits(address));
        ATF_CHECK(ir == (0x290 | (address >> 8)));
        shift_data.assign((count + 7) / 8, 0);
        for (unsigned int i = 0; i < count; ++i) {
          if (shifted[i]) {
            shift_data[i / 8] |= 1u << (i % 8);
          }
        }
      }
    }
    state = entered;
    return out;
  }

  // Timing delays are legal only after the TAP has returned to Idle.
  void WaitMilliseconds(unsigned int n) {
    ATF_CHECK(state == kIdle);
    delays.push_back(n);
  }
};

}  // namespace testing
}  // namespace atf

#endif  // ATF150X_PROGRAMMER_TESTS_TAP_MODEL_H_
