# gameboy-emu

[![CI](https://github.com/Nell-Kh/gameboy-emu/actions/workflows/ci.yml/badge.svg)](https://github.com/Nell-Kh/gameboy-emu/actions/workflows/ci.yml)

A Game Boy (DMG) emulator in C++20. Work in progress: the project skeleton is in place, the CPU is next.

Build and test: `cmake --preset asan && cmake --build --preset asan && ctest --preset asan` (needs CMake 3.24+, Ninja and a C++20 compiler).
