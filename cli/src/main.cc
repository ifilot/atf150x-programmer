// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#include <iostream>
#include <sstream>
#include <string>

#include "cli/src/jedec.h"
#include "cli/src/serial.h"
#include "cli/src/status.h"
#include "firmware/atf1502_programmer/protocol.h"

namespace atf {
namespace {

std::string FormatAddress(unsigned int row) {
  std::ostringstream text;
  text << std::hex << row;
  return text.str();
}

// Compares every mapped bit, including padding and configuration cells.
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
      return Status("Verify mismatch at row 0x" + FormatAddress(entry.first) +
                    ": expected " + EncodeHex(entry.second) + ", read " +
                    EncodeHex(actual));
    }
    if (++count % 16 == 0 || count == image.size()) {
      std::cerr << "\rVerifying " << count << '/' << image.size() << std::flush;
    }
  }
  std::cerr << '\n';
  return Status();
}

// Keeps outputs disarmed until the entire staged image passes readback.
Status ProgramImage(Connection& connection, const Image& image) {
  Image staged = image;
  staged.at(256)[3] |= 0x80;  // JEDEC 16750: arming switch, row 0x100 bit 31.
  unsigned int count = 0;
  for (const auto& entry : staged) {
    Status status = connection.Command("WRITE " + FormatAddress(entry.first) +
                                       " " + EncodeHex(entry.second));
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
    // Programming only clears bits. Rewriting the configuration word changes
    // just the arming bit; all other cells retain their verified values.
    status = connection.Command("WRITE 100 " + EncodeHex(image.at(256)));
    if (!status.ok()) {
      return status;
    }
    Image config{{256, image.at(256)}};
    return Verify(connection, config);
  }
  return Status();
}

// Runs inside an already enabled programming session. The caller always ends
// the session, including when blank-check, programming or verification fails.
Status RunOperation(Connection& connection, const std::string& action,
                    const Image& image) {
  if (action == "erase" || action == "flash") {
    std::cerr << "Erasing...\n";
    Status status = connection.Command("ERASE");
    if (!status.ok()) {
      return status;
    }
    Fuses erased_fuses{};
    erased_fuses.fill(1);
    status = Verify(connection, PackFuses(erased_fuses));
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

void PrintUsage() {
  std::cout << "ATF1502AS programmer\n"
            << "  atfprog inspect FILE.jed\n"
            << "  atfprog scan --port PORT\n"
            << "  atfprog erase --port PORT\n"
            << "  atfprog flash FILE.jed --port PORT\n"
            << "  atfprog verify FILE.jed --port PORT\n"
            << "PORT: /dev/ttyACM0 (Linux), COM3 (Windows)\n"
            << "flash erases, programs and verifies; erase destroys the "
               "existing design.\n";
}

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
  Image image;
  if (needs_file) {
    // Validate before opening the port, so bad files cannot erase a device.
    std::string text;
    Status status = ReadFile(file, &text);
    if (!status.ok()) {
      return status;
    }
    Fuses fuses;
    status = ParseJedec(text, &fuses);
    if (!status.ok()) {
      return status;
    }
    image = PackFuses(fuses);
    std::cout << "ATF1502AS: " << kFuseCount << " fuses, " << image.size()
              << " programming words; fuse checksum valid; JTAG enabled, "
                 "read protection off.\n";
    if (fuses[16750]) {
      std::cout
          << "Image leaves CPLD outputs disabled (arming switch is safe).\n";
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
  if (id != "0150203F") {
    return Status(
        "Expected ATF1502AS IDCODE 0150203F; check chip, power and wiring");
  }
  if (action == "scan") {
    return Status();
  }
  status = connection.Command("BEGIN");
  if (!status.ok()) {
    return status;
  }
  status = RunOperation(connection, action, image);
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
  atf::Status status = atf::Run(argc, argv);
  if (!status.ok()) {
    std::cerr << "\nError: " << status.message() << '\n';
    return 1;
  }
  return 0;
}
