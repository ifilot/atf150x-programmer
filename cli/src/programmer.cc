// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Erase, program, verify and activation flows shared by all front ends.
 */
#include "cli/src/programmer.h"

#include <cstdint>
#include <sstream>
#include <string>

namespace atf {
namespace {

/**
 * @brief Formats a physical row address for the wire protocol.
 *
 * @param[in] row Physical Flash row address.
 * @return Lowercase hexadecimal without a prefix or leading padding.
 */
std::string FormatAddress(unsigned int row) {
    std::ostringstream text;
    text << std::hex << row;
    return text.str();
}

/**
 * @brief Forwards progress when a receiver is present.
 *
 * @param[in] progress Possibly empty progress receiver.
 * @param[in] stage Current operation phase.
 * @param[in] done Completed units within the stage.
 * @param[in] total Total units within the stage.
 */
void Report(const ProgressCallback& progress, Stage stage, unsigned int done,
            unsigned int total) {
    if (progress) {
        progress(stage, done, total);
    }
}

/**
 * @brief Programs and verifies the staged image before final activation.
 *
 * The device must already be erased and blank-checked. Holds the arming bit
 * safe until all staged rows verify, then applies the requested final bit.
 * An error may leave a partial image; the caller must end the session.
 *
 * @param[in,out] connection Open connection with programming mode enabled.
 * @param[in] image Complete validated map, including the configuration word.
 * @param[in] progress Possibly empty progress receiver.
 * @return Success or the first programming/readback error.
 */
Status ProgramImage(Connection& connection, const Image& image,
                    const ProgressCallback& progress) {
    Image staged = image;
    // Both supported devices map their arming switch to row 0x100 bit 31.
    staged.at(256)[3] |= 0x80;
    unsigned int total = static_cast<unsigned int>(staged.size());
    unsigned int count = 0;
    Report(progress, Stage::kProgramming, 0, total);
    for (const auto& entry : staged) {
        Status status =
            connection.Command("WRITE " + FormatAddress(entry.first) + " " +
                               EncodeHex(entry.second));
        if (!status.ok()) {
            return status;
        }
        ++count;
        Report(progress, Stage::kProgramming, count, total);
    }
    Status status =
        VerifyImage(connection, staged, Stage::kVerifying, progress);
    if (!status.ok()) {
        return status;
    }
    if (staged.at(256) != image.at(256)) {
        // Programming only clears bits. Rewriting the configuration word
        // changes just the arming bit; all other cells retain their verified
        // values.
        Report(progress, Stage::kActivating, 0, 1);
        status = connection.Command("WRITE 100 " + EncodeHex(image.at(256)));
        if (!status.ok()) {
            return status;
        }
        Image config{{256, image.at(256)}};
        status = VerifyImage(connection, config, Stage::kActivating, nullptr);
        if (!status.ok()) {
            return status;
        }
        Report(progress, Stage::kActivating, 1, 1);
    }
    return Status();
}

/**
 * @brief Runs the requested operation within an enabled session.
 *
 * @param[in,out] connection Open connection with programming mode enabled.
 * @param[in] operation Erase, flash or verify.
 * @param[in] device Device selected from the physical IDCODE.
 * @param[in] image Expected map; unused for erase.
 * @param[in] progress Possibly empty progress receiver.
 * @return Success or the first erase, blank-check, program or verify error.
 */
Status RunOperation(Connection& connection, Operation operation, Device device,
                    const Image& image, const ProgressCallback& progress) {
    if (operation == Operation::kErase || operation == Operation::kFlash) {
        Report(progress, Stage::kErasing, 0, 1);
        Status status = connection.Command("ERASE");
        if (!status.ok()) {
            return status;
        }
        Report(progress, Stage::kErasing, 1, 1);
        Fuses erased_fuses(FuseCount(device), 1);
        status = VerifyImage(connection, PackFuses(device, erased_fuses),
                             Stage::kBlankChecking, progress);
        if (!status.ok()) {
            return status;
        }
    }
    if (operation == Operation::kFlash) {
        return ProgramImage(connection, image, progress);
    }
    if (operation == Operation::kVerify) {
        return VerifyImage(connection, image, Stage::kVerifying, progress);
    }
    return Status();
}

}  // namespace

/**
 * @brief Maps each stage to its progress label.
 */
const char* StageName(Stage stage) {
    switch (stage) {
        case Stage::kErasing:
            return "Erasing";
        case Stage::kBlankChecking:
            return "Blank checking";
        case Stage::kProgramming:
            return "Programming";
        case Stage::kVerifying:
            return "Verifying";
        case Stage::kActivating:
            return "Activating";
    }
    return "Working";
}

/**
 * @brief Accepts only the exact uppercase IDCODE text sent by the firmware.
 */
Device DeviceFromIdText(const std::string& idcode) {
    if (idcode == "0150203F") {
        return Device::kAtf1502as;
    }
    if (idcode == "0150403F") {
        return Device::kAtf1504as;
    }
    return Device::kUnknown;
}

/**
 * @brief Reads each mapped word and compares it with the expected bytes.
 */
Status VerifyImage(Connection& connection, const Image& image, Stage stage,
                   const ProgressCallback& progress) {
    unsigned int total = static_cast<unsigned int>(image.size());
    unsigned int count = 0;
    Report(progress, stage, 0, total);
    for (const auto& entry : image) {
        std::string response;
        Status status =
            connection.Command("READ " + FormatAddress(entry.first), &response);
        if (!status.ok()) {
            return status;
        }
        Word actual;
        status = DecodeHex(response, &actual);
        if (!status.ok()) {
            return status;
        }
        if (actual != entry.second) {
            return Status("Verify mismatch at row 0x" +
                          FormatAddress(entry.first) + ": expected " +
                          EncodeHex(entry.second) + ", read " +
                          EncodeHex(actual));
        }
        ++count;
        Report(progress, stage, count, total);
    }
    return Status();
}

/**
 * @brief Brackets one operation with BEGIN and a best-effort END.
 */
Status RunSession(Connection& connection, Operation operation, Device device,
                  const Image& image, const ProgressCallback& progress) {
    std::ostringstream idcode;
    idcode << std::hex << std::uppercase << DeviceId(device);
    std::string id = idcode.str();
    id.insert(0, 8 - id.size(), '0');
    Status status = connection.Command("BEGIN " + id);
    if (!status.ok()) {
        return status;
    }
    status = RunOperation(connection, operation, device, image, progress);
    // Attempt cleanup even on failure, preserving the original diagnostic. If
    // transport is lost, the firmware watchdog eventually releases the pins.
    Status cleanup = connection.Command("END");
    if (!status.ok()) {
        return status;
    }
    return cleanup;
}

}  // namespace atf
