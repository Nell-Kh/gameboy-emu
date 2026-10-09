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
  unit/cpu_test.cpp
  unit/registers_test.cpp
  unit/smoke_test.cpp
)

target_include_directories(unit_tests PRIVATE unit)
target_link_libraries(unit_tests PRIVATE gb::core GTest::gtest_main gb_warnings)

# Registers every TEST(...) with CTest, so `ctest` lists them one by one.
include(GoogleTest)
gtest_discover_tests(unit_tests)
