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
  unit/cpu_alu_test.cpp
  unit/cpu_bits_test.cpp
  unit/cpu_flow_test.cpp
  unit/cpu_interrupt_test.cpp
  unit/cpu_load_test.cpp
  unit/cpu_opcode_table_test.cpp
  unit/cpu_test.cpp
  unit/gameboy_test.cpp
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
# Blargg's hardware test ROMs are the reference for CPU correctness. They are
# downloaded at a pinned commit instead of being committed here (ADR-007).
# Configure with -DGB_FETCH_TEST_ROMS=OFF to build without network access.
if(GB_FETCH_TEST_ROMS)
  FetchContent_Declare(
    gb_test_roms
    GIT_REPOSITORY https://github.com/retrio/gb-test-roms.git
    GIT_TAG c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15
  )
  FetchContent_MakeAvailable(gb_test_roms)

  add_executable(rom_tests
    test_roms.cpp
  )
  target_compile_definitions(rom_tests PRIVATE GB_TEST_ROM_DIR="${gb_test_roms_SOURCE_DIR}")
  target_link_libraries(rom_tests PRIVATE gb::core GTest::gtest_main gb_warnings)
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
