// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Portable ATF15xx TAP and programming sequences.
 */
// ATF15xx algorithm derived from Project Bureau; see docs/THIRD_PARTY.md.
#ifndef ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_JTAG_H_
#define ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_JTAG_H_

#include <stdint.h>

// Shared headers must also resolve inside Arduino's copied sketch directory.
#include "protocol.h"  // NOLINT(build/include_subdir)

namespace atf {

/**
 * @brief Drives one supported ATF15xx TAP through a caller-owned I/O backend.
 *
 * The backend must outlive this object. Call Reset() or Identify() before
 * other operations. Every scan ends in Run-Test/Idle.
 *
 * @tparam Io Backend with Clock(tms, tdi) returning TDO and
 * WaitMilliseconds(ms).
 */
template <typename Io>
class Jtag {
public:
    /**
     * @brief Binds an I/O backend without touching the TAP.
     *
     * @param[in,out] io Backend that remains alive for the lifetime of this
     * object.
     */
    explicit constexpr Jtag(Io& io) : io_(io) {}

    /**
     * @brief Recovers the TAP from any state and enters Run-Test/Idle.
     */
    void Reset();

    /**
     * @brief Resets the TAP and reads its IDCODE without enabling programming.
     *
     * @return The 32-bit value shifted from the device, without identity
     * validation.
     */
    uint32_t Identify();

    /**
     * @brief Enables programming and disables normal device output operation.
     *
     * @pre The TAP is in Run-Test/Idle.
     */
    void Enable();

    /**
     * @brief Exits programming mode and resets the TAP.
     *
     * @pre The TAP is in Run-Test/Idle.
     * @details Restores normal operation according to the programmed
     * configuration.
     */
    void Disable();

    /**
     * @brief Erases the device and waits for the erase cycle to complete.
     *
     * @pre Programming mode is enabled and the TAP is in Run-Test/Idle.
     * @details Destroys the current design. This method does not perform a
     * blank check.
     */
    void Erase();

    /**
     * @brief Programs one mapped Flash word and waits for completion.
     *
     * @pre Programming mode is enabled and the TAP is in Run-Test/Idle.
     * @details Bytes and bits shift least-significant first. Programming can
     * only clear Flash bits; the caller controls erase, protection and
     * verification.
     *
     * @param[in] address Mapped physical address for which WordBits() is
     * nonzero.
     * @param[in] bits Device-specific data-register width.
     * @param[in] data Non-null buffer of at least ceil(bits / 8) bytes.
     */
    void Program(unsigned int address, unsigned int bits, const uint8_t* data);

    /**
     * @brief Reads one mapped Flash word after the device read delay.
     *
     * @pre Programming mode is enabled and the TAP is in Run-Test/Idle.
     * @details Unused high output bits are cleared. Bytes and bits are
     * least-significant first.
     *
     * @param[in] address Mapped physical address for which WordBits() is
     * nonzero.
     * @param[in] bits Device-specific data-register width.
     * @param[out] data Non-null buffer of at least ceil(bits / 8) bytes.
     */
    void Read(unsigned int address, unsigned int bits, uint8_t* data);

private:
    /**
     * @brief Shifts a nonempty register and returns the TAP to Idle.
     *
     * @pre The TAP is in Run-Test/Idle. Non-null buffers hold ceil(count / 8)
     * bytes and do not overlap, since output is cleared before shifting.
     *
     * @param[in] instruction True for IR, false for DR.
     * @param[in] count Positive bit count to shift.
     * @param[in] input Optional packed bytes; null shifts zeroes.
     * @param[out] output Optional packed TDO bytes; null discards readback.
     */
    void Scan(bool instruction, unsigned int count, const uint8_t* input,
              uint8_t* output);

    /**
     * @brief Shifts an instruction through the 10-bit IR.
     *
     * @pre The TAP is in Run-Test/Idle.
     *
     * @param[in] value Instruction value; only the low 10 bits are shifted.
     */
    void ShiftInstruction(uint16_t value);

