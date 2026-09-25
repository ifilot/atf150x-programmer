# ATF1502AS programmer

Arduino Leonardo firmware and a native C++17 command-line tool for the PCB in
`pcb/atf150X-programmer`. Programs an existing WinCUPL JEDEC file directly over
USB serial. No vendor programmer software, JEDEC converter, Python runtime or
external serial library is required.

**Status:** initial implementation, built and tested in simulation. Successful
programming on a physical PCB still needs to be confirmed. Linux builds and the
Windows cross-build have been checked; native Windows USB operation has not yet
been tested.

## Hardware

Supports one **5 V ATF1502AS** with JTAG enabled. ASV/BE variants, other densities,
JTAG chains, security locking and recovery of JTAG-disabled chips are unsupported.
The expected IDCODE is `0x0150203F`; other IDs are rejected before erasing.

The pin assignments come from the checked-in PCB:

| JTAG | Leonardo pin | Series resistor |
| --- | --- | --- |
| TDI | D2 | R3 |
| TCK | D3 | R4 |
| TDO | D4 | R5 |
| TMS | D12 | R6 |

This uses GPIO, not the Leonardo's SPI header. The shield's `5V1` pin supplies
its `+5V` net. Fit the chip with the board unpowered, then connect the Leonardo
by USB. Confirm orientation and assembly before the first scan. Firmware leaves
JTAG pins as inputs between sessions and does not control target power.

## Install the Leonardo firmware

Use the Arduino IDE, select **Arduino Leonardo**, and open
`firmware/atf1502_programmer/atf1502_programmer.ino`. Upload normally. There are no
additional Arduino library dependencies.

Alternatively, with Arduino CLI:

```sh
arduino-cli core update-index
arduino-cli core install arduino:avr
arduino-cli board list
arduino-cli compile --fqbn arduino:avr:leonardo firmware/atf1502_programmer
arduino-cli upload --fqbn arduino:avr:leonardo --port /dev/ttyACM0 firmware/atf1502_programmer
```

On Windows use the corresponding `COM` port in the upload command. Uploading
firmware and flashing the CPLD are separate operations. The Leonardo can change
ports while its bootloader is active; use its normal application port for
`atfprog`. Close Serial Monitor before opening the CLI.

## Build the CLI

### Linux

Install a C++17 compiler and CMake 3.16 or newer (for example `build-essential`
and `cmake` on Debian/Ubuntu), then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Executable: `build/atfprog`. Serial access uses the operating system's termios
API. If opening `/dev/ttyACM0` returns permission denied, grant your user serial
access according to your distribution (typically membership in `dialout`,
followed by logging out and back in). A stable `/dev/serial/by-id/...` path may
also be used.

### Windows

Install Visual Studio 2022 Build Tools with **Desktop development with C++** and
CMake. In a Developer PowerShell from the repository directory:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Executable: `build\Release\atfprog.exe`. Serial access uses the native Windows
API, including support for ports above `COM9`. MinGW-w64 is also supported; that
build links the C++ runtime statically.

Python 3 is optional for development: CMake enables the Linux PTY integration
test if an interpreter is present. It is not needed to run the CLI.

## Flash a design

Inspect the file and identify the device first:

```sh
build/atfprog inspect path/to/design.jed
build/atfprog scan --port /dev/ttyACM0
build/atfprog flash path/to/design.jed --port /dev/ttyACM0
```

Windows equivalent:

```powershell
.\build\Release\atfprog.exe inspect C:\designs\design.jed
.\build\Release\atfprog.exe scan --port COM3
.\build\Release\atfprog.exe flash C:\designs\design.jed --port COM3
```

Quote paths containing spaces. The port is explicit; `scan` identifies the chip
on that port, rather than enumerating USB devices. `inspect` only reads the file
and never opens a serial port.

`flash` replaces the existing design. It performs these steps:

1. Parse and validate the JEDEC file, before opening the port.
2. Identify the ATF1502AS and enter programming mode.
3. Erase and blank-check all 212 mapped programming words.
4. Program the image with the arming switch held in its safe state.
5. Read back and compare every mapped bit, including padding, configuration,
   JTAG options and the user signature.
6. If requested by the image, program the arming bit and verify that word.
7. Leave programming mode, enabling the verified design, and release the pins.

Progress and errors go to stderr; results go to stdout. Expect tens of seconds,
not instantaneous flashing. A zero exit status means the requested operation
completed; failures return nonzero. Do not disconnect power during programming.
If interrupted, reconnect and repeat the full `flash` operation.

Additional commands:

```sh
build/atfprog verify path/to/design.jed --port /dev/ttyACM0
build/atfprog erase --port /dev/ttyACM0
build/atfprog --help
```

`verify` reads and compares without erasing or programming. `erase` destroys the
current configuration and checks that all mapped words read as erased. Both
briefly stop the CPLD's normal operation while in programming mode.

## JEDEC support and failure handling

The parser accepts STX/ETX-framed files with a design header terminated by `*`,
`QF16808`, `F0` or `F1`, sparse/multiline `L` records, and a required `C` fuse
checksum. `N`/`NOTE`, `QP`, `QV`, and `J` metadata are ignored; `G0` is accepted.
Test vectors and other unsupported record types are rejected. Transmission
checksums are verified when nonzero; the standard `0000` trailer means that
check is disabled (as in the supplied WinCUPL examples). Files are read in binary
mode so Windows CRLF bytes are preserved for transmission checksum validation.

Overlapping/out-of-range fuse data, wrong fuse counts, invalid checksums,
nonzero reserved fuses and locking requests are rejected. The four JTAG/special
fuses at 16782–16785 must remain `1111`. No fuses are silently changed in the
final image. An image that intentionally leaves its arming switch safe remains
safe after flashing.

The serial protocol has CRC checks, response sequence checks, bounded buffers
and timeouts. Firmware also checks the chip ID before enabling erase/write
commands and rejects writes that alter the JTAG/security word. On disconnect,
a malformed frame or five seconds without a command, it exits programming mode
and releases the driven pins. USB failures are reported, not retried blindly.
The next session starts with a fresh handshake.

If `scan` returns all zeros or all ones, check the chip supply, orientation,
assembly, JTAG access and firmware pin mapping. This board/software combination
does not provide the high-voltage override needed to recover disabled JTAG.

## Development and references

See [coding style](docs/STYLE.md), [protocol and algorithm](docs/PROTOCOL.md),
[validation details](docs/VALIDATION.md), and
[third-party attribution](docs/THIRD_PARTY.md). CI builds and tests the C++ code
on Linux and Windows and compiles the Arduino sketch.

The software, firmware, tests, build configuration and accompanying
documentation are licensed under the GNU General Public License version 3
only (`GPL-3.0-only`); see [LICENSE](LICENSE). Copyright (c) 2026 ATF1502
programmer contributors. Required upstream notices are retained in
[third-party attribution](docs/THIRD_PARTY.md). The existing PCB files retain
their [CC BY-SA 4.0 license](pcb/atf150X-programmer/LICENSE).
