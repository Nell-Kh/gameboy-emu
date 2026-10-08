# Decisions

Short architecture decision records. Newest at the bottom. A decision is changed by adding a new
record that supersedes the old one, not by editing history.

## ADR-001: C++20

- **Context:** The project exists to show modern C++, and it needs fixed-width integers, bit
  operations and `constexpr` everywhere.
- **Decision:** C++20 with compiler extensions off (`-std=c++20`). No exceptions in the hot path,
  no RTTI.
- **Consequences:** `<bit>`, `std::span`, concepts and designated initialisers are available.
  Needs gcc 11+ or clang 14+, which every current CI image has.

## ADR-002: CMake + Ninja, three targets

- **Context:** The core must build and be tested without SDL, so that CI stays fast and the
  emulator logic never depends on a window.
- **Decision:** One CMake project with three targets: `core` (static library, no dependencies),
  `gameboy-emu` (the frontend executable, links `core`), `unit_tests` (GoogleTest, links `core`).
  Ninja is the generator. Named configurations live in `CMakePresets.json` (`debug`, `asan`,
  `release`). GoogleTest is fetched by CMake at a pinned tag, not vendored.
- **Consequences:** A build is `cmake --preset X && cmake --build --preset X`. The first configure
  needs network access to fetch GoogleTest.

## ADR-003: Warnings as errors, for our code only

- **Context:** Warnings that are allowed to accumulate stop being read. Bit-level code is where
  implicit conversions and sign bugs hide.
- **Decision:** `-Wall -Wextra -Wpedantic -Werror` on every target we own, carried by the
  `gb_warnings` interface target. Third-party code does not link it.
- **Consequences:** A new compiler version can break the build with a new warning; that is
  accepted and fixed when it happens. A warning inside GoogleTest cannot break the build.

## ADR-004: Sanitizers on every test run, lint as a gate

- **Context:** An emulator is array indexing and integer arithmetic. Out-of-bounds access and
  undefined behaviour can produce a result that looks right on one compiler and not on another.
- **Decision:** CI builds and runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
  (`asan` preset) on gcc and clang on Linux and on clang on macOS. UBSan findings abort the test
  (`-fno-sanitize-recover`). A separate job fails on any clang-format difference or clang-tidy
  finding.
- **Consequences:** Test runs are slower than a plain build, which does not matter at this size.
  The `release` preset stays unsanitized for real use and for measuring speed.
