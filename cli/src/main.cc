// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief CLI argument handling and erase, program, verify and activation flows.
 */
#include <iostream>
#include <sstream>
#include <string>

#include "cli/src/jedec.h"
#include "cli/src/serial.h"
#include "cli/src/status.h"
#include "firmware/atf1502_programmer/protocol.h"
#include "firmware/atf1502_programmer/version.h"

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
 * @brief Compares every mapped bit, including padding and configuration.
 *
 * Reads the target without erasing or programming. Progress goes to stderr.
 *
 * @param[in,out] connection Open connection in an enabled programming session.
 * @param[in] image Expected row addresses and packed bytes.
 * @return Success or the first transport, decoding or readback error.
 */
Status Verify(Connection& connection, const Image& image) {
    unsigned int count = 0;
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
        if (++count % 16 == 0 || count == image.size()) {
            std::cerr << "\rVerifying " << count << '/' << image.size()
                      << std::flush;
        }
    }
    std::cerr << '\n';
    return Status();
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
 * @return Success or the first programming/readback error.
 */
Status ProgramImage(Connection& connection, const Image& image) {
    Image staged = image;
    // Both supported devices map their arming switch to row 0x100 bit 31.
    staged.at(256)[3] |= 0x80;
    unsigned int count = 0;
    for (const auto& entry : staged) {
        Status status =
            connection.Command("WRITE " + FormatAddress(entry.first) + " " +
                               EncodeHex(entry.second));
        if (!status.ok()) {
            return status;
        }
        if (++count % 16 == 0 || count == staged.size()) {
            std::cerr << "\rProgramming " << count << '/' << staged.size()
                      << std::flush;
        }
    }
    std::cerr << '\n';
    Status status = Verify(connection, staged);
    if (!status.ok()) {
        return status;
    }
    if (staged.at(256) != image.at(256)) {
        // Programming only clears bits. Rewriting the configuration word
        // changes just the arming bit; all other cells retain their verified
        // values.
        status = connection.Command("WRITE 100 " + EncodeHex(image.at(256)));
        if (!status.ok()) {
            return status;
        }
        Image config{{256, image.at(256)}};
        return Verify(connection, config);
    }
    return Status();
}

/**
 * @brief Runs the requested operation within an enabled session.
 *
 * Erase and flash destroy the existing design. The caller always ends the
 * session, including on failure.
 *
 * @param[in,out] connection Open connection with programming mode enabled.
 * @param[in] action One of erase, flash or verify.
 * @param[in] device Device selected from the physical IDCODE.
 * @param[in] image Expected map; unused for erase.
 * @return Success or the first erase, blank-check, program or verify error.
 */
Status RunOperation(Connection& connection, const std::string& action,
                    Device device, const Image& image) {
    if (action == "erase" || action == "flash") {
        std::cerr << "Erasing...\n";
        Status status = connection.Command("ERASE");
        if (!status.ok()) {
            return status;
        }
        Fuses erased_fuses(FuseCount(device), 1);
        status = Verify(connection, PackFuses(device, erased_fuses));
        if (!status.ok()) {
            return status;
        }
    }
    if (action == "flash") {
        return ProgramImage(connection, image);
    }
    if (action == "verify") {
        return Verify(connection, image);
    }
    return Status();
}

/**
 * @brief Writes command syntax and erase behavior to stdout.
 */
void PrintUsage() {
    std::cout << "ATF1502AS/ATF1504AS programmer v" << kVersion << "\n"
              << "  atfprog inspect FILE.jed\n"
              << "  atfprog scan --port PORT\n"
              << "  atfprog erase --port PORT\n"
              << "  atfprog flash FILE.jed --port PORT\n"
              << "  atfprog verify FILE.jed --port PORT\n"
              << "  atfprog --version\n"
              << "PORT: /dev/ttyACM0 (Linux), COM3 (Windows)\n"
              << "flash erases, programs and verifies; erase destroys the "
                 "existing design.\n";
}

