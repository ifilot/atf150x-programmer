// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Release-build assertions for the standalone C++ test executables.
 */
#ifndef ATF150X_PROGRAMMER_TESTS_TEST_SUPPORT_H_
#define ATF150X_PROGRAMMER_TESTS_TEST_SUPPORT_H_

#include <cstdlib>
#include <iostream>

namespace atf {
namespace testing {

/**
 * @brief Checks a test invariant in both Debug and Release builds.
 *
 * Prints the location and expression, then aborts, if passed is false.
 *
 * @param[in] passed Whether the invariant holds.
 * @param[in] expression Non-null source text describing the invariant.
 * @param[in] file Non-null source filename for diagnostics.
 * @param[in] line Source line number for diagnostics.
 */
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

/**
 * @brief Checks an expression once and captures its source location.
 *
 * @param expression Condition that must evaluate to true.
 */
#define ATF_CHECK(expression)                                         \
    ::atf::testing::Check(static_cast<bool>(expression), #expression, \
                          __FILE__, __LINE__)

#endif  // ATF150X_PROGRAMMER_TESTS_TEST_SUPPORT_H_
