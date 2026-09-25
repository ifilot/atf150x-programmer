// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
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
// Match Arduino's millis() signature rather than changing the framework API.
unsigned long millis() { return fake_millis; }  // NOLINT(runtime/int)
void delay(unsigned int n) { fake_millis += n; }
void delayMicroseconds(unsigned int) {}
void digitalWrite(unsigned int pin, int value) {
  if (pin == 3 && value && !levels[pin]) {
    tdo = tap.Clock(levels[12], levels[2]);
  }
  levels[pin] = value;
}
int digitalRead(unsigned int) { return tdo; }
void pinMode(unsigned int pin, int mode) { modes[pin] = mode; }
#define ARDUINO_AVR_LEONARDO 1
// Include the real sketch so tests exercise its framing and command handlers.
#include "firmware/atf1502_programmer/atf1502_programmer.ino"  // NOLINT(build/include)
namespace {

std::string MakePacket(const std::string& body) {
  char c[8];
  snprintf(c, sizeof(c), "*%04X\n", atf::Crc16(body.data(), body.size()));
  return body + c;
}
std::string SendCommand(const std::string& command) {
  Serial.outgoing.clear();
  Serial.incoming = MakePacket("1 " + command);
  loop();
  return Serial.outgoing;
}
void ExpectResponse(const std::string& command, const std::string& response) {
  auto actual = SendCommand(command);
  auto expected = MakePacket("1 " + response);
  expected.insert(expected.size() - 1, "\r");
  ATF_CHECK(actual == expected);
}
void ExpectReleased() {
  ATF_CHECK(!active && !driving);
  ATF_CHECK(modes[2] == INPUT && modes[3] == INPUT && modes[12] == INPUT &&
            modes[4] == INPUT);
}
}  // namespace

int main() {
  setup();
  ExpectReleased();
  ExpectResponse("HELLO", "OK ATF1502 1");
  ExpectResponse("ID", "OK 0150203F");
  ExpectReleased();
  ExpectResponse("ERASE", "ERR SESSION");
  ExpectResponse("BEGIN", "OK");
  ATF_CHECK(active);
  ExpectResponse("ERASE", "OK");
  ExpectResponse("WRITE 100 012345E7", "OK");
  ExpectResponse("READ 100", "OK 012345E7");
  ExpectResponse("WRITE 200 07", "ERR PROTECTED");
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  ExpectResponse("WRITE 0 FF", "ERR LENGTH");
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  ExpectResponse("WRITE 100 XXXXXXXX", "ERR HEX");
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  ExpectResponse("WRITE 0 0000000000000000000040", "ERR PROTECTED");
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  ExpectResponse("READ 6c", "ERR ADDRESS");
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  ExpectResponse("END", "OK");
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  Serial.incoming = MakePacket("1 ERASE");
  Serial.incoming[3] = 'X';
  Serial.outgoing.clear();
  loop();
  ATF_CHECK(Serial.outgoing.empty());
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  Serial.incoming = std::string(150, 'X') + '\n';
  loop();
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  Serial.incoming = "123";
  loop();
  fake_millis += 1001;
  loop();
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  fake_millis += 5001;
  loop();
  ExpectReleased();
  ExpectResponse("BEGIN", "OK");
  Serial.connected = false;
  loop();
  ExpectReleased();
  std::cout << "Firmware protocol tests passed\n";
  return 0;
}
