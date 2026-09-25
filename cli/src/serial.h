// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#ifndef ATF150X_PROGRAMMER_CLI_SRC_SERIAL_H_
#define ATF150X_PROGRAMMER_CLI_SRC_SERIAL_H_

#include <memory>
#include <string>

#include "cli/src/status.h"

namespace atf {

// Owns a native serial handle. Not thread-safe; operations have finite
// deadlines. Construction does not perform I/O. Open must succeed before
// ReadLine/Write.
class Serial {
 public:
  Serial();
  ~Serial();
  Serial(const Serial&) = delete;
  Serial& operator=(const Serial&) = delete;

  // Opens at 115200/8N1, asserts DTR and discards startup bytes. A second Open
  // replaces the old handle. A failed Open releases the partially opened port.
  Status Open(const std::string& port);

  // Writes the complete string or reports a failure; never retries commands.
  Status Write(const std::string& text);

  // Reads a bounded line, stripping CR/LF. Non-null output changes on success.
  Status ReadLine(std::string* text);

 private:
  // Keeps platform headers and native handle lifetime out of the public API.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Exchanges CRC-protected command/reply pairs. Only one command may be active.
// A transport error requires a new connection; commands are never auto-retried.
class Connection {
 public:
  Connection() = default;
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  // Opens the serial port and verifies the firmware protocol version.
  Status Open(const std::string& port);

  // Sends a command and checks its reply's CRC, sequence and status. On
  // success, optional output receives the payload following OK. It is unchanged
  // on error.
  Status Command(const std::string& text, std::string* output = nullptr);

 private:
  Serial serial_;
  unsigned int sequence_ = 0;
};

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_CLI_SRC_SERIAL_H_
