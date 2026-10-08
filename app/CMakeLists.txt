# The frontend executable. A stub until M3, when SDL2 arrives.
add_executable(gameboy-emu
  main.cpp
)

target_link_libraries(gameboy-emu PRIVATE gb::core gb_warnings)
