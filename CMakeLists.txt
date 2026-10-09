cmake_minimum_required(VERSION 3.24)

project(gameboy-emu
  VERSION 0.1.0
  DESCRIPTION "Game Boy (DMG) emulator in C++20"
  LANGUAGES CXX
)

# C++20, no compiler-specific extensions (-std=c++20, not -std=gnu++20).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Writes build/<preset>/compile_commands.json, which clang-tidy and the IDE read.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GB_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(GB_BUILD_TESTS "Build the unit tests" ON)
option(GB_FETCH_TEST_ROMS "Download the public test ROMs and run them as tests" ON)

# Warnings as errors, for our own targets only.
# Our targets link gb_warnings; third-party code (GoogleTest) does not,
# so a warning inside a dependency can never break our build.
add_library(gb_warnings INTERFACE)
target_compile_options(gb_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

# Sanitizers apply to everything, dependencies included:
# ASan needs the whole program built with it.
if(GB_SANITIZE)
  add_compile_options(
    -fsanitize=address,undefined
    -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer
  )
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(core)
add_subdirectory(app)

if(GB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
