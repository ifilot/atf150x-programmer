// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Production sketch tests using simulated Arduino GPIO and serial I/O.
 */
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "cli/src/jedec.h"
#include "tests/arduino/Arduino.h"
#include "tests/tap_model.h"
#include "tests/test_support.h"
atf::testing::Tap tap;
FakeSerial Serial;
uint32_t fake_millis = 0;
namespace {

int levels[20] = {};
int modes[20] = {};
bool tdo = false;

}  // namespace

/**
 * @brief Returns the simulated Arduino millisecond clock.
 */
unsigned long millis() {
    return fake_millis;
}  // NOLINT(runtime/int)

/**
 * @brief Advances the simulated clock by the requested milliseconds.
 */
void delay(unsigned int n) {
    fake_millis += n;
}

/**
 * @brief Ignores sub-millisecond delays in the desktop GPIO model.
 */
void delayMicroseconds(unsigned int) {}

/**
 * @brief Updates GPIO state and advances the TAP on a rising TCK edge.
 */
void digitalWrite(unsigned int pin, int value) {
    if (pin == 3 && value && !levels[pin]) {
        tdo = tap.Clock(levels[12], levels[2]);
    }
    levels[pin] = value;
}

/**
 * @brief Returns the most recently sampled TDO value.
 */
int digitalRead(unsigned int) {
    return tdo;
}

/**
 * @brief Records the mode of a simulated pin for later assertions.
 */
void pinMode(unsigned int pin, int mode) {
    modes[pin] = mode;
}
#define ARDUINO_AVR_LEONARDO 1
// Include the real sketch so tests exercise its framing and command handlers.
#include "firmware/atf1502_programmer/atf1502_programmer.ino"  // NOLINT(build/include)
namespace {

/**
 * @brief Frames a test request with its CRC and LF terminator.
 *
 * @param[in] body Sequence number, space and command or response text.
 * @return The complete wire packet, excluding a CR character.
 */
std::string MakePacket(const std::string& body) {
    char c[8];
    snprintf(c, sizeof(c), "*%04X\n", atf::Crc16(body.data(), body.size()));
    return body + c;
}

/**
 * @brief Feeds a command through the actual sketch parser.
 *
 * Clears previous output and replaces pending input with sequence 1.
 *
 * @param[in] command Unframed command and arguments.
 * @return Bytes emitted by the sketch after one loop() call.
 */
std::string SendCommand(const std::string& command) {
    Serial.outgoing.clear();
    Serial.incoming = MakePacket("1 " + command);
    loop();
    return Serial.outgoing;
}

/**
 * @brief Checks the full reply, including sequence, CRC and CRLF.
 *
 * Aborts the test on any wire-level mismatch.
 *
 * @param[in] command Unframed command to send.
 * @param[in] response Expected payload following the echoed sequence.
 */
void ExpectResponse(const std::string& command, const std::string& response) {
    auto actual = SendCommand(command);
    auto expected = MakePacket("1 " + response);
    expected.insert(expected.size() - 1, "\r");
    ATF_CHECK(actual == expected);
}

/**
 * @brief Asserts that programming is inactive and JTAG pins are inputs.
 */
void ExpectReleased() {
    ATF_CHECK(!active && !driving);
    ATF_CHECK(modes[2] == INPUT && modes[3] == INPUT && modes[12] == INPUT &&
              modes[4] == INPUT);
}
}  // namespace

/**
 * @brief Runs command validation, protection and cleanup regression checks.
 *
 * @return Zero when all checks pass; any failed check aborts the process.
 */
int main() {
    setup();
    ExpectReleased();
    ExpectResponse("HELLO", "OK ATF15XX 2 v0.2.0");
    ExpectResponse("ID", "OK 0150203F");
    ExpectReleased();
    ExpectResponse("ERASE", "ERR SESSION");
    ExpectResponse("BEGIN 0150203F", "OK");
    ATF_CHECK(active);
    ExpectResponse("ERASE", "OK");
    ExpectResponse("WRITE 100 012345E7", "OK");
    ExpectResponse("READ 100", "OK 012345E7");
    ExpectResponse("WRITE 200 07", "ERR PROTECTED");
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    ExpectResponse("WRITE 0 FF", "ERR LENGTH");
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    ExpectResponse("WRITE 100 XXXXXXXX", "ERR HEX");
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    ExpectResponse("WRITE 0 0000000000000000000040", "ERR PROTECTED");
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    ExpectResponse("READ 6c", "ERR ADDRESS");
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    ExpectResponse("END", "OK");
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    Serial.incoming = MakePacket("1 ERASE");
    Serial.incoming[3] = 'X';
    Serial.outgoing.clear();
    loop();
    ATF_CHECK(Serial.outgoing.empty());
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    Serial.incoming = std::string(150, 'X') + '\n';
    loop();
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    Serial.incoming = "123";
    loop();
    fake_millis += 1001;
    loop();
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    fake_millis += 5001;
    loop();
    ExpectReleased();
    ExpectResponse("BEGIN 0150203F", "OK");
    Serial.connected = false;
    loop();
    ExpectReleased();
    Serial.connected = true;
    tap.device = atf::Device::kAtf1504as;
    ExpectResponse("ID", "OK 0150403F");
    ExpectResponse("BEGIN 0150403F", "OK");
    ExpectResponse("READ 0", "OK 000000000000000000000000000000000000000000");
    ExpectResponse("ERASE", "OK");
    ExpectResponse("WRITE 0 000000000000000000000000000000000000000000", "OK");
    ExpectResponse("READ 0", "OK 000000000000000000000000000000000000000000");
    ExpectResponse("END", "OK");
    ExpectReleased();
    std::cout << "Firmware protocol tests passed\n";
    return 0;
}
