# The emulator core. No SDL, no I/O: it must build and test on its own.
add_library(core STATIC
  bus.cpp
  cpu.cpp
  gameboy.cpp
  opcodes.gen.cpp
  serial.cpp
  timer.cpp
)
add_library(gb::core ALIAS core)

# Headers are included as "core/gameboy.h" from anywhere in the project.
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR})

# The version string comes from project(... VERSION ...) in the root file.
target_compile_definitions(core PRIVATE GB_VERSION="${PROJECT_VERSION}")

target_link_libraries(core PRIVATE gb_warnings)
