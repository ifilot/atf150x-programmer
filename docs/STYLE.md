# C++ style

Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
for all firmware, host code and C++ tests. Use clang-format 18 with the
repository's `.clang-format`, based on `Google` with braces enabled for control
statements. Comments should explain contracts, ownership, units, ordering and
non-obvious device behavior rather than repeat the implementation.

The code uses:

- `.cc` implementation files and self-contained `.h` headers with project/path
  include guards.
- `UpperCamelCase` functions and types, `snake_case` variables, trailing
  underscores on private class data, and `kUpperCamelCase` constants.
- Project-root include paths, explicit scalar casts, bounded buffers, and
  small helpers for parsing and protocol stages.
- Checked `Status` return values for recoverable errors, without exceptions.
  Output pointers document nullability and failure behavior. Constructors avoid
  I/O; native handles and programming-session cleanup have explicit lifetimes.

## Platform requirements

Keep host code at C++17 and shared firmware headers compatible with the Arduino
AVR toolchain's C++11 mode. The guide explicitly asks projects to consider
portability before adopting newer language features.

Arduino requires `setup()`, `loop()`, `Serial`, and its existing API names and
types. The sketch retains its `.ino` extension, and the test adapter retains
`Arduino.h` and matching framework declarations. Sketch-local includes are
necessary because Arduino copies that directory into a separate build tree.
Tests include the actual sketch to exercise the production handlers. Narrow
`NOLINT` comments mark these framework constraints and required printf argument
types; they are not blanket exclusions of firmware from style checks.

## Check formatting

After configuring CMake with clang-format installed:

```sh
cmake --build build --target format-check
```

To format the source tree from a Linux shell:

```sh
clang-format-18 -i cli/src/*.cc cli/src/*.h tests/*.cc tests/*.h \
  tests/arduino/Arduino.h firmware/atf1502_programmer/*.h \
  firmware/atf1502_programmer/*.ino
```

CI runs the formatting check alongside the Linux, Windows and Arduino builds.
Formatting does not replace review of naming, comments or error handling.
