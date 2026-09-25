# Validation

The implementation has successfully programmed and verified a physical
ATF1502AS on the assembled PCB through a Leonardo using native Windows USB
serial. Verification also passed after the user disconnected and reconnected USB.
This confirms retention for the tested device and image; broader device coverage
and application-level logic still need testing.

## Checks completed during implementation

- Native Linux C++17 Release build using GCC.
- Native Windows executable cross-built with MinGW-w64.
- Arduino Leonardo build with Arduino AVR core 1.8.8: 9,072 bytes of program
  storage and 449 bytes of static RAM.
- The `inspect` command accepts all three supplied WinCUPL JEDEC files:
  `p2000m-cpm-coboard.jed`, `p2000m-stock-fast.jed` and `p2000m-stock-prom.jed`.
  Each has 16,808 fuses and maps to 212 programming words. The files remain in
  their original project and are not copied into this repository.
- CTest: parser checksums, malformed records, rejected lock configurations,
  fuse mapping against an inverse coordinate model, and JTAG behavior against
  an independent TAP state transition table.
- Actual sketch compiled into a desktop Arduino stub: framing/CRC handling,
  command responses, invalid address/data rejection, JTAG protection, partial
  and overlong frames, disconnect and timeout cleanup.
- Linux PTY integration: the actual CLI completes erase/program/verify/activate
  against a simulated programmer, handles partial serial reads, rejects bad
  IDs/CRCs/sequences, stops on blank-check and verify failures, and does not arm
  before verifying every staged word. Separate erase and read-only verify paths
  are exercised as well.

Python is only needed for the Linux PTY tests. C++ core and firmware simulation
tests also build on Windows. CI is configured to run native Windows tests;
configuration alone is not evidence that those remote jobs have run.

## Physical Windows test — 2026-09-25

- Uploaded the current Leonardo firmware through Windows Arduino CLI. The
  bootloader appeared on COM4; avrdude verified all 9,072 bytes. The running
  firmware returned to COM3 and passed the CRC-protected USB handshake.
- Read the inserted CPLD's JTAG IDCODE: `0x0150203F` (ATF1502AS).
- Used the MinGW-built `atfprog.exe` natively on Windows to flash
  `p2000m-cpm-coboard.jed` (16,808 fuses).
- Erase and blank check passed for all 212 mapped words.
- All 212 staged programming words passed readback verification; the final
  activation word also passed. The flash command exited successfully.
- A separate `verify` command reopened COM3, identified the chip again and
  matched all 212 words against the original JEDEC file. It exited successfully.

Tested JEDEC SHA-256:

```text
abf80f86a1adcbbb0e93fcafdb2af26c866ba0351c2b0b858e342c4a9b176b53
```

The user then disconnected and reconnected USB to cycle power. A further
read-only `verify` invocation on COM3 identified the ATF1502AS again and matched
all 212 words against the same JEDEC file, exiting successfully. This confirms
retention and continued JTAG access across that power cycle. Operation in the
application circuit and physical USB programming from Linux remain untested.

## Hardware test procedure

1. Upload the firmware and run `scan` with an ATF1502AS in the socket. Record
   the reported IDCODE (expected 0150203F).
2. Run `inspect` on the intended JEDEC file, then `flash`. This erases the
   previous design. Save stdout/stderr if it fails.
3. Run a separate `verify` invocation after flashing.
4. Disconnect USB, reconnect it and run `verify` again to check retention and
   continued JTAG access across a power cycle.
5. Test the CPLD's intended logic in its application circuit.
6. Repeat the serial scan/flash/verify checks on native Windows and Linux.

A failing word reports its hexadecimal address and expected/actual byte strings.
If the first scan fails, resolve power/wiring/JTAG access first. If programming
or verification fails despite a correct ID, retain those logs before adjusting
programming timings or fuse mapping.

## Style refactor

C++ sources use Google naming, formatting, include guards, explicit scalar
casts and documented API contracts. Recoverable failures return checked
`Status` values; production GCC/MinGW builds disable exceptions. The same
parser, firmware and serial integration tests exercise the refactored code,
including preservation of outputs when parsing or hex decoding fails.
`cmake --build build --target format-check` checks every C++ source, header and
sketch with the repository's Google-based clang-format configuration.
