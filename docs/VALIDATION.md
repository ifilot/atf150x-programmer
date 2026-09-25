# Validation

Physical testing covers ATF1502AS and ATF1504AS programming on the assembled
PCB through an Arduino Leonardo and native Windows USB serial. An official
ATDH1150USB programmer independently verified an ATF1502AS image written by this
project. In the reverse direction, this project matched every word of an
ATF1504AS image written by the official programmer. This project then wrote and
independently read back the ATF1504AS test image, which the official programmer
also verified successfully. Application-level truth-table testing and physical
Linux USB operation remain pending.

## Checks completed during implementation

- Native Linux C++17 Release build using GCC.
- Native Windows executable cross-built with MinGW-w64.
- Arduino Leonardo build with Arduino AVR core 1.8.8: 9,362 bytes of program
  storage and 482 bytes of static RAM.
- Both checked-in WinCUPL examples are inspected during CTest: ATF1502AS
  `QF16808` and ATF1504AS `QF34192`.
- Parser tests cover checksums, malformed records, lock configurations, reserved
  fuses, device selection, and all 34,186 mapped ATF1504AS fuse coordinates
  against an inverse coordinate model.
- The JTAG engine is checked against an independent TAP state transition table.
- The actual sketch is compiled into a desktop Arduino stub to test framing,
  CRC handling, command responses, invalid addresses and data, device matching,
  JTAG protection, ATF1504AS erase/write access, partial and overlong frames,
  disconnects, and timeout cleanup.
- Linux PTY integration exercises the real CLI against a simulated programmer.
  It covers erase, program, complete staged verification, and activation for
  both devices, including all 216 ATF1504AS physical words.
- Google-style formatting and Doxygen warning checks pass.

Python is required only for Linux PTY tests. The C++ core and firmware simulation
tests also build on Windows.

## Physical ATF1502AS tests — 2026-09-25

The Leonardo firmware was uploaded through Windows Arduino CLI. The bootloader
appeared on COM4; avrdude verified the upload. The running firmware returned to
COM3 and completed the CRC-protected handshake.

The board reported IDCODE `0x0150203F`. The native MinGW `atfprog.exe` flashed
`p2000m-cpm-coboard.jed`: erase and blank checking passed for all 212 words,
all staged words passed readback, and final activation passed. A separate
verify invocation matched all 212 words. Verification passed again after a USB
power cycle.

The same CPLD was later programmed with the checked-in
`atf1502as-plcc44.jed` example by this project. An official ATDH1150USB
programmer then verified that image successfully, providing independent
cross-tool confirmation of the write path and fuse mapping.

For the reverse comparison, the official programmer wrote
`p2000m-cpm-coboard.jed` to an ATF1502AS. Development firmware v0.2.0 then
identified IDCODE `0x0150203F` and matched all 212 physical words through the
Leonardo's read-only `verify` command.

Tested JEDEC SHA-256 values:

```text
p2000m-cpm-coboard.jed:
abf80f86a1adcbbb0e93fcafdb2af26c866ba0351c2b0b858e342c4a9b176b53

atf1502as-plcc44.jed:
665cbbd528926a261bd20208b81b7e8f1a646bd520f6a287fd2fcb250794845e
```

## Physical ATF1504AS tests — 2026-09-25

An official ATDH1150USB programmer wrote
`examples/logic_test/atf1504as-plcc44.jed` to an ATF1504AS. The unpowered
device was moved to the Leonardo programmer. Development firmware v0.2.0
reported IDCODE `0x0150403F`, and the native Windows CLI parsed the 34,192-fuse
image with checksum `65A1`.

The `verify` command read and matched all 216 physical words and exited
successfully. No erase or write command was issued during this first
official-to-open-source comparison.

Write support was then enabled after the mapping and read path had passed that
comparison. The Leonardo erased and blank-checked all 216 words, programmed
`atf1504as-plcc44.jed`, verified every staged word, and applied the final
arming bit. A separate session again identified IDCODE `0x0150403F` and
matched all 216 words. The official ATDH1150USB programmer subsequently
verified the newly written image successfully, completing the bidirectional
cross-validation.

Tested JEDEC SHA-256:

```text
3b8704efbd7be61734a484e22f91290b992127f66df3eb0dd4d21f233a13f42e
```

## Hardware test procedure

1. Disconnect USB before fitting a CPLD, confirm PLCC-44 orientation, and
   reconnect the Leonardo.
2. Run `scan`. Expect `0150203F` for ATF1502AS or `0150403F` for ATF1504AS.
3. Run `inspect` and then `verify` with the JEDEC file for the fitted device.
4. Run `flash`, then a separate `verify`, and repeat verification after a
   power cycle.
5. Program or verify the same image with an official programmer for independent
   comparison.
6. Exercise the intended logic in an application fixture.
7. Repeat supported operations with native Windows and Linux serial ports.

A failing word reports its hexadecimal address and expected and actual byte
strings. Resolve power, orientation, wiring, and JTAG access before changing
timing or mapping code.

## Style

C++ sources follow the Google C++ layout with four-space indentation, include
guards, explicit scalar casts, and Doxygen API contracts. Recoverable failures
return checked `Status` values; production GCC and MinGW builds disable
exceptions. `cmake --build build --target format-check` checks every C++ source,
header, and sketch with the repository's Google-based clang-format
configuration.
