// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Native serial ownership and framed programmer command interfaces.
 */
#ifndef ATF150X_PROGRAMMER_CLI_SRC_SERIAL_H_
#define ATF150X_PROGRAMMER_CLI_SRC_SERIAL_H_

#include <memory>
#include <string>

#include "cli/src/status.h"

namespace atf {

/**
 * @brief Owns a native serial handle with bounded blocking operations.
 *
 * Not thread-safe. Construction performs no I/O; Open() must succeed before
 * reading or writing. Closing the owner releases the native handle.
 */
class Serial {
public:
    /**
     * @brief Constructs a closed serial transport without performing I/O.
     */
    Serial();

    /**
     * @brief Closes the owned native handle, if any, without reporting errors.
     */
    ~Serial();

    /**
     * @brief Disallows copying the native handle owner.
     */
    Serial(const Serial&) = delete;

    /**
     * @brief Disallows copy assignment of the native handle owner.
     */
    Serial& operator=(const Serial&) = delete;

    /**
     * @brief Opens a port at 115200 baud, 8N1, with DTR asserted.
     *
     * Replaces any previously open handle and discards startup bytes after a
     * 1.5-second settling interval. On failure, no handle remains open.
     *
     * @param[in] port COM port name on Windows or device path on Linux.
     * @return Success or a native port-open/configuration error.
     */
    Status Open(const std::string& port);

    /**
     * @brief Writes every byte or reports the first transport failure.
     *
     * Open() must have succeeded. A failed write may have sent a prefix; this
     * method does not retry a protocol command.
     *
     * @param[in] text Bytes to send; framing is supplied by the caller.
     * @return Success, timeout, closed-port or native I/O error.
     */
    Status Write(const std::string& text);

    /**
     * @brief Reads one bounded newline-terminated response.
     *
     * Open() must have succeeded. The line may contain at most 95 non-CR bytes.
     * The overall receive deadline is three seconds.
     *
     * @param[out] text Non-null destination without CR/LF; unchanged on
     * failure.
     * @return Success, timeout, oversized-response or native I/O error.
     */
    Status ReadLine(std::string* text);

private:
    // Keeps platform headers and native handle lifetime out of the public API.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Exchanges one CRC-protected command/reply pair at a time.
 *
 * Not thread-safe. Sequence numbers correlate replies, not duplicate
 * requests. Commands are never automatically retried after a failure.
 */
class Connection {
public:
    /**
     * @brief Constructs a disconnected protocol endpoint.
     */
    Connection() = default;

    /**
     * @brief Disallows copying the underlying serial connection.
     */
    Connection(const Connection&) = delete;

    /**
     * @brief Disallows copy assignment of the connection.
     */
    Connection& operator=(const Connection&) = delete;

    /**
     * @brief Opens the port and validates the firmware protocol handshake.
     *
     * No device is erased or programmed. After failure, reopen before sending
     * commands; the serial handle may remain open until reopening or
     * destruction.
     *
     * @param[in] port COM port name on Windows or device path on Linux.
     * @return Success or a transport/firmware-version error.
     */
    Status Open(const std::string& port);

    /**
     * @brief Sends one command and validates the entire response.
     *
     * Open() must have succeeded, and the command must fit the firmware frame.
     * The command may already have executed if its reply is lost.
     *
     * @param[in] text Unframed command and arguments; no embedded CR/LF or
     * star.
     * @param[out] output Optional reply payload; unchanged on failure.
     * @return Success or a transport, CRC, sequence, framing or firmware error.
     */
    Status Command(const std::string& text, std::string* output = nullptr);

private:
    Serial serial_;
    unsigned int sequence_ = 0;
};

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_CLI_SRC_SERIAL_H_
