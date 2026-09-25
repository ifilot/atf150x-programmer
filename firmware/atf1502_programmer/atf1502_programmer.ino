// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Arduino copies the sketch directory into its build tree.
#include "jtag.h"  // NOLINT(build/include_subdir)
#if !defined(ARDUINO_AVR_LEONARDO)
#error "Select Arduino Leonardo: arduino:avr:leonardo"
#endif
namespace {

constexpr uint8_t kTdiPin = 2;
constexpr uint8_t kTckPin = 3;
constexpr uint8_t kTdoPin = 4;
constexpr uint8_t kTmsPin = 12;

// GPIO backend: TDO is sampled while TCK is high, after a settling delay.
class Pins {
 public:
  bool Clock(bool tms, bool tdi) {
    digitalWrite(kTckPin, LOW);
    digitalWrite(kTmsPin, tms);
    digitalWrite(kTdiPin, tdi);
    delayMicroseconds(2);
    digitalWrite(kTckPin, HIGH);
    delayMicroseconds(2);
    bool bit = digitalRead(kTdoPin);
    digitalWrite(kTckPin, LOW);
    return bit;
  }
  void WaitMilliseconds(unsigned int ms) { delay(ms); }
};

Pins pins;
atf::Jtag<Pins> jtag(pins);
bool active = false;
bool driving = false;
bool overflow = false;
char line[96];
uint8_t line_length = 0;
uint32_t last_byte = 0;
uint32_t last_command = 0;

// Leave ISC before returning the GPIOs to high impedance. Safe to call again.
void ReleasePins() {
  if (active) {
    jtag.Disable();
  }
  active = false;
  driving = false;
  pinMode(kTckPin, INPUT);
  pinMode(kTdiPin, INPUT);
  pinMode(kTmsPin, INPUT);
}

// Set output levels before enabling drivers to avoid a spurious TCK edge.
void DrivePins() {
  if (driving) {
    return;
  }
  digitalWrite(kTckPin, LOW);
  digitalWrite(kTdiPin, LOW);
  digitalWrite(kTmsPin, HIGH);
  pinMode(kTdoPin, INPUT);
  pinMode(kTckPin, OUTPUT);
  pinMode(kTdiPin, OUTPUT);
  pinMode(kTmsPin, OUTPUT);
  driving = true;
}

// All responses echo the request sequence and protect the payload with CRC.
void Reply(unsigned int sequence, const char* payload) {
  char buffer[80];
  snprintf(buffer, sizeof(buffer), "%u %s", sequence, payload);
  uint16_t crc = atf::Crc16(buffer, strlen(buffer));
  Serial.print(buffer);
  Serial.print('*');
  char hex_data[5];
  snprintf(hex_data, sizeof(hex_data), "%04X", crc);
  Serial.println(hex_data);
}

// Parses and validates a mapped word before executing READ or WRITE.
void HandleWord(unsigned int sequence, bool write, char* start) {
  char* end;
  auto address = strtoul(start, &end, 16);
  unsigned int bits =
      address <= 768 ? atf::WordBits(static_cast<unsigned int>(address)) : 0;
  if (end == start || !bits || (!write && *end) || (write && *end != ' ')) {
    ReleasePins();
    Reply(sequence, "ERR ADDRESS");
    return;
  }
  unsigned int bytes = (bits + 7) / 8;
  uint8_t data[11] = {};
  if (write) {
    // Validate the entire word before issuing a programming instruction.
    const char* hex_data = end + 1;
    if (strlen(hex_data) != bytes * 2) {
      ReleasePins();
      Reply(sequence, "ERR LENGTH");
      return;
    }
    for (unsigned int i = 0; i < bytes; ++i) {
      int high_nibble = atf::HexDigit(hex_data[2 * i]);
      int low_nibble = atf::HexDigit(hex_data[2 * i + 1]);
      if (high_nibble < 0 || low_nibble < 0) {
        ReleasePins();
        Reply(sequence, "ERR HEX");
        return;
      }
      data[i] = static_cast<uint8_t>((high_nibble << 4) | low_nibble);
    }
    if ((bits % 8 && (data[bytes - 1] >> (bits % 8))) ||
        (address == 512 && data[0] != 0x0f)) {
      ReleasePins();
      Reply(sequence, "ERR PROTECTED");
      return;
    }
    jtag.Program(static_cast<unsigned int>(address), data);
    Reply(sequence, "OK");
  } else {
    jtag.Read(static_cast<unsigned int>(address), data);
    char buffer[28] = "OK ";
    for (unsigned int i = 0; i < bytes; ++i) {
      size_t offset = 3 + 2 * i;
      snprintf(buffer + offset, sizeof(buffer) - offset, "%02X", data[i]);
    }
    Reply(sequence, buffer);
  }
}

// Executes only CRC-validated commands; erase/read/write require BEGIN.
void Dispatch(unsigned int sequence, char* command) {
  last_command = millis();
  if (!strcmp(command, "HELLO")) {
    ReleasePins();
    Reply(sequence, "OK ATF1502 1");
    return;
  }
  if (!strcmp(command, "END")) {
    ReleasePins();
    Reply(sequence, "OK");
    return;
  }
  if (!strcmp(command, "ID") || !strcmp(command, "BEGIN")) {
    ReleasePins();
    DrivePins();
    uint32_t id = jtag.Identify();
    if (!strcmp(command, "ID")) {
      char buffer[24];
      // The variadic %lX conversion requires unsigned long even on AVR.
      snprintf(buffer, sizeof(buffer), "OK %08lX",
               static_cast<unsigned long>(id));  // NOLINT(runtime/int)
      ReleasePins();
      Reply(sequence, buffer);
      return;
    }
    if (id != atf::kDeviceId) {
      ReleasePins();
      Reply(sequence, "ERR DEVICE");
      return;
    }
    jtag.Enable();
    active = true;
    Reply(sequence, "OK");
    return;
  }
  if (!active) {
    Reply(sequence, "ERR SESSION");
    return;
  }
  if (!strcmp(command, "ERASE")) {
    jtag.Erase();
    Reply(sequence, "OK");
    return;
  }
  bool write = strncmp(command, "WRITE ", 6) == 0;
  if (write || strncmp(command, "READ ", 5) == 0) {
    HandleWord(sequence, write, command + (write ? 6 : 5));
    return;
  }
  ReleasePins();
  Reply(sequence, "ERR COMMAND");
}

// Validate a complete frame before interpreting any command. Invalid frames
// abort the session without a reply; the host reports its receive timeout.
void Execute() {
  char* star = strchr(line, '*');
  if (!star || strlen(star + 1) != 4) {
    ReleasePins();
    return;
  }
  unsigned int crc = 0;
  for (unsigned int i = 1; i <= 4; ++i) {
    int h = atf::HexDigit(star[i]);
    if (h < 0) {
      ReleasePins();
      return;
    }
    crc = (crc << 4) | static_cast<unsigned int>(h);
  }
  if (crc != atf::Crc16(line, static_cast<size_t>(star - line))) {
    ReleasePins();
    return;
  }
  *star = 0;
  char* end;
  auto sequence = strtoul(line, &end, 10);
  if (end == line || *end != ' ' || sequence > 65535) {
    ReleasePins();
    return;
  }
  Dispatch(static_cast<unsigned int>(sequence), end + 1);
}

}  // namespace

// Arduino entry points retain the names required by the framework.
void setup() {
  pinMode(kTdoPin, INPUT);
  ReleasePins();
  Serial.begin(115200);
}

// Bound partial-frame and session lifetimes even if the host disappears.
// Unsigned subtraction keeps these deadlines valid across millis() wraparound.
void loop() {
  if (!Serial && driving) {
    ReleasePins();
  }
  if ((line_length || overflow) && millis() - last_byte > 1000) {
    line_length = 0;
    overflow = false;
    ReleasePins();
  }
  if (active && millis() - last_command > 5000) {
    ReleasePins();
  }
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    last_byte = millis();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (!overflow) {
        line[line_length] = 0;
        Execute();
      } else {
        ReleasePins();
      }
      line_length = 0;
      overflow = false;
    } else if (line_length < sizeof(line) - 1 && !overflow) {
      line[line_length++] = c;
    } else {
      overflow = true;
    }
  }
}
