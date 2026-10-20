# The emulator core. No SDL, no I/O: it must build and test on its own.
add_library(core STATIC
  apu.cpp
  bus.cpp
  cartridge/cartridge.cpp
  cartridge/mbc1.cpp
  cartridge/mbc3.cpp
  cartridge/mbc5.cpp
  cartridge/no_mbc.cpp
  cartridge/rtc.cpp
  cpu.cpp
  gameboy.cpp
  joypad.cpp
  oam_dma.cpp
  opcodes.gen.cpp
  ppu.cpp
  save_state.cpp
  serial.cpp
  state_io.cpp
  timer.cpp
)
add_library(gb::core ALIAS core)

# Headers are included as "core/gameboy.h" from anywhere in the project.
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR})

# The version string: `git describe` of the checkout at build time, or the
# project(... VERSION ...) in the root file without git (ADR-027).
set(GB_VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h)
set(GB_VERSION_COMMAND
  ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUT=${GB_VERSION_HEADER}
  -DFALLBACK=${PROJECT_VERSION} -P ${PROJECT_SOURCE_DIR}/cmake/git_version.cmake)
# Once now, so tools that only configure (clang-tidy in the lint job) find the
# header; then again at every build, so it follows new commits.
execute_process(COMMAND ${GB_VERSION_COMMAND} COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(gb_version
  COMMAND ${GB_VERSION_COMMAND}
  BYPRODUCTS ${GB_VERSION_HEADER}
  COMMENT "Version from git describe"
)
add_dependencies(core gb_version)
target_include_directories(core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(core PRIVATE gb_warnings)