/**
 * @brief Validates arguments and executes one CLI operation.
 *
 * Validates JEDEC data before opening the port. After BEGIN succeeds, attempts
 * END even when the operation fails, preserving the original diagnostic.
 *
 * @param[in] argc Argument count, at least two.
 * @param[in] argv Non-null array of argc non-null argument strings.
 * @return Success or the first argument, file, device or operation error.
 */
Status Run(int argc, char** argv) {
    std::string action = argv[1];
    std::string file;
    std::string port;
    bool needs_file =
        action == "inspect" || action == "flash" || action == "verify";
    if (!needs_file && action != "scan" && action != "erase") {
        return Status("Unknown command: " + action);
    }
    for (int i = 2; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc && port.empty()) {
            port = argv[++i];
        } else if (needs_file && file.empty() && argument.rfind("-", 0) != 0) {
            file = argument;
        } else {
            return Status("Unexpected argument: " + argument);
        }
    }
    if (needs_file && file.empty()) {
        return Status("A JEDEC filename is required");
    }
    if (action == "inspect" && !port.empty()) {
        return Status("inspect does not use a serial port");
    }
    JedecFile jedec;
    Image image;
    if (needs_file) {
        // Validate before opening the port, so bad files cannot erase a device.
        std::string text;
        Status status = ReadFile(file, &text);
        if (!status.ok()) {
            return status;
        }
        status = ParseJedec(text, &jedec);
        if (!status.ok()) {
            return status;
        }
        image = PackFuses(jedec.device, jedec.fuses);
        std::cout << DeviceName(jedec.device) << ": " << FuseCount(jedec.device)
                  << " fuses, " << image.size()
                  << " programming words; fuse checksum valid; JTAG enabled, "
                     "read protection off.\n";
        if (jedec.fuses[ArmingFuse(jedec.device)]) {
            std::cout << "Image leaves CPLD outputs disabled (arming switch is "
                         "safe).\n";
        }
    }
    if (action == "inspect") {
        return Status();
    }
    if (port.empty()) {
        return Status("Select a serial port using --port PORT");
    }
    Connection connection;
    Status status = connection.Open(port);
    if (!status.ok()) {
        return status;
    }
    std::string id;
    status = connection.Command("ID", &id);
    if (!status.ok()) {
        return status;
    }
    std::cout << "IDCODE: 0x" << id << '\n';
    Device device = Device::kUnknown;
    if (id == "0150203F") {
        device = Device::kAtf1502as;
    } else if (id == "0150403F") {
        device = Device::kAtf1504as;
    } else {
        return Status("Unsupported ATF15xx IDCODE " + id);
    }
    if (action == "scan") {
        std::cout << "Device: " << DeviceName(device) << '\n';
        return Status();
    }
    if (needs_file && jedec.device != device) {
        return Status(std::string("JEDEC targets ") + DeviceName(jedec.device) +
                      ", but connected device is " + DeviceName(device));
    }
    status = connection.Command("BEGIN " + id);
    if (!status.ok()) {
        return status;
    }
    status = RunOperation(connection, action, device, image);
    // Attempt cleanup even on failure, preserving the original diagnostic. If
    // transport is lost, the firmware watchdog eventually releases the pins.
    Status cleanup = connection.Command("END");
    if (!status.ok()) {
        return status;
    }
    if (!cleanup.ok()) {
        return cleanup;
    }
    std::cout << (action == "erase"   ? "Erase and blank check complete.\n"
                  : action == "flash" ? "Flash and verification complete.\n"
                                      : "Verification complete.\n");
    return Status();
}

}  // namespace
}  // namespace atf

/**
 * @brief Dispatches the CLI and translates status into a process exit code.
 *
 * @param[in] argc Process argument count.
 * @param[in] argv Process argument strings.
 * @return Zero on success/help, one on failure, or two if no command was given.
 */
int main(int argc, char** argv) {
    if (argc < 2) {
        atf::PrintUsage();
        return 2;
    }
    if (argc == 2 &&
        (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        atf::PrintUsage();
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "atfprog v" << atf::kVersion << '\n';
        return 0;
    }
    atf::Status status = atf::Run(argc, argv);
    if (!status.ok()) {
        std::cerr << "\nError: " << status.message() << '\n';
        return 1;
    }
    return 0;
}
