# Changelog

All notable changes to this project are documented in this file.

The project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Versions cover the Arduino firmware and the `atfprog` CLI together; both always
ship with the same version.

## [Unreleased]

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

[Unreleased]: https://github.com/ifilot/atf150x-programmer/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/ifilot/atf150x-programmer/releases/tag/v0.1.0
