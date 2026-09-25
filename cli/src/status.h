// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#ifndef ATF150X_PROGRAMMER_CLI_SRC_STATUS_H_
#define ATF150X_PROGRAMMER_CLI_SRC_STATUS_H_

#include <string>
#include <utility>

namespace atf {

// Carries a recoverable failure without exceptions. An empty message is
// success. Callers must check ok() before consuming any associated output
// parameters.
class [[nodiscard]] Status {
 public:
  Status() = default;
  explicit Status(std::string message) : message_(std::move(message)) {}

  bool ok() const { return message_.empty(); }
  const std::string& message() const { return message_; }

 private:
  std::string message_;
};

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_CLI_SRC_STATUS_H_
