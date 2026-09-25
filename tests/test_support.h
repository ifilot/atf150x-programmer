// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#ifndef ATF150X_PROGRAMMER_TESTS_TEST_SUPPORT_H_
#define ATF150X_PROGRAMMER_TESTS_TEST_SUPPORT_H_

#include <cstdlib>
#include <iostream>

namespace atf {
namespace testing {

// Unlike assert(), test checks remain active in Release builds.
inline void Check(bool passed, const char* expression, const char* file,
                  int line) {
  if (!passed) {
    std::cerr << file << ':' << line << ": Check failed: " << expression
              << '\n';
    std::abort();
  }
}

}  // namespace testing
}  // namespace atf

#define ATF_CHECK(expression)                                                 \
  ::atf::testing::Check(static_cast<bool>(expression), #expression, __FILE__, \
                        __LINE__)

#endif  // ATF150X_PROGRAMMER_TESTS_TEST_SUPPORT_H_
