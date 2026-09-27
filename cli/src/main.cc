// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief CLI argument handling and console reporting for programmer flows.
 */
#include <iostream>
#include <string>

#include "cli/src/jedec.h"
#include "cli/src/programmer.h"
#include "cli/src/serial.h"
#include "cli/src/status.h"
#include "firmware/atf1502_programmer/protocol.h"
#include "firmware/atf1502_programmer/version.h"

namespace atf {
namespace {

/**
 * @brief Prints stage progress to stderr on a single rewritten line.
 *
 * Word-level stages update every 16 words and finish with a newline.
 *
 * @param[in] stage Current operation phase.
 * @param[in] done Completed words within the stage.
 * @param[in] total Total words within the stage.
 */
void PrintProgress(Stage stage, unsigned int done, unsigned int total) {
    if (stage == Stage::kErasing || stage == Stage::kActivating) {
        if (done == 0) {
            std::cerr << StageName(stage) << "...\n";
        }
        return;
    }
    if (done == 0) {
        return;
    }
    if (done % 16 == 0 || done == total) {
        std::cerr << '\r' << StageName(stage) << ' ' << done << '/' << total
                  << std::flush;
    }
    if (done == total) {
        std::cerr << '\n';
    }
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
    Device device = DeviceFromIdText(id);
    if (device == Device::kUnknown) {
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
    Operation operation = action == "erase"   ? Operation::kErase
                          : action == "flash" ? Operation::kFlash
                                              : Operation::kVerify;
    status = RunSession(connection, operation, device, image, PrintProgress);
    if (!status.ok()) {
        return status;
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
