// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
// ATF1502AS algorithm derived from Project Bureau; see docs/THIRD_PARTY.md.
#ifndef ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_JTAG_H_
#define ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_JTAG_H_

#include <stdint.h>

// Shared headers must also resolve inside Arduino's copied sketch directory.
#include "protocol.h"  // NOLINT(build/include_subdir)

namespace atf {

// Drives one ATF1502AS TAP. Io must provide Clock(tms, tdi), returning TDO,
// and WaitMilliseconds(). The caller owns Io and must keep it alive.
// Reset or Identify must precede other operations. All scans return to Idle.
template <typename Io>
class Jtag {
 public:
  explicit constexpr Jtag(Io& io) : io_(io) {}

  // Recovers the TAP from any state and enters Run-Test/Idle.
  void Reset();

  // Resets the TAP and returns its 32-bit IDCODE without enabling programming.
  uint32_t Identify();

  // Disables normal CPLD operation and enables programming commands.
  void Enable();

  // Exits programming mode, restores normal CPLD operation and resets the TAP.
  void Disable();

  // Erases the device. Programming mode must already be enabled.
  void Erase();

  // Programs/reads one mapped word while programming mode is enabled.
  // data must address ceil(WordBits(address) / 8) bytes. Bytes and bits are
  // transferred least-significant first; unused high output bits are zeroed.
  void Program(unsigned int address, const uint8_t* data);
  void Read(unsigned int address, uint8_t* data);

 private:
  // Shifts a nonempty register between Idle states. Null input shifts zeroes;
  // null output discards TDO. Non-null buffers must be large enough for count
  // bits and must not overlap because output is cleared before the scan.
  void Scan(bool instruction, unsigned int count, const uint8_t* input,
            uint8_t* output);
  void ShiftInstruction(uint16_t value);
  void ShiftNumber(unsigned int bits, uint16_t value);
  void SelectAddress(unsigned int address);

  Io& io_;
};

// Implementation details follow. Templates remain here so AVR and desktop test
// backends instantiate the same TAP transitions without virtual dispatch.
template <typename Io>
void Jtag<Io>::Reset() {
  for (unsigned int i = 0; i < 6; ++i) {
    io_.Clock(true, false);
  }
  io_.Clock(false, false);
}

template <typename Io>
void Jtag<Io>::Scan(bool instruction, unsigned int count, const uint8_t* input,
                    uint8_t* output) {
  io_.Clock(true, false);  // Select-DR.
  if (instruction) {
    io_.Clock(true, false);  // Select-IR.
  }
  io_.Clock(false, false);  // Capture.
  io_.Clock(false, false);  // Shift.
  if (output != nullptr) {
    for (unsigned int i = 0; i < (count + 7) / 8; ++i) {
      output[i] = 0;
    }
  }
  for (unsigned int i = 0; i < count; ++i) {
    bool tdi = input != nullptr && (input[i / 8] & (1u << (i % 8)));
    bool tdo = io_.Clock(i + 1 == count, tdi);
    if (output != nullptr && tdo) {
      output[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    }
  }
  io_.Clock(true, false);   // Exit1 -> Update.
  io_.Clock(false, false);  // Update -> Idle.
}

template <typename Io>
void Jtag<Io>::ShiftInstruction(uint16_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value),
                           static_cast<uint8_t>(value >> 8)};
  Scan(true, 10, bytes, nullptr);
}

template <typename Io>
void Jtag<Io>::ShiftNumber(unsigned int bits, uint16_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value),
                           static_cast<uint8_t>(value >> 8)};
  Scan(false, bits, bytes, nullptr);
}

template <typename Io>
uint32_t Jtag<Io>::Identify() {
  Reset();
  ShiftInstruction(0x059);
  uint8_t bytes[4];
  Scan(false, 32, nullptr, bytes);
  // Promote before shifting: unsigned int is only 16 bits on the Leonardo.
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

template <typename Io>
void Jtag<Io>::Enable() {
  ShiftInstruction(0x280);
  ShiftNumber(10, 0x1b9);  // ATF ISC enable key.
}

template <typename Io>
void Jtag<Io>::Disable() {
  ShiftInstruction(0x280);
  ShiftNumber(10, 0);
  Reset();
}

template <typename Io>
void Jtag<Io>::Erase() {
  ShiftInstruction(0x2b3);
  ShiftInstruction(0x29e);
  io_.WaitMilliseconds(210);
  ShiftInstruction(0x2bf);  // Preserve the upstream post-operation instruction.
}

template <typename Io>
void Jtag<Io>::SelectAddress(unsigned int address) {
  ShiftInstruction(0x2a1);
  ShiftNumber(11, static_cast<uint16_t>(address));
}

template <typename Io>
void Jtag<Io>::Program(unsigned int address, const uint8_t* data) {
  SelectAddress(address);
  ShiftInstruction(static_cast<uint16_t>(0x290 | (address >> 8)));
  Scan(false, WordBits(address), data, nullptr);
  ShiftInstruction(0x29e);
  io_.WaitMilliseconds(30);
  ShiftInstruction(0x2bf);
}

template <typename Io>
void Jtag<Io>::Read(unsigned int address, uint8_t* data) {
  SelectAddress(address);
  ShiftInstruction(0x28c);
  io_.WaitMilliseconds(20);
  ShiftInstruction(static_cast<uint16_t>(0x290 | (address >> 8)));
  Scan(false, WordBits(address), nullptr, data);
}

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_JTAG_H_
