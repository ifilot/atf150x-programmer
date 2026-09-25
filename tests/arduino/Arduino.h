// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Minimal Arduino API adapter for desktop firmware tests.
 */
#ifndef ATF150X_PROGRAMMER_TESTS_ARDUINO_ARDUINO_H_
#define ATF150X_PROGRAMMER_TESTS_ARDUINO_ARDUINO_H_
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
// Constants and entry points below mirror the public Arduino API.
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int INPUT = 0;
constexpr int OUTPUT = 1;
extern uint32_t fake_millis;

/**
 * @brief Returns the simulated Arduino millisecond counter.
 *
 * @return The value of fake_millis, using the Arduino ABI return type.
 */
unsigned long millis();  // NOLINT(runtime/int)

/**
 * @brief Advances simulated time without sleeping.
 *
 * @param[in] n Milliseconds to add to fake_millis.
 */
void delay(unsigned int n);

/**
 * @brief Accepts a microsecond delay without advancing simulated time.
 *
 * @param[in] n Ignored delay requested by the production GPIO backend.
 */
void delayMicroseconds(unsigned int n);

/**
 * @brief Sets a simulated pin and clocks the TAP on a TCK rising edge.
 *
 * @param[in] pin Arduino pin index, less than 20.
 * @param[in] value Requested LOW or HIGH logic level.
 */
void digitalWrite(unsigned int pin, int value);

/**
 * @brief Returns the last TDO sample produced by the simulated TAP.
 *
 * @param[in] pin Ignored pin index; this adapter models TDO reads only.
 * @return The most recently sampled TDO bit.
 */
int digitalRead(unsigned int pin);

/**
 * @brief Records the requested mode of a simulated Arduino pin.
 *
 * @param[in] pin Arduino pin index, less than 20.
 * @param[in] mode INPUT or OUTPUT mode to record.
 */
void pinMode(unsigned int pin, int mode);

/**
 * @brief Models the Arduino Serial API with in-memory byte queues.
 *
 * API spellings match Arduino so the production sketch compiles unchanged.
 * Tests fill incoming, inspect outgoing and toggle connected directly.
 */
struct FakeSerial {
    std::string incoming;
    std::string outgoing;
    bool connected = true;

    /**
     * @brief Accepts a baud-rate setting without configuring a device.
     *
     * The unnamed baud-rate argument is ignored by the in-memory adapter.
     */
    void begin(unsigned int) {}

    /**
     * @brief Reports whether the simulated host is connected.
     *
     * @return The current connected flag.
     */
    explicit operator bool() const {
        return connected;
    }

    /**
     * @brief Reports how many simulated receive bytes remain.
     *
     * @return Number of bytes queued in incoming.
     */
    int available() const {
        return static_cast<int>(incoming.size());
    }

    /**
     * @brief Consumes one byte from the simulated receive queue.
     *
     * @pre incoming is not empty; callers check available() first.
     *
     * @return The first queued byte as an unsigned character value.
     */
    int read() {
        char c = incoming.front();
        incoming.erase(0, 1);
        return static_cast<uint8_t>(c);
    }

    /**
     * @brief Appends text to the simulated transmit queue.
     *
     * @param[in] s Non-null, null-terminated text.
     */
    void print(const char* s) {
        outgoing += s;
    }

    /**
     * @brief Appends one byte to the simulated transmit queue.
     *
     * @param[in] c Byte to append.
     */
    void print(char c) {
        outgoing += c;
    }

    /**
     * @brief Appends text and a CRLF line ending to the transmit queue.
     *
     * @param[in] s Non-null, null-terminated text.
     */
    void println(const char* s) {
        outgoing += s;
        outgoing += "\r\n";
    }
};
extern FakeSerial Serial;

#endif  // ATF150X_PROGRAMMER_TESTS_ARDUINO_ARDUINO_H_
