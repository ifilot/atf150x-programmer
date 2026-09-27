// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Front-end independent erase, program, verify and activation flows.
 */
#ifndef ATF150X_PROGRAMMER_CLI_SRC_PROGRAMMER_H_
#define ATF150X_PROGRAMMER_CLI_SRC_PROGRAMMER_H_

#include <cstdint>
#include <functional>
#include <string>

#include "cli/src/jedec.h"
#include "cli/src/serial.h"
#include "cli/src/status.h"
#include "firmware/atf1502_programmer/protocol.h"

namespace atf {

/** @brief Device operations that run inside a programming session. */
enum class Operation : uint8_t {
    kErase,
    kFlash,
    kVerify,
};

/** @brief Observable phases of an operation, in execution order. */
enum class Stage : uint8_t {
    kErasing,
    kBlankChecking,
    kProgramming,
    kVerifying,
    kActivating,
};

/**
 * @brief Receives progress from the calling thread during an operation.
 *
 * Called with done == 0 when a stage starts and again as words complete; the
 * last call of a stage has done == total. Must not use the connection.
 */
using ProgressCallback =
    std::function<void(Stage stage, unsigned int done, unsigned int total)>;

/**
 * @brief Returns a short human-readable stage name.
 *
 * @param[in] stage Operation phase.
 * @return Static capitalized name such as "Programming".
 */
const char* StageName(Stage stage);

/**
 * @brief Converts an ID command reply into a supported device selector.
 *
 * @param[in] idcode Eight hexadecimal digits as returned by the firmware.
 * @return Matching selector, or Device::kUnknown for malformed or other IDs.
 */
Device DeviceFromIdText(const std::string& idcode);

/**
 * @brief Compares every mapped bit, including padding and configuration.
 *
 * Reads the target without erasing or programming.
 *
 * @param[in,out] connection Open connection in an enabled programming session.
 * @param[in] image Expected row addresses and packed bytes.
 * @param[in] stage Stage reported to progress, such as blank checking.
 * @param[in] progress Optional progress receiver; may be empty.
 * @return Success or the first transport, decoding or readback error.
 */
Status VerifyImage(Connection& connection, const Image& image, Stage stage,
                   const ProgressCallback& progress);

/**
 * @brief Enters programming mode, runs one operation and always leaves it.
 *
 * Erase and flash destroy the existing design. Flash erases, blank-checks,
 * programs with the arming switch held safe, verifies, and only then applies
 * the requested arming bit. END is attempted even after a failure, preserving
 * the original diagnostic.
 *
 * @param[in,out] connection Open connection with a validated handshake.
 * @param[in] operation Erase, flash or verify.
 * @param[in] device Device identified from the physical IDCODE.
 * @param[in] image Expected map for flash and verify; unused for erase.
 * @param[in] progress Optional progress receiver; may be empty.
 * @return Success or the first session, erase, program, verify or END error.
 */
Status RunSession(Connection& connection, Operation operation, Device device,
                  const Image& image, const ProgressCallback& progress);

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_CLI_SRC_PROGRAMMER_H_
