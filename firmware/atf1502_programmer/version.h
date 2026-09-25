// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Shared semantic version for the firmware and host flasher.
 */
#ifndef ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_VERSION_H_
#define ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_VERSION_H_

namespace atf {

/** Semantic version shared by the Leonardo firmware and atfprog. */
constexpr char kVersion[] = "0.2.0";

/** Wire-protocol revision, versioned independently from the release. */
constexpr unsigned int kProtocolVersion = 2;

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_VERSION_H_