    /**
     * @brief Shifts a small integer through the selected data register.
     *
     * @pre The TAP is in Run-Test/Idle.
     *
     * @param[in] bits Bit count from one through 16.
     * @param[in] value Integer to shift, least-significant bit first.
     */
    void ShiftNumber(unsigned int bits, uint16_t value);

    /**
     * @brief Selects the address register and shifts the physical word address.
     *
     * @pre The TAP is in Run-Test/Idle.
     *
     * @param[in] address Mapped physical address fitting the 11-bit register.
     */
    void SelectAddress(unsigned int address);

    Io& io_;
};

// Implementation details follow. Templates remain here so AVR and desktop test
// backends instantiate the same TAP transitions without virtual dispatch.

/**
 * @brief Clocks the TAP through reset to Idle.
 */
template <typename Io>
void Jtag<Io>::Reset() {
    for (unsigned int i = 0; i < 6; ++i) {
        io_.Clock(true, false);
    }
    io_.Clock(false, false);
}

/**
 * @brief Traverses Capture, Shift and Update for the selected register.
 */
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

/**
 * @brief Packs an instruction into two little-endian bytes.
 */
template <typename Io>
void Jtag<Io>::ShiftInstruction(uint16_t value) {
    const uint8_t bytes[] = {static_cast<uint8_t>(value),
                             static_cast<uint8_t>(value >> 8)};
    Scan(true, 10, bytes, nullptr);
}

/**
 * @brief Packs a small integer for a data-register scan.
 */
template <typename Io>
void Jtag<Io>::ShiftNumber(unsigned int bits, uint16_t value) {
    const uint8_t bytes[] = {static_cast<uint8_t>(value),
                             static_cast<uint8_t>(value >> 8)};
    Scan(false, bits, bytes, nullptr);
}

/**
 * @brief Reads and assembles the 32-bit ID without AVR-width truncation.
 */
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

/**
 * @brief Shifts the ATF programming-enable key.
 */
template <typename Io>
void Jtag<Io>::Enable() {
    ShiftInstruction(0x280);
    ShiftNumber(10, 0x1b9);  // ATF ISC enable key.
}

/**
 * @brief Clears the programming key and resets the TAP.
 */
template <typename Io>
void Jtag<Io>::Disable() {
    ShiftInstruction(0x280);
    ShiftNumber(10, 0);
    Reset();
}

/**
 * @brief Latches erase, executes it and observes the required delay.
 */
template <typename Io>
void Jtag<Io>::Erase() {
    ShiftInstruction(0x2b3);
    ShiftInstruction(0x29e);
    io_.WaitMilliseconds(210);
    // Preserve the upstream post-operation instruction.
    ShiftInstruction(0x2bf);
}

/**
 * @brief Selects the physical row through the address instruction.
 */
template <typename Io>
void Jtag<Io>::SelectAddress(unsigned int address) {
    ShiftInstruction(0x2a1);
    ShiftNumber(11, static_cast<uint16_t>(address));
}

/**
 * @brief Loads a word and starts the timed programming cycle.
 */
template <typename Io>
void Jtag<Io>::Program(unsigned int address, unsigned int bits,
                       const uint8_t* data) {
    SelectAddress(address);
    ShiftInstruction(static_cast<uint16_t>(0x290 | (address >> 8)));
    Scan(false, bits, data, nullptr);
    ShiftInstruction(0x29e);
    io_.WaitMilliseconds(30);
    ShiftInstruction(0x2bf);
}

/**
 * @brief Triggers readback before shifting out the selected word.
 */
template <typename Io>
void Jtag<Io>::Read(unsigned int address, unsigned int bits, uint8_t* data) {
    SelectAddress(address);
    ShiftInstruction(0x28c);
    io_.WaitMilliseconds(20);
    ShiftInstruction(static_cast<uint16_t>(0x290 | (address >> 8)));
    Scan(false, bits, nullptr, data);
}

}  // namespace atf

#endif  // ATF150X_PROGRAMMER_FIRMWARE_ATF1502_PROGRAMMER_JTAG_H_
