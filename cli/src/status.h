// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Exception-free status values for recoverable host-side errors.
 */
#ifndef ATF150X_PROGRAMMER_CLI_SRC_STATUS_H_
#define ATF150X_PROGRAMMER_CLI_SRC_STATUS_H_

#include <string>
#include <utility>

namespace atf {

/**
 * @brief Carries a recoverable error without exceptions.
 *
 * An empty message means success. Check ok() before using output parameters
 * associated with the operation. Discarding a status produces a compiler
 * warning.
 */
class [[nodiscard]] Status {
public:
    /**
     * @brief Constructs a successful status.
     */
    Status() = default;

    /**
     * @brief Constructs a status from a diagnostic message.
     *
     * @param[in] message Error description; an empty string means success.
     */
    explicit Status(std::string message) : message_(std::move(message)) {}

    /**
     * @brief Tests whether the operation succeeded.
     *
     * @return True exactly when the stored message is empty.
     */
    bool ok() const {
        return message_.empty();
    }

    /**
     * @brief Returns the stored diagnostic without copying it.
     *
     * @return A reference valid until this status is modified or destroyed.
     */
    const std::string& message() const {
        return message_;
    }

private:
    std::string message_;
};

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_CLI_SRC_STATUS_H_
