// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Independent TAP state machine used to validate the JTAG engine.
 */
#ifndef ATF150X_PROGRAMMER_TESTS_TAP_MODEL_H_
#define ATF150X_PROGRAMMER_TESTS_TAP_MODEL_H_

#include <map>
#include <vector>

#include "cli/src/jedec.h"
#include "firmware/atf1502_programmer/protocol.h"
#include "tests/test_support.h"

namespace atf {
namespace testing {

/**
 * @brief Models TAP transitions independently of the production scan code.
 *
 * Public observations let tests check instruction order, timing and data.
 * This is a sequencing model, not a complete electrical or Flash simulator.
 */
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
    atf::Device device = atf::Device::kAtf1502as;

    /**
     * @brief Interprets the captured shift history as a little-endian integer.
     *
     * @return The value of up to the first 32 shifted bits.
     */
    unsigned int ShiftedInteger() {
        unsigned int n = 0;
        for (unsigned int i = 0; i < shifted.size() && i < 32; ++i) {
            if (shifted[i]) {
                n |= 1u << i;
            }
        }
        return n;
    }

    /**
     * @brief Advances one TAP clock and applies Capture/Shift/Update effects.
     *
     * Aborts the test if an instruction uses the wrong register width or
     * programming occurs outside an enabled session.
     *
     * @param[in] tms Transition bit selecting the next TAP state.
     * @param[in] tdi Data bit sampled when the current state is Shift.
     * @return TDO bit sampled from the model before the transition.
     */
    bool Clock(bool tms, bool tdi) {
        static const int kNextState[16][2] = {
            {kIdle, kReset},         {kIdle, kSelectDr},
            {kCaptureDr, kSelectIr}, {kShiftDr, kExit1Dr},
            {kShiftDr, kExit1Dr},    {kPauseDr, kUpdateDr},
            {kPauseDr, kExit2Dr},    {kShiftDr, kUpdateDr},
            {kIdle, kSelectDr},      {kCaptureIr, kReset},
            {kShiftIr, kExit1Ir},    {kShiftIr, kExit1Ir},
            {kPauseIr, kUpdateIr},   {kPauseIr, kExit2Ir},
            {kShiftIr, kUpdateIr},   {kIdle, kSelectDr}};
        bool out = false;
        if (state == kShiftDr || state == kShiftIr) {
            if (state == kShiftDr && ir == 0x059) {
                out = (atf::DeviceId(device) >> count) & 1;
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
                ATF_CHECK(count == atf::WordBits(device, address));
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

    /**
     * @brief Records an algorithm delay without sleeping.
     *
     * Aborts the test unless the TAP is in Run-Test/Idle.
     *
     * @param[in] n Requested delay in milliseconds.
     */
    void WaitMilliseconds(unsigned int n) {
        ATF_CHECK(state == kIdle);
        delays.push_back(n);
    }
};

}  // namespace testing
}  // namespace atf

#endif  // ATF150X_PROGRAMMER_TESTS_TAP_MODEL_H_
