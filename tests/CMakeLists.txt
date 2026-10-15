include(FetchContent)

# GoogleTest is downloaded at configure time and built with our compiler and flags.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests
  unit/bus_test.cpp
  unit/cartridge_test.cpp
  unit/cpu_alu_test.cpp
  unit/cpu_bits_test.cpp
  unit/cpu_flow_test.cpp
  unit/cpu_interrupt_test.cpp
  unit/cpu_load_test.cpp
  unit/cpu_opcode_table_test.cpp
  unit/cpu_test.cpp
  unit/dpad_test.cpp
  unit/gameboy_test.cpp
  unit/joypad_test.cpp
  unit/mbc1_test.cpp
  unit/mbc5_test.cpp
  unit/oam_dma_test.cpp
  unit/ppu_render_test.cpp
  unit/ppu_test.cpp
  unit/registers_test.cpp
  unit/serial_test.cpp
  unit/smoke_test.cpp
  unit/timer_test.cpp
)

target_include_directories(unit_tests PRIVATE unit)
target_link_libraries(unit_tests PRIVATE gb::core GTest::gtest_main gb_warnings)

# Registers every TEST(...) with CTest, so `ctest` lists them one by one.
include(GoogleTest)
gtest_discover_tests(unit_tests)

# The generated decoder must match what the generator produces from the table.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME Generator.OpcodesAreUpToDate
    COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/gen_opcodes.py --check
  )
endif()

# --- Test ROMs -------------------------------------------------------------
# Blargg's and Mooneye's hardware test ROMs are the reference for correctness.
# They are downloaded at pinned versions instead of being committed here
# (ADR-007, ADR-011).
# Configure with -DGB_FETCH_TEST_ROMS=OFF to build without network access.
if(GB_FETCH_TEST_ROMS)
  FetchContent_Declare(
    gb_test_roms
    GIT_REPOSITORY https://github.com/retrio/gb-test-roms.git
    GIT_TAG c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15
  )
  FetchContent_MakeAvailable(gb_test_roms)

  # Prebuilt Mooneye Test Suite ROMs, from c-sp's collection (ADR-011).
  FetchContent_Declare(
    csp_test_roms
    URL https://github.com/c-sp/game-boy-test-roms/releases/download/v7.0/game-boy-test-roms-v7.0.zip
    URL_HASH SHA256=b9a9d7a1075aa35a3d07c07c34974048672d8520dca9e07a50178f5860c3832c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(csp_test_roms)

  # stb_image and stb_image_write, vendored at a pinned commit (ADR-015), read
  # and write PNG screenshots. Test code only: never linked into core or app.
  # The .c file is built by the C++ compiler, so every object in a test binary
  # comes from the same compiler and sanitizer runtime. SYSTEM keeps stb's
  # warnings and clang-tidy findings out of our builds.
  add_library(stb STATIC third_party/stb/stb_impl.c)
  set_source_files_properties(third_party/stb/stb_impl.c PROPERTIES LANGUAGE CXX)
  target_include_directories(stb SYSTEM INTERFACE third_party/stb)

  add_executable(rom_tests
    acid2_test.cpp
    test_roms.cpp
  )
  target_compile_definitions(rom_tests PRIVATE
    GB_TEST_ROM_DIR="${gb_test_roms_SOURCE_DIR}"
    GB_MOONEYE_DIR="${csp_test_roms_SOURCE_DIR}/mooneye-test-suite"
    GB_ACID2_DIR="${csp_test_roms_SOURCE_DIR}/dmg-acid2"
  )
  target_link_libraries(rom_tests PRIVATE gb::core stb GTest::gtest_main gb_warnings)
  gtest_discover_tests(rom_tests)

  # The command-line frontend, end to end: exit status 0 means the ROM passed.
  add_test(
    NAME App.HeadlessRunsATestRom
    COMMAND gameboy-emu --headless "${gb_test_roms_SOURCE_DIR}/cpu_instrs/individual/06-ld r,r.gb"
  )
endif()

add_test(NAME App.PrintsUsageWithoutArguments COMMAND gameboy-emu)
set_tests_properties(App.PrintsUsageWithoutArguments PROPERTIES
  PASS_REGULAR_EXPRESSION "Usage: gameboy-emu"
)
add_test(NAME App.RejectsUnknownOptions COMMAND gameboy-emu --no-such-option)
set_tests_properties(App.RejectsUnknownOptions PROPERTIES WILL_FAIL TRUE)

# Window mode is never opened in CI. These stop at the arguments or the ROM
# file, before SDL starts.
add_test(NAME App.WindowModeReportsAMissingRom COMMAND gameboy-emu no-such-rom.gb)
set_tests_properties(App.WindowModeReportsAMissingRom PROPERTIES
  PASS_REGULAR_EXPRESSION "cannot read no-such-rom.gb"
)
add_test(NAME App.TraceNeedsHeadless COMMAND gameboy-emu --trace no-such-rom.gb)
set_tests_properties(App.TraceNeedsHeadless PROPERTIES
  PASS_REGULAR_EXPRESSION "need --headless"
)
add_test(NAME App.RejectsAScaleOutOfRange COMMAND gameboy-emu --scale 0 no-such-rom.gb)
set_tests_properties(App.RejectsAScaleOutOfRange PROPERTIES
  PASS_REGULAR_EXPRESSION "--scale must be 1 to 16"
)
