# The frontend executable: an SDL2 window, or --headless for test ROMs and CI.
# SDL2 is linked here only; the core never depends on it.
find_package(SDL2 REQUIRED CONFIG)

add_executable(gameboy-emu
  main.cpp
  sdl_frontend.cpp
)

# main() is our own; SDL must not replace it with SDL_main.
target_compile_definitions(gameboy-emu PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(gameboy-emu PRIVATE gb::core SDL2::SDL2 gb_warnings)
