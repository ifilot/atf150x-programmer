# Changelog

All notable changes to this project are documented in this file.

The project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Versions cover the Arduino firmware and the `atfprog` CLI together; both always
ship with the same version.

## [Unreleased]

## [0.2.0] - 2026-09-25

### Added

- Matched ATF1502AS and ATF1504AS PLCC-44 WinCUPL logic-test sources and
  generated JEDEC files for device-support validation.
- ATF1504AS identification, JEDEC parsing, complete fuse mapping, erase, blank
  check, programming, activation, and verification across all 216 physical
  words.
- Bidirectional physical ATF1504AS cross-validation: this project verified an
  image written by an official ATDH1150USB programmer, and that programmer
  verified an image written by this project.

## [0.1.0] - 2026-09-25

### Added

- Arduino Leonardo firmware for driving an ATF1502AS over JTAG.
- Native C++ command-line flasher for Windows and Linux.
- Direct parsing and validation of ATF1502AS JEDEC files without vendor tools.
- Device identification, erase, blank check, program and readback verification.
- Safe staged programming that verifies the image before enabling CPLD outputs.
- CRC-protected and sequence-checked USB serial protocol with bounded buffers
  and session timeouts.
- Automated parser, TAP, firmware-protocol and serial-integration tests.
- GitHub Actions builds for firmware and downloadable Windows/Linux CLI
  artifacts.
- Doxygen API documentation and Google-style formatting checks.
- Physical validation using an Arduino Leonardo and ATF1502AS, including
  verification after a power cycle.

[Unreleased]: https://github.com/ifilot/atf150x-programmer/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/ifilot/atf150x-programmer/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/ifilot/atf150x-programmer/releases/tag/v0.1.0
