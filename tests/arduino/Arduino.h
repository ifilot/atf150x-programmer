// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
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
// Preserve the exact Arduino ABI for the test double.
unsigned long millis();  // NOLINT(runtime/int)
void delay(unsigned int n);
void delayMicroseconds(unsigned int n);
void digitalWrite(unsigned int pin, int value);
int digitalRead(unsigned int pin);
void pinMode(unsigned int pin, int mode);
// Arduino API names intentionally match the framework so the real sketch can
// be compiled unchanged against this in-memory test backend.
struct FakeSerial {
  std::string incoming;
  std::string outgoing;
  bool connected = true;
  void begin(unsigned int) {}
  explicit operator bool() const { return connected; }
  int available() const { return static_cast<int>(incoming.size()); }
  int read() {
    char c = incoming.front();
    incoming.erase(0, 1);
    return static_cast<uint8_t>(c);
  }
  void print(const char* s) { outgoing += s; }
  void print(char c) { outgoing += c; }
  void println(const char* s) {
    outgoing += s;
    outgoing += "\r\n";
  }
};
extern FakeSerial Serial;

#endif  // ATF150X_PROGRAMMER_TESTS_ARDUINO_ARDUINO_H_
